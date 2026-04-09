/**
 * @file gb.h
 * @brief Game Boy / Game Boy Color emulator — shared types and declarations
 *
 * Supports:
 *   - DMG (original Game Boy) and CGB (Game Boy Color) modes
 *   - MBC: ROM-only, MBC1, MBC2, MBC3, MBC5
 *   - Scanline-based PPU with pixel-accurate mode transitions
 *   - CGB double-speed mode, VRAM/WRAM banking, HDMA
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ── Screen geometry ────────────────────────────────────────────────── */
#define GB_W           160
#define GB_H           144
#define GB_FB_BYTES    (GB_W * GB_H * 2)   /* RGB565 */

/* ── Timing (T-cycles = dots) ───────────────────────────────────────── */
#define GB_CLOCK_HZ    4194304u
#define GB_DOTS_LINE   456u       /* T-cycles per scanline */
#define GB_LINES       154u       /* Total scanlines per frame (0–153) */
#define GB_DOTS_FRAME  (GB_DOTS_LINE * GB_LINES)  /* 70224 */

/* PPU mode dot budgets per line */
#define PPU_OAM_DOTS      80u   /* Mode 2 */
#define PPU_DRAW_DOTS     172u  /* Mode 3 (simplified) */
#define PPU_HBLANK_DOTS   204u  /* Mode 0 = 456 - 80 - 172 */

/* ── LCDC register bits (0xFF40) ────────────────────────────────────── */
#define LCDC_BG_EN     0x01  /* BG+window enable (DMG) / master priority (CGB) */
#define LCDC_OBJ_EN    0x02  /* Sprite enable */
#define LCDC_OBJ_SIZE  0x04  /* 0=8×8  1=8×16 */
#define LCDC_BG_MAP    0x08  /* BG tilemap:   0=0x9800  1=0x9C00 */
#define LCDC_TILE_SEL  0x10  /* Tile data:    0=0x8800 signed  1=0x8000 unsigned */
#define LCDC_WIN_EN    0x20  /* Window enable */
#define LCDC_WIN_MAP   0x40  /* Window tilemap: 0=0x9800  1=0x9C00 */
#define LCDC_LCD_EN    0x80  /* LCD on/off */

/* ── STAT register bits (0xFF41) ────────────────────────────────────── */
#define STAT_MODE_MASK 0x03
#define STAT_LYC_FLAG  0x04
#define STAT_HBLANK_IE 0x08
#define STAT_VBLANK_IE 0x10
#define STAT_OAM_IE    0x20
#define STAT_LYC_IE    0x40

/* ── Interrupt flag / enable bits (0xFF0F / 0xFFFF) ─────────────────── */
#define INT_VBLANK     0x01
#define INT_STAT       0x02
#define INT_TIMER      0x04
#define INT_SERIAL     0x08
#define INT_JOYPAD     0x10

/* ── I/O register offsets from 0xFF00 ───────────────────────────────── */
#define IO_P1          0x00   /* Joypad */
#define IO_SB          0x01
#define IO_SC          0x02
#define IO_DIV         0x04
#define IO_TIMA        0x05
#define IO_TMA         0x06
#define IO_TAC         0x07
#define IO_IF          0x0F
/* 0x10–0x3F: sound */
#define IO_LCDC        0x40
#define IO_STAT        0x41
#define IO_SCY         0x42
#define IO_SCX         0x43
#define IO_LY          0x44
#define IO_LYC         0x45
#define IO_DMA         0x46
#define IO_BGP         0x47
#define IO_OBP0        0x48
#define IO_OBP1        0x49
#define IO_WY          0x4A
#define IO_WX          0x4B
#define IO_KEY1        0x4D   /* CGB: speed switch */
#define IO_VBK         0x4F   /* CGB: VRAM bank */
#define IO_HDMA1       0x51
#define IO_HDMA2       0x52
#define IO_HDMA3       0x53
#define IO_HDMA4       0x54
#define IO_HDMA5       0x55
#define IO_BCPS        0x68   /* CGB BG palette index */
#define IO_BCPD        0x69   /* CGB BG palette data */
#define IO_OCPS        0x6A   /* CGB OBJ palette index */
#define IO_OCPD        0x6B   /* CGB OBJ palette data */
#define IO_SVBK        0x70   /* CGB WRAM bank */

/* ── CPU flag bits (F register) ─────────────────────────────────────── */
#define FLAG_Z  0x80
#define FLAG_N  0x40
#define FLAG_H  0x20
#define FLAG_C  0x10

/* ── MBC type identifiers ────────────────────────────────────────────── */
#define MBC_NONE       0
#define MBC_1          1
#define MBC_2          2
#define MBC_3          3
#define MBC_5          5

/* ── Memory sizes ────────────────────────────────────────────────────── */
#define GB_VRAM_SIZE   (2 * 8192)    /* 2 banks × 8KB (16KB total) */
#define GB_WRAM_SIZE   (8 * 4096)    /* 8 banks × 4KB (32KB total) */
#define GB_OAM_SIZE    0xA0          /* 160 bytes */
#define GB_HRAM_SIZE   0x7F          /* 127 bytes */
#define GB_CART_RAM_MAX (128 * 1024) /* up to 128KB external RAM */

/* ── GB colors (DMG 4-shade palette, converted to RGB565) ────────────── */
/* Neutral greyscale — matches the visual appearance of the original GB LCD.
 * Values verified: R=G=B within RGB565's 5/6-bit precision.
 * 0xFFFF=white  0xC618=(197,194,197)  0x528A=(82,81,82)  0x0000=black */
#define DMG_WHITE       0xFFFFu
#define DMG_LIGHT_GRAY  0xC618u
#define DMG_DARK_GRAY   0x528Au
#define DMG_BLACK       0x0000u

/* ── SM83 CPU ────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t  a, f;    /* AF */
    uint8_t  b, c;    /* BC */
    uint8_t  d, e;    /* DE */
    uint8_t  h, l;    /* HL */
    uint16_t sp;
    uint16_t pc;
    uint8_t  ime;         /* 0=off  1=on  2=pending (EI effect, enable after next) */
    uint8_t  halted;
    uint8_t  halt_bug;    /* HALT bug: PC not incremented after next read */
} SM83;

/* ── PPU ─────────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t dot;           /* Dot counter within current frame (0..70223) */
    uint8_t  mode;          /* Current mode: 0=HBlank, 1=VBlank, 2=OAM, 3=Drawing */
    uint8_t  window_ly;     /* Window internal line counter */
    uint8_t  window_active; /* Window triggered on this frame */
    uint8_t  skip_render;   /* Non-zero → skip rendering (frameskip) */
    int      frame_ready;

    /* CGB color palette RAM */
    uint8_t  bg_pal_ram[64];    /* 8 palettes × 4 colors × 2 bytes (GBC 15-bit) */
    uint8_t  obj_pal_ram[64];
    uint16_t bg_pal[8][4];      /* Pre-converted RGB565 BG palettes */
    uint16_t obj_pal[8][4];     /* Pre-converted RGB565 OBJ palettes */
    uint8_t  bg_pal_idx;        /* BCPS auto-increment index */
    uint8_t  obj_pal_idx;       /* OCPS auto-increment index */
} PPU;

/* ── GB Machine ──────────────────────────────────────────────────────── */
typedef struct {
    SM83     cpu;
    PPU      ppu;

    /* RAM banks */
    uint8_t  vram[GB_VRAM_SIZE];
    uint8_t  wram[GB_WRAM_SIZE];
    uint8_t  oam[GB_OAM_SIZE];
    uint8_t  hram[GB_HRAM_SIZE];
    uint8_t  io[0x80];          /* 0xFF00–0xFF7F */
    uint8_t  ie;                /* 0xFFFF */

    /* Cartridge ROM (pointer into embedded rom_data[]) */
    const uint8_t *rom;
    uint32_t       rom_size;
    uint32_t       rom_banks;   /* Total 16KB ROM banks */

    /* Cartridge RAM */
    uint8_t  cart_ram[GB_CART_RAM_MAX];
    uint32_t cart_ram_size;

    /* MBC state */
    uint8_t  mbc_type;
    uint16_t rom_bank;          /* Active 4000–7FFF bank (1–N) */
    uint8_t  ram_bank;          /* Active A000–BFFF RAM bank */
    uint8_t  ram_enable;        /* Cart RAM access enabled */
    uint8_t  mbc1_hi;           /* MBC1: upper 2 ROM bank bits */
    uint8_t  mbc1_mode;         /* MBC1: 0=ROM banking, 1=RAM banking */

    /* CGB */
    uint8_t  cgb_mode;          /* Running as GBC */
    uint8_t  double_speed;      /* CGB double-speed active */
    uint8_t  speed_prep;        /* KEY1 bit 0: prepare speed switch */
    uint8_t  vram_bank;         /* 0 or 1 */
    uint8_t  wram_bank;         /* 1–7 (CGB); always 1 on DMG */

    /* Timer */
    uint16_t div_counter;       /* Internal 16-bit timer (DIV = high byte) */
    uint8_t  tima_reload_cnt;   /* Cycles until TIMA reload after overflow */

    /* OAM DMA */
    uint8_t  dma_active;
    uint16_t dma_src;           /* Source base address */
    uint8_t  dma_pos;           /* Bytes transferred so far (0–159) */

    /* HDMA (CGB) */
    uint8_t  hdma_active;       /* 1=general, 2=HBlank */
    uint16_t hdma_src;
    uint16_t hdma_dst;          /* Offset into VRAM (8000 base) */
    uint16_t hdma_remain;       /* Bytes remaining */

    /* Serial port */
    int      serial_timer;      /* T-cycles until INT_SERIAL fires (0 = idle) */

    /* Joypad */
    uint8_t  joy_state;         /* Pressed buttons: A|B|Sel|Start|Right|Left|Up|Down */

    /* Framebuffer */
    uint16_t fb[GB_W * GB_H];
} GB;

/* ── Joypad bit masks (joy_state) ────────────────────────────────────── */
#define GB_BTN_RIGHT   0x01
#define GB_BTN_LEFT    0x02
#define GB_BTN_UP      0x04
#define GB_BTN_DOWN    0x08
#define GB_BTN_A       0x10
#define GB_BTN_B       0x20
#define GB_BTN_SELECT  0x40
#define GB_BTN_START   0x80

/* ── Public API ──────────────────────────────────────────────────────── */
void    gb_init(GB *gb, const uint8_t *rom, uint32_t rom_size);
void    gb_step_frame(GB *gb);
void    gb_set_buttons(GB *gb, uint8_t btns);

/* Memory bus (used by CPU and PPU) */
uint8_t  gb_read(GB *gb, uint16_t addr);
void     gb_write(GB *gb, uint16_t addr, uint8_t val);

/* CPU step — returns T-cycles consumed */
int      sm83_step(GB *gb);

/* PPU tick */
void     ppu_tick(GB *gb, int dots);

/* ── CGB color conversion ─────────────────────────────────────────────
 * GBC 15-bit format: lo = GGGRRRRR, hi = 0BBBBBGG
 * → RGB565: R5 G6 B5
 */
static inline uint16_t cgb_to_rgb565(uint8_t lo, uint8_t hi)
{
    uint16_t r = lo & 0x1Fu;
    uint16_t g = (uint16_t)((lo >> 5) | ((hi & 0x03u) << 3));
    uint16_t b = (uint16_t)((hi >> 2) & 0x1Fu);
    /* RGB565: top 5=R, mid 6=G, bot 5=B.  Scale G from 5→6 bits. */
    return (uint16_t)((r << 11) | (g << 6) | b);
}

/* ── DMG shade → RGB565 ─────────────────────────────────────────────── */
static inline uint16_t dmg_shade(uint8_t shade)
{
    static const uint16_t lut[4] = {
        DMG_WHITE, DMG_LIGHT_GRAY, DMG_DARK_GRAY, DMG_BLACK
    };
    return lut[shade & 3];
}
