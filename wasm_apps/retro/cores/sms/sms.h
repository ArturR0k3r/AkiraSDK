/**
 * @file sms.h
 * @brief Sega Master System machine top-level
 *
 * Ties together the Z80 CPU, VDP, Sega mapper, PSG (stub), and I/O ports
 * into a complete SMS machine.
 *
 * Memory map:
 *   $0000–$03FF  ROM slot 0 low  (always maps to ROM page 0, first 1KB)
 *   $0400–$3FFF  ROM slot 0 high (mapper bank 0, remaining 15KB)
 *   $4000–$7FFF  ROM slot 1       (mapper bank 1, 16KB)
 *   $8000–$BFFF  ROM slot 2       (mapper bank 2, 16KB)
 *   $C000–$DFFF  Work RAM         (8KB, mirrored at…)
 *   $E000–$FFFF  Work RAM mirror
 *   $FFFC–$FFFF  Sega mapper control registers (overlaid on RAM mirror)
 *
 * I/O port assignments (Z80 IN/OUT):
 *   $7E        — VDP V-counter read
 *   $7F        — VDP H-counter read
 *   $BE        — VDP data port (read/write)
 *   $BF / $BD  — VDP control port (read=status, write=ctrl)
 *   $DC / $DD  — Joypad ports 1 and 2
 *   $DE        — (write) I/O port control (mostly unused on SMS)
 *   $F0–$F3    — FM sound chip (if present; stubbed here)
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>
#include "z80.h"
#include "vdp.h"
#include "mapper.h"

/* ── Controller button bitmask ────────────────────────────────────────── *
 * Joypad port $DC returns active-LOW bits (0 = pressed).
 * Bit layout for port 1 ($DC):
 *   bit 7 — always 1 (unused)
 *   bit 6 — always 1 (unused)
 *   bit 5 — P2 Fire B
 *   bit 4 — P2 Fire A
 *   bit 3 — P1 Right
 *   bit 2 — P1 Left
 *   bit 1 — P1 Down
 *   bit 0 — P1 Up
 * Port $DD:
 *   bit 7 — TH output P2
 *   bit 6 — TH output P1
 *   bit 5 — always 1
 *   bit 4 — always 1
 *   bit 3 — P2 Right
 *   bit 2 — P2 Left
 *   bit 1 — P2 Down
 *   bit 0 — P2 Up
 *
 * We use a simple flat bitmask for the host to set buttons.
 * 0 = not pressed, 1 = pressed.
 */
#define SMS_BTN_UP     0x01
#define SMS_BTN_DOWN   0x02
#define SMS_BTN_LEFT   0x04
#define SMS_BTN_RIGHT  0x08
#define SMS_BTN_1      0x10   /**< Fire button 1 */
#define SMS_BTN_2      0x20   /**< Fire button 2 */
#define SMS_BTN_START  0x40   /**< Pause button (triggers NMI, not I/O) */

/* ── Framebuffer geometry ────────────────────────────────────────────── */
#define SMS_W    256   /**< Active display width  */
#define SMS_H    192   /**< Active display height (192-line NTSC mode) */
#define SMS_FB_PIXELS (SMS_W * SMS_H)
#define SMS_FB_BYTES  (SMS_FB_PIXELS * 2)

typedef struct {
    Z80      cpu;
    VDP      vdp;
    Mapper   mapper;

    uint8_t  wram[8192];   /**< 8KB work RAM at $C000–$DFFF (mirrored)   */

    /* Controller state — set by sms_set_buttons() */
    uint8_t  pad1;         /**< Player 1 button bitmask (SMS_BTN_* flags) */
    uint8_t  pad2;         /**< Player 2 button bitmask                   */

    /* V-counter and H-counter — incremented by VDP tick, read on port $7E/$7F */
    uint8_t  vcounter;
    uint8_t  hcounter;

    /* RGB565 output framebuffer (SMS_W × SMS_H) */
    uint16_t fb[SMS_FB_PIXELS];

    /* Emulator control */
    int      quit;
    int      frame_ready;  /**< Set to 1 each time a full frame is rendered */
} SMS;

/**
 * Initialise the SMS machine.
 * @param sms      Machine state to initialise (caller allocates).
 * @param rom      Raw SMS ROM image (must stay valid for machine lifetime).
 * @param rom_size Size of ROM in bytes.
 * @return 0 on success, -1 on failure.
 */
int  sms_init(SMS *sms, const uint8_t *rom, uint32_t rom_size);

/**
 * Set player button state.  Call each frame before sms_step_frame().
 * @param sms     Machine.
 * @param player  0=player1  1=player2
 * @param mask    OR'd SMS_BTN_* flags for currently pressed buttons.
 */
void sms_set_buttons(SMS *sms, int player, uint8_t mask);

/**
 * Run the machine until exactly one video frame is complete (192 scanlines
 * + VBlank).  Writes RGB565 pixels to sms->fb.
 */
void sms_step_frame(SMS *sms);

/* ── Memory/IO callbacks (called by z80.c) ──────────────────────────── *
 * These are extern-declared in z80.h and implemented in sms.c.
 * The ctx pointer passed to z80_step is always the SMS* machine pointer.
 */
uint8_t sms_mem_read (uint16_t addr, void *ctx);
void    sms_mem_write(uint16_t addr, uint8_t val, void *ctx);
uint8_t sms_io_read  (uint8_t  port, void *ctx);
void    sms_io_write (uint8_t  port, uint8_t val, void *ctx);
