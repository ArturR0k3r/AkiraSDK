/**
 * @file cpu65816.c
 * @brief WDC 65C816 interpreter for the SNES core
 *
 * Full opcode set. Timing: every bus access is charged by snes_read/write
 * (6/8/12 master cycles), plus idle() cycles for the main internal
 * operations. Not cycle-exact.
 *
 * @license Apache-2.0
 */
#include "snes.h"

#define R (s->cpu)

static inline uint8_t rd(SNES *s, uint32_t a) { return snes_read(s, a & 0xFFFFFF); }
static inline void wr(SNES *s, uint32_t a, uint8_t v) { snes_write(s, a & 0xFFFFFF, v); }
static inline uint16_t rd16(SNES *s, uint32_t a) { uint16_t l = rd(s, a); return l | (rd(s, a + 1) << 8); }
static inline void wr16(SNES *s, uint32_t a, uint16_t v) { wr(s, a, v); wr(s, a + 1, v >> 8); }
static inline void idle(SNES *s) { s->hc += 6; }

static inline uint8_t f8(SNES *s)
{
    uint8_t v = rd(s, ((uint32_t)R.pb << 16) | R.pc);
    R.pc++;
    return v;
}
static inline uint16_t f16(SNES *s) { uint16_t l = f8(s); return l | (f8(s) << 8); }
static inline uint32_t f24(SNES *s) { uint32_t l = f16(s); return l | ((uint32_t)f8(s) << 16); }

/* ── Stack ───────────────────────────────────────────────────────────── */
static inline void push8(SNES *s, uint8_t v)
{
    wr(s, R.s, v);
    R.s--;
    if (R.fE) R.s = 0x100 | (R.s & 0xFF);
}
static inline uint8_t pull8(SNES *s)
{
    R.s++;
    if (R.fE) R.s = 0x100 | (R.s & 0xFF);
    return rd(s, R.s);
}
static inline void push16(SNES *s, uint16_t v) { push8(s, v >> 8); push8(s, v); }
static inline uint16_t pull16(SNES *s) { uint16_t l = pull8(s); return l | (pull8(s) << 8); }

/* ── Flags ───────────────────────────────────────────────────────────── */
static uint8_t get_p(SNES *s, int brk)
{
    uint8_t p = R.fC | (R.fZ << 1) | (R.fI << 2) | (R.fD << 3) | (R.fV << 6) | (R.fN << 7);
    if (R.fE) p |= 0x20 | (brk ? 0x10 : 0);
    else      p |= (R.fX << 4) | (R.fM << 5);
    return p;
}
static void set_p(SNES *s, uint8_t p)
{
    R.fC = p & 1; R.fZ = (p >> 1) & 1; R.fI = (p >> 2) & 1; R.fD = (p >> 3) & 1;
    R.fV = (p >> 6) & 1; R.fN = p >> 7;
    if (R.fE) { R.fX = R.fM = 1; }
    else      { R.fX = (p >> 4) & 1; R.fM = (p >> 5) & 1; }
    if (R.fX) { R.x &= 0xFF; R.y &= 0xFF; }
}

static inline void nz(SNES *s, uint16_t v, int w8)
{
    if (w8) { R.fZ = !(v & 0xFF); R.fN = (v >> 7) & 1; }
    else    { R.fZ = !v;          R.fN = v >> 15; }
}

/* ── Addressing ──────────────────────────────────────────────────────── */
static inline uint32_t a_dp(SNES *s)
{
    uint8_t o = f8(s);
    if (R.d & 0xFF) idle(s);
    return (uint16_t)(R.d + o);
}
static inline uint32_t a_dpi(SNES *s, uint16_t idx)
{
    uint8_t o = f8(s);
    if (R.d & 0xFF) idle(s);
    idle(s);
    if (R.fE && !(R.d & 0xFF)) return (uint16_t)(R.d + (uint8_t)(o + idx));
    return (uint16_t)(R.d + o + idx);
}
static inline uint32_t a_abs(SNES *s) { return ((uint32_t)R.db << 16) | f16(s); }
static inline uint32_t a_absi(SNES *s, uint16_t idx)
{
    uint32_t b = ((uint32_t)R.db << 16) | f16(s);
    idle(s);
    return (b + idx) & 0xFFFFFF;
}
static inline uint32_t a_ptr24(SNES *s, uint32_t a) { return rd16(s, a) | ((uint32_t)rd(s, a + 2) << 16); }

/* Decode the address of an ALU-group opcode. *imm set for immediate. */
static uint32_t alu_addr(SNES *s, uint8_t op, int *imm)
{
    *imm = 0;
    if ((op & 0x1F) == 0x12) {                          /* (dp) */
        uint32_t a = a_dp(s);
        return ((uint32_t)R.db << 16) | rd16(s, a);
    }
    int m = (op >> 2) & 7;
    if ((op & 3) == 1) {
        switch (m) {
        case 0: { uint32_t a = a_dpi(s, R.x); return ((uint32_t)R.db << 16) | rd16(s, a); }
        case 1: return a_dp(s);
        case 2: *imm = 1; return 0;
        case 3: return a_abs(s);
        case 4: { uint32_t a = a_dp(s); uint32_t p = ((uint32_t)R.db << 16) | rd16(s, a); idle(s); return (p + R.y) & 0xFFFFFF; }
        case 5: return a_dpi(s, R.x);
        case 6: return a_absi(s, R.y);
        default: return a_absi(s, R.x);
        }
    }
    switch (m) {                                        /* (op & 3) == 3 */
    case 0: { uint8_t o = f8(s); idle(s); return (uint16_t)(R.s + o); }
    case 1: { uint32_t a = a_dp(s); return a_ptr24(s, a); }
    case 3: return f24(s);
    case 4: { uint8_t o = f8(s); idle(s); uint32_t p = ((uint32_t)R.db << 16) | rd16(s, (uint16_t)(R.s + o)); idle(s); return (p + R.y) & 0xFFFFFF; }
    case 5: { uint32_t a = a_dp(s); return (a_ptr24(s, a) + R.y) & 0xFFFFFF; }
    default: { uint32_t b = f24(s); idle(s); return (b + R.x) & 0xFFFFFF; }
    }
}

static inline uint16_t rdm(SNES *s, uint32_t a) { return R.fM ? rd(s, a) : rd16(s, a); }
static inline void wrm(SNES *s, uint32_t a, uint16_t v) { if (R.fM) wr(s, a, v); else wr16(s, a, v); }
static inline uint16_t rdx(SNES *s, uint32_t a) { return R.fX ? rd(s, a) : rd16(s, a); }
static inline void wrx(SNES *s, uint32_t a, uint16_t v) { if (R.fX) wr(s, a, v); else wr16(s, a, v); }

/* ── ALU ─────────────────────────────────────────────────────────────── */
static void do_adc(SNES *s, uint16_t v)
{
    int w8 = R.fM;
    uint32_t a = w8 ? (R.a & 0xFF) : R.a, r;
    int bits = w8 ? 8 : 16;
    uint32_t sign = w8 ? 0x80 : 0x8000, mask = w8 ? 0xFF : 0xFFFF;
    if (!R.fD) {
        r = a + v + R.fC;
        R.fV = (~(a ^ v) & (a ^ r) & sign) != 0;
        R.fC = r > mask;
    } else {
        int carry = R.fC, i;
        r = 0;
        for (i = 0; i < bits; i += 4) {
            int t = ((a >> i) & 0xF) + ((v >> i) & 0xF) + carry;
            if (i == bits - 4) R.fV = (~(a ^ v) & (a ^ (r | ((uint32_t)t << i))) & sign) != 0;
            if (t > 9) t += 6;
            carry = t > 0xF;
            r |= (uint32_t)(t & 0xF) << i;
        }
        R.fC = carry;
    }
    if (w8) R.a = (R.a & 0xFF00) | (r & 0xFF); else R.a = r;
    nz(s, r, w8);
}

static void do_sbc(SNES *s, uint16_t v)
{
    int w8 = R.fM;
    uint32_t a = w8 ? (R.a & 0xFF) : R.a, r;
    int bits = w8 ? 8 : 16;
    uint32_t sign = w8 ? 0x80 : 0x8000, mask = w8 ? 0xFF : 0xFFFF;
    v = ~v & mask;
    if (!R.fD) {
        r = a + v + R.fC;
        R.fV = (~(a ^ v) & (a ^ r) & sign) != 0;
        R.fC = r > mask;
    } else {
        int carry = R.fC, i;
        r = 0;
        for (i = 0; i < bits; i += 4) {
            int t = ((a >> i) & 0xF) + ((v >> i) & 0xF) + carry;
            if (i == bits - 4) R.fV = (~(a ^ v) & (a ^ (r | ((uint32_t)t << i))) & sign) != 0;
            if (t <= 0xF) t -= 6;
            carry = t > 0xF;
            r |= (uint32_t)(t & 0xF) << i;
        }
        R.fC = carry;
    }
    if (w8) R.a = (R.a & 0xFF00) | (r & 0xFF); else R.a = r;
    nz(s, r, w8);
}

static inline void do_cmp(SNES *s, uint16_t reg, uint16_t v, int w8)
{
    uint32_t mask = w8 ? 0xFF : 0xFFFF;
    uint32_t r = (reg & mask) - (v & mask);
    R.fC = (reg & mask) >= (v & mask);
    nz(s, r, w8);
}

static void alu(SNES *s, uint8_t op)
{
    int imm;
    int grp = op >> 5;
    int w8 = R.fM;
    uint32_t addr = alu_addr(s, op, &imm);
    if (grp == 4) {                                     /* STA */
        wrm(s, addr, R.a);
        return;
    }
    uint16_t v = imm ? (w8 ? f8(s) : f16(s)) : rdm(s, addr);
    switch (grp) {
    case 0: if (w8) R.a = (R.a & 0xFF00) | ((R.a | v) & 0xFF); else R.a |= v; nz(s, R.a, w8); break;
    case 1: if (w8) R.a = (R.a & 0xFF00) | ((R.a & v) & 0xFF); else R.a &= v; nz(s, R.a, w8); break;
    case 2: if (w8) R.a = (R.a & 0xFF00) | ((R.a ^ v) & 0xFF); else R.a ^= v; nz(s, R.a, w8); break;
    case 3: do_adc(s, v); break;
    case 5: if (w8) R.a = (R.a & 0xFF00) | (v & 0xFF); else R.a = v; nz(s, R.a, w8); break;
    case 6: do_cmp(s, R.a, v, w8); break;
    default: do_sbc(s, v); break;
    }
}

/* Shift/rotate/inc/dec on a value of width w8; sets flags. */
static uint16_t rmw_op(SNES *s, int kind, uint16_t v, int w8)
{
    uint32_t mask = w8 ? 0xFF : 0xFFFF, mb = w8 ? 0x80 : 0x8000;
    int c;
    v &= mask;
    switch (kind) {
    case 0: R.fC = !!(v & mb); v = (v << 1) & mask; break;                  /* ASL */
    case 1: R.fC = v & 1; v >>= 1; break;                                   /* LSR */
    case 2: c = R.fC; R.fC = !!(v & mb); v = ((v << 1) | c) & mask; break;  /* ROL */
    case 3: c = R.fC; R.fC = v & 1; v = (v >> 1) | (c ? mb : 0); break;     /* ROR */
    case 4: v = (v + 1) & mask; break;                                      /* INC */
    default: v = (v - 1) & mask; break;                                     /* DEC */
    }
    nz(s, v, w8);
    return v;
}

static void rmw_mem(SNES *s, uint32_t a, int kind)
{
    uint16_t v = rdm(s, a);
    idle(s);
    v = rmw_op(s, kind, v, R.fM);
    wrm(s, a, v);
}

static void rmw_acc(SNES *s, int kind)
{
    idle(s);
    uint16_t v = rmw_op(s, kind, R.a, R.fM);
    if (R.fM) R.a = (R.a & 0xFF00) | (v & 0xFF); else R.a = v;
}

static void do_tsb(SNES *s, uint32_t a, int trb)
{
    uint16_t v = rdm(s, a);
    idle(s);
    R.fZ = !(v & R.a & (R.fM ? 0xFF : 0xFFFF));
    v = trb ? (v & ~R.a) : (v | R.a);
    wrm(s, a, v);
}

static void do_bit(SNES *s, uint16_t v, int imm)
{
    int w8 = R.fM;
    uint32_t mask = w8 ? 0xFF : 0xFFFF;
    R.fZ = !(R.a & v & mask);
    if (!imm) {
        R.fN = (v >> (w8 ? 7 : 15)) & 1;
        R.fV = (v >> (w8 ? 6 : 14)) & 1;
    }
}

static inline void branch(SNES *s, int cond)
{
    int8_t o = (int8_t)f8(s);
    if (cond) {
        idle(s);
        if (R.fE && (((R.pc + o) ^ R.pc) & 0xFF00)) idle(s);
        R.pc += o;
    }
}

static void interrupt(SNES *s, uint16_t vec_n, uint16_t vec_e, int brk)
{
    if (!R.fE) push8(s, R.pb);
    push16(s, R.pc);
    push8(s, get_p(s, brk));
    R.fI = 1; R.fD = 0;
    R.pb = 0;
    R.pc = rd16(s, R.fE ? vec_e : vec_n);
}

static inline void wr_a(SNES *s, uint16_t v)
{
    if (R.fM) R.a = (R.a & 0xFF00) | (v & 0xFF); else R.a = v;
    nz(s, v, R.fM);
}

/* ── Reset / step ────────────────────────────────────────────────────── */
void cpu65816_reset(SNES *s)
{
    R.a = R.x = R.y = 0;
    R.s = 0x01FF; R.d = 0; R.db = R.pb = 0;
    R.fE = 1; R.fM = R.fX = 1; R.fI = 1;
    R.fC = R.fZ = R.fD = R.fV = R.fN = 0;
    R.nmi = R.wai = R.stp = 0;
    R.pc = rd16(s, 0xFFFC);
}

void cpu65816_step(SNES *s)
{
    uint8_t op;
    uint32_t a;
    uint16_t v;

    if (R.stp) { s->hc += 6; return; }
    if (R.nmi) {
        R.nmi = 0; R.wai = 0;
        idle(s); idle(s);
        interrupt(s, 0xFFEA, 0xFFFA, 0);
        return;
    }
    if (s->irq_line) {
        if (!R.fI) {
            R.wai = 0;
            idle(s); idle(s);
            interrupt(s, 0xFFEE, 0xFFFE, 0);
            return;
        }
        R.wai = 0;
    }
    if (R.wai) { s->hc += 6; return; }

    op = f8(s);

    if (op != 0x89 && (((op & 3) == 1) || ((op & 3) == 3 && (op & 0xF) != 0xB) || (op & 0x1F) == 0x12)) {
        alu(s, op);
        return;
    }

    switch (op) {
    /* ── Interrupts / misc ── */
    case 0x00: f8(s); idle(s); interrupt(s, 0xFFE6, 0xFFFE, 1); break;       /* BRK */
    case 0x02: f8(s); idle(s); interrupt(s, 0xFFE4, 0xFFF4, 0); break;       /* COP */
    case 0x40:                                                               /* RTI */
        idle(s); idle(s);
        set_p(s, pull8(s));
        R.pc = pull16(s);
        if (!R.fE) R.pb = pull8(s);
        break;
    case 0x42: f8(s); break;                                                 /* WDM */
    case 0xEA: idle(s); break;                                               /* NOP */
    case 0xCB: R.wai = 1; idle(s); idle(s); break;                           /* WAI */
    case 0xDB: R.stp = 1; idle(s); idle(s); break;                           /* STP */

    /* ── Flags ── */
    case 0x18: R.fC = 0; idle(s); break;
    case 0x38: R.fC = 1; idle(s); break;
    case 0x58: R.fI = 0; idle(s); break;
    case 0x78: R.fI = 1; idle(s); break;
    case 0xB8: R.fV = 0; idle(s); break;
    case 0xD8: R.fD = 0; idle(s); break;
    case 0xF8: R.fD = 1; idle(s); break;
    case 0xC2: v = f8(s); idle(s); set_p(s, get_p(s, 0) & ~v); break;        /* REP */
    case 0xE2: v = f8(s); idle(s); set_p(s, get_p(s, 0) | v); break;         /* SEP */
    case 0xFB: {                                                             /* XCE */
        int c = R.fC;
        R.fC = R.fE; R.fE = c;
        if (R.fE) { R.fM = R.fX = 1; R.x &= 0xFF; R.y &= 0xFF; R.s = 0x100 | (R.s & 0xFF); }
        idle(s);
        break;
    }

    /* ── Branches ── */
    case 0x10: branch(s, !R.fN); break;
    case 0x30: branch(s, R.fN); break;
    case 0x50: branch(s, !R.fV); break;
    case 0x70: branch(s, R.fV); break;
    case 0x80: branch(s, 1); break;
    case 0x90: branch(s, !R.fC); break;
    case 0xB0: branch(s, R.fC); break;
    case 0xD0: branch(s, !R.fZ); break;
    case 0xF0: branch(s, R.fZ); break;
    case 0x82: v = f16(s); idle(s); R.pc += v; break;                        /* BRL */

    /* ── Jumps / calls ── */
    case 0x4C: R.pc = f16(s); break;
    case 0x5C: a = f24(s); R.pc = a; R.pb = a >> 16; break;
    case 0x6C: v = f16(s); R.pc = rd16(s, v); break;
    case 0x7C: v = f16(s); idle(s); R.pc = rd16(s, ((uint32_t)R.pb << 16) | (uint16_t)(v + R.x)); break;
    case 0xDC: v = f16(s); a = a_ptr24(s, v); R.pc = a; R.pb = a >> 16; break;
    case 0x20: v = f16(s); idle(s); push16(s, R.pc - 1); R.pc = v; break;
    case 0x22: a = f24(s); push8(s, R.pb); idle(s); push16(s, R.pc - 1); R.pc = a; R.pb = a >> 16; break;
    case 0xFC: v = f16(s); push16(s, R.pc - 1); idle(s); R.pc = rd16(s, ((uint32_t)R.pb << 16) | (uint16_t)(v + R.x)); break;
    case 0x60: idle(s); idle(s); R.pc = pull16(s) + 1; idle(s); break;
    case 0x6B: idle(s); idle(s); R.pc = pull16(s) + 1; R.pb = pull8(s); break;

    /* ── Stack ── */
    case 0x48: idle(s); if (R.fM) push8(s, R.a); else push16(s, R.a); break;
    case 0xDA: idle(s); if (R.fX) push8(s, R.x); else push16(s, R.x); break;
    case 0x5A: idle(s); if (R.fX) push8(s, R.y); else push16(s, R.y); break;
    case 0x68: idle(s); idle(s); idle(s);
        if (R.fM) wr_a(s, pull8(s)); else wr_a(s, pull16(s));
        break;
    case 0xFA: idle(s); idle(s); idle(s);
        R.x = R.fX ? pull8(s) : pull16(s); nz(s, R.x, R.fX); break;
    case 0x7A: idle(s); idle(s); idle(s);
        R.y = R.fX ? pull8(s) : pull16(s); nz(s, R.y, R.fX); break;
    case 0x08: idle(s); push8(s, get_p(s, 1)); break;
    case 0x28: idle(s); idle(s); idle(s); set_p(s, pull8(s)); break;
    case 0x0B: idle(s); push16(s, R.d); break;
    case 0x2B: idle(s); idle(s); idle(s); R.d = pull16(s); nz(s, R.d, 0); break;
    case 0x4B: idle(s); push8(s, R.pb); break;
    case 0x8B: idle(s); push8(s, R.db); break;
    case 0xAB: idle(s); idle(s); idle(s); R.db = pull8(s); nz(s, R.db, 1); break;
    case 0xF4: v = f16(s); push16(s, v); break;                              /* PEA */
    case 0xD4: a = a_dp(s); push16(s, rd16(s, a)); break;                    /* PEI */
    case 0x62: v = f16(s); idle(s); push16(s, R.pc + v); break;              /* PER */

    /* ── Transfers ── */
    case 0xAA: idle(s); R.x = R.fX ? (R.a & 0xFF) : R.a; nz(s, R.x, R.fX); break;
    case 0xA8: idle(s); R.y = R.fX ? (R.a & 0xFF) : R.a; nz(s, R.y, R.fX); break;
    case 0x8A: idle(s); wr_a(s, R.x); break;
    case 0x98: idle(s); wr_a(s, R.y); break;
    case 0x9B: idle(s); R.y = R.x; nz(s, R.y, R.fX); break;
    case 0xBB: idle(s); R.x = R.y; nz(s, R.x, R.fX); break;
    case 0xBA: idle(s); R.x = R.fX ? (R.s & 0xFF) : R.s; nz(s, R.x, R.fX); break;
    case 0x9A: idle(s); R.s = R.fE ? (0x100 | (R.x & 0xFF)) : R.x; break;
    case 0x1B: idle(s); R.s = R.fE ? (0x100 | (R.a & 0xFF)) : R.a; break;
    case 0x3B: idle(s); R.a = R.s; nz(s, R.a, 0); break;
    case 0x5B: idle(s); R.d = R.a; nz(s, R.d, 0); break;
    case 0x7B: idle(s); R.a = R.d; nz(s, R.a, 0); break;
    case 0xEB: idle(s); idle(s); R.a = (R.a >> 8) | (R.a << 8); nz(s, R.a, 1); break;

    /* ── Inc / dec registers ── */
    case 0xE8: idle(s); R.x = (R.x + 1) & (R.fX ? 0xFF : 0xFFFF); nz(s, R.x, R.fX); break;
    case 0xC8: idle(s); R.y = (R.y + 1) & (R.fX ? 0xFF : 0xFFFF); nz(s, R.y, R.fX); break;
    case 0xCA: idle(s); R.x = (R.x - 1) & (R.fX ? 0xFF : 0xFFFF); nz(s, R.x, R.fX); break;
    case 0x88: idle(s); R.y = (R.y - 1) & (R.fX ? 0xFF : 0xFFFF); nz(s, R.y, R.fX); break;
    case 0x1A: rmw_acc(s, 4); break;
    case 0x3A: rmw_acc(s, 5); break;

    /* ── Shifts on A ── */
    case 0x0A: rmw_acc(s, 0); break;
    case 0x4A: rmw_acc(s, 1); break;
    case 0x2A: rmw_acc(s, 2); break;
    case 0x6A: rmw_acc(s, 3); break;

    /* ── RMW memory ── */
    case 0x06: rmw_mem(s, a_dp(s), 0); break;
    case 0x16: rmw_mem(s, a_dpi(s, R.x), 0); break;
    case 0x0E: rmw_mem(s, a_abs(s), 0); break;
    case 0x1E: rmw_mem(s, a_absi(s, R.x), 0); break;
    case 0x46: rmw_mem(s, a_dp(s), 1); break;
    case 0x56: rmw_mem(s, a_dpi(s, R.x), 1); break;
    case 0x4E: rmw_mem(s, a_abs(s), 1); break;
    case 0x5E: rmw_mem(s, a_absi(s, R.x), 1); break;
    case 0x26: rmw_mem(s, a_dp(s), 2); break;
    case 0x36: rmw_mem(s, a_dpi(s, R.x), 2); break;
    case 0x2E: rmw_mem(s, a_abs(s), 2); break;
    case 0x3E: rmw_mem(s, a_absi(s, R.x), 2); break;
    case 0x66: rmw_mem(s, a_dp(s), 3); break;
    case 0x76: rmw_mem(s, a_dpi(s, R.x), 3); break;
    case 0x6E: rmw_mem(s, a_abs(s), 3); break;
    case 0x7E: rmw_mem(s, a_absi(s, R.x), 3); break;
    case 0xE6: rmw_mem(s, a_dp(s), 4); break;
    case 0xF6: rmw_mem(s, a_dpi(s, R.x), 4); break;
    case 0xEE: rmw_mem(s, a_abs(s), 4); break;
    case 0xFE: rmw_mem(s, a_absi(s, R.x), 4); break;
    case 0xC6: rmw_mem(s, a_dp(s), 5); break;
    case 0xD6: rmw_mem(s, a_dpi(s, R.x), 5); break;
    case 0xCE: rmw_mem(s, a_abs(s), 5); break;
    case 0xDE: rmw_mem(s, a_absi(s, R.x), 5); break;
    case 0x04: do_tsb(s, a_dp(s), 0); break;
    case 0x0C: do_tsb(s, a_abs(s), 0); break;
    case 0x14: do_tsb(s, a_dp(s), 1); break;
    case 0x1C: do_tsb(s, a_abs(s), 1); break;

    /* ── BIT ── */
    case 0x89: do_bit(s, R.fM ? f8(s) : f16(s), 1); break;
    case 0x24: do_bit(s, rdm(s, a_dp(s)), 0); break;
    case 0x34: do_bit(s, rdm(s, a_dpi(s, R.x)), 0); break;
    case 0x2C: do_bit(s, rdm(s, a_abs(s)), 0); break;
    case 0x3C: do_bit(s, rdm(s, a_absi(s, R.x)), 0); break;

    /* ── STX / STY / STZ ── */
    case 0x84: wrx(s, a_dp(s), R.y); break;
    case 0x94: wrx(s, a_dpi(s, R.x), R.y); break;
    case 0x8C: wrx(s, a_abs(s), R.y); break;
    case 0x86: wrx(s, a_dp(s), R.x); break;
    case 0x96: wrx(s, a_dpi(s, R.y), R.x); break;
    case 0x8E: wrx(s, a_abs(s), R.x); break;
    case 0x64: wrm(s, a_dp(s), 0); break;
    case 0x74: wrm(s, a_dpi(s, R.x), 0); break;
    case 0x9C: wrm(s, a_abs(s), 0); break;
    case 0x9E: wrm(s, a_absi(s, R.x), 0); break;

    /* ── LDX / LDY ── */
    case 0xA0: R.y = R.fX ? f8(s) : f16(s); nz(s, R.y, R.fX); break;
    case 0xA4: R.y = rdx(s, a_dp(s)); nz(s, R.y, R.fX); break;
    case 0xB4: R.y = rdx(s, a_dpi(s, R.x)); nz(s, R.y, R.fX); break;
    case 0xAC: R.y = rdx(s, a_abs(s)); nz(s, R.y, R.fX); break;
    case 0xBC: R.y = rdx(s, a_absi(s, R.x)); nz(s, R.y, R.fX); break;
    case 0xA2: R.x = R.fX ? f8(s) : f16(s); nz(s, R.x, R.fX); break;
    case 0xA6: R.x = rdx(s, a_dp(s)); nz(s, R.x, R.fX); break;
    case 0xB6: R.x = rdx(s, a_dpi(s, R.y)); nz(s, R.x, R.fX); break;
    case 0xAE: R.x = rdx(s, a_abs(s)); nz(s, R.x, R.fX); break;
    case 0xBE: R.x = rdx(s, a_absi(s, R.y)); nz(s, R.x, R.fX); break;

    /* ── CPX / CPY ── */
    case 0xC0: do_cmp(s, R.y, R.fX ? f8(s) : f16(s), R.fX); break;
    case 0xC4: do_cmp(s, R.y, rdx(s, a_dp(s)), R.fX); break;
    case 0xCC: do_cmp(s, R.y, rdx(s, a_abs(s)), R.fX); break;
    case 0xE0: do_cmp(s, R.x, R.fX ? f8(s) : f16(s), R.fX); break;
    case 0xE4: do_cmp(s, R.x, rdx(s, a_dp(s)), R.fX); break;
    case 0xEC: do_cmp(s, R.x, rdx(s, a_abs(s)), R.fX); break;

    /* ── Block moves ── */
    case 0x44: case 0x54: {
        uint8_t dst = f8(s), src = f8(s);
        uint8_t b = rd(s, ((uint32_t)src << 16) | R.x);
        wr(s, ((uint32_t)dst << 16) | R.y, b);
        R.db = dst;
        uint16_t m = R.fX ? 0xFF : 0xFFFF;
        if (op == 0x54) { R.x = (R.x + 1) & m; R.y = (R.y + 1) & m; }
        else            { R.x = (R.x - 1) & m; R.y = (R.y - 1) & m; }
        R.a--;
        idle(s); idle(s);
        if (R.a != 0xFFFF) R.pc -= 3;
        break;
    }

    default: break;                                                          /* unreachable */
    }
}
