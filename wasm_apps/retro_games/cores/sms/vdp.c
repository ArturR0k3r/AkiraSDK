/**
 * @file vdp.c
 * @brief SMS VDP (Mode 4) implementation
 *
 * Rendering pipeline per scanline:
 *   1.  fill_background() — draw all 32 background tiles, applying H/V-flip,
 *       palette, priority, and horizontal/vertical scroll
 *   2.  collect_sprites() — scan the SAT for sprites on this line (max 8)
 *   3.  draw_sprites()    — render sprite pixels (color 0 = transparent)
 *   4.  merge()           — compose sprite layer on top of background,
 *       respecting the BG priority bit
 *
 * All pixel output is RGB565.
 *
 * VRAM layout in Mode 4 (typical):
 *   0x0000–0x37FF  Pattern data (tile graphics, 32 bytes each × max 512 tiles)
 *   0x3800–0x3EFF  Name table  (32×28 = 896 entries × 2 bytes)
 *   0x3F00–0x3F7F  Sprite attribute table
 *
 * These base addresses are configurable via registers.
 *
 * @license Apache-2.0
 */
#include "vdp.h"
#include <string.h>

/* ── Internal helpers ────────────────────────────────────────────────── */

/* Name table base address from R2 (192-line mode formula) */
static inline uint16_t nt_base(const VDP *vdp)
{
    return (uint16_t)((vdp->reg[2] & 0x0E) << 10);
}

/* Sprite attribute table base from R5 */
static inline uint16_t sat_base(const VDP *vdp)
{
    return (uint16_t)((vdp->reg[5] & 0x7E) << 7);
}

/* Sprite pattern base: R6 bit 2 shifts pattern index into bank 1 */
static inline uint16_t spat_base(const VDP *vdp)
{
    return (uint16_t)((vdp->reg[6] & 0x04) << 11);
}

/* Convert a 6-bit SMS color (00BBGGRR) to RGB565 */
static inline uint16_t cram_to_rgb565(uint8_t c)
{
    uint8_t r2 = c & 0x03;
    uint8_t g2 = (c >> 2) & 0x03;
    uint8_t b2 = (c >> 4) & 0x03;
    /*
     * Expand 2-bit channel to 5-bit (R,B) or 6-bit (G):
     *   v5 = v2<<3 | v2<<1 | v2>>1   → maps 0→0, 1→10, 2→21, 3→31
     *   v6 = v2<<4 | v2<<2 | v2      → maps 0→0, 1→21, 2→42, 3→63
     */
    uint8_t r5 = (r2 << 3) | (r2 << 1) | (r2 >> 1);
    uint8_t g6 = (g2 << 4) | (g2 << 2) | g2;
    uint8_t b5 = (b2 << 3) | (b2 << 1) | (b2 >> 1);
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

/* Read a 4-bit pixel from a tile pattern in VRAM.
 *
 * Each tile is 32 bytes: 8 rows × 4 bytes per row (one byte per bitplane).
 * Pixel x (0=leftmost) of row y comes from mixing bits of the 4 plane bytes:
 *   addr = tile_base + y*4
 *   bit  = 7 - x            (SMS stores MSB on the left)
 *   color = ((vram[addr]>>bit)&1)
 *         | ((vram[addr+1]>>bit)&1) << 1
 *         | ((vram[addr+2]>>bit)&1) << 2
 *         | ((vram[addr+3]>>bit)&1) << 3
 */
static inline uint8_t tile_pixel(const VDP *vdp,
                                  uint16_t tile_idx, int tx, int ty)
{
    uint16_t base = (tile_idx * 32) + (ty * 4);
    int      bit  = 7 - tx;
    return (uint8_t)(((vdp->vram[base]     >> bit) & 1)
                   | (((vdp->vram[base + 1] >> bit) & 1) << 1)
                   | (((vdp->vram[base + 2] >> bit) & 1) << 2)
                   | (((vdp->vram[base + 3] >> bit) & 1) << 3));
}

/* ── Background rendering ────────────────────────────────────────────── *
 *
 * The background is a 32-column × 28-row grid of 8×8 tiles.
 * H-scroll (R8) shifts the visible window: pixel 0 on screen corresponds to
 * tilemap x = (256 - R8) & 0xFF (the window scrolls right as R8 increases).
 * V-scroll (R9) works the same way vertically, wrapping at 224 pixels.
 *
 * Each name table entry (2 bytes little-endian):
 *   [12] palette  (0 = CRAM 0-15,  1 = CRAM 16-31)
 *   [11] priority (1 = BG tile drawn over non-transparent sprites)
 *   [10] V-flip
 *   [9]  H-flip
 *   [8:0] tile index (0-511)
 */
static void fill_background(VDP *vdp, int line)
{
    uint16_t *row  = vdp->framebuf + line * VDP_SCREEN_W;
    uint8_t  *prio = vdp->priority_line;

    uint16_t nt    = nt_base(vdp);
    uint8_t  hscrl = vdp->reg[8];
    uint8_t  vscrl = vdp->reg[9];

    int bg_y     = (line + (int)vscrl) % 224;  /* 28 tiles × 8 px = 224 */
    int tile_row = bg_y >> 3;
    int fine_y   = bg_y & 7;

    /* Process one tile column (8 pixels) at a time instead of one pixel at
     * a time.  This reads the name table entry and 4 bitplane bytes once
     * per tile instead of once per pixel — 8× fewer VRAM accesses.       */
    for (int tc = 0; tc < 32; tc++) {
        /* Name table entry for this tile column */
        uint16_t nt_addr = nt + (uint16_t)(tile_row * 64 + tc * 2);
        uint16_t entry   = vdp->vram[nt_addr] | ((uint16_t)vdp->vram[nt_addr + 1] << 8);

        uint16_t pat_idx = entry & NT_PAT_MASK;
        int      hflip   = (entry & NT_H_FLIP)   ? 1 : 0;
        int      vflip   = (entry & NT_V_FLIP)   ? 1 : 0;
        int      palette = (entry & NT_PALETTE)  ? 16 : 0;
        int      pbit    = (entry & NT_PRIORITY) ? 1 : 0;

        int ty = vflip ? (7 - fine_y) : fine_y;

        /* Read the 4 bitplane bytes for this tile row once */
        uint16_t tile_addr = (uint16_t)(pat_idx * 32 + ty * 4);
        uint8_t  p0 = vdp->vram[tile_addr];
        uint8_t  p1 = vdp->vram[tile_addr + 1];
        uint8_t  p2 = vdp->vram[tile_addr + 2];
        uint8_t  p3 = vdp->vram[tile_addr + 3];

        /* H-scroll maps tile tc to screen_start = (hscrl + tc*8) & 0xFF */
        int screen_start = ((int)hscrl + tc * 8) & 0xFF;

        for (int k = 0; k < 8; k++) {
            /* bit index into the 4 bitplanes: flip reverses pixel order */
            int     bit = hflip ? k : (7 - k);
            uint8_t c4  = (uint8_t)(((p0 >> bit) & 1)
                                  | (((p1 >> bit) & 1) << 1)
                                  | (((p2 >> bit) & 1) << 2)
                                  | (((p3 >> bit) & 1) << 3));
            int sc = (screen_start + k) & 0xFF;
            row[sc]  = vdp->cram_cache[(uint8_t)(palette + c4)];
            prio[sc] = (uint8_t)(pbit & (c4 != 0));
        }
    }
}

/* ── Sprite rendering ────────────────────────────────────────────────── *
 *
 * Sprite Attribute Table layout in VRAM (at sat_base):
 *   Bytes 0x00–0x3F  : Y positions for sprites 0–63
 *                      Y=0xD0 (208) terminates the list in 192-line mode
 *   Bytes 0x80–0xFF  : For sprite n: byte[0x80+n*2]=X, byte[0x81+n*2]=tile
 *
 * Y offset: the VDP adds 1 to the stored Y before comparing (so Y=0 means
 * the sprite appears on scanline 1; Y=0xFF wraps to scanline 0).
 *
 * Sprite size: 8×8 when R1[1]=0, 8×16 when R1[1]=1.
 * R1[0]: sprite magnification — doubles pixel size (rarely used in SMS games).
 *
 * Sprite rendering does NOT set the "transparent" CRAM entry for color 0;
 * color0 pixels in sprites are simply skipped.
 *
 * The sprite palette always uses CRAM entries 16–31 (palette 1).
 */
static void draw_sprites(VDP *vdp, int line)
{
    /* Clear sprite pixel buffer — 0xFF is safe "empty" marker since max
     * sprite color index is 31 */
    memset(vdp->sprite_line, 0xFF, VDP_SCREEN_W);

    uint16_t sat = sat_base(vdp);
    uint16_t spat = spat_base(vdp);
    int tall = (vdp->reg[1] & VDP_R1_SPH) ? 1 : 0;  /* 8×16 sprites */
    int sprite_h = tall ? 16 : 8;
    int count = 0;

    for (int n = 0; n < VDP_MAX_SPRITES; n++) {
        /* Y coordinate byte (note: sprite appears at Y+1) */
        int sy = vdp->vram[sat + n];
        if (sy == 0xD0) break;           /* end-of-list sentinel */

        int screen_y = (sy + 1) & 0xFF; /* wrap */
        /* Sprites at Y=0xD0+ are also off-screen (handled by sentinel above) */

        /* Does this sprite cover our scanline? */
        if (line < screen_y || line >= screen_y + sprite_h) continue;

        if (count == VDP_SPRITES_PER_LINE) {
            vdp->status |= VDP_STAT_OVERFLOW;
            break;
        }
        count++;

        /* X and tile index come from the second half of the SAT */
        int sx         = vdp->vram[sat + 0x80 + n * 2];
        int tile_idx   = vdp->vram[sat + 0x80 + n * 2 + 1];

        /* For 8×16, bit 0 of tile index is forced to 0 (pair of tiles) */
        if (tall) tile_idx &= ~1;

        int local_y = line - screen_y;
        /* No V-flip for sprites in Mode 4 (unlike GG) */

        uint16_t pat_base = spat | (uint16_t)(tile_idx * 32);
        if (tall && local_y >= 8) {
            /* Second tile in the 8×16 pair */
            pat_base = spat | (uint16_t)((tile_idx + 1) * 32);
            local_y -= 8;
        }

        /* Draw 8 pixels */
        for (int px = 0; px < 8; px++) {
            int screen_x = sx + px;
            if (screen_x >= VDP_SCREEN_W) break;
            if (screen_x < 0) continue;

            uint16_t tile_addr = pat_base + (uint16_t)(local_y * 4);
            int      bit       = 7 - px;
            uint8_t c4 = (uint8_t)(((vdp->vram[tile_addr]     >> bit) & 1)
                                 | (((vdp->vram[tile_addr + 1] >> bit) & 1) << 1)
                                 | (((vdp->vram[tile_addr + 2] >> bit) & 1) << 2)
                                 | (((vdp->vram[tile_addr + 3] >> bit) & 1) << 3));

            if (c4 == 0) continue;  /* transparent */

            /* Sprite collision: two non-transparent sprites on same pixel */
            if (vdp->sprite_line[screen_x] != 0xFF)
                vdp->status |= VDP_STAT_COLLIDE;
            else
                vdp->sprite_line[screen_x] = (uint8_t)(16 + c4); /* sprite CRAM offset */
        }
    }
}

/* ── Merge background and sprite layers ──────────────────────────────── */
static void merge_layers(VDP *vdp, int line)
{
    uint16_t *row = vdp->framebuf + line * VDP_SCREEN_W;
    for (int x = 0; x < VDP_SCREEN_W; x++) {
        uint8_t sp = vdp->sprite_line[x];
        if (sp == 0xFF) continue;            /* no sprite here */
        if (vdp->priority_line[x]) continue; /* BG priority wins */
        row[x] = vdp->cram_cache[sp];
    }
}

/* ── Render one visible scanline ─────────────────────────────────────── */
static void render_scanline(VDP *vdp, int line)
{
    if (vdp->skip_render) return;  /* frameskip — reuse previous frame */

    if (!(vdp->reg[1] & VDP_R1_BLANK)) {
        /* Display disabled — fill with border color from R7 */
        uint16_t border = vdp->cram_cache[vdp->reg[7] & 0x0F];
        uint16_t *row = vdp->framebuf + line * VDP_SCREEN_W;
        for (int x = 0; x < VDP_SCREEN_W; x++) row[x] = border;
        return;
    }

    fill_background(vdp, line);
    draw_sprites(vdp, line);
    merge_layers(vdp, line);
}

/* ── Public API ──────────────────────────────────────────────────────── */

void vdp_reset(VDP *vdp, uint16_t *framebuf)
{
    memset(vdp, 0, sizeof(VDP));
    vdp->framebuf     = framebuf;
    vdp->line_counter = 0xFF;    /* will reload from R10 on first line */
    /* Power-on: display is disabled (R1 bit 6 = 0) */
}

uint8_t vdp_read_data(VDP *vdp)
{
    /* The VDP pre-fetches: return the buffered byte, then load the next */
    uint8_t val      = vdp->read_buf;
    vdp->read_buf    = vdp->vram[vdp->addr & (VDP_VRAM_SIZE - 1)];
    vdp->addr        = (vdp->addr + 1) & (VDP_VRAM_SIZE - 1);
    vdp->latch       = 0;  /* reset address latch after any data access */
    return val;
}

void vdp_write_data(VDP *vdp, uint8_t val)
{
    vdp->latch = 0;  /* reset latch */
    if (vdp->code == 3) {
        /* CRAM write — update raw byte and RGB565 cache together */
        uint8_t idx = vdp->addr & (VDP_CRAM_SIZE - 1);
        vdp->cram[idx]       = val;
        vdp->cram_cache[idx] = cram_to_rgb565(val);
    } else {
        /* VRAM write */
        vdp->vram[vdp->addr & (VDP_VRAM_SIZE - 1)] = val;
    }
    vdp->read_buf = val;  /* update read buffer to written value */
    vdp->addr     = (vdp->addr + 1) & (VDP_VRAM_SIZE - 1);
}

uint8_t vdp_read_status(VDP *vdp)
{
    uint8_t s    = vdp->status;
    vdp->status &= ~(VDP_STAT_INT | VDP_STAT_OVERFLOW | VDP_STAT_COLLIDE);
    vdp->frame_irq = 0;
    vdp->line_irq  = 0;
    vdp->latch     = 0;  /* reading status resets the address latch */
    return s;
}

void vdp_write_ctrl(VDP *vdp, uint8_t val)
{
    if (!vdp->latch) {
        /* First byte: latch it and wait for second */
        vdp->first_byte = val;
        vdp->latch      = 1;
    } else {
        /* Second byte:
         *   bits 7-6 = code:
         *     00 = VRAM read  (also pre-fetches one byte)
         *     01 = VRAM write
         *     10 = register write  (val[3:0] = reg index, first_byte = value)
         *     11 = CRAM write
         */
        vdp->latch = 0;
        vdp->code  = (val >> 6) & 3;

        if (vdp->code == 2) {
            /* Register write: first_byte = data, val & 0x0F = register index */
            int ridx = val & 0x0F;
            if (ridx < VDP_NUM_REGS)
                vdp->reg[ridx] = vdp->first_byte;
        } else {
            /* Address write: combine to 14-bit address */
            vdp->addr = (uint16_t)(vdp->first_byte | ((val & 0x3F) << 8));
            if (vdp->code == 0) {
                /* VRAM read: pre-fetch byte at address, advance address */
                vdp->read_buf = vdp->vram[vdp->addr & (VDP_VRAM_SIZE - 1)];
                vdp->addr     = (vdp->addr + 1) & (VDP_VRAM_SIZE - 1);
            }
        }
    }
}

/* ── vdp_tick ─────────────────────────────────────────────────────────── *
 *
 * The SMS runs at a master clock of ~10.74 MHz.  The Z80 runs at 1/3 of
 * that, and the VDP effectively runs at the same rate as the CPU for timing
 * purposes in this emulator.
 *
 * We track cycles within a scanline.  When we've accumulated 228 cycles we
 * advance the scanline, check for interrupts, and (if in active display)
 * render a line.
 *
 * Line interrupt:
 *   The line counter (reg[10]) decrements each active scanline.
 *   When it underflows (was 0, now -1), a line IRQ fires and the counter
 *   reloads from reg[10].
 *   During VBlank the counter reloads from reg[10] but does NOT decrement.
 *
 * Frame interrupt:
 *   Fires at the beginning of scanline 192 (first VBlank line).
 *
 * Returns a bitmask: bit 0 = frame IRQ asserted, bit 1 = line IRQ asserted.
 */
int vdp_tick(VDP *vdp, int cycles)
{
    int irqs = 0;
    vdp->cycle += cycles;

    while (vdp->cycle >= VDP_CYCLES_LINE) {
        vdp->cycle -= VDP_CYCLES_LINE;

        int line = vdp->scanline;

        /* ── Line counter / line interrupt ──────────────────────────── */
        if (line < VDP_VBLANK_LINE) {
            /* Active display: decrement counter */
            if (vdp->line_counter == 0) {
                /* Counter expires: fire line IRQ if enabled */
                if (vdp->reg[0] & VDP_R0_IE1) {
                    vdp->line_irq = 1;
                    irqs |= 2;
                }
                vdp->line_counter = vdp->reg[10];  /* reload */
            } else {
                vdp->line_counter--;
            }
        } else if (line == VDP_VBLANK_LINE) {
            /* First VBlank line: reload counter, fire frame interrupt */
            vdp->line_counter = vdp->reg[10];
            vdp->status      |= VDP_STAT_INT;
            if (vdp->reg[1] & VDP_R1_IE0) {
                vdp->frame_irq = 1;
                irqs |= 1;
            }
        }

        /* ── Render the current line ─────────────────────────────────── */
        if (line < VDP_VBLANK_LINE) {
            render_scanline(vdp, line);
        }

        /* ── Advance scanline ───────────────────────────────────────── */
        vdp->scanline++;
        if (vdp->scanline >= VDP_SCANLINES) {
            vdp->scanline = 0;
            /* Clear per-frame collision/overflow at frame wrap */
            vdp->status &= ~(VDP_STAT_OVERFLOW | VDP_STAT_COLLIDE);
        }
    }

    return irqs;
}
