/**
 * @file ppu.c
 * @brief Game Boy / GBC Picture Processing Unit — scanline renderer
 *
 * Models the PPU state machine with four modes per scanline:
 *   Mode 2 (OAM scan):   80 dots
 *   Mode 3 (Drawing):   172 dots
 *   Mode 0 (HBlank):    204 dots
 *   Mode 1 (VBlank):    lines 144–153
 *
 * Rendering is scanline-based.  Each scanline call:
 *   1. Draws the background tilemap (with scroll)
 *   2. Draws the window (if enabled and on-screen)
 *   3. Overlays sprites (up to 10 per line, priority rules applied)
 *
 * Both DMG (4-shade) and CGB (15-bit RGB) modes are supported.
 *
 * @license Apache-2.0
 */
#include "gb.h"

/* ── VRAM helpers ────────────────────────────────────────────────────── */
/* Read from VRAM bank 0 or 1 (CGB).  In DMG mode only bank 0 exists. */
static inline uint8_t vram_read(GB *gb, uint8_t bank, uint16_t offset)
{
    return gb->vram[(bank & 1u) * 8192u + (offset & 0x1FFFu)];
}

/* ── Tile data fetch ─────────────────────────────────────────────────── */
/* Returns a byte of 2bpp tile row data.
 * tile_num: 0–255 unsigned (LCDC bit 4 = 1) or signed –128..127 (bit 4 = 0)
 * bank:     0 or 1 (CGB only)
 * row:      0–7 (or 0–15 for 8×16 sprites)
 * hi:       0 = low byte of row, 1 = high byte */
static inline uint8_t tile_byte(GB *gb, uint8_t tile_num, uint8_t bank,
                                  uint8_t row, uint8_t hi, uint8_t tile_sel)
{
    uint16_t addr;
    if (tile_sel) {
        /* 0x8000 base — unsigned tile index */
        addr = (uint16_t)(tile_num * 16u + row * 2u);
    } else {
        /* 0x8800 base — signed tile index (-128..127) */
        int16_t signed_idx = (int8_t)tile_num;
        addr = (uint16_t)((0x1000 + signed_idx * 16) + row * 2u);
    }
    return vram_read(gb, bank, addr + hi);
}

/* ── Extract pixel color index (0–3) from a pair of tile row bytes ───── */
static inline uint8_t tile_pixel(uint8_t lo, uint8_t hi, uint8_t x)
{
    uint8_t bit = (uint8_t)(7u - (x & 7u));
    return (uint8_t)(((lo >> bit) & 1u) | (((hi >> bit) & 1u) << 1));
}

/* ── Background/Window scanline renderer ────────────────────────────── */
static void render_bg_win(GB *gb, int line,
                           uint8_t *bg_pix,    /* [160] 2bpp color index */
                           uint8_t *bg_attr,   /* [160] CGB attr byte    */
                           uint8_t *bg_prio)   /* [160] BG-over-OBJ flag */
{
    uint8_t lcdc    = gb->io[IO_LCDC];
    uint8_t tile_sel = (lcdc & LCDC_TILE_SEL) ? 1 : 0;

    /* Background tilemap base in VRAM */
    uint16_t bg_map  = (lcdc & LCDC_BG_MAP)  ? 0x1C00u : 0x1800u;
    uint16_t win_map = (lcdc & LCDC_WIN_MAP)  ? 0x1C00u : 0x1800u;

    uint8_t scy = gb->io[IO_SCY];
    uint8_t scx = gb->io[IO_SCX];
    uint8_t wy  = gb->io[IO_WY];
    uint8_t wx  = gb->io[IO_WX];  /* Real x = wx - 7 */

    /* Window active this line? */
    uint8_t win_en = (lcdc & LCDC_WIN_EN) && (line >= wy) &&
                     gb->ppu.window_active && (wx <= 166u);

    for (int sx = 0; sx < GB_W; sx++) {
        /* Check if pixel is in window */
        uint8_t in_win = win_en && (sx >= (int)wx - 7);

        uint8_t tile_num, attr;
        uint8_t py, px_in_tile;
        uint16_t map;

        if (in_win) {
            map         = win_map;
            int wy_off  = gb->ppu.window_ly;
            int wx_off  = sx - (wx - 7);
            py          = (uint8_t)(wy_off & 7u);
            uint8_t tx  = (uint8_t)((wx_off >> 3) & 0x1Fu);
            uint8_t ty  = (uint8_t)((wy_off >> 3) & 0x1Fu);
            uint16_t map_off = (uint16_t)(ty * 32u + tx);
            tile_num    = vram_read(gb, 0, map + map_off);
            attr        = gb->cgb_mode ? vram_read(gb, 1, map + map_off) : 0;
            px_in_tile  = (uint8_t)(wx_off & 7u);
        } else {
            map         = bg_map;
            uint8_t bg_y = (uint8_t)(scy + line);
            uint8_t bg_x = (uint8_t)(scx + sx);
            py          = (uint8_t)(bg_y & 7u);
            uint8_t tx  = (uint8_t)(bg_x >> 3);
            uint8_t ty  = (uint8_t)(bg_y >> 3);
            uint16_t map_off = (uint16_t)(ty * 32u + tx);
            tile_num    = vram_read(gb, 0, map + map_off);
            attr        = gb->cgb_mode ? vram_read(gb, 1, map + map_off) : 0;
            px_in_tile  = (uint8_t)(bg_x & 7u);
        }

        /* CGB attribute bits:
         * bit 0-2: BG palette number
         * bit 3:   VRAM bank (0 or 1)
         * bit 5:   X-flip
         * bit 6:   Y-flip
         * bit 7:   BG-to-OBJ priority (1 = bg/win colors 1–3 cover sprites) */
        uint8_t pal_num  = attr & 0x07u;
        uint8_t vbank    = (attr >> 3) & 1u;
        uint8_t xflip    = (attr >> 5) & 1u;
        uint8_t yflip    = (attr >> 6) & 1u;
        uint8_t bg_pri   = (attr >> 7) & 1u;

        uint8_t row = yflip ? (uint8_t)(7u - py) : py;
        uint8_t lo  = tile_byte(gb, tile_num, vbank, row, 0, tile_sel);
        uint8_t hi  = tile_byte(gb, tile_num, vbank, row, 1, tile_sel);

        uint8_t xi = xflip ? (uint8_t)(7u - (px_in_tile & 7u)) : (uint8_t)(px_in_tile & 7u);
        uint8_t color = tile_pixel(lo, hi, xi);

        bg_pix[sx]  = color;
        bg_attr[sx] = pal_num;
        bg_prio[sx] = bg_pri;
    }
}

/* ── Sprite renderer ─────────────────────────────────────────────────── */
#define MAX_SPRITES_PER_LINE  10

static void render_sprites(GB *gb, int line, const uint8_t *bg_pix,
                             const uint8_t *bg_prio, uint16_t *row_out)
{
    uint8_t lcdc    = gb->io[IO_LCDC];
    uint8_t h       = (lcdc & LCDC_OBJ_SIZE) ? 16 : 8;
    /* Sprites always use 0x8000 base (unsigned, tile_sel=1 implied) */

    /* Collect sprites visible on this line (first 10 by OAM order) */
    struct {
        uint8_t y, x, tile, attr;
    } sprites[MAX_SPRITES_PER_LINE];
    int n = 0;

    for (int i = 0; i < 40 && n < MAX_SPRITES_PER_LINE; i++) {
        uint8_t sy   = gb->oam[i * 4 + 0];
        uint8_t sx   = gb->oam[i * 4 + 1];
        uint8_t tile = gb->oam[i * 4 + 2];
        uint8_t attr = gb->oam[i * 4 + 3];

        int sprite_y = (int)sy - 16;
        if (line >= sprite_y && line < sprite_y + h) {
            sprites[n].y    = sy;
            sprites[n].x    = sx;
            sprites[n].tile = tile;
            sprites[n].attr = attr;
            n++;
        }
    }

    if (n == 0) return;

    /* Draw sprites in reverse order (first OAM entry wins on DMG) */
    for (int i = n - 1; i >= 0; i--) {
        uint8_t sy   = sprites[i].y;
        uint8_t sx   = sprites[i].x;
        uint8_t tile = sprites[i].tile;
        uint8_t attr = sprites[i].attr;

        uint8_t yflip  = (attr >> 6) & 1u;
        uint8_t xflip  = (attr >> 5) & 1u;
        uint8_t bg_prio_bit = (attr >> 7) & 1u;

        /* Sprite's row within the tile */
        int sprite_y = (int)sy - 16;
        int row_idx  = line - sprite_y;
        if (yflip) row_idx = (h - 1) - row_idx;

        /* For 8×16 sprites, tile bit 0 is forced to 0 for top half, 1 for bottom */
        uint8_t t = tile;
        if (h == 16) {
            t &= 0xFEu;
            if (row_idx >= 8) { t |= 0x01u; row_idx -= 8; }
        }

        /* CGB sprite attributes */
        uint8_t vbank    = gb->cgb_mode ? ((attr >> 3) & 1u) : 0;
        uint8_t pal_num  = gb->cgb_mode ? (attr & 0x07u) : ((attr >> 4) & 1u);

        /* Fetch tile row bytes */
        uint16_t taddr = (uint16_t)(t * 16u + row_idx * 2u);
        uint8_t lo = vram_read(gb, vbank, taddr);
        uint8_t hi = vram_read(gb, vbank, taddr + 1u);

        for (int px = 0; px < 8; px++) {
            int scr_x = (int)sx - 8 + px;
            if (scr_x < 0 || scr_x >= GB_W) continue;

            uint8_t xi = xflip ? (uint8_t)px : (uint8_t)(7 - px);
            uint8_t color = tile_pixel(lo, hi, xi);
            if (color == 0) continue;  /* Transparent */

            /* Priority: sprite hidden behind BG colors 1–3 if either
             * sprite attr bit 7 or CGB BG map attr bit 7 is set */
            if (bg_pix[scr_x] != 0 && (bg_prio_bit || bg_prio[scr_x])) continue;

            /* Write pixel */
            if (gb->cgb_mode) {
                row_out[scr_x] = gb->ppu.obj_pal[pal_num][color];
            } else {
                /* DMG: OBP0 or OBP1 */
                uint8_t obp  = gb->io[pal_num ? IO_OBP1 : IO_OBP0];
                uint8_t shade = (obp >> (color * 2)) & 3u;
                row_out[scr_x] = dmg_shade(shade);
            }
        }
    }
}

/* ── Render a complete scanline into gb->fb ──────────────────────────── */
static void ppu_render_scanline(GB *gb, int line)
{
    if (gb->ppu.skip_render) return;

    uint8_t lcdc = gb->io[IO_LCDC];
    uint16_t *row = &gb->fb[line * GB_W];

    /* Default: fill with color 0 BG shade */
    uint8_t bgp = gb->io[IO_BGP];
    uint16_t bg0_color;
    if (gb->cgb_mode) {
        bg0_color = gb->ppu.bg_pal[0][0];
    } else {
        bg0_color = dmg_shade(bgp & 3u);
    }
    for (int x = 0; x < GB_W; x++) row[x] = bg0_color;

    /* Line buffers */
    static uint8_t bg_pix[GB_W];
    static uint8_t bg_attr[GB_W];
    static uint8_t bg_prio[GB_W];

    /* ── Background + Window ──────────────────────────────────────── */
    int bg_win_en = (gb->cgb_mode) ? 1 : (lcdc & LCDC_BG_EN ? 1 : 0);
    if (bg_win_en) {
        render_bg_win(gb, line, bg_pix, bg_attr, bg_prio);

        for (int x = 0; x < GB_W; x++) {
            uint8_t c = bg_pix[x];
            if (gb->cgb_mode) {
                row[x] = gb->ppu.bg_pal[bg_attr[x]][c];
            } else {
                uint8_t shade = (bgp >> (c * 2)) & 3u;
                row[x] = dmg_shade(shade);
            }
        }
    } else {
        /* BG disabled — treat all pixels as color 0 (transparent for priority) */
        for (int x = 0; x < GB_W; x++) {
            bg_pix[x]  = 0;
            bg_prio[x] = 0;
        }
    }

    /* ── Sprites ──────────────────────────────────────────────────── */
    if (lcdc & LCDC_OBJ_EN) {
        render_sprites(gb, line, bg_pix, bg_prio, row);
    }
}

/* ── Window line counter ─────────────────────────────────────────────── */
static inline void update_window_counter(GB *gb, int line)
{
    uint8_t lcdc = gb->io[IO_LCDC];
    uint8_t wx   = gb->io[IO_WX];
    uint8_t wy   = gb->io[IO_WY];

    if ((lcdc & LCDC_WIN_EN) && (line >= wy) && (wx <= 166) && gb->ppu.window_active) {
        gb->ppu.window_ly++;
    }
}

/* ── STAT interrupt helper ───────────────────────────────────────────── */
static inline void maybe_stat_irq(GB *gb, uint8_t condition_bit)
{
    if (gb->io[IO_STAT] & condition_bit) {
        gb->io[IO_IF] |= INT_STAT;
    }
}

/* ── PPU tick — advance by `dots` T-cycles ───────────────────────────── */
void ppu_tick(GB *gb, int dots)
{
    uint8_t lcdc = gb->io[IO_LCDC];

    /* LCD disabled: keep LY=0, mode=0 */
    if (!(lcdc & LCDC_LCD_EN)) {
        gb->io[IO_LY]   = 0;
        gb->io[IO_STAT] = (uint8_t)(gb->io[IO_STAT] & ~STAT_MODE_MASK);
        gb->ppu.dot     = 0;
        gb->ppu.mode    = 0;
        return;
    }

    gb->ppu.dot += (uint32_t)dots;

    while (gb->ppu.dot >= GB_DOTS_LINE) {
        /* ── End of current scanline ─────────────────────────────── */
        int line = (int)gb->io[IO_LY];

        if (line < GB_H) {
            /* Render line on transition out of Mode 3 */ 
            ppu_render_scanline(gb, line);
            update_window_counter(gb, line);
        }

        gb->ppu.dot -= GB_DOTS_LINE;
        line = (line + 1) % (int)GB_LINES;
        gb->io[IO_LY] = (uint8_t)line;

        /* LYC coincidence */
        uint8_t lyc = gb->io[IO_LYC];
        if (line == lyc) {
            gb->io[IO_STAT] |= STAT_LYC_FLAG;
            maybe_stat_irq(gb, STAT_LYC_IE);
        } else {
            gb->io[IO_STAT] &= (uint8_t)~STAT_LYC_FLAG;
        }

        if (line == 0) {
            /* New frame */
            gb->ppu.window_ly     = 0;
            gb->ppu.window_active = (gb->io[IO_WY] == 0) ? 1 : 0;
        }

        if (line == (int)GB_H) {
            /* Enter VBlank */
            gb->ppu.mode = 1;
            gb->io[IO_STAT] = (uint8_t)((gb->io[IO_STAT] & ~STAT_MODE_MASK) | 1u);
            gb->io[IO_IF]  |= INT_VBLANK;
            maybe_stat_irq(gb, STAT_VBLANK_IE);
            gb->ppu.frame_ready = 1;
        } else if (line < (int)GB_H) {
            /* OAM scan at start of line */
            gb->ppu.mode = 2;
            gb->io[IO_STAT] = (uint8_t)((gb->io[IO_STAT] & ~STAT_MODE_MASK) | 2u);
            maybe_stat_irq(gb, STAT_OAM_IE);

            /* Track when WY condition is met */
            if (line == (int)gb->io[IO_WY]) {
                gb->ppu.window_active = 1;
            }
        }
    }

    /* Sub-scanline mode transitions */
    if (gb->io[IO_LY] < GB_H) {
        uint32_t dot_in_line = gb->ppu.dot;
        uint8_t  new_mode;

        if (dot_in_line < PPU_OAM_DOTS) {
            new_mode = 2;
        } else if (dot_in_line < PPU_OAM_DOTS + PPU_DRAW_DOTS) {
            new_mode = 3;
        } else {
            new_mode = 0;
        }

        if (new_mode != gb->ppu.mode) {
            uint8_t prev = gb->ppu.mode;
            gb->ppu.mode = new_mode;
            gb->io[IO_STAT] = (uint8_t)((gb->io[IO_STAT] & ~STAT_MODE_MASK) | new_mode);

            if (new_mode == 0 && prev == 3) {
                maybe_stat_irq(gb, STAT_HBLANK_IE);
                /* HDMA HBlank transfer */
                if (gb->hdma_active == 2 && gb->hdma_remain > 0) {
                    /* Transfer 16 bytes per HBlank */
                    for (int i = 0; i < 16 && gb->hdma_remain > 0; i++) {
                        uint8_t v = gb_read(gb, gb->hdma_src++);
                        uint16_t dst = (uint16_t)(0x8000u + gb->hdma_dst);
                        gb->vram[gb->vram_bank * 8192u + (dst & 0x1FFFu)] = v;
                        gb->hdma_dst++;
                        gb->hdma_remain--;
                    }
                    if (gb->hdma_remain == 0) {
                        gb->hdma_active  = 0;
                        gb->io[IO_HDMA5] = 0xFF;
                    } else {
                        gb->io[IO_HDMA5] = (uint8_t)((gb->hdma_remain / 16u) - 1u);
                    }
                }
            }
            if (new_mode == 2) {
                maybe_stat_irq(gb, STAT_OAM_IE);
            }
        }
    }
}
