/**
 * @file save_state.c
 * @brief Pack/unpack in-progress state (see save_state.h for the pointer-
 * safety rationale and the akira_api.h single-inclusion constraint).
 *
 * @license Apache-2.0
 */
#include "save_state.h"

/* akira_api.h's freestanding declarations (e.g. a non-static printf defined
 * directly in the header) may only be included once per app (main.c) —
 * duplicate-symbol at link time otherwise. Use builtins instead. */
#define memset  __builtin_memset
#define memcpy  __builtin_memcpy

extern const uint32_t rom_size;

#define NES_SAVESTATE_MAGIC   0x314E4B41u /* "AKN1" */
#define NES_SAVESTATE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t rom_size;      /* must match the currently loaded ROM */
    int32_t  mapper_id;     /* sanity check against the loaded mapper */

    /* CPU */
    uint16_t cpu_pc;
    uint8_t  cpu_sp, cpu_a, cpu_x, cpu_y, cpu_p;

    /* PPU */
    uint8_t  ppu_ctrl, ppu_mask, ppu_status, ppu_oam_addr;
    uint16_t ppu_v, ppu_t;
    uint8_t  ppu_fine_x, ppu_latch, ppu_read_buf;
    uint8_t  ppu_vram[2048];
    uint8_t  ppu_palette[32];
    uint8_t  ppu_oam[256];
    int32_t  ppu_scanline, ppu_cycle, ppu_odd_frame, ppu_dots, ppu_nmi_pending;

    /* Work/save RAM */
    uint8_t  wram[2048];
    uint8_t  sram[8192];

    /* Controller shift registers */
    uint8_t  ctrl_latch;
    uint8_t  ctrl_shift[2];
    uint8_t  ctrl_state[2];

    /* Mapper (pointer-free fields only) */
    int32_t  mirroring;
    int32_t  irq_pending;
    uint8_t  chr_ram[8192];
    uint8_t  mapper_s[32]; /* raw bytes of Mapper.s (largest member is 8 bytes; padded) */
} NESSaveState;

static NESSaveState g_state_buf;

int nes_state_size(void) { return (int)sizeof(NESSaveState); }
void *nes_state_buf(void) { return &g_state_buf; }

void nes_state_pack(const NES *nes)
{
    NESSaveState *st = &g_state_buf;
    memset(st, 0, sizeof(*st));

    st->magic     = NES_SAVESTATE_MAGIC;
    st->version   = NES_SAVESTATE_VERSION;
    st->rom_size  = rom_size;
    st->mapper_id = nes->mapper.id;

    st->cpu_pc = nes->cpu.pc;
    st->cpu_sp = nes->cpu.sp;
    st->cpu_a  = nes->cpu.a;
    st->cpu_x  = nes->cpu.x;
    st->cpu_y  = nes->cpu.y;
    st->cpu_p  = nes->cpu.p;

    st->ppu_ctrl     = nes->ppu.ctrl;
    st->ppu_mask     = nes->ppu.mask;
    st->ppu_status   = nes->ppu.status;
    st->ppu_oam_addr = nes->ppu.oam_addr;
    st->ppu_v        = nes->ppu.v;
    st->ppu_t        = nes->ppu.t;
    st->ppu_fine_x   = nes->ppu.fine_x;
    st->ppu_latch    = nes->ppu.latch;
    st->ppu_read_buf = nes->ppu.read_buf;
    memcpy(st->ppu_vram,    nes->ppu.vram,    sizeof(st->ppu_vram));
    memcpy(st->ppu_palette, nes->ppu.palette, sizeof(st->ppu_palette));
    memcpy(st->ppu_oam,     nes->ppu.oam,     sizeof(st->ppu_oam));
    st->ppu_scanline    = nes->ppu.scanline;
    st->ppu_cycle       = nes->ppu.cycle;
    st->ppu_odd_frame   = nes->ppu.odd_frame;
    st->ppu_dots        = nes->ppu.dots;
    st->ppu_nmi_pending = nes->ppu.nmi_pending;

    memcpy(st->wram, nes->wram, sizeof(st->wram));
    memcpy(st->sram, nes->sram, sizeof(st->sram));

    st->ctrl_latch = nes->ctrl_latch;
    memcpy(st->ctrl_shift, nes->ctrl_shift, sizeof(st->ctrl_shift));
    memcpy(st->ctrl_state, nes->ctrl_state, sizeof(st->ctrl_state));

    st->mirroring   = nes->mapper.mirroring;
    st->irq_pending = nes->mapper.irq_pending;
    memcpy(st->chr_ram, nes->mapper.chr_ram, sizeof(st->chr_ram));
    _Static_assert(sizeof(nes->mapper.s) <= sizeof(st->mapper_s),
                   "mapper_s buffer too small for Mapper.s");
    memcpy(st->mapper_s, &nes->mapper.s, sizeof(nes->mapper.s));
}

int nes_state_unpack(NES *nes)
{
    NESSaveState *st = &g_state_buf;

    if (st->magic != NES_SAVESTATE_MAGIC || st->version != NES_SAVESTATE_VERSION)
        return -1;
    if (st->rom_size != rom_size || st->mapper_id != nes->mapper.id)
        return -1;

    nes->cpu.pc = st->cpu_pc;
    nes->cpu.sp = st->cpu_sp;
    nes->cpu.a  = st->cpu_a;
    nes->cpu.x  = st->cpu_x;
    nes->cpu.y  = st->cpu_y;
    nes->cpu.p  = st->cpu_p;

    nes->ppu.ctrl     = st->ppu_ctrl;
    nes->ppu.mask     = st->ppu_mask;
    nes->ppu.status   = st->ppu_status;
    nes->ppu.oam_addr = st->ppu_oam_addr;
    nes->ppu.v        = st->ppu_v;
    nes->ppu.t        = st->ppu_t;
    nes->ppu.fine_x   = st->ppu_fine_x;
    nes->ppu.latch    = st->ppu_latch;
    nes->ppu.read_buf = st->ppu_read_buf;
    memcpy(nes->ppu.vram,    st->ppu_vram,    sizeof(st->ppu_vram));
    memcpy(nes->ppu.palette, st->ppu_palette, sizeof(st->ppu_palette));
    memcpy(nes->ppu.oam,     st->ppu_oam,     sizeof(st->ppu_oam));
    nes->ppu.scanline    = st->ppu_scanline;
    nes->ppu.cycle       = st->ppu_cycle;
    nes->ppu.odd_frame   = st->ppu_odd_frame;
    nes->ppu.dots        = st->ppu_dots;
    nes->ppu.nmi_pending = st->ppu_nmi_pending;

    memcpy(nes->wram, st->wram, sizeof(st->wram));
    memcpy(nes->sram, st->sram, sizeof(st->sram));

    nes->ctrl_latch = st->ctrl_latch;
    memcpy(nes->ctrl_shift, st->ctrl_shift, sizeof(st->ctrl_shift));
    memcpy(nes->ctrl_state, st->ctrl_state, sizeof(st->ctrl_state));

    nes->mapper.mirroring   = st->mirroring;
    nes->mapper.irq_pending = st->irq_pending;
    memcpy(nes->mapper.chr_ram, st->chr_ram, sizeof(st->chr_ram));
    memcpy(&nes->mapper.s, st->mapper_s, sizeof(nes->mapper.s));

    /* Bank-select state changed; rebuild the cached page pointer tables
     * against this run's prg_rom/chr_rom (pointers are never persisted). */
    mapper_resync(&nes->mapper);

    return 0;
}
