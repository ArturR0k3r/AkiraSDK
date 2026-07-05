/**
 * @file save_state.c
 * @brief Pack/unpack in-progress state (see save_state.h for the pointer-
 * safety rationale, the cart_ram streaming rationale, and the akira_api.h
 * single-inclusion constraint).
 *
 * @license Apache-2.0
 */
#include "save_state.h"

/* akira_api.h's freestanding declarations (e.g. a non-static printf defined
 * directly in the header) may only be included once per app (main.c) —
 * duplicate-symbol at link time otherwise. Use builtins instead. */
#define memset  __builtin_memset
#define memcpy  __builtin_memcpy

#define GB_SAVESTATE_MAGIC   0x314E4B41u /* "AKN1" (shared convention w/ NES) */
#define GB_SAVESTATE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t rom_size;
    uint32_t rom_banks;
    uint32_t mbc_type;
    uint32_t cart_ram_size;  /* how many extra bytes follow in storage */

    /* CPU */
    uint8_t  cpu_a, cpu_f, cpu_b, cpu_c, cpu_d, cpu_e, cpu_h, cpu_l;
    uint16_t cpu_sp, cpu_pc;
    uint8_t  cpu_ime, cpu_halted, cpu_halt_bug;

    /* PPU */
    uint32_t ppu_dot;
    uint8_t  ppu_mode, ppu_window_ly, ppu_window_active;
    uint8_t  ppu_bg_pal_ram[64], ppu_obj_pal_ram[64];
    uint16_t ppu_bg_pal[8][4], ppu_obj_pal[8][4];
    uint8_t  ppu_bg_pal_idx, ppu_obj_pal_idx;

    /* Memory */
    uint8_t  vram[GB_VRAM_SIZE];
    uint8_t  wram[GB_WRAM_SIZE];
    uint8_t  oam[GB_OAM_SIZE];
    uint8_t  hram[GB_HRAM_SIZE];
    uint8_t  io[0x80];
    uint8_t  ie;

    /* MBC state */
    uint16_t rom_bank;
    uint8_t  ram_bank, ram_enable, mbc1_hi, mbc1_mode;

    /* CGB */
    uint8_t  cgb_mode, double_speed, speed_prep, vram_bank, wram_bank;

    /* Timer */
    uint16_t div_counter;
    uint8_t  tima_reload_cnt;

    /* OAM DMA */
    uint8_t  dma_active, dma_pos;
    uint16_t dma_src;

    /* HDMA */
    uint8_t  hdma_active;
    uint16_t hdma_src, hdma_dst, hdma_remain;

    /* Serial */
    int32_t  serial_timer;

    /* Joypad */
    uint8_t  joy_state;
} GBSaveState;

static GBSaveState g_state_buf;

int gb_state_header_size(void) { return (int)sizeof(GBSaveState); }
void *gb_state_header_buf(void) { return &g_state_buf; }

void gb_state_pack(const GB *gb)
{
    GBSaveState *st = &g_state_buf;
    memset(st, 0, sizeof(*st));

    st->magic         = GB_SAVESTATE_MAGIC;
    st->version       = GB_SAVESTATE_VERSION;
    st->rom_size      = gb->rom_size;
    st->rom_banks     = gb->rom_banks;
    st->mbc_type      = gb->mbc_type;
    st->cart_ram_size = gb->cart_ram_size;

    st->cpu_a = gb->cpu.a; st->cpu_f = gb->cpu.f;
    st->cpu_b = gb->cpu.b; st->cpu_c = gb->cpu.c;
    st->cpu_d = gb->cpu.d; st->cpu_e = gb->cpu.e;
    st->cpu_h = gb->cpu.h; st->cpu_l = gb->cpu.l;
    st->cpu_sp = gb->cpu.sp; st->cpu_pc = gb->cpu.pc;
    st->cpu_ime = gb->cpu.ime; st->cpu_halted = gb->cpu.halted;
    st->cpu_halt_bug = gb->cpu.halt_bug;

    st->ppu_dot            = gb->ppu.dot;
    st->ppu_mode           = gb->ppu.mode;
    st->ppu_window_ly      = gb->ppu.window_ly;
    st->ppu_window_active  = gb->ppu.window_active;
    memcpy(st->ppu_bg_pal_ram,  gb->ppu.bg_pal_ram,  sizeof(st->ppu_bg_pal_ram));
    memcpy(st->ppu_obj_pal_ram, gb->ppu.obj_pal_ram, sizeof(st->ppu_obj_pal_ram));
    memcpy(st->ppu_bg_pal,  gb->ppu.bg_pal,  sizeof(st->ppu_bg_pal));
    memcpy(st->ppu_obj_pal, gb->ppu.obj_pal, sizeof(st->ppu_obj_pal));
    st->ppu_bg_pal_idx  = gb->ppu.bg_pal_idx;
    st->ppu_obj_pal_idx = gb->ppu.obj_pal_idx;

    memcpy(st->vram, gb->vram, sizeof(st->vram));
    memcpy(st->wram, gb->wram, sizeof(st->wram));
    memcpy(st->oam,  gb->oam,  sizeof(st->oam));
    memcpy(st->hram, gb->hram, sizeof(st->hram));
    memcpy(st->io,   gb->io,   sizeof(st->io));
    st->ie = gb->ie;

    st->rom_bank   = gb->rom_bank;
    st->ram_bank   = gb->ram_bank;
    st->ram_enable = gb->ram_enable;
    st->mbc1_hi    = gb->mbc1_hi;
    st->mbc1_mode  = gb->mbc1_mode;

    st->cgb_mode     = gb->cgb_mode;
    st->double_speed = gb->double_speed;
    st->speed_prep   = gb->speed_prep;
    st->vram_bank    = gb->vram_bank;
    st->wram_bank    = gb->wram_bank;

    st->div_counter     = gb->div_counter;
    st->tima_reload_cnt = gb->tima_reload_cnt;

    st->dma_active = gb->dma_active;
    st->dma_src    = gb->dma_src;
    st->dma_pos    = gb->dma_pos;

    st->hdma_active = gb->hdma_active;
    st->hdma_src    = gb->hdma_src;
    st->hdma_dst    = gb->hdma_dst;
    st->hdma_remain = gb->hdma_remain;

    st->serial_timer = gb->serial_timer;
    st->joy_state    = gb->joy_state;
}

int gb_state_unpack(GB *gb)
{
    GBSaveState *st = &g_state_buf;

    if (st->magic != GB_SAVESTATE_MAGIC || st->version != GB_SAVESTATE_VERSION)
        return -1;
    if (st->rom_size != gb->rom_size || st->mbc_type != gb->mbc_type)
        return -1;
    if (st->cart_ram_size > GB_CART_RAM_MAX)
        return -1;

    gb->cpu.a = st->cpu_a; gb->cpu.f = st->cpu_f;
    gb->cpu.b = st->cpu_b; gb->cpu.c = st->cpu_c;
    gb->cpu.d = st->cpu_d; gb->cpu.e = st->cpu_e;
    gb->cpu.h = st->cpu_h; gb->cpu.l = st->cpu_l;
    gb->cpu.sp = st->cpu_sp; gb->cpu.pc = st->cpu_pc;
    gb->cpu.ime = st->cpu_ime; gb->cpu.halted = st->cpu_halted;
    gb->cpu.halt_bug = st->cpu_halt_bug;

    gb->ppu.dot            = st->ppu_dot;
    gb->ppu.mode           = st->ppu_mode;
    gb->ppu.window_ly      = st->ppu_window_ly;
    gb->ppu.window_active  = st->ppu_window_active;
    memcpy(gb->ppu.bg_pal_ram,  st->ppu_bg_pal_ram,  sizeof(st->ppu_bg_pal_ram));
    memcpy(gb->ppu.obj_pal_ram, st->ppu_obj_pal_ram, sizeof(st->ppu_obj_pal_ram));
    memcpy(gb->ppu.bg_pal,  st->ppu_bg_pal,  sizeof(st->ppu_bg_pal));
    memcpy(gb->ppu.obj_pal, st->ppu_obj_pal, sizeof(st->ppu_obj_pal));
    gb->ppu.bg_pal_idx  = st->ppu_bg_pal_idx;
    gb->ppu.obj_pal_idx = st->ppu_obj_pal_idx;

    memcpy(gb->vram, st->vram, sizeof(st->vram));
    memcpy(gb->wram, st->wram, sizeof(st->wram));
    memcpy(gb->oam,  st->oam,  sizeof(st->oam));
    memcpy(gb->hram, st->hram, sizeof(st->hram));
    memcpy(gb->io,   st->io,   sizeof(st->io));
    gb->ie = st->ie;

    gb->rom_bank   = st->rom_bank;
    gb->ram_bank   = st->ram_bank;
    gb->ram_enable = st->ram_enable;
    gb->mbc1_hi    = st->mbc1_hi;
    gb->mbc1_mode  = st->mbc1_mode;

    gb->cgb_mode     = st->cgb_mode;
    gb->double_speed = st->double_speed;
    gb->speed_prep   = st->speed_prep;
    gb->vram_bank    = st->vram_bank;
    gb->wram_bank    = st->wram_bank;

    gb->div_counter     = st->div_counter;
    gb->tima_reload_cnt = st->tima_reload_cnt;

    gb->dma_active = st->dma_active;
    gb->dma_src    = st->dma_src;
    gb->dma_pos    = st->dma_pos;

    gb->hdma_active = st->hdma_active;
    gb->hdma_src    = st->hdma_src;
    gb->hdma_dst    = st->hdma_dst;
    gb->hdma_remain = st->hdma_remain;

    gb->serial_timer = st->serial_timer;
    gb->joy_state    = st->joy_state;

    gb->cart_ram_size = st->cart_ram_size;

    return 0;
}
