/**
 * @file cpu6502.h
 * @brief Minimal MOS 6502 CPU interpreter for NES emulation
 *
 * Implements all 56 official opcodes + common unofficial opcodes across
 * 13 addressing modes.  Not cycle-exact; uses canonical cycle counts.
 *
 * @license Apache-2.0
 */
#pragma once
#include <stdint.h>

/* Processor status flags */
#define P_C 0x01  /**< Carry            */
#define P_Z 0x02  /**< Zero             */
#define P_I 0x04  /**< IRQ disable      */
#define P_D 0x08  /**< Decimal (unused) */
#define P_B 0x10  /**< Break            */
#define P_U 0x20  /**< Unused (=1)      */
#define P_V 0x40  /**< Overflow         */
#define P_N 0x80  /**< Negative         */

typedef struct {
    uint16_t pc;       /**< Program counter      */
    uint8_t  sp;       /**< Stack pointer        */
    uint8_t  a;        /**< Accumulator          */
    uint8_t  x;        /**< X index register     */
    uint8_t  y;        /**< Y index register     */
    uint8_t  p;        /**< Processor status     */
    int      cycles;   /**< Cycles used by last instruction */
    uint8_t *ram;      /**< Direct WRAM pointer for fast stack/ZP access */
    const uint8_t **rom_pages; /**< 4 × 8KB PRG-ROM page ptrs (from mapper) */
    uint8_t *sram;     /**< Direct SRAM pointer for code execution from $6000-$7FFF */
} CPU6502;

/** Reset: read reset vector and initialise registers. */
void cpu6502_reset(CPU6502 *cpu, void *ctx);

/**
 * Execute one instruction.
 * @return Number of CPU cycles consumed.
 */
int  cpu6502_step (CPU6502 *cpu, void *ctx);

/** Trigger a Non-Maskable Interrupt. */
void cpu6502_nmi  (CPU6502 *cpu, void *ctx);

/** Trigger a maskable IRQ (honours I flag). */
void cpu6502_irq  (CPU6502 *cpu, void *ctx);
