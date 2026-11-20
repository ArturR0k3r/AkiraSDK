/**
 * @file z80.h
 * @brief Zilog Z80 CPU — register file and interface
 *
 * The Z80 has more registers than the 6502:
 *   - A (accumulator) + F (flags)
 *   - Three 16-bit pairs: BC, DE, HL — each usable as two 8-bit regs
 *   - Shadow copies A'F' B'C' D'E' H'L' — swapped atomically by EXX/EX AF,AF'
 *   - Two index registers IX, IY — work like HL but with a signed byte offset
 *   - SP, PC — stack pointer and program counter
 *   - I (interrupt vector high byte), R (DRAM refresh counter)
 *
 * I/O is separate from memory on Z80.  The CPU has IN/OUT instructions that
 * address an 8-bit I/O space.  On the SMS this is how the game accesses the
 * VDP, PSG and joypad — NOT through memory-mapped registers.
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>

/* ── Flag bit positions in F register ────────────────────────────────── */
#define Z80_CF  0x01   /**< Carry                                         */
#define Z80_NF  0x02   /**< Add/Subtract: 0=add, 1=sub                   */
#define Z80_PF  0x04   /**< Parity/Overflow                               */
#define Z80_XF  0x08   /**< Undocumented: copy of bit 3 of result         */
#define Z80_HF  0x10   /**< Half-carry (carry out of bit 3)               */
#define Z80_YF  0x20   /**< Undocumented: copy of bit 5 of result         */
#define Z80_ZF  0x40   /**< Zero                                          */
#define Z80_SF  0x80   /**< Sign (= bit 7 of result)                      */

typedef struct {
    /* ── Main register file ──────────────────────────────────────────── */
    uint8_t  a, f;          /**< Accumulator and flags                    */
    uint8_t  b, c;          /**< BC pair — B = high, C = low              */
    uint8_t  d, e;          /**< DE pair                                  */
    uint8_t  h, l;          /**< HL pair — used as 16-bit memory pointer  */

    /* ── Shadow registers (swapped by EXX / EX AF,AF') ─────────────── */
    uint8_t  a2, f2;        /**< A' F'                                    */
    uint8_t  b2, c2;        /**< B' C'                                    */
    uint8_t  d2, e2;        /**< D' E'                                    */
    uint8_t  h2, l2;        /**< H' L'                                    */

    /* ── Index and special registers ────────────────────────────────── */
    uint16_t ix;            /**< Index register X (HL + signed offset)    */
    uint16_t iy;            /**< Index register Y                         */
    uint16_t sp;            /**< Stack pointer                            */
    uint16_t pc;            /**< Program counter                          */
    uint8_t  i;             /**< Interrupt vector high byte               */
    uint8_t  r;             /**< DRAM refresh counter (incremented per op)*/

    /* ── Interrupt control ───────────────────────────────────────────── */
    uint8_t  iff1;          /**< Interrupt flip-flop 1 (maskable IRQ gate)*/
    uint8_t  iff2;          /**< Interrupt flip-flop 2 (saved during NMI) */
    uint8_t  im;            /**< Interrupt mode: 0, 1, or 2               */
    uint8_t  halted;        /**< CPU is in HALT state (waiting for IRQ)   */

    /* ── Pending interrupts ──────────────────────────────────────────── */
    uint8_t  nmi_pending;   /**< NMI line was pulsed (non-maskable)       */
    uint8_t  irq_pending;   /**< /INT line is asserted (VDP frame/line)   */
} Z80;

/*
 * Helper macros to read/write 16-bit register pairs.
 * The Z80 stores pairs in big-endian order (high byte first in the struct)
 * but little-endian in memory during PUSH/POP.
 * These macros let us access e.g. z80->bc as a 16-bit value.
 */
#define Z80_BC(z) ((uint16_t)((z)->b << 8 | (z)->c))
#define Z80_DE(z) ((uint16_t)((z)->d << 8 | (z)->e))
#define Z80_HL(z) ((uint16_t)((z)->h << 8 | (z)->l))
#define Z80_AF(z) ((uint16_t)((z)->a << 8 | (z)->f))

/* NOTE: v must not be evaluated more than once — store it in a temp first */
#define Z80_SET_BC(z, v) do { uint16_t _v=(uint16_t)(v); (z)->b=_v>>8; (z)->c=_v&0xFF; } while(0)
#define Z80_SET_DE(z, v) do { uint16_t _v=(uint16_t)(v); (z)->d=_v>>8; (z)->e=_v&0xFF; } while(0)
#define Z80_SET_HL(z, v) do { uint16_t _v=(uint16_t)(v); (z)->h=_v>>8; (z)->l=_v&0xFF; } while(0)
#define Z80_SET_AF(z, v) do { uint16_t _v=(uint16_t)(v); (z)->a=_v>>8; (z)->f=_v&0xFF; } while(0)

/**
 * Reset the Z80 to power-on state.
 * PC=0, SP=0xFFFF, IFF1=IFF2=0, IM=0.
 */
void z80_reset(Z80 *cpu);

/**
 * Execute one instruction.
 * @param cpu   Z80 state
 * @param ctx   Opaque pointer passed to all memory/IO callbacks (SMS machine)
 * @return      T-states (clock cycles) consumed
 */
int z80_step(Z80 *cpu, void *ctx);

/**
 * Trigger a Non-Maskable Interrupt.
 * Always accepted regardless of IFF1.  Saves PC to stack, jumps to $0066.
 * @return T-states consumed (11)
 */
int z80_nmi(Z80 *cpu, void *ctx);

/**
 * Trigger a maskable IRQ.
 * Only accepted if IFF1=1.  On SMS, mode 1 is always used:
 * saves PC, jumps to $0038.
 * @return T-states consumed (13 if taken, 0 if masked)
 */
int z80_irq(Z80 *cpu, void *ctx);

/* ── Memory/IO callbacks — implemented in sms.c ─────────────────────── */

/**
 * These are declared extern here and implemented directly in sms.c.
 * Using direct calls (not function pointers) avoids WASM call_indirect
 * overhead — same optimization as the NES CPU.
 */
extern uint8_t sms_mem_read (uint16_t addr, void *ctx);
extern void    sms_mem_write(uint16_t addr, uint8_t val, void *ctx);
extern uint8_t sms_io_read  (uint8_t  port, void *ctx);
extern void    sms_io_write (uint8_t  port, uint8_t val, void *ctx);
