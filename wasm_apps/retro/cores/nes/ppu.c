/**
 * @file ppu.c
 * @brief NES PPU renderer for AkiraOS retro emulator
 *
 * Rendering model: scanline-based (not dot-accurate).
 * Each scanline is rendered as a whole when the PPU dot counter
 * crosses the scanline boundary.
 *
 * Supports:
 *   - Background: nametable fetch, attribute palette, CHR pattern tables
 *   - Horizontal and vertical scrolling via v/t registers
 *   - Sprites: 8×8 and 8×16, horizontal/vertical flip, priority
 *   - Sprite-0 hit detection
 *   - NMI at VBlank (scanline 241)
 *   - Horizontal and vertical nametable mirroring
 *
 * @license Apache-2.0
 */
#include "ppu.h"

/* ── NES master palette → RGB565 ─────────────────────────────────────
 * Source: NesDev wiki NTSC 2C02 palette (Nestopia).
 * RGB565 = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3)              */
/* On AkiraOS the ST7789V is driven via Zephyr's MIPI DBI SPI driver which
 * passes pixel data raw — no byte-swap is performed before SPI transmission.
 * The SPI shifts bytes out in memory order (low byte first for LE uint16_t),
 * so the display controller receives each pixel byte-swapped w.r.t. the
 * uint16_t value.  Pre-swapping every palette entry here compensates:
 *   grey C(84,84,84) computes 0x52AA → stored as 0xAA52
 *   SPI sends bytes [0x52, 0xAA] → display reconstructs 0x52AA = grey ✓
 *
 * (The earlier note "ILI9341 driver already byte-swaps" was correct for the
 * custom akira_ili9341.c driver which is no longer in use on this board.) */
#define C(r,g,b) (uint16_t)(                                               \
    ( (((uint16_t)(r)>>3)<<11) | (((uint16_t)(g)>>2)<<5) | ((uint16_t)(b)>>3) ) >> 8 | \
    ( (((uint16_t)(r)>>3)<<11) | (((uint16_t)(g)>>2)<<5) | ((uint16_t)(b)>>3) ) << 8 )
static const uint16_t NES_PAL[64] = {
    C( 84, 84, 84), C(  0, 30,116), C(  8, 16,144), C( 48,  0,136),
    C( 68,  0,100), C( 92,  0, 48), C( 84,  4,  0), C( 60, 24,  0),
    C( 32, 42,  0), C(  8, 58,  0), C(  0, 64,  0), C(  0, 60,  0),
    C(  0, 50, 60), C(  0,  0,  0), C(  0,  0,  0), C(  0,  0,  0),
    C(152,150,152), C(  8, 76,196), C( 48, 50,236), C( 92, 30,228),
    C(136, 20,176), C(160, 20,100), C(152, 34, 32), C(120, 60,  0),
    C( 84, 90,  0), C( 40,114,  0), C(  8,124,  0), C(  0,118, 40),
    C(  0,102,120), C(  0,  0,  0), C(  0,  0,  0), C(  0,  0,  0),
    C(236,238,236), C( 76,154,236), C(120,124,236), C(176, 98,236),
    C(228, 84,236), C(236, 88,180), C(236,106,100), C(212,136, 32),
    C(160,170,  0), C(116,196,  0), C( 76,208, 32), C( 56,204,108),
    C( 56,180,204), C( 60, 60, 60), C(  0,  0,  0), C(  0,  0,  0),
    C(236,238,236), C(168,204,236), C(188,188,236), C(212,178,236),
    C(236,174,236), C(236,174,212), C(236,180,176), C(228,196,144),
    C(204,210,120), C(180,222,120), C(168,226,144), C(152,226,180),
    C(160,214,228), C(160,162,160), C(  0,  0,  0), C(  0,  0,  0),
};
#undef C

/* ── VRAM nametable mirroring ─────────────────────────────────────────
 * Map PPU address $2000-$2FFF to VRAM offset (0-2047).
 * mirroring: 0 = horizontal (top/bottom share), 1 = vertical (left/right share) */
static inline uint16_t mirror_nt(int mirroring, uint16_t addr)
{
    addr &= 0x0FFFu; /* 0x000-0xFFF within nametable space */
    switch (mirroring) {
    default:
    case MIRROR_H:  /* NT0=NT1=$0000, NT2=NT3=$0400 */
        return (addr < 0x800u) ? (addr & 0x3FFu) : (0x400u | (addr & 0x3FFu));
    case MIRROR_V:  /* NT0=NT2=$0000, NT1=NT3=$0400 */
        return addr & 0x7FFu;
    case MIRROR_1A: /* All nametables → VRAM[0–$3FF] */
        return addr & 0x3FFu;
    case MIRROR_1B: /* All nametables → VRAM[$400–$7FF] */
        return 0x400u | (addr & 0x3FFu);
    }
}

/* ── PPU memory access ────────────────────────────────────────────────*/
static inline uint8_t ppu_mem_read(PPU *ppu, uint16_t addr)
{
    addr &= 0x3FFFu;
    if (addr < 0x2000u) return mapper_ppu_read(ppu->mapper, addr);
    if (addr < 0x3F00u) return ppu->vram[mirror_nt(ppu->mapper->mirroring, addr)];
    /* Palette ($3F00-$3FFF) — mirrors of 32 bytes */
    addr &= 0x1Fu;
    /* Backdrop mirrors: $10, $14, $18, $1C → $00, $04, $08, $0C */
    if (addr == 0x10u || addr == 0x14u || addr == 0x18u || addr == 0x1Cu)
        addr &= 0x0Fu;
    return ppu->palette[addr];
}

static inline void ppu_mem_write(PPU *ppu, uint16_t addr, uint8_t val)
{
    addr &= 0x3FFFu;
    if (addr < 0x2000u) { mapper_ppu_write(ppu->mapper, addr, val); return; }
    if (addr < 0x3F00u) { ppu->vram[mirror_nt(ppu->mapper->mirroring, addr)] = val; return; }
    addr &= 0x1Fu;
    if (addr == 0x10u || addr == 0x14u || addr == 0x18u || addr == 0x1Cu)
        addr &= 0x0Fu;
    ppu->palette[addr] = val;
}

/* ── Palette colour lookup ─────────────────────────────────────────── */
static inline uint16_t pal_color(PPU *ppu, int idx)
{
    return NES_PAL[ppu->palette[idx & 0x1F] & 0x3F];
}

/* ── Sprite helpers ────────────────────────────────────────────────── */
/* Returns the pattern table byte for a given sprite tile row */
static inline uint8_t sprite_pattern_byte(PPU *ppu, int tile, int row,
                                           int flip_v, int high_bit, int size8x16)
{
    if (size8x16) {
        /* 8×16 sprites: tile selects bank (bit 0), row selects top/bottom half */
        int bank = (tile & 1) ? 0x1000 : 0x0000;
        tile &= 0xFEu;
        if (flip_v) row = 15 - row;
        if (row >= 8) { tile++; row -= 8; }
        return ppu_mem_read(ppu, (uint16_t)(bank + tile * 16 + row + (high_bit ? 8 : 0)));
    } else {
        /* 8×8 sprites */
        int base = (ppu->ctrl & CTRL_SP_TABLE) ? 0x1000 : 0x0000;
        if (flip_v) row = 7 - row;
        return ppu_mem_read(ppu, (uint16_t)(base + tile * 16 + row + (high_bit ? 8 : 0)));
    }
}

/* ── Scanline renderer ──────────────────────────────────────────────── *
 * Optimised: pre-fetch all 33 BG tiles for the scanline so that CHR
 * and nametable reads are done once per tile (not once per pixel).
 * Sprites are limited to 8 per scanline — the actual NES hardware limit.
 * ──────────────────────────────────────────────────────────────────── */
static void render_scanline(PPU *ppu, int sl)
{
    uint16_t *line = ppu->fb + sl * NES_W;
    int bg_on    = (ppu->mask & MASK_SHOW_BG) != 0;
    int spr_on   = (ppu->mask & MASK_SHOW_SP) != 0;
    int size16   = (ppu->ctrl & CTRL_SPR_SIZE) != 0;
    int sp_height = size16 ? 16 : 8;

    /* ── Sprite buffer ──────────────────────────────────────────────
     * sp_buf[x] encodes:  bits[3:0]=pal-idx, bit4=behind-BG, bit5=spr0
     * Limited to 8 active sprites per scanline (NES hardware limit).  */
    uint8_t sp_buf[NES_W];
    int     sp_any = 0;
    for (int i = 0; i < NES_W; i++) sp_buf[i] = 0;

    if (spr_on) {
        int sp_count = 0;
        for (int s = 63; s >= 0; s--) {
            int sy  = (int)ppu->oam[s * 4 + 0] + 1;
            int row = sl - sy;
            if (row < 0 || row >= sp_height) continue;
            if (sp_count >= 8) { ppu->status |= STAT_SP_OFLOW; break; }
            sp_count++;

            int tile   = ppu->oam[s * 4 + 1];
            int attr   = ppu->oam[s * 4 + 2];
            int sx     = ppu->oam[s * 4 + 3];
            int flip_h = (attr >> 6) & 1;
            int flip_v = (attr >> 7) & 1;
            int behind = (attr >> 5) & 1;

            uint8_t lo = sprite_pattern_byte(ppu, tile, row, flip_v, 0, size16);
            uint8_t hi = sprite_pattern_byte(ppu, tile, row, flip_v, 1, size16);

            for (int bit = 7; bit >= 0; bit--) {
                int px = sx + (flip_h ? bit : (7 - bit));
                if ((unsigned)px >= (unsigned)NES_W) continue;
                int pix = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (!pix) continue;
                sp_buf[px] = (uint8_t)(pix | ((attr & 3) << 2) |
                                       (behind ? 0x10 : 0) |
                                       (s == 0 ? 0x20 : 0));
                sp_any = 1;
            }
        }
    }

    /* ── Background: decode v register ─────────────────────────────── */
    int bg_nt    = (ppu->v >> 10) & 3;
    int coarse_y = (ppu->v >>  5) & 0x1F;
    int fine_y   = (ppu->v >> 12) & 7;
    int coarse_x =  ppu->v        & 0x1F;
    int fine_x   =  ppu->fine_x;
    int bg_base  = (ppu->ctrl & CTRL_BG_TABLE) ? 0x1000 : 0;

    int ty   = coarse_y;
    int nt_v = (bg_nt >> 1) & 1;
    if (ty >= 30) { ty -= 30; nt_v ^= 1; }

    /* ── BG tile pre-fetch ──────────────────────────────────────────
     * Fetch nametable byte + attribute + CHR lo/hi for every tile that
     * the scanline touches (33 tiles covers 256 px at any fine_x).
     * This replaces per-pixel ppu_mem_read calls with one pass here. */
    uint8_t tile_lo[33], tile_hi[33], tile_pal[33];

    if (bg_on) {
        for (int t = 0; t <= 32; t++) {
            int abs_x  = (coarse_x + t) & 0x3F;
            int nt_h   = (abs_x >= 32) ? 1 : 0;
            int tx     =  abs_x & 0x1F;
            int nt_idx = (nt_h ^ (bg_nt & 1)) | (nt_v << 1);
            uint16_t nt_base = 0x2000u + (uint16_t)(nt_idx << 10);

            uint8_t  tnr  = ppu_mem_read(ppu, nt_base + (uint16_t)(ty * 32 + tx));
            uint16_t adr  = nt_base + 0x3C0u + (uint16_t)((ty >> 2) * 8 + (tx >> 2));
            int      sht  = ((ty & 2) ? 4 : 0) | ((tx & 2) ? 2 : 0);
            tile_pal[t]   = (ppu_mem_read(ppu, adr) >> sht) & 3;

            uint16_t pt   = (uint16_t)(bg_base + tnr * 16 + fine_y);
            tile_lo[t]    = ppu_mem_read(ppu, pt);
            tile_hi[t]    = ppu_mem_read(ppu, (uint16_t)(pt + 8));
        }
    }

    int sp0_ok = (ppu->oam[0] + 1 <= sl) && (sl < (int)ppu->oam[0] + 1 + sp_height);

    /* ── Pixel output ───────────────────────────────────────────────── */
    for (int px = 0; px < NES_W; px++) {
        uint8_t bg_pix = 0;

        if (bg_on && (px >= 8 || (ppu->mask & MASK_SHOW_BG_L))) {
            int dot = px + fine_x;
            int t   = dot >> 3;
            int bit = 7 - (dot & 7);
            int pix = ((tile_lo[t] >> bit) & 1) | (((tile_hi[t] >> bit) & 1) << 1);
            if (pix) bg_pix = (uint8_t)(pix | (tile_pal[t] << 2));
        }

        if (spr_on && sp_any) {
            uint8_t sp = sp_buf[px];
            if ((sp & 0x0F) && (px >= 8 || (ppu->mask & MASK_SHOW_SP_L))) {
                if ((sp & 0x20) && bg_pix && sp0_ok) ppu->status |= STAT_SP0_HIT;
                if (!(sp & 0x10) || !bg_pix) {
                    line[px] = pal_color(ppu, 0x10 | (sp & 0x0F));
                    continue;
                }
            }
        }

        line[px] = bg_pix ? pal_color(ppu, bg_pix) : pal_color(ppu, 0);
    }

    /* ── Horizontal scroll: reload from t ──────────────────────────── */
    ppu->v = (ppu->v & ~0x041Fu) | (ppu->t & 0x041Fu);

    /* ── Vertical scroll: fine Y, then coarse Y ────────────────────── */
    if ((ppu->v & 0x7000u) != 0x7000u) {
        ppu->v += 0x1000u;
    } else {
        ppu->v &= ~0x7000u;
        int y = (ppu->v >> 5) & 0x1F;
        if      (y == 29) { y = 0; ppu->v ^= 0x0800u; }
        else if (y == 31) { y = 0; }
        else              { y++; }
        ppu->v = (ppu->v & ~0x03E0u) | (uint16_t)(y << 5);
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void ppu_init(PPU *ppu, Mapper *mapper, uint16_t *fb)
{
    /* Zero all fields */
    uint8_t *p = (uint8_t *)ppu;
    for (int i = 0; i < (int)sizeof(*ppu); i++) p[i] = 0;
    ppu->mapper   = mapper;
    ppu->fb       = fb;
    ppu->scanline = 0;
    ppu->cycle    = 0;
    ppu->dots     = 0;
}

uint8_t ppu_reg_read(PPU *ppu, int reg)
{
    switch (reg) {
    case 2: { /* PPUSTATUS */
        uint8_t s = ppu->status;
        ppu->status &= ~STAT_VBLANK; /* Reading clears VBlank flag */
        ppu->latch = 0;
        return s;
    }
    case 4: /* OAMDATA */
        return ppu->oam[ppu->oam_addr];
    case 7: { /* PPUDATA */
        uint8_t val;
        if ((ppu->v & 0x3FFFu) < 0x3F00u) {
            val = ppu->read_buf;
            ppu->read_buf = ppu_mem_read(ppu, ppu->v);
        } else {
            /* Palette read bypasses buffer */
            ppu->read_buf = ppu_mem_read(ppu, (uint16_t)(ppu->v - 0x1000u));
            val = ppu_mem_read(ppu, ppu->v);
        }
        ppu->v += (ppu->ctrl & CTRL_VRAM_INC) ? 32 : 1;
        return val;
    }
    default: return 0;
    }
}

void ppu_reg_write(PPU *ppu, int reg, uint8_t val)
{
    switch (reg) {
    case 0: /* PPUCTRL */
        ppu->ctrl = val;
        /* t[11:10] = d[1:0] (nametable select) */
        ppu->t = (ppu->t & ~0x0C00u) | (uint16_t)((val & 0x03) << 10);
        break;
    case 1: /* PPUMASK */
        ppu->mask = val;
        break;
    case 3: /* OAMADDR */
        ppu->oam_addr = val;
        break;
    case 4: /* OAMDATA */
        ppu->oam[ppu->oam_addr++] = val;
        break;
    case 5: /* PPUSCROLL */
        if (ppu->latch == 0) {
            /* First write: horizontal scroll */
            ppu->t = (ppu->t & ~0x001Fu) | (uint16_t)(val >> 3);
            ppu->fine_x = val & 7;
            ppu->latch = 1;
        } else {
            /* Second write: vertical scroll */
            ppu->t = (ppu->t & ~0x73E0u) |
                     (uint16_t)((val & 0x07u) << 12) |
                     (uint16_t)((val & 0xF8u) << 2);
            ppu->latch = 0;
        }
        break;
    case 6: /* PPUADDR */
        if (ppu->latch == 0) {
            /* First write: high byte of address */
            ppu->t = (ppu->t & 0x00FFu) | (uint16_t)((val & 0x3F) << 8);
            ppu->latch = 1;
        } else {
            /* Second write: low byte, copy t to v */
            ppu->t = (ppu->t & 0xFF00u) | val;
            ppu->v = ppu->t;
            ppu->latch = 0;
        }
        break;
    case 7: /* PPUDATA */
        ppu_mem_write(ppu, ppu->v, val);
        ppu->v += (ppu->ctrl & CTRL_VRAM_INC) ? 32 : 1;
        break;
    }
}

void ppu_oam_dma(PPU *ppu, const uint8_t *page)
{
    for (int i = 0; i < 256; i++)
        ppu->oam[(ppu->oam_addr + i) & 0xFF] = page[i];
}

int ppu_run(PPU *ppu, int cpu_cycles)
{
    /* PPU runs at 3× CPU clock */
    ppu->dots += cpu_cycles * 3;

    int frame_done = 0;

    /* Process all accumulated dots */
    while (ppu->dots > 0) {
        int sl = ppu->scanline;

        /* Pre-render scanline (261): copy t vertical bits to v */
        if (sl == 261) {
            ppu->status &= ~(STAT_VBLANK | STAT_SP0_HIT | STAT_SP_OFLOW);
            /* At cycle 304 (simplified: start of pre-render), copy t to v */
            ppu->v = (ppu->v & ~0x7BE0u) | (ppu->t & 0x7BE0u);
            ppu->dots -= 341;
            ppu->scanline = 0;
            ppu->odd_frame ^= 1;
            /* Skip one cycle on odd frames (affects cycle count) */
            if (ppu->odd_frame &&
                (ppu->mask & (MASK_SHOW_BG | MASK_SHOW_SP)))
                ppu->dots--;
            frame_done = 1;
            continue;
        }

        /* Visible scanlines: render one scanline per 341 PPU dots */
        if (sl < 240) {
            if (ppu->dots < 341) break; /* not enough dots yet */
            /* Copy horizontal bits from t to v at start of scanline */
            ppu->v = (ppu->v & ~0x041Fu) | (ppu->t & 0x041Fu);
            if (ppu->mask & (MASK_SHOW_BG | MASK_SHOW_SP)) {
                render_scanline(ppu, sl);
                mapper_scanline(ppu->mapper); /* drives MMC3 IRQ counter */
            } else {
                /* All black */
                uint16_t *line = ppu->fb + sl * NES_W;
                uint16_t col = pal_color(ppu, 0);
                for (int i = 0; i < NES_W; i++) line[i] = col;
            }
            ppu->dots -= 341;
            ppu->scanline++;
            continue;
        }

        /* Post-render scanline 240: idle */
        if (sl == 240) {
            if (ppu->dots < 341) break;
            ppu->dots -= 341;
            ppu->scanline++;
            continue;
        }

        /* VBlank: scanlines 241-260 */
        if (sl == 241) {
            /* NMI fires at start of scanline 241 */
            if (!(ppu->status & STAT_VBLANK)) {
                ppu->status |= STAT_VBLANK;
                if (ppu->ctrl & CTRL_NMI_EN)
                    ppu->nmi_pending = 1;
            }
        }
        if (ppu->dots < 341) break;
        ppu->dots -= 341;
        ppu->scanline++;
    }

    return frame_done;
}
