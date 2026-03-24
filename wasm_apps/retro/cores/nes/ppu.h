/**
 * @file ppu.h
 * @brief NES PPU (Picture Processing Unit) for AkiraOS retro emulator
 *
 * Implements background tile rendering with horizontal and vertical
 * scrolling, 64-sprite OAM rendering, and the standard NES NTSC
 * colour palette output as RGB565.
 *
 * Timing model: frame-based (not cycle-exact).  One full frame is
 * rendered each time ppu_run() accumulates enough CPU cycles.
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>
#include "mapper.h"

/* ── Screen dimensions ──────────────────────────────────────────────── */
#define NES_W  256
#define NES_H  240

/* ── PPUCTRL ($2000) flags ──────────────────────────────────────────── */
#define CTRL_NMI_EN    0x80  /**< Enable NMI at VBlank              */
#define CTRL_SPR_SIZE  0x20  /**< 0=8×8, 1=8×16 sprites            */
#define CTRL_BG_TABLE  0x10  /**< BG pattern table: 0=$0000, 1=$1000*/
#define CTRL_SP_TABLE  0x08  /**< Sprite PT: 0=$0000, 1=$1000       */
#define CTRL_VRAM_INC  0x04  /**< VRAM addr increment: 0=+1, 1=+32 */
#define CTRL_NT_MASK   0x03  /**< Base nametable (0-3)              */

/* ── PPUMASK ($2001) flags ──────────────────────────────────────────── */
#define MASK_SHOW_SP_L 0x04  /**< Show sprites in leftmost 8 pixels */
#define MASK_SHOW_BG_L 0x02  /**< Show BG in leftmost 8 pixels      */
#define MASK_SHOW_SP   0x10  /**< Render sprites                    */
#define MASK_SHOW_BG   0x08  /**< Render background                 */

/* ── PPUSTATUS ($2002) flags ────────────────────────────────────────── */
#define STAT_VBLANK    0x80
#define STAT_SP0_HIT   0x40
#define STAT_SP_OFLOW  0x20

typedef struct {
    /* Registers visible to CPU */
    uint8_t  ctrl;
    uint8_t  mask;
    uint8_t  status;
    uint8_t  oam_addr;

    /* Internal address registers (t/v/x/w as per NesDev wiki) */
    uint16_t v;          /**< Current VRAM address (15-bit)         */
    uint16_t t;          /**< Temporary VRAM address (15-bit)       */
    uint8_t  fine_x;     /**< Fine X scroll (3-bit)                 */
    uint8_t  latch;      /**< Write toggle (0 or 1)                 */

    /* Data read buffer */
    uint8_t  read_buf;

    /* Memory */
    uint8_t  vram[2048]; /**< 2KB nametable RAM                     */
    uint8_t  palette[32];/**< 32-byte palette RAM                   */
    uint8_t  oam[256];   /**< Object Attribute Memory (64 sprites)  */

    /* Mapper reference for CHR access */
    Mapper  *mapper;

    /* Scanline / cycle counters */
    int      scanline;   /**< 0-261 (240=post-render, 241=vblank)   */
    int      cycle;      /**< 0-340 PPU dots per scanline           */
    int      odd_frame;  /**< Odd-frame flag (affects pre-render)   */

    /* Accumulated PPU dot counter (PPU runs 3× faster than CPU) */
    int      dots;       /**< Total PPU dots since last frame start */

    /* Pending signals */
    int      nmi_pending;/**< Set when NMI should fire              */

    /* Frameskip: when set, skip pixel rendering but still track timing
     * and sprite-0 hit detection so game logic proceeds correctly.    */
    uint8_t  skip_render;

    /* Output frame buffer (RGB565, NES_W × NES_H pixels) */
    uint16_t *fb;
} PPU;

/** Initialise PPU, link to mapper and output framebuffer. */
void    ppu_init     (PPU *ppu, Mapper *mapper, uint16_t *fb);

/** CPU reads PPU register 0-7 (addr $2000-$2007). */
uint8_t ppu_reg_read (PPU *ppu, int reg);

/** CPU writes PPU register 0-7. */
void    ppu_reg_write(PPU *ppu, int reg, uint8_t val);

/** OAM DMA: copy 256 bytes from @p page into OAM. */
void    ppu_oam_dma  (PPU *ppu, const uint8_t *page);

/**
 * Advance PPU by cpu_cycles CPU clocks (= cpu_cycles × 3 PPU dots).
 * Renders scanlines as they become due.
 * @return Non-zero if a frame has just completed (display_bitmap ready).
 */
int     ppu_run      (PPU *ppu, int cpu_cycles);
