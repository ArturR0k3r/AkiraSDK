/**
 * @file vdp.h
 * @brief SMS Video Display Processor (TMS9918A derivative — Mode 4)
 *
 * The SMS VDP is similar to the NES PPU in concept:
 *   - A tilemap ("name table") describes the background
 *   - Up to 64 sprites are stored in a Sprite Attribute Table (SAT)
 *
 * Key differences from NES PPU:
 *   - Registers are accessed through the Z80 I/O space (ports $BE/$BF),
 *     NOT memory-mapped
 *   - VRAM is 16KB (vs NES 8KB), accessed through a data port with a
 *     two-byte address latch
 *   - Color: 32-entry CRAM (6-bit BGR each), 16 for BG tiles + 16 for sprites
 *   - Name table entry is 2 bytes with flip bits and a palette select bit
 *   - Tile patterns are 4-bitplane (4 color bits per pixel = 16 colors)
 *   - Active display is 256×192 pixels (NTSC); 256×224 also possible
 *
 * Register summary:
 *   R0 — Mode Control 1  (IE1, no-sync, M1/M2/M3 mode bits, overscan/blank)
 *   R1 — Mode Control 2  (display enable, IE0, sprite size, sprite magnify)
 *   R2 — Name table base address (bits shifted into VRAM address)
 *   R3 — (Mode 4: unused legacy from TMS9918)
 *   R4 — (Mode 4: only bit 2 used for sprite pattern base extension)
 *   R5 — Sprite attribute table base address
 *   R6 — Sprite pattern generator base address
 *   R7 — Overscan/border color (index into palette 1)
 *   R8 — Background horizontal scroll (pixels; larger = scrolls right)
 *   R9 — Background vertical scroll (pixels; larger = scrolls down)
 *   R10 — Line interrupt counter reload value
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>

/* ── Dimensions ──────────────────────────────────────────────────────── */
#define VDP_SCREEN_W      256   /**< Active pixel columns                 */
#define VDP_SCREEN_H      192   /**< Active scanlines (192-line mode)     */
#define VDP_VRAM_SIZE     0x4000/**< 16 KB video RAM                      */
#define VDP_CRAM_SIZE     32    /**< 32 color entries (6-bit BGR each)    */
#define VDP_NUM_REGS      11    /**< R0–R10                               */
#define VDP_MAX_SPRITES   64    /**< Sprites in the attribute table       */
#define VDP_SPRITES_PER_LINE 8  /**< Max sprites drawn per scanline       */

/* ── Timing (NTSC) ───────────────────────────────────────────────────── */
#define VDP_SCANLINES     262   /**< Total scanlines per frame (NTSC)     */
#define VDP_CYCLES_LINE   228   /**< Z80 cycles per scanline              */
#define VDP_VBLANK_LINE   192   /**< First VBlank scanline                */

/* ── Register bit definitions ────────────────────────────────────────── */
/* R0 — Mode Control 1 */
#define VDP_R0_IE1    0x10  /**< Enable line interrupts                   */
#define VDP_R0_M4     0x04  /**< Enable Mode 4 (must be set for SMS)      */
#define VDP_R0_M2     0x02  /**< Mode 2 bit (selects 224/240-line height) */

/* R1 — Mode Control 2 */
#define VDP_R1_BLANK  0x40  /**< Display active (0 = forced blank)        */
#define VDP_R1_IE0    0x20  /**< Enable frame (VBlank) interrupt          */
#define VDP_R1_SPH    0x02  /**< Sprite height: 0=8×8, 1=8×16            */
#define VDP_R1_MAG    0x01  /**< Sprite magnification (double size)       */

/* Status register bits */
#define VDP_STAT_INT      0x80  /**< Frame interrupt pending              */
#define VDP_STAT_OVERFLOW 0x40  /**< Sprite overflow (>8 on a line)       */
#define VDP_STAT_COLLIDE  0x20  /**< Sprite collision                     */

/* Name table entry bits (16-bit word, little-endian in VRAM) */
#define NT_PAT_MASK   0x01FF /**< Bits 8-0: pattern index (0-511)         */
#define NT_H_FLIP     0x0200 /**< Bit 9: horizontal flip                  */
#define NT_V_FLIP     0x0400 /**< Bit 10: vertical flip                   */
#define NT_PRIORITY   0x0800 /**< Bit 11: priority (drawn over sprites)   */
#define NT_PALETTE    0x1000 /**< Bit 12: palette (0=colors 0-15, 1=16-31)*/

/* ── VDP state ───────────────────────────────────────────────────────── */
typedef struct {
    /* Registers and memory */
    uint8_t  reg[VDP_NUM_REGS];     /**< Control registers R0–R10         */
    uint8_t  vram[VDP_VRAM_SIZE];   /**< 16KB video RAM                   */
    uint8_t  cram[VDP_CRAM_SIZE];   /**< Color RAM (32 × 6-bit entries)   */

    /* Status */
    uint8_t  status;                /**< Status register (read clears INT) */

    /* Address latch — VDP uses a 2-byte write sequence to set VRAM addr.
     * First write: low byte of address (or low byte of register write)
     * Second write: sets addr_high and code (code 2=reg write, 3=CRAM, etc.)
     */
    uint16_t addr;                  /**< Current VRAM/CRAM address        */
    uint8_t  first_byte;            /**< Latched first byte of cmd pair   */
    uint8_t  latch;                 /**< 0=waiting for first byte, 1=got it*/
    uint8_t  code;                  /**< 0=VRAMrd 1=VRAMwr 2=reg 3=CRAM  */
    uint8_t  read_buf;              /**< Pre-fetched byte for data reads   */

    /* Line interrupt counter */
    int      line_counter;          /**< Counts down; reload from R10     */

    /* Interrupt output flags */
    uint8_t  frame_irq;             /**< Frame interrupt is pending       */
    uint8_t  line_irq;              /**< Line interrupt is pending        */

    /* Rendering */
    int      scanline;              /**< Current scanline (0-261)         */
    int      cycle;                 /**< Cycle within current scanline    */
    uint16_t *framebuf;             /**< Output: VDP_SCREEN_W×VDP_SCREEN_H RGB565 */

    /* Sprite priority scratch buffer — avoids reading VRAM twice per line */
    uint8_t  sprite_line[VDP_SCREEN_W]; /**< Sprite pixels for this line  */
    uint8_t  priority_line[VDP_SCREEN_W]; /**< Priority flags for BG pixels*/

    /* Render control */
    uint8_t  skip_render;               /**< Non-zero → skip scanline rendering */

    /* Pre-converted RGB565 CRAM cache — rebuilt whenever CRAM is written.
     * Avoids per-pixel cram_to_rgb565() in fill_background/merge_layers. */
    uint16_t cram_cache[VDP_CRAM_SIZE];
} VDP;

/* ── API ─────────────────────────────────────────────────────────────── */

/** Reset VDP to power-on state.  framebuf must point to a caller-allocated
 *  VDP_SCREEN_W × VDP_SCREEN_H array of uint16_t (RGB565 pixels). */
void vdp_reset(VDP *vdp, uint16_t *framebuf);

/** Called by sms.c I/O handler for port $BE (data port) reads. */
uint8_t vdp_read_data(VDP *vdp);

/** Called by sms.c I/O handler for port $BE (data port) writes. */
void    vdp_write_data(VDP *vdp, uint8_t val);

/** Called by sms.c I/O handler for port $BF (control port) reads.
 *  Returns status byte and clears the frame-interrupt flag. */
uint8_t vdp_read_status(VDP *vdp);

/** Called by sms.c I/O handler for port $BF (control port) writes.
 *  Two bytes must be written in sequence to set address or register. */
void    vdp_write_ctrl(VDP *vdp, uint8_t val);

/** Advance VDP by the given number of Z80 cycles.
 *  Renders scanlines and fires IRQs as needed.
 *  Returns a bitmask: bit 0 = frame IRQ pending, bit 1 = line IRQ pending. */
int vdp_tick(VDP *vdp, int cycles);

/** Query / clear IRQ flags (used by sms.c to drive z80_irq) */
static inline int  vdp_frame_irq_pending(VDP *vdp) { return vdp->frame_irq; }
static inline int  vdp_line_irq_pending (VDP *vdp) { return vdp->line_irq;  }
static inline void vdp_clear_frame_irq  (VDP *vdp) { vdp->frame_irq = 0;    }
static inline void vdp_clear_line_irq   (VDP *vdp) { vdp->line_irq  = 0;    }
