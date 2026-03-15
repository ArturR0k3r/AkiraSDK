/**
 * @file cpu6502.c
 * @brief Minimal MOS 6502 CPU interpreter for NES emulation
 *
 * Covers all 56 official opcodes + the most common unofficial opcodes
 * (LAX, SAX, DCP, ISC, SLO, RLA, SRE, RRA, various NOPs).
 *
 * @license Apache-2.0
 */
#include "cpu6502.h"

/* ── Internal helpers ───────────────────────────────────────────────── */

/* Stack lives at 0x0100-0x01FF */
#define PUSH(v)  do { wr(0x0100u | cpu->sp--, (uint8_t)(v), ctx); } while(0)
#define POP()    (rd(0x0100u | (uint8_t)(++cpu->sp), ctx))

/* Flag manipulation */
#define SET(f)   (cpu->p |= (uint8_t)(f))
#define CLR(f)   (cpu->p &= (uint8_t)~(f))
#define GET(f)   ((cpu->p & (f)) ? 1 : 0)

/* Set N and Z flags from value v */
#define SETNZ(v) do { \
    uint8_t _v = (uint8_t)(v); \
    if (_v)       CLR(P_Z); else SET(P_Z); \
    if (_v & 0x80) SET(P_N); else CLR(P_N); \
} while(0)

/* Read a 16-bit little-endian word from address a */
#define RD16(a) ((uint16_t)(rd((uint16_t)(a), ctx) | \
                            ((uint16_t)rd((uint16_t)((a)+1), ctx) << 8)))

/* 1 if high bytes of a and b differ (page crossing) */
#define PAGE_CROSS(a,b) (((a) & 0xFF00u) != ((b) & 0xFF00u))

/* ── Addressing mode helpers (advance PC, return effective address) ── */

static inline uint16_t zp (CPU6502 *c, cpu_read_fn rd, void *ctx)
    { return rd(c->pc++, ctx); }

static inline uint16_t zpx(CPU6502 *c, cpu_read_fn rd, void *ctx)
    { return (uint8_t)(rd(c->pc++, ctx) + c->x); }

static inline uint16_t zpy(CPU6502 *c, cpu_read_fn rd, void *ctx)
    { return (uint8_t)(rd(c->pc++, ctx) + c->y); }

static inline uint16_t ab (CPU6502 *c, cpu_read_fn rd, void *ctx)
    { uint16_t a = RD16(c->pc); c->pc += 2; return a; }

static inline uint16_t abx(CPU6502 *c, cpu_read_fn rd, void *ctx, int *xc)
{
    uint16_t base = RD16(c->pc); c->pc += 2;
    uint16_t eff  = base + c->x;
    if (xc && PAGE_CROSS(base, eff)) (*xc)++;
    return eff;
}

static inline uint16_t aby(CPU6502 *c, cpu_read_fn rd, void *ctx, int *xc)
{
    uint16_t base = RD16(c->pc); c->pc += 2;
    uint16_t eff  = base + c->y;
    if (xc && PAGE_CROSS(base, eff)) (*xc)++;
    return eff;
}

static inline uint16_t izx(CPU6502 *c, cpu_read_fn rd, void *ctx)
{
    uint8_t ptr = rd(c->pc++, ctx) + c->x;
    return (uint16_t)rd(ptr, ctx) | ((uint16_t)rd((uint8_t)(ptr+1), ctx) << 8);
}

static inline uint16_t izy(CPU6502 *c, cpu_read_fn rd, void *ctx, int *xc)
{
    uint8_t  ptr  = rd(c->pc++, ctx);
    uint16_t base = (uint16_t)rd(ptr, ctx) | ((uint16_t)rd((uint8_t)(ptr+1), ctx) << 8);
    uint16_t eff  = base + c->y;
    if (xc && PAGE_CROSS(base, eff)) (*xc)++;
    return eff;
}

/* ── Branching ───────────────────────────────────────────────────────── */
static inline int branch(CPU6502 *c, cpu_read_fn rd, void *ctx, int taken)
{
    int8_t   off = (int8_t)rd(c->pc++, ctx);
    if (!taken) return 2;
    uint16_t old = c->pc;
    c->pc = (uint16_t)(c->pc + off);
    return 3 + (PAGE_CROSS(old, c->pc) ? 1 : 0);
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

void cpu6502_reset(CPU6502 *cpu, cpu_read_fn rd, void *ctx)
{
    cpu->sp = 0xFD;
    cpu->a = cpu->x = cpu->y = 0;
    cpu->p = P_U | P_I;
    cpu->pc = RD16(0xFFFC);
    cpu->cycles = 7;
}

void cpu6502_nmi(CPU6502 *cpu, cpu_read_fn rd, cpu_write_fn wr, void *ctx)
{
    PUSH(cpu->pc >> 8);
    PUSH(cpu->pc & 0xFF);
    CLR(P_B); SET(P_U);
    PUSH(cpu->p);
    SET(P_I);
    cpu->pc = RD16(0xFFFA);
    cpu->cycles += 7;
}

void cpu6502_irq(CPU6502 *cpu, cpu_read_fn rd, cpu_write_fn wr, void *ctx)
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

int cpu6502_step(CPU6502 *cpu, cpu_read_fn rd, cpu_write_fn wr, void *ctx)
{
    cpu->cycles = 0;
    int xc = 0;
    uint8_t  op  = rd(cpu->pc++, ctx);
    uint16_t ea;
    uint8_t  val;

    switch (op) {

    /* ── LDA ────────────────────────────────────────────────────────── */
    case 0xA9: cpu->a=rd(cpu->pc++,ctx);       SETNZ(cpu->a); cpu->cycles=2; break;
    case 0xA5: cpu->a=rd(zp(cpu,rd,ctx),ctx);  SETNZ(cpu->a); cpu->cycles=3; break;
    case 0xB5: cpu->a=rd(zpx(cpu,rd,ctx),ctx); SETNZ(cpu->a); cpu->cycles=4; break;
    case 0xAD: cpu->a=rd(ab(cpu,rd,ctx),ctx);  SETNZ(cpu->a); cpu->cycles=4; break;
    case 0xBD: ea=abx(cpu,rd,ctx,&xc); cpu->a=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0xB9: ea=aby(cpu,rd,ctx,&xc); cpu->a=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0xA1: cpu->a=rd(izx(cpu,rd,ctx),ctx); SETNZ(cpu->a); cpu->cycles=6; break;
    case 0xB1: ea=izy(cpu,rd,ctx,&xc); cpu->a=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── LDX ────────────────────────────────────────────────────────── */
    case 0xA2: cpu->x=rd(cpu->pc++,ctx);       SETNZ(cpu->x); cpu->cycles=2; break;
    case 0xA6: cpu->x=rd(zp(cpu,rd,ctx),ctx);  SETNZ(cpu->x); cpu->cycles=3; break;
    case 0xB6: cpu->x=rd(zpy(cpu,rd,ctx),ctx); SETNZ(cpu->x); cpu->cycles=4; break;
    case 0xAE: cpu->x=rd(ab(cpu,rd,ctx),ctx);  SETNZ(cpu->x); cpu->cycles=4; break;
    case 0xBE: ea=aby(cpu,rd,ctx,&xc); cpu->x=rd(ea,ctx); SETNZ(cpu->x); cpu->cycles=4+xc; break;

    /* ── LDY ────────────────────────────────────────────────────────── */
    case 0xA0: cpu->y=rd(cpu->pc++,ctx);       SETNZ(cpu->y); cpu->cycles=2; break;
    case 0xA4: cpu->y=rd(zp(cpu,rd,ctx),ctx);  SETNZ(cpu->y); cpu->cycles=3; break;
    case 0xB4: cpu->y=rd(zpx(cpu,rd,ctx),ctx); SETNZ(cpu->y); cpu->cycles=4; break;
    case 0xAC: cpu->y=rd(ab(cpu,rd,ctx),ctx);  SETNZ(cpu->y); cpu->cycles=4; break;
    case 0xBC: ea=abx(cpu,rd,ctx,&xc); cpu->y=rd(ea,ctx); SETNZ(cpu->y); cpu->cycles=4+xc; break;

    /* ── STA ────────────────────────────────────────────────────────── */
    case 0x85: wr(zp(cpu,rd,ctx), cpu->a, ctx);  cpu->cycles=3; break;
    case 0x95: wr(zpx(cpu,rd,ctx),cpu->a, ctx);  cpu->cycles=4; break;
    case 0x8D: wr(ab(cpu,rd,ctx), cpu->a, ctx);  cpu->cycles=4; break;
    case 0x9D: ea=abx(cpu,rd,ctx,0); wr(ea,cpu->a,ctx); cpu->cycles=5; break;
    case 0x99: ea=aby(cpu,rd,ctx,0); wr(ea,cpu->a,ctx); cpu->cycles=5; break;
    case 0x81: wr(izx(cpu,rd,ctx),cpu->a, ctx);  cpu->cycles=6; break;
    case 0x91: ea=izy(cpu,rd,ctx,0); wr(ea,cpu->a,ctx); cpu->cycles=6; break;

    /* ── STX ────────────────────────────────────────────────────────── */
    case 0x86: wr(zp(cpu,rd,ctx), cpu->x, ctx); cpu->cycles=3; break;
    case 0x96: wr(zpy(cpu,rd,ctx),cpu->x, ctx); cpu->cycles=4; break;
    case 0x8E: wr(ab(cpu,rd,ctx), cpu->x, ctx); cpu->cycles=4; break;

    /* ── STY ────────────────────────────────────────────────────────── */
    case 0x84: wr(zp(cpu,rd,ctx), cpu->y, ctx); cpu->cycles=3; break;
    case 0x94: wr(zpx(cpu,rd,ctx),cpu->y, ctx); cpu->cycles=4; break;
    case 0x8C: wr(ab(cpu,rd,ctx), cpu->y, ctx); cpu->cycles=4; break;

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
    case 0x69: do_adc(cpu,rd(cpu->pc++,ctx));         cpu->cycles=2; break;
    case 0x65: do_adc(cpu,rd(zp(cpu,rd,ctx),ctx));    cpu->cycles=3; break;
    case 0x75: do_adc(cpu,rd(zpx(cpu,rd,ctx),ctx));   cpu->cycles=4; break;
    case 0x6D: do_adc(cpu,rd(ab(cpu,rd,ctx),ctx));    cpu->cycles=4; break;
    case 0x7D: ea=abx(cpu,rd,ctx,&xc); do_adc(cpu,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0x79: ea=aby(cpu,rd,ctx,&xc); do_adc(cpu,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0x61: do_adc(cpu,rd(izx(cpu,rd,ctx),ctx));   cpu->cycles=6; break;
    case 0x71: ea=izy(cpu,rd,ctx,&xc); do_adc(cpu,rd(ea,ctx)); cpu->cycles=5+xc; break;

    /* ── SBC ────────────────────────────────────────────────────────── */
    case 0xE9: do_sbc(cpu,rd(cpu->pc++,ctx));         cpu->cycles=2; break;
    case 0xEB: do_sbc(cpu,rd(cpu->pc++,ctx));         cpu->cycles=2; break; /* unofficial */
    case 0xE5: do_sbc(cpu,rd(zp(cpu,rd,ctx),ctx));    cpu->cycles=3; break;
    case 0xF5: do_sbc(cpu,rd(zpx(cpu,rd,ctx),ctx));   cpu->cycles=4; break;
    case 0xED: do_sbc(cpu,rd(ab(cpu,rd,ctx),ctx));    cpu->cycles=4; break;
    case 0xFD: ea=abx(cpu,rd,ctx,&xc); do_sbc(cpu,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0xF9: ea=aby(cpu,rd,ctx,&xc); do_sbc(cpu,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0xE1: do_sbc(cpu,rd(izx(cpu,rd,ctx),ctx));   cpu->cycles=6; break;
    case 0xF1: ea=izy(cpu,rd,ctx,&xc); do_sbc(cpu,rd(ea,ctx)); cpu->cycles=5+xc; break;

    /* ── AND ────────────────────────────────────────────────────────── */
    case 0x29: cpu->a&=rd(cpu->pc++,ctx);         SETNZ(cpu->a); cpu->cycles=2; break;
    case 0x25: cpu->a&=rd(zp(cpu,rd,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=3; break;
    case 0x35: cpu->a&=rd(zpx(cpu,rd,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x2D: cpu->a&=rd(ab(cpu,rd,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x3D: ea=abx(cpu,rd,ctx,&xc); cpu->a&=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x39: ea=aby(cpu,rd,ctx,&xc); cpu->a&=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x21: cpu->a&=rd(izx(cpu,rd,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x31: ea=izy(cpu,rd,ctx,&xc); cpu->a&=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── ORA ────────────────────────────────────────────────────────── */
    case 0x09: cpu->a|=rd(cpu->pc++,ctx);         SETNZ(cpu->a); cpu->cycles=2; break;
    case 0x05: cpu->a|=rd(zp(cpu,rd,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=3; break;
    case 0x15: cpu->a|=rd(zpx(cpu,rd,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x0D: cpu->a|=rd(ab(cpu,rd,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x1D: ea=abx(cpu,rd,ctx,&xc); cpu->a|=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x19: ea=aby(cpu,rd,ctx,&xc); cpu->a|=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x01: cpu->a|=rd(izx(cpu,rd,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x11: ea=izy(cpu,rd,ctx,&xc); cpu->a|=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── EOR ────────────────────────────────────────────────────────── */
    case 0x49: cpu->a^=rd(cpu->pc++,ctx);         SETNZ(cpu->a); cpu->cycles=2; break;
    case 0x45: cpu->a^=rd(zp(cpu,rd,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=3; break;
    case 0x55: cpu->a^=rd(zpx(cpu,rd,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x4D: cpu->a^=rd(ab(cpu,rd,ctx),ctx);    SETNZ(cpu->a); cpu->cycles=4; break;
    case 0x5D: ea=abx(cpu,rd,ctx,&xc); cpu->a^=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x59: ea=aby(cpu,rd,ctx,&xc); cpu->a^=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0x41: cpu->a^=rd(izx(cpu,rd,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x51: ea=izy(cpu,rd,ctx,&xc); cpu->a^=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── BIT ────────────────────────────────────────────────────────── */
    case 0x24: val=rd(zp(cpu,rd,ctx),ctx);
        if(!(cpu->a&val))SET(P_Z);else CLR(P_Z);
        if(val&0x80)SET(P_N);else CLR(P_N);
        if(val&0x40)SET(P_V);else CLR(P_V); cpu->cycles=3; break;
    case 0x2C: val=rd(ab(cpu,rd,ctx),ctx);
        if(!(cpu->a&val))SET(P_Z);else CLR(P_Z);
        if(val&0x80)SET(P_N);else CLR(P_N);
        if(val&0x40)SET(P_V);else CLR(P_V); cpu->cycles=4; break;

    /* ── CMP ────────────────────────────────────────────────────────── */
    case 0xC9: do_cmp(cpu,cpu->a,rd(cpu->pc++,ctx));          cpu->cycles=2; break;
    case 0xC5: do_cmp(cpu,cpu->a,rd(zp(cpu,rd,ctx),ctx));     cpu->cycles=3; break;
    case 0xD5: do_cmp(cpu,cpu->a,rd(zpx(cpu,rd,ctx),ctx));    cpu->cycles=4; break;
    case 0xCD: do_cmp(cpu,cpu->a,rd(ab(cpu,rd,ctx),ctx));     cpu->cycles=4; break;
    case 0xDD: ea=abx(cpu,rd,ctx,&xc); do_cmp(cpu,cpu->a,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0xD9: ea=aby(cpu,rd,ctx,&xc); do_cmp(cpu,cpu->a,rd(ea,ctx)); cpu->cycles=4+xc; break;
    case 0xC1: do_cmp(cpu,cpu->a,rd(izx(cpu,rd,ctx),ctx));    cpu->cycles=6; break;
    case 0xD1: ea=izy(cpu,rd,ctx,&xc); do_cmp(cpu,cpu->a,rd(ea,ctx)); cpu->cycles=5+xc; break;

    /* ── CPX ────────────────────────────────────────────────────────── */
    case 0xE0: do_cmp(cpu,cpu->x,rd(cpu->pc++,ctx));          cpu->cycles=2; break;
    case 0xE4: do_cmp(cpu,cpu->x,rd(zp(cpu,rd,ctx),ctx));     cpu->cycles=3; break;
    case 0xEC: do_cmp(cpu,cpu->x,rd(ab(cpu,rd,ctx),ctx));     cpu->cycles=4; break;

    /* ── CPY ────────────────────────────────────────────────────────── */
    case 0xC0: do_cmp(cpu,cpu->y,rd(cpu->pc++,ctx));          cpu->cycles=2; break;
    case 0xC4: do_cmp(cpu,cpu->y,rd(zp(cpu,rd,ctx),ctx));     cpu->cycles=3; break;
    case 0xCC: do_cmp(cpu,cpu->y,rd(ab(cpu,rd,ctx),ctx));     cpu->cycles=4; break;

    /* ── INC ────────────────────────────────────────────────────────── */
    case 0xE6: ea=zp(cpu,rd,ctx);   val=rd(ea,ctx)+1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=5; break;
    case 0xF6: ea=zpx(cpu,rd,ctx);  val=rd(ea,ctx)+1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=6; break;
    case 0xEE: ea=ab(cpu,rd,ctx);   val=rd(ea,ctx)+1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=6; break;
    case 0xFE: ea=abx(cpu,rd,ctx,0); val=rd(ea,ctx)+1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=7; break;

    /* ── DEC ────────────────────────────────────────────────────────── */
    case 0xC6: ea=zp(cpu,rd,ctx);   val=rd(ea,ctx)-1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=5; break;
    case 0xD6: ea=zpx(cpu,rd,ctx);  val=rd(ea,ctx)-1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=6; break;
    case 0xCE: ea=ab(cpu,rd,ctx);   val=rd(ea,ctx)-1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=6; break;
    case 0xDE: ea=abx(cpu,rd,ctx,0); val=rd(ea,ctx)-1; wr(ea,val,ctx); SETNZ(val); cpu->cycles=7; break;

    /* ── INX/INY/DEX/DEY ─────────────────────────────────────────────── */
    case 0xE8: cpu->x++; SETNZ(cpu->x); cpu->cycles=2; break;
    case 0xC8: cpu->y++; SETNZ(cpu->y); cpu->cycles=2; break;
    case 0xCA: cpu->x--; SETNZ(cpu->x); cpu->cycles=2; break;
    case 0x88: cpu->y--; SETNZ(cpu->y); cpu->cycles=2; break;

    /* ── ASL ────────────────────────────────────────────────────────── */
    case 0x0A: cpu->a=do_asl(cpu,cpu->a); cpu->cycles=2; break;
    case 0x06: ea=zp(cpu,rd,ctx);   wr(ea,do_asl(cpu,rd(ea,ctx)),ctx); cpu->cycles=5; break;
    case 0x16: ea=zpx(cpu,rd,ctx);  wr(ea,do_asl(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x0E: ea=ab(cpu,rd,ctx);   wr(ea,do_asl(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x1E: ea=abx(cpu,rd,ctx,0); wr(ea,do_asl(cpu,rd(ea,ctx)),ctx); cpu->cycles=7; break;

    /* ── LSR ────────────────────────────────────────────────────────── */
    case 0x4A: cpu->a=do_lsr(cpu,cpu->a); cpu->cycles=2; break;
    case 0x46: ea=zp(cpu,rd,ctx);   wr(ea,do_lsr(cpu,rd(ea,ctx)),ctx); cpu->cycles=5; break;
    case 0x56: ea=zpx(cpu,rd,ctx);  wr(ea,do_lsr(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x4E: ea=ab(cpu,rd,ctx);   wr(ea,do_lsr(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x5E: ea=abx(cpu,rd,ctx,0); wr(ea,do_lsr(cpu,rd(ea,ctx)),ctx); cpu->cycles=7; break;

    /* ── ROL ────────────────────────────────────────────────────────── */
    case 0x2A: cpu->a=do_rol(cpu,cpu->a); cpu->cycles=2; break;
    case 0x26: ea=zp(cpu,rd,ctx);   wr(ea,do_rol(cpu,rd(ea,ctx)),ctx); cpu->cycles=5; break;
    case 0x36: ea=zpx(cpu,rd,ctx);  wr(ea,do_rol(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x2E: ea=ab(cpu,rd,ctx);   wr(ea,do_rol(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x3E: ea=abx(cpu,rd,ctx,0); wr(ea,do_rol(cpu,rd(ea,ctx)),ctx); cpu->cycles=7; break;

    /* ── ROR ────────────────────────────────────────────────────────── */
    case 0x6A: cpu->a=do_ror(cpu,cpu->a); cpu->cycles=2; break;
    case 0x66: ea=zp(cpu,rd,ctx);   wr(ea,do_ror(cpu,rd(ea,ctx)),ctx); cpu->cycles=5; break;
    case 0x76: ea=zpx(cpu,rd,ctx);  wr(ea,do_ror(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x6E: ea=ab(cpu,rd,ctx);   wr(ea,do_ror(cpu,rd(ea,ctx)),ctx); cpu->cycles=6; break;
    case 0x7E: ea=abx(cpu,rd,ctx,0); wr(ea,do_ror(cpu,rd(ea,ctx)),ctx); cpu->cycles=7; break;

    /* ── Jumps ───────────────────────────────────────────────────────── */
    case 0x4C: cpu->pc=ab(cpu,rd,ctx); cpu->cycles=3; break;   /* JMP abs */
    case 0x6C: {  /* JMP ind — 6502 page-wrap bug */
        uint16_t ptr = RD16(cpu->pc); cpu->pc += 2;
        cpu->pc = rd(ptr,ctx) | ((uint16_t)rd((ptr&0xFF00u)|((ptr+1)&0xFF),ctx)<<8);
        cpu->cycles=5;
    } break;
    case 0x20: {  /* JSR */
        uint16_t tgt=RD16(cpu->pc); cpu->pc+=2;
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
    case 0x10: cpu->cycles=branch(cpu,rd,ctx,!(cpu->p&P_N)); break; /* BPL */
    case 0x30: cpu->cycles=branch(cpu,rd,ctx,  cpu->p&P_N);  break; /* BMI */
    case 0x50: cpu->cycles=branch(cpu,rd,ctx,!(cpu->p&P_V)); break; /* BVC */
    case 0x70: cpu->cycles=branch(cpu,rd,ctx,  cpu->p&P_V);  break; /* BVS */
    case 0x90: cpu->cycles=branch(cpu,rd,ctx,!(cpu->p&P_C)); break; /* BCC */
    case 0xB0: cpu->cycles=branch(cpu,rd,ctx,  cpu->p&P_C);  break; /* BCS */
    case 0xD0: cpu->cycles=branch(cpu,rd,ctx,!(cpu->p&P_Z)); break; /* BNE */
    case 0xF0: cpu->cycles=branch(cpu,rd,ctx,  cpu->p&P_Z);  break; /* BEQ */

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
        ea=abx(cpu,rd,ctx,&xc); (void)ea; cpu->cycles=4+xc; break; /* NOP abx */

    /* ── Unofficial: LAX (LDA then TXA simultaneously) ─────────────── */
    case 0xA7: cpu->a=cpu->x=rd(zp(cpu,rd,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=3; break;
    case 0xB7: cpu->a=cpu->x=rd(zpy(cpu,rd,ctx),ctx);  SETNZ(cpu->a); cpu->cycles=4; break;
    case 0xAF: cpu->a=cpu->x=rd(ab(cpu,rd,ctx),ctx);   SETNZ(cpu->a); cpu->cycles=4; break;
    case 0xBF: ea=aby(cpu,rd,ctx,&xc); cpu->a=cpu->x=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=4+xc; break;
    case 0xA3: cpu->a=cpu->x=rd(izx(cpu,rd,ctx),ctx);  SETNZ(cpu->a); cpu->cycles=6; break;
    case 0xB3: ea=izy(cpu,rd,ctx,&xc); cpu->a=cpu->x=rd(ea,ctx); SETNZ(cpu->a); cpu->cycles=5+xc; break;

    /* ── Unofficial: SAX (store A AND X) ────────────────────────────── */
    case 0x87: wr(zp(cpu,rd,ctx), cpu->a&cpu->x, ctx); cpu->cycles=3; break;
    case 0x97: wr(zpy(cpu,rd,ctx),cpu->a&cpu->x, ctx); cpu->cycles=4; break;
    case 0x8F: wr(ab(cpu,rd,ctx), cpu->a&cpu->x, ctx); cpu->cycles=4; break;
    case 0x83: wr(izx(cpu,rd,ctx),cpu->a&cpu->x, ctx); cpu->cycles=6; break;

    /* ── Unofficial: DCP (DEC memory then CMP) ──────────────────────── */
    case 0xC7: ea=zp(cpu,rd,ctx);   val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=5; break;
    case 0xD7: ea=zpx(cpu,rd,ctx);  val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=6; break;
    case 0xCF: ea=ab(cpu,rd,ctx);   val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=6; break;
    case 0xDF: ea=abx(cpu,rd,ctx,0); val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=7; break;
    case 0xDB: ea=aby(cpu,rd,ctx,0); val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=7; break;
    case 0xC3: ea=izx(cpu,rd,ctx);  val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=8; break;
    case 0xD3: ea=izy(cpu,rd,ctx,0); val=rd(ea,ctx)-1; wr(ea,val,ctx); do_cmp(cpu,cpu->a,val); cpu->cycles=8; break;

    /* ── Unofficial: ISC (INC memory then SBC) ──────────────────────── */
    case 0xE7: ea=zp(cpu,rd,ctx);   val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=5; break;
    case 0xF7: ea=zpx(cpu,rd,ctx);  val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=6; break;
    case 0xEF: ea=ab(cpu,rd,ctx);   val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=6; break;
    case 0xFF: ea=abx(cpu,rd,ctx,0); val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=7; break;
    case 0xFB: ea=aby(cpu,rd,ctx,0); val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=7; break;
    case 0xE3: ea=izx(cpu,rd,ctx);  val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=8; break;
    case 0xF3: ea=izy(cpu,rd,ctx,0); val=rd(ea,ctx)+1; wr(ea,val,ctx); do_sbc(cpu,val); cpu->cycles=8; break;

    /* ── Unofficial: SLO (ASL memory then ORA) ──────────────────────── */
    case 0x07: ea=zp(cpu,rd,ctx);   val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=5; break;
    case 0x17: ea=zpx(cpu,rd,ctx);  val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x0F: ea=ab(cpu,rd,ctx);   val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x1F: ea=abx(cpu,rd,ctx,0); val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x1B: ea=aby(cpu,rd,ctx,0); val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x03: ea=izx(cpu,rd,ctx);  val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=8; break;
    case 0x13: ea=izy(cpu,rd,ctx,0); val=do_asl(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a|=val; SETNZ(cpu->a); cpu->cycles=8; break;

    /* ── Unofficial: RLA (ROL memory then AND) ──────────────────────── */
    case 0x27: ea=zp(cpu,rd,ctx);   val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=5; break;
    case 0x37: ea=zpx(cpu,rd,ctx);  val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x2F: ea=ab(cpu,rd,ctx);   val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x3F: ea=abx(cpu,rd,ctx,0); val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x3B: ea=aby(cpu,rd,ctx,0); val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x23: ea=izx(cpu,rd,ctx);  val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=8; break;
    case 0x33: ea=izy(cpu,rd,ctx,0); val=do_rol(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a&=val; SETNZ(cpu->a); cpu->cycles=8; break;

    /* ── Unofficial: SRE (LSR memory then EOR) ──────────────────────── */
    case 0x47: ea=zp(cpu,rd,ctx);   val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=5; break;
    case 0x57: ea=zpx(cpu,rd,ctx);  val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x4F: ea=ab(cpu,rd,ctx);   val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=6; break;
    case 0x5F: ea=abx(cpu,rd,ctx,0); val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x5B: ea=aby(cpu,rd,ctx,0); val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=7; break;
    case 0x43: ea=izx(cpu,rd,ctx);  val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=8; break;
    case 0x53: ea=izy(cpu,rd,ctx,0); val=do_lsr(cpu,rd(ea,ctx)); wr(ea,val,ctx); cpu->a^=val; SETNZ(cpu->a); cpu->cycles=8; break;

    /* ── Unofficial: RRA (ROR memory then ADC) ──────────────────────── */
    case 0x67: ea=zp(cpu,rd,ctx);   val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=5; break;
    case 0x77: ea=zpx(cpu,rd,ctx);  val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=6; break;
    case 0x6F: ea=ab(cpu,rd,ctx);   val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=6; break;
    case 0x7F: ea=abx(cpu,rd,ctx,0); val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=7; break;
    case 0x7B: ea=aby(cpu,rd,ctx,0); val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=7; break;
    case 0x63: ea=izx(cpu,rd,ctx);  val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=8; break;
    case 0x73: ea=izy(cpu,rd,ctx,0); val=do_ror(cpu,rd(ea,ctx)); wr(ea,val,ctx); do_adc(cpu,val); cpu->cycles=8; break;

    default: cpu->cycles=2; break; /* Unknown: treat as 2-cycle NOP */
    }

    return cpu->cycles;
}
