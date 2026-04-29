/**
 * @file sms.c
 * @brief Sega Master System machine top-level implementation
 *
 * Implements the four Z80 bus callbacks (sms_mem_read/write, sms_io_read/write)
 * and the two public functions sms_init() and sms_step_frame().
 *
 * Timing: the SMS Z80 runs at ~3.58 MHz.  Each scanline takes 228 Z80 cycles.
 * sms_step_frame() runs exactly VDP_SCANLINES (262) scanlines per call, which
 * is one NTSC frame.  The VDP is ticked in lockstep with the CPU — after every
 * z80_step() we call vdp_tick() with the same cycle count.
 *
 * @license Apache-2.0
 */
#include "sms.h"
#include <string.h>

/* ── Memory read ──────────────────────────────────────────────────────── *
 *
 * Address decoding:
 *   $0000–$BFFF  ROM (3 × 16KB banks via Sega mapper)
 *   $C000–$DFFF  8KB work RAM
 *   $E000–$FFFF  Work RAM mirror (writes to $FFFC–$FFFF hit mapper regs)
 *
 * The first 1KB ($0000–$03FF) always maps to ROM page 0 regardless of
 * the bank register.  This protects the interrupt vectors.
 */
uint8_t sms_mem_read(uint16_t addr, void *ctx)
{
    SMS *sms = (SMS *)ctx;

    if (addr < 0xC000u) {
        /* ROM area — delegate entirely to mapper */
        return mapper_read(&sms->mapper, addr);
    }
    /* Work RAM (and its mirror) */
    return sms->wram[addr & 0x1FFF];
}

/* ── Memory write ─────────────────────────────────────────────────────── *
 *
 * Only RAM and mapper registers are writable.  Writes to ROM are silently
 * ignored (some games try them on cartridge SRAM — not implemented here).
 *
 * Sega mapper registers sit at:
 *   $FFFC — RAM select / ROM bank 2 extra bit
 *   $FFFD — Bank register for slot 0 ($0000–$3FFF, partial: low 15KB)
 *   $FFFE — Bank register for slot 1 ($4000–$7FFF)
 *   $FFFF — Bank register for slot 2 ($8000–$BFFF)
 *
 * Writing to the RAM mirror ($E000–$FFFF) writes to the underlying RAM AND
 * checks whether we have also hit a mapper register.
 */
void sms_mem_write(uint16_t addr, uint8_t val, void *ctx)
{
    SMS *sms = (SMS *)ctx;

    if (addr < 0xC000u) {
        /* ROM area — ignore (or handle cartridge RAM if added later) */
        return;
    }

    /* Write to RAM (both physical $C000 and mirror $E000 go to same bytes) */
    sms->wram[addr & 0x1FFF] = val;

    /* Sega mapper register intercept — only at the top 4 bytes of the mirror */
    if (addr >= 0xFFFC) {
        mapper_write(&sms->mapper, addr, val);
    }
}

/* ── I/O read ─────────────────────────────────────────────────────────── *
 *
 * The SMS has an 8-bit I/O space.  The Z80's A0 line determines whether
 * the access is to "even" or "odd" ports but the SMS hardware only decodes
 * the upper bits.  We match the standard port decode:
 *
 *   $7E        V-counter (scanline number as seen by the game)
 *   $7F        H-counter
 *   $BE        VDP data port
 *   $BF / $BD  VDP status port
 *   $DC / $C0  Joypad port 1: bits[5:0] = /fire2 /fire1 /right /left /down /up
 *   $DD / $C1  Joypad port 2: bits[3:0] = P2 directions; bits[5:4] = P1 fire
 *
 * All joypad bits are active-LOW: 0 means the button IS pressed.
 */
uint8_t sms_io_read(uint8_t port, void *ctx)
{
    SMS *sms = (SMS *)ctx;

    /* SMS hardware port decode uses bits 7-6 and bit 0 (even/odd):
     *   $00-$3F  Memory control / I/O direction (return 0xFF)
     *   $40-$7F  V-counter (even) / H-counter (odd)
     *   $80-$BF  VDP data (even) / VDP status (odd)
     *   $C0-$FF  Joypad 1 (even) / Joypad 2 (odd)
     */
    if (port < 0x40) {
        return 0xFF;
    }
    if (port < 0x80) {
        /* $40-$7F: counters */
        return (port & 1) ? sms->hcounter : sms->vcounter;
    }
    if (port < 0xC0) {
        /* $80-$BF: VDP */
        return (port & 1) ? vdp_read_status(&sms->vdp)
                          : vdp_read_data(&sms->vdp);
    }
    /* $C0-$FF: joypad */
    if (port & 1) {
        /* Joypad port 2 */
        uint8_t v = 0xFF;
        if (sms->pad2 & SMS_BTN_UP)    v &= ~0x01;
        if (sms->pad2 & SMS_BTN_DOWN)  v &= ~0x02;
        if (sms->pad2 & SMS_BTN_LEFT)  v &= ~0x04;
        if (sms->pad2 & SMS_BTN_RIGHT) v &= ~0x08;
        return v;
    } else {
        /* Joypad port 1 */
        uint8_t v = 0xFF;
        if (sms->pad1 & SMS_BTN_UP)    v &= ~0x01;
        if (sms->pad1 & SMS_BTN_DOWN)  v &= ~0x02;
        if (sms->pad1 & SMS_BTN_LEFT)  v &= ~0x04;
        if (sms->pad1 & SMS_BTN_RIGHT) v &= ~0x08;
        if (sms->pad1 & SMS_BTN_1)     v &= ~0x10;
        if (sms->pad1 & SMS_BTN_2)     v &= ~0x20;
        return v;
    }
}

/* ── I/O write ────────────────────────────────────────────────────────── *
 *
 *   $7E / $7F   — write to H-counter latch (rarely used; ignore)
 *   $BE         — VDP data port write
 *   $BF / $BD   — VDP control port write
 *   $DE         — I/O port control (enable/disable keyboard; ignore)
 *   $F0–$F3     — YM2413 FM chip (stub; ignore)
 *   $3F         — I/O port A/B direction (stub)
 */
void sms_io_write(uint8_t port, uint8_t val, void *ctx)
{
    SMS *sms = (SMS *)ctx;

    /* SMS hardware port decode — same range logic as reads:
     *   $00-$3F  Memory control / I/O direction (ignored)
     *   $40-$7F  PSG / H-counter latch (ignored)
     *   $80-$BF  VDP data (even) / VDP control (odd)
     *   $C0-$FF  I/O port control (ignored)
     */
    if (port >= 0x80 && port < 0xC0) {
        if (port & 1) {
            vdp_write_ctrl(&sms->vdp, val);
        } else {
            vdp_write_data(&sms->vdp, val);
        }
    }
    /* All other ports silently ignored */
}

/* ── sms_init ─────────────────────────────────────────────────────────── */
int sms_init(SMS *sms, const uint8_t *rom, uint32_t rom_size)
{
    memset(sms, 0, sizeof(SMS));

    if (mapper_init(&sms->mapper, rom, rom_size) != 0) {
        return -1;
    }

    vdp_reset(&sms->vdp, sms->fb);
    z80_reset(&sms->cpu);

    /* Initial controller state: all bits high (no buttons pressed) */
    sms->pad1 = 0;
    sms->pad2 = 0;

    return 0;
}

/* ── sms_set_buttons ──────────────────────────────────────────────────── */
void sms_set_buttons(SMS *sms, int player, uint8_t mask)
{
    if (player == 0) sms->pad1 = mask;
    else             sms->pad2 = mask;
}

/* ── sms_step_frame ───────────────────────────────────────────────────── *
 *
 * Run the machine for one full frame.
 *
 * Timing loop:
 *   For each of the 262 scanlines:
 *     Run enough CPU steps to fill 228 cycles.
 *     Tick VDP those same cycles — it renders and fires IRQs.
 *     If VDP fired frame IRQ  → call z80_irq()
 *     If VDP fired line IRQ   → call z80_irq()
 *     If Start button pressed → call z80_nmi()
 *
 * We use a cycle-accumulator approach: run one z80_step() at a time,
 * accumulate cycles, stop when we reach the scanline budget (228).
 * Any overshoot carries into the next scanline.
 */
void sms_step_frame(SMS *sms)
{
    /* Cycles per frame = 228 × 262 */
    static const int CYCLES_PER_FRAME = VDP_CYCLES_LINE * VDP_SCANLINES;

    int frame_cycles = 0;
    sms->frame_ready = 0;

    /* Track Start button for NMI edge detection (pause button on SMS) */
    static int prev_start = 0;
    int start_now = (sms->pad1 & SMS_BTN_START) ? 1 : 0;
    if (start_now && !prev_start) {
        z80_nmi(&sms->cpu, sms);
    }
    prev_start = start_now;

    while (frame_cycles < CYCLES_PER_FRAME) {
        /* Step the CPU one instruction */
        int cycles = z80_step(&sms->cpu, sms);

        /* Tick VDP by the same number of cycles */
        vdp_tick(&sms->vdp, cycles);

        /* Deliver IRQs — use persistent flags (level-triggered).
         * The VDP INT line stays asserted until the CPU reads the status
         * register (vdp_read_status clears frame_irq/line_irq).  This
         * ensures that if the CPU has interrupts disabled when the VDP
         * first fires, the IRQ will be delivered as soon as EI runs. */
        if (sms->vdp.frame_irq || sms->vdp.line_irq) {
            z80_irq(&sms->cpu, sms);
        }

        /* Update counters */
        sms->vcounter = (uint8_t)(sms->vdp.scanline & 0xFF);
        /* H-counter: the SMS H-counter advances ~1.5× per Z80 cycle.
         * Using frame_cycles (which wraps at 256 as uint8_t) ensures the
         * counter cycles through all values 0-255 each frame, so games
         * polling for a specific H-counter value (e.g. Sonic 2 boot sync
         * loop at 0x0003 waits for 0xB0) will eventually exit. */
        sms->hcounter = (uint8_t)frame_cycles;
        frame_cycles += cycles;
    }

    sms->frame_ready = 1;
}
