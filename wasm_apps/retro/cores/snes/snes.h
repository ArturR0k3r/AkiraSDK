/**
 * @file snes.h
 * @brief SNES machine for AkiraOS retro emulator (65816 + PPU + DMA)
 *
 * Scanline-based, not cycle-exact. No SPC700/DSP: the APU is a stub that
 * answers the IPL boot handshake and then echoes the CPU->APU ports.
 * Output is RGB565 (native byte order), SNES_W x SNES_H.
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>

/* The AkiraOS display driver sends pixels unswapped, so wasm builds pre-swap
 * the RGB565 bytes (same convention as the NES core). Host builds stay native. */
#ifdef __wasm__
#define SNES_SWAP565 1
#endif

#define SNES_W 256
#define SNES_H 224

/* Joypad bits, as returned by the auto-joypad registers ($4218) */
#define SNES_BTN_B      0x8000
#define SNES_BTN_Y      0x4000
#define SNES_BTN_SELECT 0x2000
#define SNES_BTN_START  0x1000
#define SNES_BTN_UP     0x0800
#define SNES_BTN_DOWN   0x0400
#define SNES_BTN_LEFT   0x0200
#define SNES_BTN_RIGHT  0x0100
#define SNES_BTN_A      0x0080
#define SNES_BTN_X      0x0040
#define SNES_BTN_L      0x0020
#define SNES_BTN_R      0x0010

typedef struct {
    uint16_t a, x, y, s, d, pc;
    uint8_t  db, pb;
    uint8_t  fC, fZ, fI, fD, fX, fM, fV, fN, fE;
    uint8_t  nmi, wai, stp;
} CPU65816;

typedef struct {
    uint8_t  dmap, bbad;
    uint16_t a1t;
    uint8_t  a1b;
    uint16_t das;
    uint8_t  dasb;
    uint16_t a2a;
    uint8_t  ntrl;
    uint8_t  unused;
    uint8_t  hdma_do, hdma_term;
} DMACh;

typedef struct {
    uint8_t  inidisp, obsel, bgmode, mosaic;
    uint8_t  bgsc[4], bg12nba, bg34nba;
    uint16_t hofs[4], vofs[4];
    uint8_t  bgofs_latch;
    uint8_t  vmain;
    uint16_t vmaddr, vram_prefetch;
    uint8_t  m7sel, m7_latch;
    int16_t  m7[4];
    int16_t  m7x, m7y, m7hofs, m7vofs;
    uint8_t  cgadd, cg_latch, cg_flip;
    uint8_t  w12sel, w34sel, wobjsel, wh[4], wbglog, wobjlog;
    uint8_t  tm, ts, tmw, tsw;
    uint8_t  cgwsel, cgadsub, fix_r, fix_g, fix_b, setini;
    uint16_t oamadd, oamaddr;
    uint8_t  oam_latch;
    int32_t  mpy;
    uint8_t  hv_latch_flip, ophct_flip, opvct_flip;
    uint16_t hcount, vcount;
    uint32_t vram_ver;       /* bumped on every VRAM data write (renderer cache key) */
    uint16_t vram[32768];
    uint16_t cgram[256];
    uint8_t  oam[544];
} PPU;

typedef struct SNES {
    CPU65816 cpu;
    PPU      ppu;
    DMACh    dma[8];

    uint8_t  wram[0x20000];
    uint8_t  sram[0x8000];
    uint32_t sram_mask;
    const uint8_t *rom;
    uint32_t rom_size, rom_mask;
    int      hirom;

    /* 24-bit WRAM port ($2180-$2183) */
    uint32_t wmadd;

    /* CPU I/O ($4200-$421F) */
    uint8_t  nmitimen, rdnmi, timeup, hvbjoy;
    uint8_t  wrmpya, wrdiv_lo, wrdiv_hi;
    uint16_t rddiv, rdmpy;
    uint16_t htime, vtime;
    uint8_t  memsel;
    uint8_t  mdmaen, hdmaen;
    uint8_t  irq_line;

    /* Joypads */
    uint16_t pad[2];          /* host-provided state                      */
    uint16_t joy_auto[2];     /* latched by auto-read                     */
    uint16_t joy_shift[2];
    uint8_t  joy_strobe;

    /* APU stub */
    uint8_t  apu_out[4], apu_in[4];
    uint8_t  apu_running;

    /* Timing */
    int32_t  hc;              /* master cycles into the current scanline  */
    int      line;
    uint32_t frame;
    uint8_t  open_bus;
    uint8_t  skip_render;     /* host-set: skip PPU line rendering (frameskip) */

    /* Idle-loop detection: bumped on every bus write / time-varying I/O read */
    uint32_t wr_count, vol_reads;

    /* Bus fast path: 8 KB pages; non-NULL = plain memory (WRAM/ROM/SRAM) */
    uint8_t *rd_page[2048];
    uint8_t *wr_page[2048];
    uint8_t  spd_page[2048];

    uint16_t fb[SNES_W * SNES_H];
} SNES;

/** Initialise from a ROM image (with or without 512-byte copier header). */
int  snes_init(SNES *s, const uint8_t *rom, uint32_t size);
void snes_reset(SNES *s);
/** Run one full video frame; result in s->fb. */
void snes_run_frame(SNES *s);
void snes_set_pad(SNES *s, int port, uint16_t mask);
void snes_rebuild_pages(SNES *s);

/* Bus (used by the CPU core and DMA). Reads/writes account for access time. */
uint8_t snes_read(SNES *s, uint32_t addr);
void    snes_write(SNES *s, uint32_t addr, uint8_t v);

/* CPU */
void cpu65816_reset(SNES *s);
void cpu65816_step(SNES *s);

/* PPU */
void ppu_reset(PPU *p);
uint8_t ppu_read(SNES *s, uint16_t reg);
void    ppu_write(SNES *s, uint16_t reg, uint8_t v);
void    ppu_render_line(SNES *s, int y);
void    ppu_vblank_start(SNES *s);
