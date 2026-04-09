/**
 * @file z80.c
 * @brief Zilog Z80 CPU interpreter for AkiraOS SMS emulator
 *
 * Covers all official Z80 opcodes including prefixed groups:
 *   main, CB (bit ops), ED (extended), DD (IX), FD (IY), DDCB, FDCB.
 *
 * Design choices identical to the NES cpu6502.c:
 *   - Direct extern calls (no function pointers) for sms_mem_read/write
 *     and sms_io_read/write to avoid WASM call_indirect overhead.
 *   - Precomputed flag tables for S, Z, P to avoid per-instruction branches.
 *
 * @license Apache-2.0
 */
#include "z80.h"
#include <string.h>

/* ── Extern bus functions (implemented in sms.c) ─────────────────────── */
extern uint8_t sms_mem_read (uint16_t addr, void *ctx);
extern void    sms_mem_write(uint16_t addr, uint8_t val, void *ctx);
extern uint8_t sms_io_read  (uint8_t  port, void *ctx);
extern void    sms_io_write (uint8_t  port, uint8_t val, void *ctx);

/* ── Precomputed flag tables ──────────────────────────────────────────── *
 * sz_table[n]  — S and Z flags for byte n (no other flags)
 * szp_table[n] — S, Z, and P (parity) flags for byte n
 *
 * Parity: P=1 when the number of set bits in n is EVEN.
 * We use these for logical ops (AND/OR/XOR) and block instructions.
 * Arithmetic overflow (also stored in PF) is computed inline since it
 * depends on both operands, not just the result.
 */
static uint8_t sz_table[256];
static uint8_t szp_table[256];

static void build_flag_tables(void)
{
    for (int i = 0; i < 256; i++) {
        uint8_t s = (i & 0x80) ? Z80_SF : 0;           /* bit 7 → sign */
        uint8_t z =  (i == 0)  ? Z80_ZF : 0;           /* zero */
        sz_table[i] = s | z;

        /* count set bits for parity */
        int bits = 0;
        for (int b = 0; b < 8; b++) if ((i >> b) & 1) bits++;
        uint8_t p = (bits % 2 == 0) ? Z80_PF : 0;      /* even parity = PF=1 */
        szp_table[i] = s | z | p;
    }
}

/* ── Memory/fetch helpers ─────────────────────────────────────────────── */

/*
 * FETCH: read one byte at PC and advance PC.
 * The Z80's PC wraps at 0xFFFF (16-bit address space).
 */
static inline uint8_t fetch(Z80 *cpu, void *ctx)
{
    return sms_mem_read(cpu->pc++, ctx);
}

/* Read a 16-bit little-endian word from memory */
static inline uint16_t mem_read16(uint16_t addr, void *ctx)
{
    uint8_t lo = sms_mem_read(addr,     ctx);
    uint8_t hi = sms_mem_read(addr + 1, ctx);
    return (uint16_t)(lo | (hi << 8));
}

static inline void mem_write16(uint16_t addr, uint16_t val, void *ctx)
{
    sms_mem_write(addr,     val & 0xFF, ctx);
    sms_mem_write(addr + 1, val >> 8,   ctx);
}

/* PUSH / POP use the stack (SP grows downward) */
static inline void push16(Z80 *cpu, uint16_t val, void *ctx)
{
    cpu->sp -= 2;
    mem_write16(cpu->sp, val, ctx);
}

static inline uint16_t pop16(Z80 *cpu, void *ctx)
{
    uint16_t v = mem_read16(cpu->sp, ctx);
    cpu->sp += 2;
    return v;
}

/* Fetch a signed 8-bit displacement (used by JR and IX/IY instructions) */
static inline int8_t fetch_d(Z80 *cpu, void *ctx)
{
    return (int8_t)fetch(cpu, ctx);
}

/* Fetch a 16-bit immediate */
static inline uint16_t fetch16(Z80 *cpu, void *ctx)
{
    uint8_t lo = fetch(cpu, ctx);
    uint8_t hi = fetch(cpu, ctx);
    return (uint16_t)(lo | (hi << 8));
}

/* ── 8-bit register decode ────────────────────────────────────────────── *
 * Many Z80 ops encode src and dst registers in 3-bit fields of the opcode:
 *   bits 5-3 = destination register  (for LD r,r' and most ALU ops)
 *   bits 2-0 = source register
 *
 * Encoding: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
 * Value 6 means "memory at address HL" — handled specially by the caller.
 *
 * These functions return a pointer to the actual byte in the Z80 struct.
 */
static uint8_t *reg8(Z80 *cpu, int r)
{
    switch (r & 7) {
    case 0: return &cpu->b;
    case 1: return &cpu->c;
    case 2: return &cpu->d;
    case 3: return &cpu->e;
    case 4: return &cpu->h;
    case 5: return &cpu->l;
    case 7: return &cpu->a;
    default: return NULL; /* 6 = (HL), caller must handle */
    }
}

/* ── Arithmetic helpers ───────────────────────────────────────────────── *
 * The Z80 flag rules are intricate.  We centralise them here.
 * All functions update cpu->f completely.
 */

/* ADD A, val  (with optional carry bit cy for ADC) */
static inline void do_add(Z80 *cpu, uint8_t val, int cy)
{
    int a   = cpu->a;
    int res = a + val + cy;
    int hc  = (a & 0xF) + (val & 0xF) + cy;           /* half-carry */
    /* Overflow: if two positives give negative, or two negatives give positive */
    int ov  = (~(a ^ val) & (a ^ res)) & 0x80;

    cpu->a  = (uint8_t)res;
    cpu->f  = sz_table[cpu->a]
            | (ov       ? Z80_PF : 0)                   /* PF = overflow */
            | ((hc & 0x10) ? Z80_HF : 0)
            | ((res & 0x100) ? Z80_CF : 0);
    /* N=0 for addition */
}

/* SUB A, val  (with optional borrow cy for SBC) */
static inline void do_sub(Z80 *cpu, uint8_t val, int cy)
{
    int a   = cpu->a;
    int res = a - val - cy;
    int hc  = (a & 0xF) - (val & 0xF) - cy;
    int ov  = ((a ^ val) & (a ^ res)) & 0x80;          /* overflow on sub */

    cpu->a  = (uint8_t)res;
    cpu->f  = sz_table[cpu->a]
            | (ov       ? Z80_PF : 0)
            | ((hc & 0x10) ? Z80_HF : 0)
            | Z80_NF                                     /* N=1 for subtraction */
            | ((res & 0x100) ? Z80_CF : 0);
}

/* CP A, val  — like SUB but result NOT written back to A */
static inline void do_cp(Z80 *cpu, uint8_t val)
{
    int a   = cpu->a;
    int res = a - val;
    int hc  = (a & 0xF) - (val & 0xF);
    int ov  = ((a ^ val) & (a ^ res)) & 0x80;

    cpu->f  = sz_table[(uint8_t)res]
            | (ov       ? Z80_PF : 0)
            | ((hc & 0x10) ? Z80_HF : 0)
            | Z80_NF
            | ((res & 0x100) ? Z80_CF : 0);
}

/* AND A, val */
static inline void do_and(Z80 *cpu, uint8_t val)
{
    cpu->a &= val;
    cpu->f  = szp_table[cpu->a] | Z80_HF;   /* H=1, N=0, C=0 for AND */
}

/* OR A, val */
static inline void do_or(Z80 *cpu, uint8_t val)
{
    cpu->a |= val;
    cpu->f  = szp_table[cpu->a];             /* H=0, N=0, C=0 */
}

/* XOR A, val */
static inline void do_xor(Z80 *cpu, uint8_t val)
{
    cpu->a ^= val;
    cpu->f  = szp_table[cpu->a];
}

/* INC reg — does NOT affect carry */
static inline uint8_t do_inc(Z80 *cpu, uint8_t v)
{
    uint8_t res = v + 1;
    int ov  = (v == 0x7F) ? Z80_PF : 0;    /* wraps 0x7F→0x80: signed overflow */
    uint8_t hf  = ((v & 0xF) == 0xF) ? Z80_HF : 0;
    cpu->f = sz_table[res] | ov | hf        /* N=0 */
           | (cpu->f & Z80_CF);             /* C unchanged */
    return res;
}

/* DEC reg — does NOT affect carry */
static inline uint8_t do_dec(Z80 *cpu, uint8_t v)
{
    uint8_t res = v - 1;
    int ov  = (v == 0x80) ? Z80_PF : 0;    /* wraps 0x80→0x7F: signed overflow */
    uint8_t hf  = ((v & 0xF) == 0x0) ? Z80_HF : 0;
    cpu->f = sz_table[res] | ov | hf | Z80_NF   /* N=1 */
           | (cpu->f & Z80_CF);
    return res;
}

/* ADD HL, rr — 16-bit add, only touches H, N, C flags */
static inline uint16_t do_add16(Z80 *cpu, uint16_t hl, uint16_t rr)
{
    uint32_t res = hl + rr;
    uint32_t hc  = (hl & 0xFFF) + (rr & 0xFFF);
    cpu->f = (cpu->f & (Z80_SF | Z80_ZF | Z80_PF))  /* S/Z/P unchanged */
           | ((hc & 0x1000) ? Z80_HF : 0)
           | ((res & 0x10000) ? Z80_CF : 0);
    return (uint16_t)res;
}

/* ── Rotate / shift helpers ──────────────────────────────────────────── */

static inline uint8_t do_rlc(Z80 *cpu, uint8_t v)
{
    uint8_t c = v >> 7;
    uint8_t r = (v << 1) | c;
    cpu->f = szp_table[r] | c;
    return r;
}
static inline uint8_t do_rrc(Z80 *cpu, uint8_t v)
{
    uint8_t c = v & 1;
    uint8_t r = (v >> 1) | (c << 7);
    cpu->f = szp_table[r] | c;
    return r;
}
static inline uint8_t do_rl(Z80 *cpu, uint8_t v)
{
    uint8_t old_c = cpu->f & Z80_CF;
    uint8_t c = v >> 7;
    uint8_t r = (v << 1) | old_c;
    cpu->f = szp_table[r] | c;
    return r;
}
static inline uint8_t do_rr(Z80 *cpu, uint8_t v)
{
    uint8_t old_c = cpu->f & Z80_CF;
    uint8_t c = v & 1;
    uint8_t r = (v >> 1) | (old_c << 7);
    cpu->f = szp_table[r] | c;
    return r;
}
static inline uint8_t do_sla(Z80 *cpu, uint8_t v)
{
    uint8_t c = v >> 7;
    uint8_t r = v << 1;
    cpu->f = szp_table[r] | c;
    return r;
}
static inline uint8_t do_sra(Z80 *cpu, uint8_t v)   /* arithmetic shift: sign extends */
{
    uint8_t c = v & 1;
    uint8_t r = (v >> 1) | (v & 0x80);
    cpu->f = szp_table[r] | c;
    return r;
}
static inline uint8_t do_srl(Z80 *cpu, uint8_t v)   /* logical shift: zero fills */
{
    uint8_t c = v & 1;
    uint8_t r = v >> 1;
    cpu->f = szp_table[r] | c;
    return r;
}

/* ── CB prefix: bit operations ───────────────────────────────────────── *
 * Opcode layout after CB:
 *   bits 7-6 = operation: 00=rotate/shift  01=BIT  10=RES  11=SET
 *   bits 5-3 = bit number (for BIT/SET/RES) or shift type (for 00)
 *   bits 2-0 = register (0-5=B,C,D,E,H,L  6=(HL)  7=A)
 */
static int exec_cb(Z80 *cpu, void *ctx)
{
    uint8_t op = fetch(cpu, ctx);
    int     r  = op & 7;
    int     b  = (op >> 3) & 7;

    /* Read operand — either a register or memory at HL */
    uint8_t val;
    if (r == 6)
        val = sms_mem_read(Z80_HL(cpu), ctx);
    else
        val = *reg8(cpu, r);

    uint8_t res = val;

    switch (op >> 6) {
    case 0: /* rotate/shift */
        switch (b) {
        case 0: res = do_rlc(cpu, val); break;
        case 1: res = do_rrc(cpu, val); break;
        case 2: res = do_rl (cpu, val); break;
        case 3: res = do_rr (cpu, val); break;
        case 4: res = do_sla(cpu, val); break;
        case 5: res = do_sra(cpu, val); break;
        case 6: res = do_sla(cpu, val); break; /* SLL (undocumented) = SLA with bit0=1 */
        case 7: res = do_srl(cpu, val); break;
        }
        break;

    case 1: /* BIT n,r — test bit b of val */
        /*
         * BIT sets Z flag if bit is 0, clears if 1.
         * S = bit 7 of (val & mask) only when testing bit 7.
         * H=1, N=0, PF=Z (same as Z).
         * Does NOT write back to register.
         */
        {
            uint8_t mask = 1u << b;
            uint8_t test = val & mask;
            cpu->f = (cpu->f & Z80_CF)       /* C unchanged */
                   | Z80_HF                  /* H always 1 */
                   | (test ? 0 : Z80_ZF)     /* Z = bit was 0 */
                   | (test ? 0 : Z80_PF)     /* PF = Z */
                   | (test & Z80_SF);        /* S = bit 7 if testing bit 7 */
        }
        return (r == 6) ? 12 : 8;           /* BIT doesn't write back */

    case 2: /* RES n,r — clear bit b */
        res = val & ~(1u << b);
        break;

    case 3: /* SET n,r — set bit b */
        res = val | (1u << b);
        break;
    }

    /* Write result back */
    if (r == 6)
        sms_mem_write(Z80_HL(cpu), res, ctx);
    else
        *reg8(cpu, r) = res;

    return (r == 6) ? 15 : 8;
}

/* ── ED prefix: extended operations ─────────────────────────────────── *
 * These are rarer instructions — 16-bit loads, block moves, port I/O.
 * The most important for SMS games:
 *   LDIR ($B0): copy BC bytes from HL to DE, increment both, repeat
 *   LDDR ($B8): same but decrement
 *   OTIR ($B3): output BC bytes from (HL) to port C
 *   IN r,(C)  ($40+r*8): read I/O port C into register r, set flags
 *   OUT (C),r ($41+r*8): write register r to I/O port C
 */
static int exec_ed(Z80 *cpu, void *ctx)
{
    uint8_t op = fetch(cpu, ctx);

    switch (op) {

    /* IN r,(C) — read I/O port C into register r, set S/Z/P flags */
    case 0x40: case 0x48: case 0x50: case 0x58:
    case 0x60: case 0x68: case 0x70: case 0x78: {
        int r    = (op >> 3) & 7;
        uint8_t val = sms_io_read(cpu->c, ctx);
        if (r != 6) *reg8(cpu, r) = val;   /* 0x70 reads but discards (flag-only) */
        cpu->f = szp_table[val] | (cpu->f & Z80_CF);
        return 12;
    }

    /* OUT (C),r — write register r to I/O port C */
    case 0x41: case 0x49: case 0x51: case 0x59:
    case 0x61: case 0x69: case 0x71: case 0x79: {
        int r = (op >> 3) & 7;
        uint8_t val = (r == 6) ? 0 : *reg8(cpu, r);
        sms_io_write(cpu->c, val, ctx);
        return 12;
    }

    /* SBC HL, rr */
    case 0x42: { uint16_t rr = Z80_BC(cpu); goto sbc_hl; case 0x52: rr = Z80_DE(cpu); goto sbc_hl;
                 case 0x62: rr = Z80_HL(cpu); goto sbc_hl; case 0x72: rr = cpu->sp;
        sbc_hl: { int cy = cpu->f & Z80_CF;
                  int res = Z80_HL(cpu) - rr - cy;
                  int ov  = ((Z80_HL(cpu) ^ rr) & (Z80_HL(cpu) ^ res)) & 0x8000;
                  int hc  = (Z80_HL(cpu) & 0xFFF) - (rr & 0xFFF) - cy;
                  Z80_SET_HL(cpu, res & 0xFFFF);
                  cpu->f = ((res & 0x8000) ? Z80_SF : 0)
                         | ((res & 0xFFFF) ? 0 : Z80_ZF)
                         | (ov ? Z80_PF : 0)
                         | ((hc & 0x1000) ? Z80_HF : 0)
                         | Z80_NF
                         | ((res & 0x10000) ? Z80_CF : 0);
                  return 15; } }

    /* ADC HL, rr */
    case 0x4A: { uint16_t rr = Z80_BC(cpu); goto adc_hl; case 0x5A: rr = Z80_DE(cpu); goto adc_hl;
                 case 0x6A: rr = Z80_HL(cpu); goto adc_hl; case 0x7A: rr = cpu->sp;
        adc_hl: { int cy = cpu->f & Z80_CF;
                  int res = Z80_HL(cpu) + rr + cy;
                  int ov  = (~(Z80_HL(cpu) ^ rr) & (Z80_HL(cpu) ^ res)) & 0x8000;
                  int hc  = (Z80_HL(cpu) & 0xFFF) + (rr & 0xFFF) + cy;
                  Z80_SET_HL(cpu, res & 0xFFFF);
                  cpu->f = ((res & 0x8000) ? Z80_SF : 0)
                         | ((res & 0xFFFF) ? 0 : Z80_ZF)
                         | (ov ? Z80_PF : 0)
                         | ((hc & 0x1000) ? Z80_HF : 0)
                         | ((res & 0x10000) ? Z80_CF : 0);
                  return 15; } }

    /* LD (nn), rr  /  LD rr, (nn) — 16-bit loads to/from absolute address */
    case 0x43: { uint16_t nn = fetch16(cpu, ctx); mem_write16(nn, Z80_BC(cpu), ctx); return 20; }
    case 0x53: { uint16_t nn = fetch16(cpu, ctx); mem_write16(nn, Z80_DE(cpu), ctx); return 20; }
    case 0x63: { uint16_t nn = fetch16(cpu, ctx); mem_write16(nn, Z80_HL(cpu), ctx); return 20; }
    case 0x73: { uint16_t nn = fetch16(cpu, ctx); mem_write16(nn, cpu->sp,      ctx); return 20; }
    case 0x4B: { uint16_t nn = fetch16(cpu, ctx); Z80_SET_BC(cpu, mem_read16(nn, ctx)); return 20; }
    case 0x5B: { uint16_t nn = fetch16(cpu, ctx); Z80_SET_DE(cpu, mem_read16(nn, ctx)); return 20; }
    case 0x6B: { uint16_t nn = fetch16(cpu, ctx); Z80_SET_HL(cpu, mem_read16(nn, ctx)); return 20; }
    case 0x7B: { uint16_t nn = fetch16(cpu, ctx); cpu->sp = mem_read16(nn, ctx);        return 20; }

    /* NEG — negate A (A = 0 - A) */
    case 0x44: case 0x4C: case 0x54: case 0x5C:
    case 0x64: case 0x6C: case 0x74: case 0x7C: {
        uint8_t old = cpu->a;
        cpu->a = 0;
        do_sub(cpu, old, 0);
        return 8;
    }

    /* RETN — return from NMI; restores IFF1 from IFF2 */
    case 0x45: case 0x55: case 0x65: case 0x75:
        cpu->iff1 = cpu->iff2;
        cpu->pc   = pop16(cpu, ctx);
        return 14;

    /* RETI — return from IRQ */
    case 0x4D: case 0x5D: case 0x6D: case 0x7D:
        cpu->iff1 = cpu->iff2;
        cpu->pc   = pop16(cpu, ctx);
        return 14;

    /* IM 0 / IM 1 / IM 2 — interrupt mode select.  SMS uses IM 1 always. */
    case 0x46: case 0x66: cpu->im = 0; return 8;
    case 0x56: case 0x76: cpu->im = 1; return 8;
    case 0x5E: case 0x7E: cpu->im = 2; return 8;

    /* LD I,A / LD R,A */
    case 0x47: cpu->i = cpu->a; return 9;
    case 0x4F: cpu->r = cpu->a; return 9;

    /* LD A,I / LD A,R — also updates S/Z/P flags */
    case 0x57: cpu->a = cpu->i; cpu->f = szp_table[cpu->a] | (cpu->f & Z80_CF) | (cpu->iff2 ? Z80_PF : 0); return 9;
    case 0x5F: cpu->a = cpu->r; cpu->f = szp_table[cpu->a] | (cpu->f & Z80_CF) | (cpu->iff2 ? Z80_PF : 0); return 9;

    /* RRD / RLD — 4-bit rotates between A and (HL) */
    case 0x67: { /* RLD */
        uint8_t m = sms_mem_read(Z80_HL(cpu), ctx);
        uint8_t r = (m << 4) | (cpu->a & 0x0F);
        cpu->a    = (cpu->a & 0xF0) | (m >> 4);
        sms_mem_write(Z80_HL(cpu), r, ctx);
        cpu->f    = szp_table[cpu->a] | (cpu->f & Z80_CF);
        return 18;
    }
    case 0x6F: { /* RRD */
        uint8_t m = sms_mem_read(Z80_HL(cpu), ctx);
        uint8_t r = (cpu->a << 4) | (m >> 4);
        cpu->a    = (cpu->a & 0xF0) | (m & 0x0F);
        sms_mem_write(Z80_HL(cpu), r, ctx);
        cpu->f    = szp_table[cpu->a] | (cpu->f & Z80_CF);
        return 18;
    }

    /* ── Block instructions ──────────────────────────────────────────── *
     * LDI / LDD / LDIR / LDDR: copy memory HL→DE, adjust BC counter
     * CPI / CPD / CPIR / CPDR: search memory for A
     * INI / IND / INIR / INDR: input from port C to (HL)
     * OUTI / OUTD / OTIR / OTDR: output (HL) to port C
     */

    /* LDI — copy (HL)→(DE), HL++, DE++, BC-- */
    case 0xA0: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        sms_mem_write(Z80_DE(cpu), v, ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) + 1);
        Z80_SET_DE(cpu, Z80_DE(cpu) + 1);
        Z80_SET_BC(cpu, Z80_BC(cpu) - 1);
        cpu->f = (cpu->f & (Z80_SF | Z80_ZF | Z80_CF))
               | (Z80_BC(cpu) ? Z80_PF : 0);
        return 16;
    }
    /* CPI — compare (HL) with A, HL++, BC-- */
    case 0xA1: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        int res = cpu->a - v;
        int hc  = (cpu->a & 0xF) - (v & 0xF);
        Z80_SET_HL(cpu, Z80_HL(cpu) + 1);
        Z80_SET_BC(cpu, Z80_BC(cpu) - 1);
        cpu->f = sz_table[(uint8_t)res]
               | ((hc & 0x10) ? Z80_HF : 0)
               | (Z80_BC(cpu) ? Z80_PF : 0)
               | Z80_NF
               | (cpu->f & Z80_CF);
        return 16;
    }
    /* INI — read port C → (HL), HL++, B-- */
    case 0xA2: {
        uint8_t v = sms_io_read(cpu->c, ctx);
        sms_mem_write(Z80_HL(cpu), v, ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) + 1);
        cpu->b--;
        cpu->f = sz_table[cpu->b] | Z80_NF | (cpu->f & Z80_CF);
        return 16;
    }
    /* OUTI — write (HL) → port C, HL++, B-- */
    case 0xA3: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) + 1);
        cpu->b--;
        sms_io_write(cpu->c, v, ctx);
        cpu->f = sz_table[cpu->b] | Z80_NF | (cpu->f & Z80_CF);
        return 16;
    }
    /* LDD — like LDI but decrement HL, DE */
    case 0xA8: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        sms_mem_write(Z80_DE(cpu), v, ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) - 1);
        Z80_SET_DE(cpu, Z80_DE(cpu) - 1);
        Z80_SET_BC(cpu, Z80_BC(cpu) - 1);
        cpu->f = (cpu->f & (Z80_SF | Z80_ZF | Z80_CF))
               | (Z80_BC(cpu) ? Z80_PF : 0);
        return 16;
    }
    /* CPD */
    case 0xA9: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        int res = cpu->a - v;
        int hc  = (cpu->a & 0xF) - (v & 0xF);
        Z80_SET_HL(cpu, Z80_HL(cpu) - 1);
        Z80_SET_BC(cpu, Z80_BC(cpu) - 1);
        cpu->f = sz_table[(uint8_t)res]
               | ((hc & 0x10) ? Z80_HF : 0)
               | (Z80_BC(cpu) ? Z80_PF : 0)
               | Z80_NF
               | (cpu->f & Z80_CF);
        return 16;
    }
    /* IND */
    case 0xAA: {
        uint8_t v = sms_io_read(cpu->c, ctx);
        sms_mem_write(Z80_HL(cpu), v, ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) - 1);
        cpu->b--;
        cpu->f = sz_table[cpu->b] | Z80_NF | (cpu->f & Z80_CF);
        return 16;
    }
    /* OUTD */
    case 0xAB: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) - 1);
        cpu->b--;
        sms_io_write(cpu->c, v, ctx);
        cpu->f = sz_table[cpu->b] | Z80_NF | (cpu->f & Z80_CF);
        return 16;
    }
    /* LDIR — repeat LDI until BC=0 */
    case 0xB0: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        sms_mem_write(Z80_DE(cpu), v, ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) + 1);
        Z80_SET_DE(cpu, Z80_DE(cpu) + 1);
        Z80_SET_BC(cpu, Z80_BC(cpu) - 1);
        cpu->f = (cpu->f & (Z80_SF | Z80_ZF | Z80_CF));
        if (Z80_BC(cpu)) { cpu->pc -= 2; return 21; }
        return 16;
    }
    /* CPIR — repeat CPI until BC=0 or A=(HL) */
    case 0xB1: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        int res = cpu->a - v;
        int hc  = (cpu->a & 0xF) - (v & 0xF);
        Z80_SET_HL(cpu, Z80_HL(cpu) + 1);
        Z80_SET_BC(cpu, Z80_BC(cpu) - 1);
        cpu->f = sz_table[(uint8_t)res]
               | ((hc & 0x10) ? Z80_HF : 0)
               | (Z80_BC(cpu) ? Z80_PF : 0)
               | Z80_NF
               | (cpu->f & Z80_CF);
        if (Z80_BC(cpu) && (uint8_t)res) { cpu->pc -= 2; return 21; }
        return 16;
    }
    /* INIR */
    case 0xB2: {
        uint8_t v = sms_io_read(cpu->c, ctx);
        sms_mem_write(Z80_HL(cpu), v, ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) + 1);
        cpu->b--;
        cpu->f = sz_table[cpu->b] | Z80_NF | (cpu->f & Z80_CF);
        if (cpu->b) { cpu->pc -= 2; return 21; }
        return 16;
    }
    /* OTIR */
    case 0xB3: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) + 1);
        cpu->b--;
        sms_io_write(cpu->c, v, ctx);
        cpu->f = sz_table[cpu->b] | Z80_NF | (cpu->f & Z80_CF);
        if (cpu->b) { cpu->pc -= 2; return 21; }
        return 16;
    }
    /* LDDR */
    case 0xB8: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        sms_mem_write(Z80_DE(cpu), v, ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) - 1);
        Z80_SET_DE(cpu, Z80_DE(cpu) - 1);
        Z80_SET_BC(cpu, Z80_BC(cpu) - 1);
        cpu->f = (cpu->f & (Z80_SF | Z80_ZF | Z80_CF));
        if (Z80_BC(cpu)) { cpu->pc -= 2; return 21; }
        return 16;
    }
    /* CPDR */
    case 0xB9: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        int res = cpu->a - v;
        int hc  = (cpu->a & 0xF) - (v & 0xF);
        Z80_SET_HL(cpu, Z80_HL(cpu) - 1);
        Z80_SET_BC(cpu, Z80_BC(cpu) - 1);
        cpu->f = sz_table[(uint8_t)res]
               | ((hc & 0x10) ? Z80_HF : 0)
               | (Z80_BC(cpu) ? Z80_PF : 0)
               | Z80_NF
               | (cpu->f & Z80_CF);
        if (Z80_BC(cpu) && (uint8_t)res) { cpu->pc -= 2; return 21; }
        return 16;
    }
    /* INDR */
    case 0xBA: {
        uint8_t v = sms_io_read(cpu->c, ctx);
        sms_mem_write(Z80_HL(cpu), v, ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) - 1);
        cpu->b--;
        cpu->f = sz_table[cpu->b] | Z80_NF | (cpu->f & Z80_CF);
        if (cpu->b) { cpu->pc -= 2; return 21; }
        return 16;
    }
    /* OTDR */
    case 0xBB: {
        uint8_t v = sms_mem_read(Z80_HL(cpu), ctx);
        Z80_SET_HL(cpu, Z80_HL(cpu) - 1);
        cpu->b--;
        sms_io_write(cpu->c, v, ctx);
        cpu->f = sz_table[cpu->b] | Z80_NF | (cpu->f & Z80_CF);
        if (cpu->b) { cpu->pc -= 2; return 21; }
        return 16;
    }

    default: return 8; /* unknown ED xx — treated as NOP */
    }
}

/* ── DD/FD prefix: IX and IY instructions ───────────────────────────── *
 * DD and FD work identically except DD→IX, FD→IY.
 * Most opcodes are the same as the main table but HL is replaced by IX/IY,
 * and (HL) becomes (IX+d) or (IY+d) where d is a signed byte displacement.
 *
 * We handle both with a single function that receives a pointer to IX or IY.
 */
static int exec_idx(Z80 *cpu, void *ctx, uint16_t *idx)
{
    uint8_t op = fetch(cpu, ctx);

    /* DDCB / FDCB: displacement comes BEFORE the final opcode byte */
    if (op == 0xCB) {
        int8_t   d   = fetch_d(cpu, ctx);
        uint8_t  op2 = fetch(cpu, ctx);
        uint16_t ea  = *idx + d;
        uint8_t  val = sms_mem_read(ea, ctx);
        uint8_t  res = val;
        int      r   = op2 & 7;

        switch (op2 >> 6) {
        case 0:
            switch ((op2 >> 3) & 7) {
            case 0: res = do_rlc(cpu, val); break;
            case 1: res = do_rrc(cpu, val); break;
            case 2: res = do_rl (cpu, val); break;
            case 3: res = do_rr (cpu, val); break;
            case 4: res = do_sla(cpu, val); break;
            case 5: res = do_sra(cpu, val); break;
            case 6: res = do_sla(cpu, val); break;
            case 7: res = do_srl(cpu, val); break;
            }
            break;
        case 1: { /* BIT */
            uint8_t mask = 1u << ((op2 >> 3) & 7);
            uint8_t test = val & mask;
            cpu->f = (cpu->f & Z80_CF) | Z80_HF
                   | (test ? 0 : Z80_ZF) | (test ? 0 : Z80_PF)
                   | (test & Z80_SF);
            return 20;
        }
        case 2: res = val & ~(1u << ((op2 >> 3) & 7)); break;
        case 3: res = val |  (1u << ((op2 >> 3) & 7)); break;
        }
        sms_mem_write(ea, res, ctx);
        /* Also store in register if r != 6 (undocumented behaviour) */
        if (r != 6) *reg8(cpu, r) = res;
        return 23;
    }

    switch (op) {
    /* ADD idx, rr */
    case 0x09: *idx = do_add16(cpu, *idx, Z80_BC(cpu)); return 15;
    case 0x19: *idx = do_add16(cpu, *idx, Z80_DE(cpu)); return 15;
    case 0x29: *idx = do_add16(cpu, *idx, *idx);        return 15;
    case 0x39: *idx = do_add16(cpu, *idx, cpu->sp);     return 15;

    /* LD idx, nn */
    case 0x21: *idx = fetch16(cpu, ctx); return 14;

    /* LD (nn), idx */
    case 0x22: { uint16_t nn = fetch16(cpu, ctx); mem_write16(nn, *idx, ctx); return 20; }

    /* INC idx */
    case 0x23: (*idx)++; return 10;

    /* INC/DEC high/low bytes of idx (undocumented but common) */
    case 0x24: *idx = (*idx & 0x00FF) | ((uint16_t)do_inc(cpu, *idx >> 8) << 8); return 8;
    case 0x25: *idx = (*idx & 0x00FF) | ((uint16_t)do_dec(cpu, *idx >> 8) << 8); return 8;
    case 0x2C: *idx = (*idx & 0xFF00) | do_inc(cpu, *idx & 0xFF); return 8;
    case 0x2D: *idx = (*idx & 0xFF00) | do_dec(cpu, *idx & 0xFF); return 8;

    /* LD idx, (nn) */
    case 0x2A: { uint16_t nn = fetch16(cpu, ctx); *idx = mem_read16(nn, ctx); return 20; }

    /* DEC idx */
    case 0x2B: (*idx)--; return 10;

    /* INC (idx+d) */
    case 0x34: { int8_t d = fetch_d(cpu, ctx); uint16_t ea = *idx + d;
                 sms_mem_write(ea, do_inc(cpu, sms_mem_read(ea, ctx)), ctx); return 23; }

    /* DEC (idx+d) */
    case 0x35: { int8_t d = fetch_d(cpu, ctx); uint16_t ea = *idx + d;
                 sms_mem_write(ea, do_dec(cpu, sms_mem_read(ea, ctx)), ctx); return 23; }

    /* LD (idx+d), n */
    case 0x36: { int8_t  d = fetch_d(cpu, ctx); uint8_t n = fetch(cpu, ctx);
                 sms_mem_write(*idx + d, n, ctx); return 19; }

    /* LD r, (idx+d) — load register from memory */
    case 0x46: case 0x4E: case 0x56: case 0x5E:
    case 0x66: case 0x6E: case 0x7E: {
        int8_t d = fetch_d(cpu, ctx);
        uint8_t v = sms_mem_read(*idx + d, ctx);
        int r = (op >> 3) & 7;
        if (r == 4) cpu->h = v;   /* idx high byte replaced on main table only */
        else if (r == 5) cpu->l = v;
        else *reg8(cpu, r) = v;
        return 19;
    }

    /* LD (idx+d), r */
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x77: {
        int8_t d = fetch_d(cpu, ctx);
        int r = op & 7;
        uint8_t v = *reg8(cpu, r);
        sms_mem_write(*idx + d, v, ctx);
        return 19;
    }

    /* ALU ops with (idx+d) */
    case 0x86: { int8_t d = fetch_d(cpu, ctx); do_add(cpu, sms_mem_read(*idx+d,ctx), 0); return 19; }
    case 0x8E: { int8_t d = fetch_d(cpu, ctx); do_add(cpu, sms_mem_read(*idx+d,ctx), cpu->f&Z80_CF); return 19; }
    case 0x96: { int8_t d = fetch_d(cpu, ctx); do_sub(cpu, sms_mem_read(*idx+d,ctx), 0); return 19; }
    case 0x9E: { int8_t d = fetch_d(cpu, ctx); do_sub(cpu, sms_mem_read(*idx+d,ctx), cpu->f&Z80_CF); return 19; }
    case 0xA6: { int8_t d = fetch_d(cpu, ctx); do_and(cpu, sms_mem_read(*idx+d,ctx)); return 19; }
    case 0xAE: { int8_t d = fetch_d(cpu, ctx); do_xor(cpu, sms_mem_read(*idx+d,ctx)); return 19; }
    case 0xB6: { int8_t d = fetch_d(cpu, ctx); do_or (cpu, sms_mem_read(*idx+d,ctx)); return 19; }
    case 0xBE: { int8_t d = fetch_d(cpu, ctx); do_cp (cpu, sms_mem_read(*idx+d,ctx)); return 19; }

    /* PUSH / POP idx */
    case 0xE1: *idx = pop16(cpu, ctx); return 14;
    case 0xE5: push16(cpu, *idx, ctx); return 15;

    /* EX (SP), idx */
    case 0xE3: { uint16_t t = mem_read16(cpu->sp, ctx); mem_write16(cpu->sp, *idx, ctx); *idx = t; return 23; }

    /* JP (idx) — jump to address in idx register */
    case 0xE9: cpu->pc = *idx; return 8;

    /* LD SP, idx */
    case 0xF9: cpu->sp = *idx; return 10;

    default:
        /* Any unhandled DD/FD opcode falls back to executing as normal opcode.
         * This is correct Z80 behaviour — DD/FD is just a prefix hint,
         * not all following opcodes are affected. */
        cpu->pc--; /* un-consume the opcode */
        return 0;  /* caller handles via main table */
    }
}

/* ── Main opcode table ───────────────────────────────────────────────── *
 * This covers all opcodes 0x00–0xFF not handled by a prefix.
 * Organization follows the Z80 opcode map:
 *
 *   0x00–0x3F: misc (NOP, LD r,n, INC/DEC rr, RLCA etc.), JR, DJNZ
 *   0x40–0x7F: LD r,r' (64 variants, minus 0x76=HALT)
 *   0x80–0xBF: ALU A,r (ADD/ADC/SUB/SBC/AND/XOR/OR/CP × 8 registers)
 *   0xC0–0xFF: control flow (RET cc, JP, CALL, RST, PUSH/POP, EI/DI etc.)
 */
static int exec_main(Z80 *cpu, void *ctx, uint8_t op)
{
    switch (op) {

    case 0x00: return 4; /* NOP */

    /* ── LD rr, nn (16-bit immediate load) ─────────────────────────── */
    case 0x01: Z80_SET_BC(cpu, fetch16(cpu, ctx)); return 10;
    case 0x11: Z80_SET_DE(cpu, fetch16(cpu, ctx)); return 10;
    case 0x21: Z80_SET_HL(cpu, fetch16(cpu, ctx)); return 10;
    case 0x31: cpu->sp = fetch16(cpu, ctx);         return 10;

    /* ── LD (rr), A  /  LD A, (rr) ─────────────────────────────────── */
    case 0x02: sms_mem_write(Z80_BC(cpu), cpu->a, ctx); return 7;
    case 0x12: sms_mem_write(Z80_DE(cpu), cpu->a, ctx); return 7;
    case 0x0A: cpu->a = sms_mem_read(Z80_BC(cpu), ctx); return 7;
    case 0x1A: cpu->a = sms_mem_read(Z80_DE(cpu), ctx); return 7;

    /* ── LD (nn), HL  /  LD HL, (nn) ──────────────────────────────── */
    case 0x22: { uint16_t nn = fetch16(cpu, ctx); mem_write16(nn, Z80_HL(cpu), ctx); return 16; }
    case 0x2A: { uint16_t nn = fetch16(cpu, ctx); Z80_SET_HL(cpu, mem_read16(nn, ctx)); return 16; }

    /* ── LD (nn), A  /  LD A, (nn) ────────────────────────────────── */
    case 0x32: { uint16_t nn = fetch16(cpu, ctx); sms_mem_write(nn, cpu->a, ctx); return 13; }
    case 0x3A: { uint16_t nn = fetch16(cpu, ctx); cpu->a = sms_mem_read(nn, ctx); return 13; }

    /* ── INC/DEC 16-bit registers (no flags) ───────────────────────── */
    case 0x03: Z80_SET_BC(cpu, Z80_BC(cpu) + 1); return 6;
    case 0x13: Z80_SET_DE(cpu, Z80_DE(cpu) + 1); return 6;
    case 0x23: Z80_SET_HL(cpu, Z80_HL(cpu) + 1); return 6;
    case 0x33: cpu->sp++;                          return 6;
    case 0x0B: Z80_SET_BC(cpu, Z80_BC(cpu) - 1); return 6;
    case 0x1B: Z80_SET_DE(cpu, Z80_DE(cpu) - 1); return 6;
    case 0x2B: Z80_SET_HL(cpu, Z80_HL(cpu) - 1); return 6;
    case 0x3B: cpu->sp--;                          return 6;

    /* ── ADD HL, rr ────────────────────────────────────────────────── */
    case 0x09: Z80_SET_HL(cpu, do_add16(cpu, Z80_HL(cpu), Z80_BC(cpu))); return 11;
    case 0x19: Z80_SET_HL(cpu, do_add16(cpu, Z80_HL(cpu), Z80_DE(cpu))); return 11;
    case 0x29: Z80_SET_HL(cpu, do_add16(cpu, Z80_HL(cpu), Z80_HL(cpu))); return 11;
    case 0x39: Z80_SET_HL(cpu, do_add16(cpu, Z80_HL(cpu), cpu->sp));     return 11;

    /* ── INC/DEC 8-bit registers ────────────────────────────────────── */
    case 0x04: cpu->b = do_inc(cpu, cpu->b); return 4;
    case 0x0C: cpu->c = do_inc(cpu, cpu->c); return 4;
    case 0x14: cpu->d = do_inc(cpu, cpu->d); return 4;
    case 0x1C: cpu->e = do_inc(cpu, cpu->e); return 4;
    case 0x24: cpu->h = do_inc(cpu, cpu->h); return 4;
    case 0x2C: cpu->l = do_inc(cpu, cpu->l); return 4;
    case 0x34: { uint16_t a = Z80_HL(cpu); sms_mem_write(a, do_inc(cpu, sms_mem_read(a,ctx)), ctx); return 11; }
    case 0x3C: cpu->a = do_inc(cpu, cpu->a); return 4;

    case 0x05: cpu->b = do_dec(cpu, cpu->b); return 4;
    case 0x0D: cpu->c = do_dec(cpu, cpu->c); return 4;
    case 0x15: cpu->d = do_dec(cpu, cpu->d); return 4;
    case 0x1D: cpu->e = do_dec(cpu, cpu->e); return 4;
    case 0x25: cpu->h = do_dec(cpu, cpu->h); return 4;
    case 0x2D: cpu->l = do_dec(cpu, cpu->l); return 4;
    case 0x35: { uint16_t a = Z80_HL(cpu); sms_mem_write(a, do_dec(cpu, sms_mem_read(a,ctx)), ctx); return 11; }
    case 0x3D: cpu->a = do_dec(cpu, cpu->a); return 4;

    /* ── LD r, n (8-bit immediate) ──────────────────────────────────── */
    case 0x06: cpu->b = fetch(cpu, ctx); return 7;
    case 0x0E: cpu->c = fetch(cpu, ctx); return 7;
    case 0x16: cpu->d = fetch(cpu, ctx); return 7;
    case 0x1E: cpu->e = fetch(cpu, ctx); return 7;
    case 0x26: cpu->h = fetch(cpu, ctx); return 7;
    case 0x2E: cpu->l = fetch(cpu, ctx); return 7;
    case 0x36: sms_mem_write(Z80_HL(cpu), fetch(cpu, ctx), ctx); return 10;
    case 0x3E: cpu->a = fetch(cpu, ctx); return 7;

    /* ── Accumulator rotates (affect only C, H=0, N=0) ─────────────── */
    case 0x07: { /* RLCA */
        uint8_t c = cpu->a >> 7;
        cpu->a = (cpu->a << 1) | c;
        cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | c;
        return 4;
    }
    case 0x0F: { /* RRCA */
        uint8_t c = cpu->a & 1;
        cpu->a = (cpu->a >> 1) | (c << 7);
        cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | c;
        return 4;
    }
    case 0x17: { /* RLA */
        uint8_t old_c = cpu->f & Z80_CF;
        uint8_t c = cpu->a >> 7;
        cpu->a = (cpu->a << 1) | old_c;
        cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | c;
        return 4;
    }
    case 0x1F: { /* RRA */
        uint8_t old_c = cpu->f & Z80_CF;
        uint8_t c = cpu->a & 1;
        cpu->a = (cpu->a >> 1) | (old_c << 7);
        cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | c;
        return 4;
    }

    /* ── DAA — Decimal Adjust Accumulator ──────────────────────────── *
     * Adjusts A after BCD addition/subtraction so each nibble stays 0-9.
     * The Z80 DAA is notorious for its complexity.
     * This is the standard lookup-table-free implementation.
     */
    case 0x27: {
        int a  = cpu->a;
        int cf = cpu->f & Z80_CF;
        int hf = cpu->f & Z80_HF;
        int nf = cpu->f & Z80_NF;
        if (!nf) {
            if (hf || (a & 0xF) > 9)  a += 0x06;
            if (cf || a > 0x9F)        a += 0x60;
        } else {
            if (hf)  a -= 0x06;
            if (cf)  a -= 0x60;
        }
        cpu->f = szp_table[a & 0xFF]
               | (cf || (a & 0x100) ? Z80_CF : 0)
               | nf    /* N unchanged */
               | (((cpu->a ^ a) & 0x10) ? Z80_HF : 0);
        cpu->a = (uint8_t)a;
        return 4;
    }

    /* ── CPL — complement A (all bits flipped) ──────────────────────── */
    case 0x2F: cpu->a ^= 0xFF; cpu->f |= Z80_HF | Z80_NF; return 4;

    /* ── SCF / CCF — set/complement carry flag ──────────────────────── */
    case 0x37: cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | Z80_CF; return 4;
    case 0x3F: {
        uint8_t old_c = cpu->f & Z80_CF;
        cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_PF))
               | (old_c ? Z80_HF : 0)   /* old carry → H */
               | (old_c ^ Z80_CF);      /* complement carry */
        return 4;
    }

    /* ── HALT ────────────────────────────────────────────────────────── */
    case 0x76: cpu->halted = 1; cpu->pc--; return 4;

    /* ── LD r, r' (64 opcodes: 0x40–0x7F minus 0x76=HALT) ──────────── *
     * Bits 5-3 = dst, bits 2-0 = src.  Src/dst 6 = (HL).
     * Moving between any two registers in one clock cycle.
     */
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4F:
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x57:
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5F:
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x67:
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6F:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7F:
    case 0x46: case 0x4E: case 0x56: case 0x5E:
    case 0x66: case 0x6E: case 0x7E: {
        int dst = (op >> 3) & 7;
        int src = op & 7;
        uint8_t val;
        if (src == 6) { val = sms_mem_read(Z80_HL(cpu), ctx); }
        else           { val = *reg8(cpu, src); }
        if (dst == 6)  { sms_mem_write(Z80_HL(cpu), val, ctx); return 7; }
        else           { *reg8(cpu, dst) = val; }
        return 4;
    }

    /* ── ALU A, r (0x80–0xBF) ───────────────────────────────────────── *
     * 8 operations × 8 registers = 64 opcodes.
     * Ops: ADD ADC SUB SBC AND XOR OR CP
     * Registers: B C D E H L (HL) A
     */
    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
    case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
    {
        int alu_op = (op >> 3) & 7;
        int r      =  op & 7;
        uint8_t val = (r == 6) ? sms_mem_read(Z80_HL(cpu), ctx) : *reg8(cpu, r);
        switch (alu_op) {
        case 0: do_add(cpu, val, 0);             break; /* ADD */
        case 1: do_add(cpu, val, cpu->f&Z80_CF); break; /* ADC */
        case 2: do_sub(cpu, val, 0);             break; /* SUB */
        case 3: do_sub(cpu, val, cpu->f&Z80_CF); break; /* SBC */
        case 4: do_and(cpu, val);                break; /* AND */
        case 5: do_xor(cpu, val);                break; /* XOR */
        case 6: do_or (cpu, val);                break; /* OR  */
        case 7: do_cp (cpu, val);                break; /* CP  */
        }
        return (r == 6) ? 7 : 4;
    }

    /* ── ALU A, n (immediate operand versions) ──────────────────────── */
    case 0xC6: do_add(cpu, fetch(cpu,ctx), 0);             return 7;
    case 0xCE: do_add(cpu, fetch(cpu,ctx), cpu->f&Z80_CF); return 7;
    case 0xD6: do_sub(cpu, fetch(cpu,ctx), 0);             return 7;
    case 0xDE: do_sub(cpu, fetch(cpu,ctx), cpu->f&Z80_CF); return 7;
    case 0xE6: do_and(cpu, fetch(cpu,ctx));                return 7;
    case 0xEE: do_xor(cpu, fetch(cpu,ctx));                return 7;
    case 0xF6: do_or (cpu, fetch(cpu,ctx));                return 7;
    case 0xFE: do_cp (cpu, fetch(cpu,ctx));                return 7;

    /* ── PUSH / POP ─────────────────────────────────────────────────── */
    case 0xC1: Z80_SET_BC(cpu, pop16(cpu,ctx));  return 10;
    case 0xD1: Z80_SET_DE(cpu, pop16(cpu,ctx));  return 10;
    case 0xE1: Z80_SET_HL(cpu, pop16(cpu,ctx));  return 10;
    case 0xF1: Z80_SET_AF(cpu, pop16(cpu,ctx));  return 10;
    case 0xC5: push16(cpu, Z80_BC(cpu), ctx);    return 11;
    case 0xD5: push16(cpu, Z80_DE(cpu), ctx);    return 11;
    case 0xE5: push16(cpu, Z80_HL(cpu), ctx);    return 11;
    case 0xF5: push16(cpu, Z80_AF(cpu), ctx);    return 11;

    /* ── JP / JR (jumps and relative jumps) ────────────────────────── */
    case 0xC3: { uint16_t nn = fetch16(cpu,ctx); cpu->pc = nn; return 10; }
    case 0xE9: cpu->pc = Z80_HL(cpu); return 4;  /* JP (HL) */

    /* Conditional JP cc, nn */
    case 0xC2: { uint16_t nn = fetch16(cpu,ctx); if (!(cpu->f&Z80_ZF)) cpu->pc=nn; return 10; }
    case 0xCA: { uint16_t nn = fetch16(cpu,ctx); if ( (cpu->f&Z80_ZF)) cpu->pc=nn; return 10; }
    case 0xD2: { uint16_t nn = fetch16(cpu,ctx); if (!(cpu->f&Z80_CF)) cpu->pc=nn; return 10; }
    case 0xDA: { uint16_t nn = fetch16(cpu,ctx); if ( (cpu->f&Z80_CF)) cpu->pc=nn; return 10; }
    case 0xE2: { uint16_t nn = fetch16(cpu,ctx); if (!(cpu->f&Z80_PF)) cpu->pc=nn; return 10; }
    case 0xEA: { uint16_t nn = fetch16(cpu,ctx); if ( (cpu->f&Z80_PF)) cpu->pc=nn; return 10; }
    case 0xF2: { uint16_t nn = fetch16(cpu,ctx); if (!(cpu->f&Z80_SF)) cpu->pc=nn; return 10; }
    case 0xFA: { uint16_t nn = fetch16(cpu,ctx); if ( (cpu->f&Z80_SF)) cpu->pc=nn; return 10; }

    /* JR e — relative jump (signed byte offset from next instruction) */
    case 0x18: { int8_t e = fetch_d(cpu,ctx); cpu->pc += e; return 12; }

    /* Conditional JR */
    case 0x20: { int8_t e = fetch_d(cpu,ctx); if (!(cpu->f&Z80_ZF)) { cpu->pc+=e; return 12; } return 7; }
    case 0x28: { int8_t e = fetch_d(cpu,ctx); if ( (cpu->f&Z80_ZF)) { cpu->pc+=e; return 12; } return 7; }
    case 0x30: { int8_t e = fetch_d(cpu,ctx); if (!(cpu->f&Z80_CF)) { cpu->pc+=e; return 12; } return 7; }
    case 0x38: { int8_t e = fetch_d(cpu,ctx); if ( (cpu->f&Z80_CF)) { cpu->pc+=e; return 12; } return 7; }

    /* DJNZ e — decrement B; jump if B != 0 */
    case 0x10: { int8_t e = fetch_d(cpu,ctx); cpu->b--; if (cpu->b) { cpu->pc+=e; return 13; } return 8; }

    /* ── CALL / RET ─────────────────────────────────────────────────── */
    case 0xCD: { uint16_t nn = fetch16(cpu,ctx); push16(cpu, cpu->pc, ctx); cpu->pc = nn; return 17; }
    case 0xC9: cpu->pc = pop16(cpu,ctx); return 10;

    /* Conditional CALL cc, nn */
    case 0xC4: { uint16_t nn=fetch16(cpu,ctx); if(!(cpu->f&Z80_ZF)){push16(cpu,cpu->pc,ctx);cpu->pc=nn;return 17;} return 10; }
    case 0xCC: { uint16_t nn=fetch16(cpu,ctx); if( (cpu->f&Z80_ZF)){push16(cpu,cpu->pc,ctx);cpu->pc=nn;return 17;} return 10; }
    case 0xD4: { uint16_t nn=fetch16(cpu,ctx); if(!(cpu->f&Z80_CF)){push16(cpu,cpu->pc,ctx);cpu->pc=nn;return 17;} return 10; }
    case 0xDC: { uint16_t nn=fetch16(cpu,ctx); if( (cpu->f&Z80_CF)){push16(cpu,cpu->pc,ctx);cpu->pc=nn;return 17;} return 10; }
    case 0xE4: { uint16_t nn=fetch16(cpu,ctx); if(!(cpu->f&Z80_PF)){push16(cpu,cpu->pc,ctx);cpu->pc=nn;return 17;} return 10; }
    case 0xEC: { uint16_t nn=fetch16(cpu,ctx); if( (cpu->f&Z80_PF)){push16(cpu,cpu->pc,ctx);cpu->pc=nn;return 17;} return 10; }
    case 0xF4: { uint16_t nn=fetch16(cpu,ctx); if(!(cpu->f&Z80_SF)){push16(cpu,cpu->pc,ctx);cpu->pc=nn;return 17;} return 10; }
    case 0xFC: { uint16_t nn=fetch16(cpu,ctx); if( (cpu->f&Z80_SF)){push16(cpu,cpu->pc,ctx);cpu->pc=nn;return 17;} return 10; }

    /* Conditional RET cc */
    case 0xC0: if(!(cpu->f&Z80_ZF)){cpu->pc=pop16(cpu,ctx);return 11;} return 5;
    case 0xC8: if( (cpu->f&Z80_ZF)){cpu->pc=pop16(cpu,ctx);return 11;} return 5;
    case 0xD0: if(!(cpu->f&Z80_CF)){cpu->pc=pop16(cpu,ctx);return 11;} return 5;
    case 0xD8: if( (cpu->f&Z80_CF)){cpu->pc=pop16(cpu,ctx);return 11;} return 5;
    case 0xE0: if(!(cpu->f&Z80_PF)){cpu->pc=pop16(cpu,ctx);return 11;} return 5;
    case 0xE8: if( (cpu->f&Z80_PF)){cpu->pc=pop16(cpu,ctx);return 11;} return 5;
    case 0xF0: if(!(cpu->f&Z80_SF)){cpu->pc=pop16(cpu,ctx);return 11;} return 5;
    case 0xF8: if( (cpu->f&Z80_SF)){cpu->pc=pop16(cpu,ctx);return 11;} return 5;

    /* ── RST n — restart (vectored call to fixed address) ───────────── */
    case 0xC7: push16(cpu,cpu->pc,ctx); cpu->pc=0x00; return 11;
    case 0xCF: push16(cpu,cpu->pc,ctx); cpu->pc=0x08; return 11;
    case 0xD7: push16(cpu,cpu->pc,ctx); cpu->pc=0x10; return 11;
    case 0xDF: push16(cpu,cpu->pc,ctx); cpu->pc=0x18; return 11;
    case 0xE7: push16(cpu,cpu->pc,ctx); cpu->pc=0x20; return 11;
    case 0xEF: push16(cpu,cpu->pc,ctx); cpu->pc=0x28; return 11;
    case 0xF7: push16(cpu,cpu->pc,ctx); cpu->pc=0x30; return 11;
    case 0xFF: push16(cpu,cpu->pc,ctx); cpu->pc=0x38; return 11;

    /* ── I/O instructions ───────────────────────────────────────────── */
    case 0xDB: { uint8_t p = fetch(cpu,ctx); cpu->a = sms_io_read(p, ctx); return 11; }  /* IN A,(n)  */
    case 0xD3: { uint8_t p = fetch(cpu,ctx); sms_io_write(p, cpu->a, ctx); return 11; }  /* OUT (n),A */

    /* ── EX instructions ────────────────────────────────────────────── */
    case 0xEB: { /* EX DE, HL */
        uint16_t t = Z80_DE(cpu);
        Z80_SET_DE(cpu, Z80_HL(cpu));
        Z80_SET_HL(cpu, t);
        return 4;
    }
    case 0xE3: { /* EX (SP), HL */
        uint16_t t = mem_read16(cpu->sp, ctx);
        mem_write16(cpu->sp, Z80_HL(cpu), ctx);
        Z80_SET_HL(cpu, t);
        return 19;
    }
    case 0x08: { /* EX AF, AF' */
        uint8_t ta = cpu->a, tf = cpu->f;
        cpu->a = cpu->a2; cpu->f = cpu->f2;
        cpu->a2 = ta;     cpu->f2 = tf;
        return 4;
    }
    case 0xD9: { /* EXX — swap BC, DE, HL with their shadow copies */
        uint8_t t;
        t=cpu->b; cpu->b=cpu->b2; cpu->b2=t;
        t=cpu->c; cpu->c=cpu->c2; cpu->c2=t;
        t=cpu->d; cpu->d=cpu->d2; cpu->d2=t;
        t=cpu->e; cpu->e=cpu->e2; cpu->e2=t;
        t=cpu->h; cpu->h=cpu->h2; cpu->h2=t;
        t=cpu->l; cpu->l=cpu->l2; cpu->l2=t;
        return 4;
    }

    /* ── SP instructions ────────────────────────────────────────────── */
    case 0xF9: cpu->sp = Z80_HL(cpu); return 6;  /* LD SP, HL */

    /* ── Interrupt enable/disable ───────────────────────────────────── */
    case 0xF3: cpu->iff1 = cpu->iff2 = 0; return 4; /* DI */
    case 0xFB: cpu->iff1 = cpu->iff2 = 1; return 4; /* EI */

    /* ── RETI / prefix collision handled as NOPs here ───────────────── */
    default: return 4; /* unknown opcode: NOP-equivalent */
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void z80_reset(Z80 *cpu)
{
    memset(cpu, 0, sizeof(Z80));
    cpu->pc  = 0x0000;
    cpu->sp  = 0xFFFF;
    cpu->f   = 0xFF;   /* undefined on real hardware; 0xFF is common */
    /* IFF1=IFF2=0: interrupts disabled */
    /* IM=0: interrupt mode 0 */
    build_flag_tables();
}

int z80_step(Z80 *cpu, void *ctx)
{
    /* When HALT, CPU executes NOP until an interrupt wakes it */
    if (cpu->halted) return 4;

    cpu->r = (cpu->r & 0x80) | ((cpu->r + 1) & 0x7F); /* refresh counter */

    uint8_t op = fetch(cpu, ctx);
    switch (op) {
    case 0xCB: return exec_cb(cpu, ctx);
    case 0xED: return exec_ed(cpu, ctx);
    case 0xDD: { int t = exec_idx(cpu, ctx, &cpu->ix); if (t) return t;
                 op = fetch(cpu, ctx); return exec_main(cpu, ctx, op); }
    case 0xFD: { int t = exec_idx(cpu, ctx, &cpu->iy); if (t) return t;
                 op = fetch(cpu, ctx); return exec_main(cpu, ctx, op); }
    default:   return exec_main(cpu, ctx, op);
    }
}

int z80_nmi(Z80 *cpu, void *ctx)
{
    cpu->halted = 0;
    cpu->iff2   = cpu->iff1;  /* save iff1 into iff2 */
    cpu->iff1   = 0;          /* disable maskable interrupts */
    push16(cpu, cpu->pc, ctx);
    cpu->pc = 0x0066;         /* NMI vector — fixed on Z80 */
    return 11;
}

int z80_irq(Z80 *cpu, void *ctx)
{
    if (!cpu->iff1) return 0; /* masked */
    cpu->halted = 0;
    cpu->iff2   = cpu->iff1;  /* save IFF1 → IFF2 (restored by RETI) */
    cpu->iff1   = 0;
    push16(cpu, cpu->pc, ctx);
    /* SMS always uses interrupt mode 1 → jump to $0038 */
    cpu->pc = 0x0038;
    return 13;
}
