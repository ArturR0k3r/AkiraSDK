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

#define SMS_SAVESTATE_MAGIC   0x314E4B41u /* "AKN1" (shared convention w/ NES/GB) */
#define SMS_SAVESTATE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t rom_size;
    uint32_t num_pages;

    /* Z80 */
    uint8_t  z_a, z_f, z_b, z_c, z_d, z_e, z_h, z_l;
    uint8_t  z_a2, z_f2, z_b2, z_c2, z_d2, z_e2, z_h2, z_l2;
    uint16_t z_ix, z_iy, z_sp, z_pc;
    uint8_t  z_i, z_r, z_iff1, z_iff2, z_im, z_halted;
    uint8_t  z_nmi_pending, z_irq_pending;

    /* VDP */
    uint8_t  vdp_reg[VDP_NUM_REGS];
    uint8_t  vdp_vram[VDP_VRAM_SIZE];
    uint8_t  vdp_cram[VDP_CRAM_SIZE];
    uint8_t  vdp_status;
    uint16_t vdp_addr;
    uint8_t  vdp_first_byte, vdp_latch, vdp_code, vdp_read_buf;
    int32_t  vdp_line_counter;
    uint8_t  vdp_frame_irq, vdp_line_irq;
    int32_t  vdp_scanline, vdp_cycle;
    uint16_t vdp_cram_cache[VDP_CRAM_SIZE];

    /* Mapper (pointer-free fields only) */
    uint8_t  mapper_bank[3];
    uint8_t  mapper_ram_select;

    /* Work RAM */
    uint8_t  wram[8192];

    /* Controller / counters */
    uint8_t  pad1, pad2, vcounter, hcounter;
} SMSSaveState;

static SMSSaveState g_state_buf;

int sms_state_size(void) { return (int)sizeof(SMSSaveState); }
void *sms_state_buf(void) { return &g_state_buf; }

void sms_state_pack(const SMS *sms)
{
    SMSSaveState *st = &g_state_buf;
    memset(st, 0, sizeof(*st));

    st->magic     = SMS_SAVESTATE_MAGIC;
    st->version   = SMS_SAVESTATE_VERSION;
    st->rom_size  = sms->mapper.rom_size;
    st->num_pages = sms->mapper.num_pages;

    st->z_a=sms->cpu.a; st->z_f=sms->cpu.f; st->z_b=sms->cpu.b; st->z_c=sms->cpu.c;
    st->z_d=sms->cpu.d; st->z_e=sms->cpu.e; st->z_h=sms->cpu.h; st->z_l=sms->cpu.l;
    st->z_a2=sms->cpu.a2; st->z_f2=sms->cpu.f2; st->z_b2=sms->cpu.b2; st->z_c2=sms->cpu.c2;
    st->z_d2=sms->cpu.d2; st->z_e2=sms->cpu.e2; st->z_h2=sms->cpu.h2; st->z_l2=sms->cpu.l2;
    st->z_ix=sms->cpu.ix; st->z_iy=sms->cpu.iy; st->z_sp=sms->cpu.sp; st->z_pc=sms->cpu.pc;
    st->z_i=sms->cpu.i; st->z_r=sms->cpu.r;
    st->z_iff1=sms->cpu.iff1; st->z_iff2=sms->cpu.iff2; st->z_im=sms->cpu.im;
    st->z_halted=sms->cpu.halted;
    st->z_nmi_pending=sms->cpu.nmi_pending; st->z_irq_pending=sms->cpu.irq_pending;

    memcpy(st->vdp_reg,  sms->vdp.reg,  sizeof(st->vdp_reg));
    memcpy(st->vdp_vram, sms->vdp.vram, sizeof(st->vdp_vram));
    memcpy(st->vdp_cram, sms->vdp.cram, sizeof(st->vdp_cram));
    st->vdp_status      = sms->vdp.status;
    st->vdp_addr        = sms->vdp.addr;
    st->vdp_first_byte  = sms->vdp.first_byte;
    st->vdp_latch       = sms->vdp.latch;
    st->vdp_code        = sms->vdp.code;
    st->vdp_read_buf    = sms->vdp.read_buf;
    st->vdp_line_counter= sms->vdp.line_counter;
    st->vdp_frame_irq   = sms->vdp.frame_irq;
    st->vdp_line_irq    = sms->vdp.line_irq;
    st->vdp_scanline    = sms->vdp.scanline;
    st->vdp_cycle       = sms->vdp.cycle;
    memcpy(st->vdp_cram_cache, sms->vdp.cram_cache, sizeof(st->vdp_cram_cache));

    memcpy(st->mapper_bank, sms->mapper.bank, sizeof(st->mapper_bank));
    st->mapper_ram_select = sms->mapper.ram_select;

    memcpy(st->wram, sms->wram, sizeof(st->wram));

    st->pad1 = sms->pad1; st->pad2 = sms->pad2;
    st->vcounter = sms->vcounter; st->hcounter = sms->hcounter;
}

int sms_state_unpack(SMS *sms)
{
    SMSSaveState *st = &g_state_buf;

    if (st->magic != SMS_SAVESTATE_MAGIC || st->version != SMS_SAVESTATE_VERSION)
        return -1;
    if (st->rom_size != sms->mapper.rom_size || st->num_pages != sms->mapper.num_pages)
        return -1;

    sms->cpu.a=st->z_a; sms->cpu.f=st->z_f; sms->cpu.b=st->z_b; sms->cpu.c=st->z_c;
    sms->cpu.d=st->z_d; sms->cpu.e=st->z_e; sms->cpu.h=st->z_h; sms->cpu.l=st->z_l;
    sms->cpu.a2=st->z_a2; sms->cpu.f2=st->z_f2; sms->cpu.b2=st->z_b2; sms->cpu.c2=st->z_c2;
    sms->cpu.d2=st->z_d2; sms->cpu.e2=st->z_e2; sms->cpu.h2=st->z_h2; sms->cpu.l2=st->z_l2;
    sms->cpu.ix=st->z_ix; sms->cpu.iy=st->z_iy; sms->cpu.sp=st->z_sp; sms->cpu.pc=st->z_pc;
    sms->cpu.i=st->z_i; sms->cpu.r=st->z_r;
    sms->cpu.iff1=st->z_iff1; sms->cpu.iff2=st->z_iff2; sms->cpu.im=st->z_im;
    sms->cpu.halted=st->z_halted;
    sms->cpu.nmi_pending=st->z_nmi_pending; sms->cpu.irq_pending=st->z_irq_pending;

    memcpy(sms->vdp.reg,  st->vdp_reg,  sizeof(st->vdp_reg));
    memcpy(sms->vdp.vram, st->vdp_vram, sizeof(st->vdp_vram));
    memcpy(sms->vdp.cram, st->vdp_cram, sizeof(st->vdp_cram));
    sms->vdp.status       = st->vdp_status;
    sms->vdp.addr         = st->vdp_addr;
    sms->vdp.first_byte   = st->vdp_first_byte;
    sms->vdp.latch        = st->vdp_latch;
    sms->vdp.code         = st->vdp_code;
    sms->vdp.read_buf     = st->vdp_read_buf;
    sms->vdp.line_counter = st->vdp_line_counter;
    sms->vdp.frame_irq    = st->vdp_frame_irq;
    sms->vdp.line_irq     = st->vdp_line_irq;
    sms->vdp.scanline     = st->vdp_scanline;
    sms->vdp.cycle        = st->vdp_cycle;
    memcpy(sms->vdp.cram_cache, st->vdp_cram_cache, sizeof(st->vdp_cram_cache));

    memcpy(sms->mapper.bank, st->mapper_bank, sizeof(st->mapper_bank));
    sms->mapper.ram_select = st->mapper_ram_select;

    memcpy(sms->wram, st->wram, sizeof(st->wram));

    sms->pad1 = st->pad1; sms->pad2 = st->pad2;
    sms->vcounter = st->vcounter; sms->hcounter = st->hcounter;

    /* Bank-select state changed; rebuild the cached page pointers against
     * this run's rom pointer (pointers are never persisted). */
    mapper_resync(&sms->mapper);

    return 0;
}
