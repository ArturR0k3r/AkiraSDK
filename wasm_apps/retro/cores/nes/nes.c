/**
 * @file nes.c
 * @brief NES machine implementation for AkiraOS retro emulator
 *
 * CPU memory map:
 *   $0000-$07FF  2KB WRAM (mirrored to $1FFF)
 *   $2000-$2007  PPU registers (mirrored to $3FFF)
 *   $4000-$4013  APU channels (stub — writes ignored, reads return 0)
 *   $4014        OAM DMA
 *   $4015        APU status (stub)
 *   $4016        Controller 1 strobe/read
 *   $4017        Controller 2 read / APU frame counter
 *   $4020-$FFFF  Cartridge (mapper)
 *
 * @license Apache-2.0
 */
#include "nes.h"

/* ── CPU memory bus callbacks ────────────────────────────────────────── */

static uint8_t cpu_read(uint16_t addr, void *ctx)
{
    NES *n = (NES *)ctx;

    if (addr < 0x2000u)
        return n->wram[addr & 0x7FFu]; /* 2KB WRAM mirrored */

    if (addr < 0x4000u)
        return ppu_reg_read(&n->ppu, (addr - 0x2000u) & 7); /* PPU registers */

    if (addr == 0x4016u) {
        /* Controller 1 */
        uint8_t bit = (n->ctrl_shift[0] >> 7) & 1;
        n->ctrl_shift[0] <<= 1;
        return bit | 0x40u; /* open-bus high bits */
    }
    if (addr == 0x4017u) {
        /* Controller 2 */
        uint8_t bit = (n->ctrl_shift[1] >> 7) & 1;
        n->ctrl_shift[1] <<= 1;
        return bit | 0x40u;
    }
    if (addr >= 0x4000u && addr < 0x4020u)
        return 0; /* APU stub */

    return mapper_cpu_read(&n->mapper, addr);
}

static void cpu_write(uint16_t addr, uint8_t val, void *ctx)
{
    NES *n = (NES *)ctx;

    if (addr < 0x2000u) {
        n->wram[addr & 0x7FFu] = val;
        return;
    }
    if (addr < 0x4000u) {
        ppu_reg_write(&n->ppu, (addr - 0x2000u) & 7, val);
        return;
    }
    if (addr == 0x4014u) {
        /* OAM DMA: copy 256 bytes from CPU page val*$100 */
        uint16_t base = (uint16_t)val << 8;
        /* Inline copy — reads must go through the bus */
        uint8_t page[256];
        for (int i = 0; i < 256; i++)
            page[i] = cpu_read((uint16_t)(base + i), ctx);
        ppu_oam_dma(&n->ppu, page);
        /* DMA stalls the CPU for 513 (or 514 on odd PPU cycles) cycles.
         * Signal this to nes_step_frame so the PPU sees the full penalty. */
        n->dma_stall = 513;
        return;
    }
    if (addr == 0x4016u) {
        /* Controller strobe */
        if (val & 1) {
            n->ctrl_latch = 1;
        } else if (n->ctrl_latch) {
            /* Latch button state into shift registers on falling edge */
            n->ctrl_shift[0] = n->ctrl_state[0];
            n->ctrl_shift[1] = n->ctrl_state[1];
            n->ctrl_latch = 0;
        }
        return;
    }
    if (addr >= 0x4000u && addr < 0x4020u)
        return; /* APU stub */

    mapper_cpu_write(&n->mapper, addr, val);
}

/* ── Public API ──────────────────────────────────────────────────────── */

int nes_init(NES *nes, const uint8_t *rom, uint32_t rom_size)
{
    /* Zero all fields */
    uint8_t *p = (uint8_t *)nes;
    for (int i = 0; i < (int)sizeof(NES); i++) p[i] = 0;

    /* Initialise mapper */
    if (mapper_init(&nes->mapper, rom, (int)rom_size) != 0)
        return -1;

    /* Initialise PPU with mapper and framebuffer */
    ppu_init(&nes->ppu, &nes->mapper, nes->fb);

    /* Initialise CPU and trigger reset sequence */
    cpu6502_reset(&nes->cpu, cpu_read, nes);

    return 0;
}

void nes_set_controller(NES *nes, int port, uint8_t mask)
{
    if (port >= 0 && port < 2)
        nes->ctrl_state[port] = mask;
}

void nes_step_frame(NES *nes)
{
    int frame_done = 0;
    while (!frame_done) {
        /* Check for PPU NMI */
        if (nes->ppu.nmi_pending) {
            nes->ppu.nmi_pending = 0;
            cpu6502_nmi(&nes->cpu, cpu_read, cpu_write, nes);
        }

        /* Check for mapper IRQ (e.g. MMC3 scanline counter) */
        if (mapper_irq_pending(&nes->mapper))
            cpu6502_irq(&nes->cpu, cpu_read, cpu_write, nes);

        /* Run one CPU instruction */
        int cycles = cpu6502_step(&nes->cpu, cpu_read, cpu_write, nes);

        /* Account for OAM DMA stall cycles (set in cpu_write on $4014) */
        cycles += nes->dma_stall;
        nes->dma_stall = 0;

        /* Advance PPU; it returns 1 when a frame has been completed */
        frame_done = ppu_run(&nes->ppu, cycles);
    }
}
