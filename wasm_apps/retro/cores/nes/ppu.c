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
    if (addr < 0x2000u) {
        /* Mapper 9 (MMC2) needs the function call for latch side-effects */
        if (__builtin_expect(ppu->mapper->id == 9, 0))
            return mapper_ppu_read(ppu->mapper, addr);
        return ppu->mapper->chr_page[addr >> 10][addr & 0x3FFu];
    }
    if (addr < 0x3F00u) return ppu->vram[mirror_nt(ppu->mapper->mirroring, addr)];
    /* Palette ($3F00-$3FFF) — mirrors of 32 bytes */
    addr &= 0x1Fu;
    /* Backdrop mirrors: $10, $14, $18, $1C → $00, $04, $08, $0C */
    if (addr == 0x10u || addr == 0x14u || addr == 0x18u || addr == 0x1Cu)
        addr &= 0x0Fu;
    return ppu->palette[addr];
}

/* ── Cached palette: rebuilt only when palette RAM changes ──────────── */
static uint16_t s_pal[32];
static int      s_pal_dirty = 1;

/* ── Sprite Y-range: pre-computed per frame to skip scanlines
 *    where no sprite can possibly appear.  Saves scanning all 64
 *    OAM entries on every one of those empty scanlines.              */
static int s_spr_y_min;
static int s_spr_y_max;
static int s_spr_range_valid;

static inline void ppu_mem_write(PPU *ppu, uint16_t addr, uint8_t val)
{
    addr &= 0x3FFFu;
    if (addr < 0x2000u) { mapper_ppu_write(ppu->mapper, addr, val); return; }
    if (addr < 0x3F00u) { ppu->vram[mirror_nt(ppu->mapper->mirroring, addr)] = val; return; }
    addr &= 0x1Fu;
    if (addr == 0x10u || addr == 0x14u || addr == 0x18u || addr == 0x1Cu)
        addr &= 0x0Fu;
    ppu->palette[addr] = val;
    s_pal_dirty = 1;
}

/* ── Palette colour lookup ─────────────────────────────────────────── */
static inline uint16_t pal_color(PPU *ppu, int idx)
{
    return NES_PAL[ppu->palette[idx & 0x1F] & 0x3F];
}

/* ── Scanline renderer ──────────────────────────────────────────────── *
 * Optimised: pre-fetch + pre-decode all 33 BG tiles so the pixel      *
 * output loop is a simple byte-array lookup.  Nametable / attribute   *
 * reads go directly to VRAM (bypass ppu_mem_read), CHR reads go       *
 * directly through chr_page[].  Sprites are inlined (no function      *
 * calls for pattern reads).  Palette is pre-built per scanline.       *
 * ──────────────────────────────────────────────────────────────────── */

/* Map nametable index (0-3) to VRAM base offset (0 or $400) */
static inline uint16_t nt_vram_base(int mirroring, int nt_idx)
{
    switch (mirroring) {
    default:
    case MIRROR_H:  return (nt_idx >= 2) ? 0x400u : 0x000u;
    case MIRROR_V:  return (nt_idx & 1)  ? 0x400u : 0x000u;
    case MIRROR_1A: return 0x000u;
    case MIRROR_1B: return 0x400u;
    }
}

static void render_scanline(PPU *ppu, int sl)
{
    uint16_t *line = ppu->fb + sl * NES_W;
    int bg_on    = (ppu->mask & MASK_SHOW_BG) != 0;
    int spr_on   = (ppu->mask & MASK_SHOW_SP) != 0;
    int size16   = (ppu->ctrl & CTRL_SPR_SIZE) != 0;
    int sp_height = size16 ? 16 : 8;

    /* Cached palette: only rebuilt when palette RAM actually changes.
     * Saves ~38K WASM ops per frame (was rebuilt 240× per frame). */
    if (s_pal_dirty) {
        for (int i = 0; i < 32; i++)
            s_pal[i] = NES_PAL[ppu->palette[i] & 0x3F];
        s_pal_dirty = 0;
    }
    uint16_t *pal = s_pal;
    uint16_t bg_color = s_pal[0];

    int is_mmc2 = __builtin_expect(ppu->mapper->id == 9, 0);
    const uint8_t **cp = (const uint8_t **)ppu->mapper->chr_page;

    /* ── Sprite Y-range: compute once at the start of each frame ──── */
    if (sl == 0) {
        s_spr_y_min = 240;
        s_spr_y_max = -1;
        for (int s = 0; s < 64; s++) {
            int sy = (int)ppu->oam[s * 4] + 1;
            if (sy < 240) {
                if (sy < s_spr_y_min) s_spr_y_min = sy;
                int bottom = sy + sp_height - 1;
                if (bottom > s_spr_y_max) s_spr_y_max = bottom;
            }
        }
        s_spr_range_valid = (s_spr_y_max >= s_spr_y_min);
    }

    /* ── Sprite buffer ──────────────────────────────────────────────
     * sp_buf[x] encodes:  bits[3:0]=pal-idx, bit4=behind-BG, bit5=spr0
     * Lazy zeroing: only initialised when first sprite found.
     * Fast Y-range cull: skip entirely if no sprites overlap this sl. */
    uint8_t sp_buf[NES_W];
    int     sp_any = 0;

    if (spr_on && s_spr_range_valid && sl >= s_spr_y_min && sl <= s_spr_y_max) {
        int sp_count = 0;
        for (int s = 63; s >= 0; s--) {
            int sy  = (int)ppu->oam[s * 4 + 0] + 1;
            int row = sl - sy;
            if (row < 0 || row >= sp_height) continue;
            if (!sp_any) {
                for (int i = 0; i < NES_W; i++) sp_buf[i] = 0;
                sp_any = 1;
            }
            if (sp_count >= 8) { ppu->status |= STAT_SP_OFLOW; break; }
            sp_count++;

            int tile   = ppu->oam[s * 4 + 1];
            int attr   = ppu->oam[s * 4 + 2];
            int sx     = ppu->oam[s * 4 + 3];
            int flip_h = (attr >> 6) & 1;
            int flip_v = (attr >> 7) & 1;
            int behind = (attr >> 5) & 1;

            /* Inline sprite CHR reads — no function call */
            uint16_t chr_addr;
            if (size16) {
                int bank = (tile & 1) ? 0x1000 : 0x0000;
                tile &= 0xFE;
                if (flip_v) row = 15 - row;
                if (row >= 8) { tile++; row -= 8; }
                chr_addr = (uint16_t)(bank + tile * 16 + row);
            } else {
                int base = (ppu->ctrl & CTRL_SP_TABLE) ? 0x1000 : 0x0000;
                if (flip_v) row = 7 - row;
                chr_addr = (uint16_t)(base + tile * 16 + row);
            }
            uint8_t lo, hi;
            if (is_mmc2) {
                lo = mapper_ppu_read(ppu->mapper, chr_addr);
                hi = mapper_ppu_read(ppu->mapper, (uint16_t)(chr_addr + 8));
            } else {
                lo = cp[chr_addr >> 10][chr_addr & 0x3FFu];
                hi = cp[(chr_addr + 8) >> 10][(chr_addr + 8) & 0x3FFu];
            }

            for (int bit = 7; bit >= 0; bit--) {
                int px = sx + (flip_h ? bit : (7 - bit));
                if ((unsigned)px >= (unsigned)NES_W) continue;
                int pix = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (!pix) continue;
                sp_buf[px] = (uint8_t)(pix | ((attr & 3) << 2) |
                                       (behind ? 0x10 : 0) |
                                       (s == 0 ? 0x20 : 0));
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

    /* ── BG tile pre-fetch + pixel decode ──────────────────────────
     * Pre-decode each tile's 8 pixels into palette indices so the
     * output loop is a single byte-array lookup (no bit extraction). */
    uint8_t bg_pixels[264]; /* 33 tiles × 8 pixels */

    if (bg_on) {
        int mir = ppu->mapper->mirroring;
        /* Pre-compute VRAM bases for the two horizontal NT halves */
        uint16_t vb0 = nt_vram_base(mir, (0 ^ (bg_nt & 1)) | (nt_v << 1));
        uint16_t vb1 = nt_vram_base(mir, (1 ^ (bg_nt & 1)) | (nt_v << 1));
        int ty_off  = ty * 32;
        int attr_y  = (ty >> 2) * 8;
        int sht_y   = (ty & 2) ? 4 : 0;

        for (int t = 0; t <= 32; t++) {
            int abs_x  = (coarse_x + t) & 0x3F;
            int tx     = abs_x & 0x1F;
            uint16_t vb = (abs_x >= 32) ? vb1 : vb0;

            /* Direct VRAM reads — bypass ppu_mem_read */
            uint8_t tnr = ppu->vram[vb + ty_off + tx];
            int sht = sht_y | ((tx & 2) ? 2 : 0);
            int tile_pal = (ppu->vram[vb + 0x3C0 + attr_y + (tx >> 2)] >> sht) & 3;

            /* CHR reads via chr_page[] */
            uint16_t pt = (uint16_t)(bg_base + tnr * 16 + fine_y);
            uint8_t lo, hi;
            if (is_mmc2) {
                lo = mapper_ppu_read(ppu->mapper, pt);
                hi = mapper_ppu_read(ppu->mapper, (uint16_t)(pt + 8));
            } else {
                lo = cp[pt >> 10][pt & 0x3FFu];
                hi = cp[(pt + 8) >> 10][(pt + 8) & 0x3FFu];
            }

            /* Decode 8 pixels into palette indices — branchless.
             * Each pixel: 2-bit colour index from interleaved bitplanes.
             * Using direct bit extraction avoids a loop and per-pixel
             * conditional, saving ~63K branches per render frame.       */
            uint8_t *dst = &bg_pixels[t * 8];
            if ((lo | hi) == 0) {
                /* Blank tile row — zero 8 pixels without bit loop */
                dst[0]=dst[1]=dst[2]=dst[3]=dst[4]=dst[5]=dst[6]=dst[7]=0;
            } else {
                int pb = tile_pal << 2;
                /* Unrolled: extract each pixel without branches.
                 * p=0 means transparent → must stay 0 (not pb). */
                int p0 = ((lo >> 7) & 1) | ((hi >> 6) & 2);
                int p1 = ((lo >> 6) & 1) | ((hi >> 5) & 2);
                int p2 = ((lo >> 5) & 1) | ((hi >> 4) & 2);
                int p3 = ((lo >> 4) & 1) | ((hi >> 3) & 2);
                int p4 = ((lo >> 3) & 1) | ((hi >> 2) & 2);
                int p5 = ((lo >> 2) & 1) | ((hi >> 1) & 2);
                int p6 = ((lo >> 1) & 1) | ((hi     ) & 2);
                int p7 = ((lo     ) & 1) | ((hi << 1) & 2);
                dst[0] = p0 ? (uint8_t)(p0 | pb) : 0;
                dst[1] = p1 ? (uint8_t)(p1 | pb) : 0;
                dst[2] = p2 ? (uint8_t)(p2 | pb) : 0;
                dst[3] = p3 ? (uint8_t)(p3 | pb) : 0;
                dst[4] = p4 ? (uint8_t)(p4 | pb) : 0;
                dst[5] = p5 ? (uint8_t)(p5 | pb) : 0;
                dst[6] = p6 ? (uint8_t)(p6 | pb) : 0;
                dst[7] = p7 ? (uint8_t)(p7 | pb) : 0;
            }
        }
    }

    int sp0_ok = (ppu->oam[0] + 1 <= sl) && (sl < (int)ppu->oam[0] + 1 + sp_height);

    /* ── Pixel output — fast paths ──────────────────────────────────── */
    if (!sp_any) {
        /* No sprites on this scanline: straightforward BG output */
        if (bg_on) {
            int bg_left = (ppu->mask & MASK_SHOW_BG_L) ? 0 : 8;
            for (int px = 0; px < bg_left; px++)
                line[px] = bg_color;
            for (int px = bg_left; px < NES_W; px++) {
                uint8_t b = bg_pixels[px + fine_x];
                line[px] = b ? pal[b] : bg_color;
            }
        } else {
            for (int px = 0; px < NES_W; px++)
                line[px] = bg_color;
        }
    } else {
        /* Sprites present: full composite */
        int bg_left_mask = bg_on && !(ppu->mask & MASK_SHOW_BG_L);
        int sp_left_mask = !(ppu->mask & MASK_SHOW_SP_L);

        for (int px = 0; px < NES_W; px++) {
            uint8_t bg_pix = 0;
            if (bg_on && !(bg_left_mask && px < 8))
                bg_pix = bg_pixels[px + fine_x];

            uint8_t sp = sp_buf[px];
            if ((sp & 0x0F) && !(sp_left_mask && px < 8)) {
                if ((sp & 0x20) && bg_pix && sp0_ok) ppu->status |= STAT_SP0_HIT;
                if (!(sp & 0x10) || !bg_pix) {
                    line[px] = pal[0x10 | (sp & 0x0F)];
                    continue;
                }
            }

            line[px] = bg_pix ? pal[bg_pix] : bg_color;
        }
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

/* ── Lightweight scanline timing (frameskip mode) ────────────────────
 * Skips pixel rendering entirely.  Only checks sprite-0 hit (so games
 * polling $2002 don't hang) and performs the scroll updates that
 * render_scanline normally does.                                      */
static void fast_scanline_timing(PPU *ppu, int sl)
{
    int bg_on  = ppu->mask & MASK_SHOW_BG;
    int spr_on = ppu->mask & MASK_SHOW_SP;

    /* ── Sprite-0 hit (approximate) ──────────────────────────────── */
    if (spr_on && bg_on && !(ppu->status & STAT_SP0_HIT)) {
        int sp0_y  = (int)ppu->oam[0] + 1;
        int size16 = (ppu->ctrl & CTRL_SPR_SIZE) != 0;
        int row    = sl - sp0_y;
        if (row >= 0 && row < (size16 ? 16 : 8)) {
            int tile = ppu->oam[1];
            int attr = ppu->oam[2];
            int flip_v = (attr >> 7) & 1;
            uint16_t chr_addr;
            if (size16) {
                int bank = (tile & 1) ? 0x1000 : 0;
                tile &= 0xFE;
                if (flip_v) row = 15 - row;
                if (row >= 8) { tile++; row -= 8; }
                chr_addr = (uint16_t)(bank + tile * 16 + row);
            } else {
                int base = (ppu->ctrl & CTRL_SP_TABLE) ? 0x1000 : 0;
                if (flip_v) row = 7 - row;
                chr_addr = (uint16_t)(base + tile * 16 + row);
            }
            const uint8_t **cp = (const uint8_t **)ppu->mapper->chr_page;
            uint8_t lo = cp[chr_addr >> 10][chr_addr & 0x3FFu];
            uint8_t hi = cp[(chr_addr + 8) >> 10][(chr_addr + 8) & 0x3FFu];
            if (lo | hi)
                ppu->status |= STAT_SP0_HIT;
        }
    }

    /* ── Scroll updates (identical to end of render_scanline) ────── */
    ppu->v = (ppu->v & ~0x041Fu) | (ppu->t & 0x041Fu);
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
    s_pal_dirty   = 1;
    s_spr_range_valid = 0;
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
    case 0: { /* PPUCTRL */
        uint8_t prev = ppu->ctrl;
        ppu->ctrl = val;
        /* t[11:10] = d[1:0] (nametable select) */
        ppu->t = (ppu->t & ~0x0C00u) | (uint16_t)((val & 0x03) << 10);
        /* NMI edge: if NMI-enable transitions from 0→1 while VBlank is
         * already set, fire NMI immediately.  This is required by many
         * games that enable NMI after the boot sequence completes during
         * VBlank, or poll VBlank then enable NMI.                       */
        if (!(prev & CTRL_NMI_EN) && (val & CTRL_NMI_EN) &&
            (ppu->status & STAT_VBLANK))
            ppu->nmi_pending = 1;
        break;
    }
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

        /* Pre-render scanline (261) */
        if (sl == 261) {
            if (ppu->dots < 341) break;
            ppu->status &= ~(STAT_VBLANK | STAT_SP0_HIT | STAT_SP_OFLOW);
            if (ppu->mask & (MASK_SHOW_BG | MASK_SHOW_SP)) {
                /* Copy ALL scroll bits from t to v (vertical + horizontal).
                 * On real hardware: v-bits copied at dots 280-304, h-bits at
                 * dot 257.  Only happens when rendering is enabled. */
                ppu->v = (ppu->v & ~0x7BE0u) | (ppu->t & 0x7BE0u); /* vertical */
                ppu->v = (ppu->v & ~0x041Fu) | (ppu->t & 0x041Fu); /* horizontal */
            }
            ppu->dots -= 341;
            ppu->scanline = 0;
            ppu->odd_frame ^= 1;
            /* Skip one cycle on odd frames (affects cycle count) */
            if (ppu->odd_frame &&
                (ppu->mask & (MASK_SHOW_BG | MASK_SHOW_SP)))
                ppu->dots--;
            frame_done = 1;
            break; /* stop here — do not render into the next frame */
        }

        /* Visible scanlines: render one scanline per 341 PPU dots */
        if (sl < 240) {
            if (ppu->dots < 341) break; /* not enough dots yet */
            if (ppu->mask & (MASK_SHOW_BG | MASK_SHOW_SP)) {
                if (ppu->skip_render)
                    fast_scanline_timing(ppu, sl);
                else
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

        /* Post-render scanline 240: idle.
         * VBlank begins on the transition from scanline 240 → 241.
         * Fire the VBLANK flag and (if NMI enabled) set nmi_pending HERE,
         * exactly once per frame.  Previous code checked on every ppu_run
         * call while scanline==241, using STAT_VBLANK as a guard — but
         * the game can clear STAT_VBLANK by reading $2002, causing the
         * guard to re-trigger and fire NMI again (recursive NMI crash). */
        if (sl == 240) {
            if (ppu->dots < 341) break;
            ppu->dots -= 341;
            ppu->scanline++;
            /* ── VBlank begins ──────────────────────────────────────── */
            ppu->status |= STAT_VBLANK;
            if (ppu->ctrl & CTRL_NMI_EN)
                ppu->nmi_pending = 1;
            continue;
        }

        /* VBlank: scanlines 241-260 — consume in bulk */
        {
            int remaining = 261 - sl;
            int have = ppu->dots / 341;
            int consume = have < remaining ? have : remaining;
            if (consume == 0) break;
            ppu->dots -= consume * 341;
            ppu->scanline += consume;
        }
    }

    return frame_done;
}
