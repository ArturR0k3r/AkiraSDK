/**
 * @file cpu6502.c
 * @brief Minimal MOS 6502 CPU interpreter for NES emulation
 *
 * Covers all 56 official opcodes + the most common unofficial opcodes
 * (LAX, SAX, DCP, ISC, SLO, RLA, SRE, RRA, various NOPs).
 *
 * Performance note: bus access uses direct function calls (not function
 * pointers) to avoid expensive WASM call_indirect overhead.  Stack and
 * zero-page accesses go through the cpu->ram pointer, bypassing the bus
 * entirely for the most frequent memory operations.
 *
 * @license Apache-2.0
 */
#include "cpu6502.h"

/* ── Direct bus access (implemented in nes.c) ───────────────────────
 * Using extern + direct call instead of function pointers eliminates
 * WASM call_indirect overhead (~40,000 indirect calls per frame).    */
extern uint8_t nes_cpu_read (uint16_t addr, void *ctx);
extern void    nes_cpu_write(uint16_t addr, uint8_t val, void *ctx);

/* ── Inline ROM fetch ────────────────────────────────────────────────
 * PC is virtually always in ROM ($8000+).  Fetching opcode/operand
 * bytes via the rom_pages pointer eliminates a WASM function call
 * on every instruction (~20,000–30,000 calls per frame).            */
static inline uint8_t fetch(CPU6502 *cpu) {
    uint16_t a = cpu->pc++;
    if (__builtin_expect(a >= 0x8000u, 1))
        return cpu->rom_pages[(a >> 13) & 3][a & 0x1FFFu];
    if (a < 0x2000u)  return cpu->ram[a & 0x7FFu];
    if (a >= 0x6000u) return cpu->sram[a - 0x6000u];
    return 0; /* $2000-$5FFF: I/O, not executable */
}
static inline uint16_t fetch16(CPU6502 *cpu) {
    uint8_t lo = fetch(cpu);
    uint8_t hi = fetch(cpu);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}
#define FETCH()   fetch(cpu)
#define FETCH16() fetch16(cpu)

/* ── General read / write with ROM+WRAM fast paths ──────────────────
 * Only calls the bus function for PPU registers and I/O ($2000-$7FFF).
 * ROM reads and WRAM reads/writes are handled inline.               */
static inline uint8_t rd_fn(CPU6502 *cpu, uint16_t a, void *ctx) {
    if (a >= 0x8000u) return cpu->rom_pages[(a >> 13) & 3][a & 0x1FFFu];
    if (a < 0x2000u)  return cpu->ram[a & 0x7FFu];
    return nes_cpu_read(a, ctx);
}
static inline void wr_fn(CPU6502 *cpu, uint16_t a, uint8_t v, void *ctx) {
    if (a < 0x2000u) { cpu->ram[a & 0x7FFu] = v; return; }
    nes_cpu_write(a, v, ctx);
}
#define rd(a, c)    rd_fn(cpu, (uint16_t)(a), (c))
#define wr(a, v, c) wr_fn(cpu, (uint16_t)(a), (uint8_t)(v), (c))

/* Fast zero-page read/write — ZP is always WRAM, bypass bus entirely */
#define ZP_RD(a)     (cpu->ram[(uint8_t)(a)])
#define ZP_WR(a, v)  do { cpu->ram[(uint8_t)(a)] = (uint8_t)(v); } while(0)

/* ── NZ flag table: maps byte value → P_N|P_Z flags ────────────────
 * Eliminates 2 branches per instruction (~15K branches/frame).      */
static const uint8_t nz_table[256] = {
    /* 0x00 */ P_Z,
    /* 0x01..0x7F: no flags (positive non-zero) */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x80..0xFF: P_N (negative) */
    P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,
    P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,
    P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,
    P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,
    P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,
    P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,
    P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,
    P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,P_N,
};

/* ── Internal helpers ───────────────────────────────────────────────── */

/* Stack lives at 0x0100-0x01FF — always in WRAM, bypass bus entirely */
#define PUSH(v)  do { cpu->ram[0x0100u | cpu->sp--] = (uint8_t)(v); } while(0)
#define POP()    (cpu->ram[0x0100u | (uint8_t)(++cpu->sp)])

/* Flag manipulation */
#define SET(f)   (cpu->p |= (uint8_t)(f))
#define CLR(f)   (cpu->p &= (uint8_t)~(f))
#define GET(f)   ((cpu->p & (f)) ? 1 : 0)

/* Set N and Z flags — single table lookup instead of 2 branches */
#define SETNZ(v) do { \
    cpu->p = (cpu->p & ~(P_N|P_Z)) | nz_table[(uint8_t)(v)]; \
} while(0)

/* Read a 16-bit little-endian word from address a */
#define RD16(a) ((uint16_t)(rd((uint16_t)(a), ctx) | \
                            ((uint16_t)rd((uint16_t)((a)+1), ctx) << 8)))

/* 1 if high bytes of a and b differ (page crossing) */
#define PAGE_CROSS(a,b) (((a) & 0xFF00u) != ((b) & 0xFF00u))

/* ── Addressing mode helpers (advance PC, return effective address) ── */

static inline uint16_t zp (CPU6502 *cpu, void *ctx)
    { (void)ctx; return FETCH(); }

static inline uint16_t zpx(CPU6502 *cpu, void *ctx)
    { (void)ctx; return (uint8_t)(FETCH() + cpu->x); }

static inline uint16_t zpy(CPU6502 *cpu, void *ctx)
    { (void)ctx; return (uint8_t)(FETCH() + cpu->y); }

static inline uint16_t ab (CPU6502 *cpu, void *ctx)
    { (void)ctx; return FETCH16(); }

static inline uint16_t abx(CPU6502 *cpu, void *ctx, int *xc)
{
    (void)ctx;
    uint16_t base = FETCH16();
    uint16_t eff  = base + cpu->x;
    if (xc && PAGE_CROSS(base, eff)) (*xc)++;
    return eff;
}

static inline uint16_t aby(CPU6502 *cpu, void *ctx, int *xc)
{
    (void)ctx;
    uint16_t base = FETCH16();
    uint16_t eff  = base + cpu->y;
    if (xc && PAGE_CROSS(base, eff)) (*xc)++;
    return eff;
}

static inline uint16_t izx(CPU6502 *cpu, void *ctx)
{
    (void)ctx;
    uint8_t ptr = FETCH() + cpu->x;
    return (uint16_t)cpu->ram[ptr] | ((uint16_t)cpu->ram[(uint8_t)(ptr+1)] << 8);
}

static inline uint16_t izy(CPU6502 *cpu, void *ctx, int *xc)
{
    (void)ctx;
    uint8_t  ptr  = FETCH();
    uint16_t base = (uint16_t)cpu->ram[ptr] | ((uint16_t)cpu->ram[(uint8_t)(ptr+1)] << 8);
    uint16_t eff  = base + cpu->y;
    if (xc && PAGE_CROSS(base, eff)) (*xc)++;
    return eff;
}

/* ── Branching ───────────────────────────────────────────────────────── */
static inline int branch(CPU6502 *cpu, void *ctx, int taken)
{
    (void)ctx;
    int8_t   off = (int8_t)FETCH();
    if (!taken) return 2;
    uint16_t old = cpu->pc;
    cpu->pc = (uint16_t)(cpu->pc + off);
    return 3 + (PAGE_CROSS(old, cpu->pc) ? 1 : 0);
}

/* ── ALU operations ──────────────────────────────────────────────────── */
static inline void do_adc(CPU6502 *cpu, uint8_t val)
{
    uint16_t sum = cpu->a + val + GET(P_C);
    if (sum > 0xFF)                          SET(P_C); else CLR(P_C);
    if (~(cpu->a ^ val) & (cpu->a ^ sum) & 0x80) SET(P_V); else CLR(P_V);
    cpu->a = (uint8_t)sum;
    SETNZ(cpu->a);
}
static inline void do_sbc(CPU6502 *cpu, uint8_t val) { do_adc(cpu, ~val); }

static inline void do_cmp(CPU6502 *cpu, uint8_t reg, uint8_t val)
{
    uint16_t r = (uint16_t)reg - val;
    if (r < 0x100) SET(P_C); else CLR(P_C);
    SETNZ((uint8_t)r);
}

static inline uint8_t do_asl(CPU6502 *cpu, uint8_t v)
    { if (v & 0x80) SET(P_C); else CLR(P_C); v<<=1; SETNZ(v); return v; }

static inline uint8_t do_lsr(CPU6502 *cpu, uint8_t v)
    { if (v & 0x01) SET(P_C); else CLR(P_C); v>>=1; SETNZ(v); return v; }

static inline uint8_t do_rol(CPU6502 *cpu, uint8_t v)
    { uint8_t old=GET(P_C); if(v&0x80)SET(P_C);else CLR(P_C); v=(v<<1)|old; SETNZ(v); return v; }

static inline uint8_t do_ror(CPU6502 *cpu, uint8_t v)
    { uint8_t old=GET(P_C); if(v&0x01)SET(P_C);else CLR(P_C); v=(v>>1)|(old<<7); SETNZ(v); return v; }

/* ── Public API ──────────────────────────────────────────────────────── */

void cpu6502_reset(CPU6502 *cpu, void *ctx)
{
    cpu->sp = 0xFD;
    cpu->a = cpu->x = cpu->y = 0;
    cpu->p = P_U | P_I;
    cpu->pc = RD16(0xFFFC);
    cpu->cycles = 7;
}

void cpu6502_nmi(CPU6502 *cpu, void *ctx)
{
    PUSH(cpu->pc >> 8);
    PUSH(cpu->pc & 0xFF);
    CLR(P_B); SET(P_U);
    PUSH(cpu->p);
    SET(P_I);
    cpu->pc = RD16(0xFFFA);
    cpu->cycles += 7;
}

void cpu6502_irq(CPU6502 *cpu, void *ctx)
{
    if (cpu->p & P_I) return;
    PUSH(cpu->pc >> 8);
    PUSH(cpu->pc & 0xFF);
    CLR(P_B); SET(P_U);
    PUSH(cpu->p);
    SET(P_I);
    cpu->pc = RD16(0xFFFE);
    cpu->cycles += 7;
}

int cpu6502_step(CPU6502 *cpu, void *ctx)
{
    cpu->cycles = 0;
    int xc = 0;
    uint8_t  op  = FETCH();
    uint16_t ea;
    uint8_t  val;

    switch (op) {

    /* ── LDA ────────────────────────────────────────────────────────── */
    case 0xA9: cpu->a=FETCH();       SETNZ(cpu->a); cpu->cycles=2; break;
    case 0xA5: cpu->a=ZP_RD(zp(cpu,ctx));  SETNZ(cpu->a); cpu->cycles=3; break;
    case 0xB5: cpu->a=ZP_RD(zpx(cpu,ctx)); SETNZ(cpu->a); cpu->cycles=4; break;
    case 0xAD: cpu->a=rd(ab(cpu,ctx),ctx);  SETNZ(cpu->a); cpu->cycles=4; break;
    case 0xBD: ea=abx(cpu,ctx,&xc); cpu->a=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0xB9: ea=aby(cpu,ctx,&xc); cpu->a=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0xA1: cpu->a=rd(izx(cpu,ctx),ctx); SETNZ(cpu->a); cpu->cycles=6; break;
    case 0xB1: ea=izy(cpu,ctx,&xc); cpu->a=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── LDX ────────────────────────────────────────────────────────── */
    case 0xA2: cpu->x=FETCH();       SETNZ(cpu->x); cpu->cycles=2; break;
    case 0xA6: cpu->x=ZP_RD(zp(cpu,ctx));  SETNZ(cpu->x); cpu->cycles=3; break;
    case 0xB6: cpu->x=ZP_RD(zpy(cpu,ctx)); SETNZ(cpu->x); cpu->cycles=4; break;
    case 0xAE: cpu->x=rd(ab(cpu,ctx),ctx);  SETNZ(cpu->x); cpu->cycles=4; break;
    case 0xBE: ea=aby(cpu,ctx,&xc); cpu->x=rd(ea,ctx); SETNZ(cpu->x); cpu->cycles=4+xc; break;

    /* ── LDY ────────────────────────────────────────────────────────── */
    case 0xA0: cpu->y=FETCH();       SETNZ(cpu->y); cpu->cycles=2; break;
    case 0xA4: cpu->y=ZP_RD(zp(cpu,ctx));  SETNZ(cpu->y); cpu->cycles=3; break;
    case 0xB4: cpu->y=ZP_RD(zpx(cpu,ctx)); SETNZ(cpu->y); cpu->cycles=4; break;
    case 0xAC: cpu->y=rd(ab(cpu,ctx),ctx);  SETNZ(cpu->y); cpu->cycles=4; break;
    case 0xBC: ea=abx(cpu,ctx,&xc); cpu->y=rd(ea,ctx); SETNZ(cpu->y); cpu->cycles=4+xc; break;

    /* ── STA ────────────────────────────────────────────────────────── */
    case 0x85: ZP_WR(zp(cpu,ctx), cpu->a);  cpu->cycles=3; break;
    case 0x95: ZP_WR(zpx(cpu,ctx),cpu->a);  cpu->cycles=4; break;
    case 0x8D: wr(ab(cpu,ctx), cpu->a, ctx);  cpu->cycles=4; break;
    case 0x9D: ea=abx(cpu,ctx,0); wr(ea,cpu->a,ctx); cpu->cycles=5; break;
    case 0x99: ea=aby(cpu,ctx,0); wr(ea,cpu->a,ctx); cpu->cycles=5; break;
    case 0x81: wr(izx(cpu,ctx),cpu->a, ctx);  cpu->cycles=6; break;
    case 0x91: ea=izy(cpu,ctx,0); wr(ea,cpu->a,ctx); cpu->cycles=6; break;

    /* ── STX ────────────────────────────────────────────────────────── */
    case 0x86: ZP_WR(zp(cpu,ctx), cpu->x); cpu->cycles=3; break;
    case 0x96: ZP_WR(zpy(cpu,ctx),cpu->x); cpu->cycles=4; break;
    case 0x8E: wr(ab(cpu,ctx), cpu->x, ctx); cpu->cycles=4; break;

    /* ── STY ────────────────────────────────────────────────────────── */
    case 0x84: ZP_WR(zp(cpu,ctx), cpu->y); cpu->cycles=3; break;
    case 0x94: ZP_WR(zpx(cpu,ctx),cpu->y); cpu->cycles=4; break;
    case 0x8C: wr(ab(cpu,ctx), cpu->y, ctx); cpu->cycles=4; break;

    /* ── Register transfers ──────────────────────────────────────────── */
    case 0xAA: cpu->x=cpu->a; SETNZ(cpu->x); cpu->cycles=2; break; /* TAX */
    case 0x8A: cpu->a=cpu->x; SETNZ(cpu->a); cpu->cycles=2; break; /* TXA */
    case 0xA8: cpu->y=cpu->a; SETNZ(cpu->y); cpu->cycles=2; break; /* TAY */
    case 0x98: cpu->a=cpu->y; SETNZ(cpu->a); cpu->cycles=2; break; /* TYA */
    case 0xBA: cpu->x=cpu->sp; SETNZ(cpu->x); cpu->cycles=2; break;/* TSX */
    case 0x9A: cpu->sp=cpu->x; cpu->cycles=2; break;               /* TXS */

    /* ── Stack ───────────────────────────────────────────────────────── */
    case 0x48: PUSH(cpu->a); cpu->cycles=3; break;                          /* PHA */
    case 0x68: cpu->a=POP(); SETNZ(cpu->a); cpu->cycles=4; break;           /* PLA */
    case 0x08: PUSH(cpu->p | P_B | P_U); cpu->cycles=3; break;              /* PHP */
    case 0x28: cpu->p=(POP() | P_U) & ~P_B; cpu->cycles=4; break;           /* PLP */

    /* ── ADC ────────────────────────────────────────────────────────── */
    case 0x69: do_adc(cpu,FETCH());         cpu->cycles=2; break;
    case 0x65: do_adc(cpu,ZP_RD(zp(cpu,ctx)));    cpu->cycles=3; break;
    case 0x75: do_adc(cpu,ZP_RD(zpx(cpu,ctx)));   cpu->cycles=4; break;
    case 0x6D: do_adc(cpu,rd(ab(cpu,ctx),ctx));    cpu->cycles=4; break;
    case 0x7D: ea=abx(cpu,ctx,&xc); do_adc(cpu,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0x79: ea=aby(cpu,ctx,&xc); do_adc(cpu,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0x61: do_adc(cpu,rd(izx(cpu,ctx),ctx));   cpu->cycles=6; break;
    case 0x71: ea=izy(cpu,ctx,&xc); do_adc(cpu,rd(ea,ctx)); cpu->cycles=5+xc; break;

    /* ── SBC ────────────────────────────────────────────────────────── */
    case 0xE9: do_sbc(cpu,FETCH());         cpu->cycles=2; break;
    case 0xEB: do_sbc(cpu,FETCH());         cpu->cycles=2; break; /* unofficial */
    case 0xE5: do_sbc(cpu,ZP_RD(zp(cpu,ctx)));    cpu->cycles=3; break;
    case 0xF5: do_sbc(cpu,ZP_RD(zpx(cpu,ctx)));   cpu->cycles=4; break;
    case 0xED: do_sbc(cpu,rd(ab(cpu,ctx),ctx));    cpu->cycles=4; break;
    case 0xFD: ea=abx(cpu,ctx,&xc); do_sbc(cpu,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0xF9: ea=aby(cpu,ctx,&xc); do_sbc(cpu,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0xE1: do_sbc(cpu,rd(izx(cpu,ctx),ctx));   cpu->cycles=6; break;
    case 0xF1: ea=izy(cpu,ctx,&xc); do_sbc(cpu,rd(ea,ctx)); cpu->cycles=5+xc; break;

    /* ── AND ────────────────────────────────────────────────────────── */
    case 0x29: cpu->a&=FETCH();         SETNZ(cpu->a); cpu->cycles=2; break;
    case 0x25: cpu->a&=ZP_RD(zp(cpu,ctx));    SETNZ(cpu->a); cpu->cycles=3; break;
    case 0x35: cpu->a&=ZP_RD(zpx(cpu,ctx));   SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x2D: cpu->a&=rd(ab(cpu,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x3D: ea=abx(cpu,ctx,&xc); cpu->a&=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x39: ea=aby(cpu,ctx,&xc); cpu->a&=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x21: cpu->a&=rd(izx(cpu,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x31: ea=izy(cpu,ctx,&xc); cpu->a&=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── ORA ────────────────────────────────────────────────────────── */
    case 0x09: cpu->a|=FETCH();         SETNZ(cpu->a); cpu->cycles=2; break;
    case 0x05: cpu->a|=ZP_RD(zp(cpu,ctx));    SETNZ(cpu->a); cpu->cycles=3; break;
    case 0x15: cpu->a|=ZP_RD(zpx(cpu,ctx));   SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x0D: cpu->a|=rd(ab(cpu,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x1D: ea=abx(cpu,ctx,&xc); cpu->a|=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x19: ea=aby(cpu,ctx,&xc); cpu->a|=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x01: cpu->a|=rd(izx(cpu,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x11: ea=izy(cpu,ctx,&xc); cpu->a|=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── EOR ────────────────────────────────────────────────────────── */
    case 0x49: cpu->a^=FETCH();         SETNZ(cpu->a); cpu->cycles=2; break;
    case 0x45: cpu->a^=ZP_RD(zp(cpu,ctx));    SETNZ(cpu->a); cpu->cycles=3; break;
    case 0x55: cpu->a^=ZP_RD(zpx(cpu,ctx));   SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x4D: cpu->a^=rd(ab(cpu,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x5D: ea=abx(cpu,ctx,&xc); cpu->a^=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x59: ea=aby(cpu,ctx,&xc); cpu->a^=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x41: cpu->a^=rd(izx(cpu,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x51: ea=izy(cpu,ctx,&xc); cpu->a^=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── BIT ────────────────────────────────────────────────────────── */
    case 0x24: val=ZP_RD(zp(cpu,ctx));
        if(!(cpu->a&val))SET(P_Z);else CLR(P_Z);
        if(val&0x80)SET(P_N);else CLR(P_N);
        if(val&0x40)SET(P_V);else CLR(P_V); cpu->cycles=3; break;
    case 0x2C: val=rd(ab(cpu,ctx),ctx);
        if(!(cpu->a&val))SET(P_Z);else CLR(P_Z);
        if(val&0x80)SET(P_N);else CLR(P_N);
        if(val&0x40)SET(P_V);else CLR(P_V); cpu->cycles=4; break;

    /* ── CMP ────────────────────────────────────────────────────────── */
    case 0xC9: do_cmp(cpu,cpu->a,FETCH());          cpu->cycles=2; break;
    case 0xC5: do_cmp(cpu,cpu->a,ZP_RD(zp(cpu,ctx)));     cpu->cycles=3; break;
    case 0xD5: do_cmp(cpu,cpu->a,ZP_RD(zpx(cpu,ctx)));    cpu->cycles=4; break;
    case 0xCD: do_cmp(cpu,cpu->a,rd(ab(cpu,ctx),ctx));     cpu->cycles=4; break;
    case 0xDD: ea=abx(cpu,ctx,&xc); do_cmp(cpu,cpu->a,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0xD9: ea=aby(cpu,ctx,&xc); do_cmp(cpu,cpu->a,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0xC1: do_cmp(cpu,cpu->a,rd(izx(cpu,ctx),ctx));    cpu->cycles=6; break;
    case 0xD1: ea=izy(cpu,ctx,&xc); do_cmp(cpu,cpu->a,rd(ea,ctx)); cpu->cycles=5+xc; break;

    /* ── CPX ────────────────────────────────────────────────────────── */
    case 0xE0: do_cmp(cpu,cpu->x,FETCH());          cpu->cycles=2; break;
    case 0xE4: do_cmp(cpu,cpu->x,ZP_RD(zp(cpu,ctx)));     cpu->cycles=3; break;
    case 0xEC: do_cmp(cpu,cpu->x,rd(ab(cpu,ctx),ctx));     cpu->cycles=4; break;

    /* ── CPY ────────────────────────────────────────────────────────── */
    case 0xC0: do_cmp(cpu,cpu->y,FETCH());          cpu->cycles=2; break;
    case 0xC4: do_cmp(cpu,cpu->y,ZP_RD(zp(cpu,ctx)));     cpu->cycles=3; break;
    case 0xCC: do_cmp(cpu,cpu->y,rd(ab(cpu,ctx),ctx));     cpu->cycles=4; break;

    /* ── INC ────────────────────────────────────────────────────────── */
    case 0xE6: ea=zp(cpu,ctx);   val=ZP_RD(ea)+1; ZP_WR(ea,val); SETNZ(val); cpu->cycles=5; break;
    case 0xF6: ea=zpx(cpu,ctx);  val=ZP_RD(ea)+1; ZP_WR(ea,val); SETNZ(val); cpu->cycles=6; break;
    case 0xEE: ea=ab(cpu,ctx);   val=rd(ea,ctx)+1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=6; break;
    case 0xFE: ea=abx(cpu,ctx,0); val=rd(ea,ctx)+1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=7; break;

    /* ── DEC ────────────────────────────────────────────────────────── */
    case 0xC6: ea=zp(cpu,ctx);   val=ZP_RD(ea)-1; ZP_WR(ea,val); SETNZ(val); cpu->cycles=5; break;
    case 0xD6: ea=zpx(cpu,ctx);  val=ZP_RD(ea)-1; ZP_WR(ea,val); SETNZ(val); cpu->cycles=6; break;
    case 0xCE: ea=ab(cpu,ctx);   val=rd(ea,ctx)-1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=6; break;
    case 0xDE: ea=abx(cpu,ctx,0); val=rd(ea,ctx)-1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=7; break;

    /* ── INX/INY/DEX/DEY ─────────────────────────────────────────────── */
    case 0xE8: cpu->x++; SETNZ(cpu->x); cpu->cycles=2; break;
    case 0xC8: cpu->y++; SETNZ(cpu->y); cpu->cycles=2; break;
    case 0xCA: cpu->x--; SETNZ(cpu->x); cpu->cycles=2; break;
    case 0x88: cpu->y--; SETNZ(cpu->y); cpu->cycles=2; break;

    /* ── ASL ────────────────────────────────────────────────────────── */
    case 0x0A: cpu->a=do_asl(cpu,cpu->a); cpu->cycles=2; break;
    case 0x06: ea=zp(cpu,ctx);   ZP_WR(ea,do_asl(cpu,ZP_RD(ea))); cpu->cycles=5; break;
    case 0x16: ea=zpx(cpu,ctx);  ZP_WR(ea,do_asl(cpu,ZP_RD(ea))); cpu->cycles=6; break;
    case 0x0E: ea=ab(cpu,ctx);   wr(ea,do_asl(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x1E: ea=abx(cpu,ctx,0); wr(ea,do_asl(cpu,rd(ea,ctx)),ctx); cpu->cycles=7; break;

    /* ── LSR ────────────────────────────────────────────────────────── */
    case 0x4A: cpu->a=do_lsr(cpu,cpu->a); cpu->cycles=2; break;
    case 0x46: ea=zp(cpu,ctx);   ZP_WR(ea,do_lsr(cpu,ZP_RD(ea))); cpu->cycles=5; break;
    case 0x56: ea=zpx(cpu,ctx);  ZP_WR(ea,do_lsr(cpu,ZP_RD(ea))); cpu->cycles=6; break;
    case 0x4E: ea=ab(cpu,ctx);   wr(ea,do_lsr(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x5E: ea=abx(cpu,ctx,0); wr(ea,do_lsr(cpu,rd(ea,ctx)),ctx); cpu->cycles=7; break;

    /* ── ROL ────────────────────────────────────────────────────────── */
    case 0x2A: cpu->a=do_rol(cpu,cpu->a); cpu->cycles=2; break;
    case 0x26: ea=zp(cpu,ctx);   ZP_WR(ea,do_rol(cpu,ZP_RD(ea))); cpu->cycles=5; break;
    case 0x36: ea=zpx(cpu,ctx);  ZP_WR(ea,do_rol(cpu,ZP_RD(ea))); cpu->cycles=6; break;
    case 0x2E: ea=ab(cpu,ctx);   wr(ea,do_rol(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x3E: ea=abx(cpu,ctx,0); wr(ea,do_rol(cpu,rd(ea,ctx)),ctx); cpu->cycles=7; break;

    /* ── ROR ────────────────────────────────────────────────────────── */
    case 0x6A: cpu->a=do_ror(cpu,cpu->a); cpu->cycles=2; break;
    case 0x66: ea=zp(cpu,ctx);   ZP_WR(ea,do_ror(cpu,ZP_RD(ea))); cpu->cycles=5; break;
    case 0x76: ea=zpx(cpu,ctx);  ZP_WR(ea,do_ror(cpu,ZP_RD(ea))); cpu->cycles=6; break;
    case 0x6E: ea=ab(cpu,ctx);   wr(ea,do_ror(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x7E: ea=abx(cpu,ctx,0); wr(ea,do_ror(cpu,rd(ea,ctx)),ctx); cpu->cycles=7; break;

    /* ── Jumps ───────────────────────────────────────────────────────── */
    case 0x4C: cpu->pc=ab(cpu,ctx); cpu->cycles=3; break;   /* JMP abs */
    case 0x6C: {  /* JMP ind — 6502 page-wrap bug */
        uint16_t ptr = FETCH16();
        cpu->pc = rd(ptr,ctx) | ((uint16_t)rd((ptr&0xFF00u)|((ptr+1)&0xFF),ctx)<<8);
        cpu->cycles=5;
    } break;
    case 0x20: {  /* JSR */
        uint16_t tgt=FETCH16();
        uint16_t ret=cpu->pc-1;
        PUSH(ret>>8); PUSH(ret&0xFF);
        cpu->pc=tgt; cpu->cycles=6;
    } break;
    case 0x60: cpu->pc=(POP()|((uint16_t)POP()<<8))+1; cpu->cycles=6; break; /* RTS */
    case 0x40: {  /* RTI */
        cpu->p=(POP()|P_U)&~P_B;
        cpu->pc=POP()|((uint16_t)POP()<<8);
        cpu->cycles=6;
    } break;
    case 0x00: {  /* BRK */
        cpu->pc++;
        PUSH(cpu->pc>>8); PUSH(cpu->pc&0xFF);
        PUSH(cpu->p|P_B|P_U);
        SET(P_I);
        cpu->pc=RD16(0xFFFE);
        cpu->cycles=7;
    } break;

    /* ── Branches ────────────────────────────────────────────────────── */
    case 0x10: cpu->cycles=branch(cpu,ctx,!(cpu->p&P_N)); break; /* BPL */
    case 0x30: cpu->cycles=branch(cpu,ctx,  cpu->p&P_N);  break; /* BMI */
    case 0x50: cpu->cycles=branch(cpu,ctx,!(cpu->p&P_V)); break; /* BVC */
    case 0x70: cpu->cycles=branch(cpu,ctx,  cpu->p&P_V);  break; /* BVS */
    case 0x90: cpu->cycles=branch(cpu,ctx,!(cpu->p&P_C)); break; /* BCC */
    case 0xB0: cpu->cycles=branch(cpu,ctx,  cpu->p&P_C);  break; /* BCS */
    case 0xD0: cpu->cycles=branch(cpu,ctx,!(cpu->p&P_Z)); break; /* BNE */
    case 0xF0: cpu->cycles=branch(cpu,ctx,  cpu->p&P_Z);  break; /* BEQ */

    /* ── Flag ops ────────────────────────────────────────────────────── */
    case 0x18: CLR(P_C); cpu->cycles=2; break; /* CLC */
    case 0x38: SET(P_C); cpu->cycles=2; break; /* SEC */
    case 0x58: CLR(P_I); cpu->cycles=2; break; /* CLI */
    case 0x78: SET(P_I); cpu->cycles=2; break; /* SEI */
    case 0xB8: CLR(P_V); cpu->cycles=2; break; /* CLV */
    case 0xD8: CLR(P_D); cpu->cycles=2; break; /* CLD */
    case 0xF8: SET(P_D); cpu->cycles=2; break; /* SED */

    /* ── NOP (official + common unofficial) ─────────────────────────── */
    case 0xEA:
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
        cpu->cycles=2; break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
        cpu->pc++; cpu->cycles=2; break;               /* NOP imm */
    case 0x04: case 0x44: case 0x64:
        cpu->pc++; cpu->cycles=3; break;               /* NOP zp */
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
        cpu->pc++; cpu->cycles=4; break;               /* NOP zpx */
    case 0x0C: cpu->pc+=2; cpu->cycles=4; break;       /* NOP abs */
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
        ea=abx(cpu,ctx,&xc); (void)ea; cpu->cycles=4+xc; break; /* NOP abx */

    /* ── Unofficial: LAX (LDA then TXA simultaneously) ─────────────── */
    case 0xA7: cpu->a=cpu->x=ZP_RD(zp(cpu,ctx));   SETNZ(cpu->a); cpu->cycles=3; break;
    case 0xB7: cpu->a=cpu->x=ZP_RD(zpy(cpu,ctx));  SETNZ(cpu->a); cpu->cycles=4; break;
    case 0xAF: cpu->a=cpu->x=rd(ab(cpu,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=4; break;
    case 0xBF: ea=aby(cpu,ctx,&xc); cpu->a=cpu->x=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0xA3: cpu->a=cpu->x=rd(izx(cpu,ctx),ctx);  SETNZ(cpu->a); cpu->cycles=6; break;
    case 0xB3: ea=izy(cpu,ctx,&xc); cpu->a=cpu->x=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── Unofficial: SAX (store A AND X) ────────────────────────────── */
    case 0x87: ZP_WR(zp(cpu,ctx), cpu->a&cpu->x); cpu->cycles=3; break;
    case 0x97: ZP_WR(zpy(cpu,ctx),cpu->a&cpu->x); cpu->cycles=4; break;
    case 0x8F: wr(ab(cpu,ctx), cpu->a&cpu->x, ctx); cpu->cycles=4; break;
    case 0x83: wr(izx(cpu,ctx),cpu->a&cpu->x, ctx); cpu->cycles=6; break;

    /* ── Unofficial: DCP (DEC memory then CMP) ──────────────────────── */
    case 0xC7: ea=zp(cpu,ctx);   val=ZP_RD(ea)-1; ZP_WR(ea,val); do_cmp(cpu,cpu->a,val); cpu->cycles=5; break;
    case 0xD7: ea=zpx(cpu,ctx);  val=ZP_RD(ea)-1; ZP_WR(ea,val); do_cmp(cpu,cpu->a,val); cpu->cycles=6; break;
    case 0xCF: ea=ab(cpu,ctx);   val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=6; break;
    case 0xDF: ea=abx(cpu,ctx,0); val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=7; break;
    case 0xDB: ea=aby(cpu,ctx,0); val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=7; break;
    case 0xC3: ea=izx(cpu,ctx);  val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=8; break;
    case 0xD3: ea=izy(cpu,ctx,0); val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=8; break;

    /* ── Unofficial: ISC (INC memory then SBC) ──────────────────────── */
    case 0xE7: ea=zp(cpu,ctx);   val=ZP_RD(ea)+1; ZP_WR(ea,val); do_sbc(cpu,val); cpu->cycles=5; break;
    case 0xF7: ea=zpx(cpu,ctx);  val=ZP_RD(ea)+1; ZP_WR(ea,val); do_sbc(cpu,val); cpu->cycles=6; break;
    case 0xEF: ea=ab(cpu,ctx);   val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=6; break;
    case 0xFF: ea=abx(cpu,ctx,0); val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=7; break;
    case 0xFB: ea=aby(cpu,ctx,0); val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=7; break;
    case 0xE3: ea=izx(cpu,ctx);  val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=8; break;
    case 0xF3: ea=izy(cpu,ctx,0); val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=8; break;

    /* ── Unofficial: SLO (ASL memory then ORA) ──────────────────────── */
    case 0x07: ea=zp(cpu,ctx);   val=do_asl(cpu,ZP_RD(ea)); ZP_WR(ea,val); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=5; break;
    case 0x17: ea=zpx(cpu,ctx);  val=do_asl(cpu,ZP_RD(ea)); ZP_WR(ea,val); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x0F: ea=ab(cpu,ctx);   val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x1F: ea=abx(cpu,ctx,0); val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x1B: ea=aby(cpu,ctx,0); val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x03: ea=izx(cpu,ctx);  val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=8; break;
    case 0x13: ea=izy(cpu,ctx,0); val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=8; break;

    /* ── Unofficial: RLA (ROL memory then AND) ──────────────────────── */
    case 0x27: ea=zp(cpu,ctx);   val=do_rol(cpu,ZP_RD(ea)); ZP_WR(ea,val); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=5; break;
    case 0x37: ea=zpx(cpu,ctx);  val=do_rol(cpu,ZP_RD(ea)); ZP_WR(ea,val); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x2F: ea=ab(cpu,ctx);   val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x3F: ea=abx(cpu,ctx,0); val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x3B: ea=aby(cpu,ctx,0); val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x23: ea=izx(cpu,ctx);  val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=8; break;
    case 0x33: ea=izy(cpu,ctx,0); val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=8; break;

    /* ── Unofficial: SRE (LSR memory then EOR) ──────────────────────── */
    case 0x47: ea=zp(cpu,ctx);   val=do_lsr(cpu,ZP_RD(ea)); ZP_WR(ea,val); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=5; break;
    case 0x57: ea=zpx(cpu,ctx);  val=do_lsr(cpu,ZP_RD(ea)); ZP_WR(ea,val); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x4F: ea=ab(cpu,ctx);   val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x5F: ea=abx(cpu,ctx,0); val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x5B: ea=aby(cpu,ctx,0); val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x43: ea=izx(cpu,ctx);  val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=8; break;
    case 0x53: ea=izy(cpu,ctx,0); val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=8; break;

    /* ── Unofficial: RRA (ROR memory then ADC) ──────────────────────── */
    case 0x67: ea=zp(cpu,ctx);   val=do_ror(cpu,ZP_RD(ea)); ZP_WR(ea,val); do_adc(cpu,val); cpu->cycles=5; break;
    case 0x77: ea=zpx(cpu,ctx);  val=do_ror(cpu,ZP_RD(ea)); ZP_WR(ea,val); do_adc(cpu,val); cpu->cycles=6; break;
    case 0x6F: ea=ab(cpu,ctx);   val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=6; break;
    case 0x7F: ea=abx(cpu,ctx,0); val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=7; break;
    case 0x7B: ea=aby(cpu,ctx,0); val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=7; break;
    case 0x63: ea=izx(cpu,ctx);  val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=8; break;
    case 0x73: ea=izy(cpu,ctx,0); val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=8; break;

    default: cpu->cycles=2; break; /* Unknown: treat as 2-cycle NOP */
    }

    return cpu->cycles;
}
