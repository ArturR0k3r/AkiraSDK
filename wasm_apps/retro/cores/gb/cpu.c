/**
 * @file cpu.c
 * @brief Sharp SM83 (LR35902) CPU — Game Boy / GBC processor
 *
 * The SM83 is a modified Z80-like 8-bit CPU.  Notable differences from Z80:
 *   - No alternate register bank, no IX/IY/I/R
 *   - Simplified flag set: only Z, N, H, C (bits 7-4 of F; bits 3-0 always 0)
 *   - EI takes effect one instruction later
 *   - HALT bug: when HALT is executed with IME=0 and a pending interrupt,
 *     the PC fails to advance past the next opcode fetch
 *   - CB prefix uses 4 extra cycles for (HL) operand
 *
 * Cycle counts are returned as T-cycles (1 machine cycle = 4 T-cycles on DMG).
 *
 * @license Apache-2.0
 */
#include "gb.h"

/* ── Register pair helpers ──────────────────────────────────────────── */
#define rBC   ((uint16_t)((gb->cpu.b << 8) | gb->cpu.c))
#define rDE   ((uint16_t)((gb->cpu.d << 8) | gb->cpu.e))
#define rHL   ((uint16_t)((gb->cpu.h << 8) | gb->cpu.l))
#define rAF   ((uint16_t)((gb->cpu.a << 8) | gb->cpu.f))
#define rSP   (gb->cpu.sp)
#define rPC   (gb->cpu.pc)

#define setBC(v) do { gb->cpu.b = (uint8_t)((v)>>8); gb->cpu.c = (uint8_t)(v); } while(0)
#define setDE(v) do { gb->cpu.d = (uint8_t)((v)>>8); gb->cpu.e = (uint8_t)(v); } while(0)
#define setHL(v) do { gb->cpu.h = (uint8_t)((v)>>8); gb->cpu.l = (uint8_t)(v); } while(0)
#define setAF(v) do { gb->cpu.a = (uint8_t)((v)>>8); gb->cpu.f = (uint8_t)((v) & 0xF0); } while(0)

/* ── Flag helpers ───────────────────────────────────────────────────── */
#define F   gb->cpu.f
#define ZF  (F & FLAG_Z)
#define NF  (F & FLAG_N)
#define HF  (F & FLAG_H)
#define CF  (F & FLAG_C)
#define SET_FLAGS(z,n,h,c) \
    F = (uint8_t)(((z)?FLAG_Z:0)|((n)?FLAG_N:0)|((h)?FLAG_H:0)|((c)?FLAG_C:0))

/* ── Memory bus short aliases ───────────────────────────────────────── */
#define RD(a)     gb_read(gb, (uint16_t)(a))
#define WR(a,v)   gb_write(gb, (uint16_t)(a), (uint8_t)(v))
#define RDPC()    (gb->cpu.halt_bug ? (gb->cpu.halt_bug=0, RD(rPC)) : RD(rPC++))
#define READ16(a) ((uint16_t)(RD(a) | (RD((uint16_t)((a)+1)) << 8)))

/* ── Stack helpers ──────────────────────────────────────────────────── */
#define PUSH16(v) do { \
    WR(--rSP, (uint8_t)((v)>>8)); \
    WR(--rSP, (uint8_t)(v)); \
} while(0)
#define POP16(dst) do { \
    uint8_t _lo = RD(rSP++); \
    uint8_t _hi = RD(rSP++); \
    (dst) = (uint16_t)((_hi<<8)|_lo); \
} while(0)

/* ── 8-bit ALU helpers ──────────────────────────────────────────────── */
static inline void alu_add(GB *gb, uint8_t val, uint8_t carry)
{
    uint16_t res = gb->cpu.a + val + carry;
    SET_FLAGS((res & 0xFF) == 0, 0,
              ((gb->cpu.a & 0xF) + (val & 0xF) + carry) > 0xF,
              res > 0xFF);
    gb->cpu.a = (uint8_t)res;
}

static inline void alu_sub(GB *gb, uint8_t val, uint8_t carry)
{
    int16_t res = gb->cpu.a - val - carry;
    SET_FLAGS((res & 0xFF) == 0, 1,
              ((int)(gb->cpu.a & 0xF) - (val & 0xF) - carry) < 0,
              res < 0);
    gb->cpu.a = (uint8_t)res;
}

static inline void alu_and(GB *gb, uint8_t val)
{
    gb->cpu.a &= val;
    SET_FLAGS(gb->cpu.a == 0, 0, 1, 0);
}

static inline void alu_or(GB *gb, uint8_t val)
{
    gb->cpu.a |= val;
    SET_FLAGS(gb->cpu.a == 0, 0, 0, 0);
}

static inline void alu_xor(GB *gb, uint8_t val)
{
    gb->cpu.a ^= val;
    SET_FLAGS(gb->cpu.a == 0, 0, 0, 0);
}

static inline void alu_cp(GB *gb, uint8_t val)
{
    int16_t res = gb->cpu.a - val;
    SET_FLAGS((res & 0xFF) == 0, 1,
              ((int)(gb->cpu.a & 0xF) - (val & 0xF)) < 0,
              res < 0);
}

static inline uint8_t alu_inc(GB *gb, uint8_t val)
{
    uint8_t r = val + 1;
    F = (uint8_t)((r ? 0 : FLAG_Z) | 0 | ((val & 0xF) == 0xF ? FLAG_H : 0) | (F & FLAG_C));
    return r;
}

static inline uint8_t alu_dec(GB *gb, uint8_t val)
{
    uint8_t r = val - 1;
    F = (uint8_t)((r ? 0 : FLAG_Z) | FLAG_N | ((val & 0xF) == 0 ? FLAG_H : 0) | (F & FLAG_C));
    return r;
}

/* ── 16-bit ADD HL,rr ───────────────────────────────────────────────── */
static inline void add_hl(GB *gb, uint16_t val)
{
    uint32_t res = rHL + val;
    F = (uint8_t)((F & FLAG_Z) |
                   ((((rHL & 0xFFF) + (val & 0xFFF)) > 0xFFF) ? FLAG_H : 0) |
                   (res > 0xFFFF ? FLAG_C : 0));
    setHL((uint16_t)res);
}

/* ── CB prefix — rotate/shift/bit ops on a byte value ──────────────── */
static inline uint8_t cb_rlc(GB *gb, uint8_t v)
{
    uint8_t c = v >> 7;
    v = (uint8_t)((v << 1) | c);
    SET_FLAGS(v == 0, 0, 0, c);
    return v;
}
static inline uint8_t cb_rrc(GB *gb, uint8_t v)
{
    uint8_t c = v & 1;
    v = (uint8_t)((v >> 1) | (c << 7));
    SET_FLAGS(v == 0, 0, 0, c);
    return v;
}
static inline uint8_t cb_rl(GB *gb, uint8_t v)
{
    uint8_t old_c = CF ? 1 : 0;
    uint8_t new_c = v >> 7;
    v = (uint8_t)((v << 1) | old_c);
    SET_FLAGS(v == 0, 0, 0, new_c);
    return v;
}
static inline uint8_t cb_rr(GB *gb, uint8_t v)
{
    uint8_t old_c = CF ? 1 : 0;
    uint8_t new_c = v & 1;
    v = (uint8_t)((v >> 1) | (old_c << 7));
    SET_FLAGS(v == 0, 0, 0, new_c);
    return v;
}
static inline uint8_t cb_sla(GB *gb, uint8_t v)
{
    uint8_t c = v >> 7;
    v = (uint8_t)(v << 1);
    SET_FLAGS(v == 0, 0, 0, c);
    return v;
}
static inline uint8_t cb_sra(GB *gb, uint8_t v)
{
    uint8_t c = v & 1;
    v = (uint8_t)((v >> 1) | (v & 0x80));
    SET_FLAGS(v == 0, 0, 0, c);
    return v;
}
static inline uint8_t cb_swap(GB *gb, uint8_t v)
{
    v = (uint8_t)((v >> 4) | (v << 4));
    SET_FLAGS(v == 0, 0, 0, 0);
    return v;
}
static inline uint8_t cb_srl(GB *gb, uint8_t v)
{
    uint8_t c = v & 1;
    v >>= 1;
    SET_FLAGS(v == 0, 0, 0, c);
    return v;
}

/* ── No-carry wrappers for ALU_OP macro ─────────────────────────────── */
static inline void alu_add0(GB *gb, uint8_t v) { alu_add(gb, v, 0); }
static inline void alu_sub0(GB *gb, uint8_t v) { alu_sub(gb, v, 0); }

/* ── CB prefix execution ────────────────────────────────────────────── */
static int exec_cb(GB *gb)
{
    uint8_t op  = RDPC();
    uint8_t reg = op & 0x07;
    uint8_t bit = (op >> 3) & 0x07;
    uint8_t grp = op >> 6;

    /* Read operand */
    uint8_t val;
    int hl_access = (reg == 6);
    if (hl_access) val = RD(rHL);
    else {
        uint8_t *regs8[8] = {
            &gb->cpu.b, &gb->cpu.c, &gb->cpu.d, &gb->cpu.e,
            &gb->cpu.h, &gb->cpu.l, (uint8_t*)0, &gb->cpu.a
        };
        val = *regs8[reg];
    }

    /* Execute */
    if (grp == 0) {
        /* Rotate/shift */
        switch (bit) {
            case 0: val = cb_rlc(gb, val);  break;
            case 1: val = cb_rrc(gb, val);  break;
            case 2: val = cb_rl(gb, val);   break;
            case 3: val = cb_rr(gb, val);   break;
            case 4: val = cb_sla(gb, val);  break;
            case 5: val = cb_sra(gb, val);  break;
            case 6: val = cb_swap(gb, val); break;
            case 7: val = cb_srl(gb, val);  break;
        }
        /* Write back */
        if (hl_access) WR(rHL, val);
        else {
            uint8_t *regs8[8] = {
                &gb->cpu.b, &gb->cpu.c, &gb->cpu.d, &gb->cpu.e,
                &gb->cpu.h, &gb->cpu.l, (uint8_t*)0, &gb->cpu.a
            };
            *regs8[reg] = val;
        }
    } else if (grp == 1) {
        /* BIT b,r */
        F = (uint8_t)(((val & (1 << bit)) ? 0 : FLAG_Z) | FLAG_H | (F & FLAG_C));
    } else if (grp == 2) {
        /* RES b,r */
        val &= (uint8_t)~(1 << bit);
        if (hl_access) WR(rHL, val);
        else {
            uint8_t *regs8[8] = {
                &gb->cpu.b, &gb->cpu.c, &gb->cpu.d, &gb->cpu.e,
                &gb->cpu.h, &gb->cpu.l, (uint8_t*)0, &gb->cpu.a
            };
            *regs8[reg] = val;
        }
    } else {
        /* SET b,r */
        val |= (uint8_t)(1 << bit);
        if (hl_access) WR(rHL, val);
        else {
            uint8_t *regs8[8] = {
                &gb->cpu.b, &gb->cpu.c, &gb->cpu.d, &gb->cpu.e,
                &gb->cpu.h, &gb->cpu.l, (uint8_t*)0, &gb->cpu.a
            };
            *regs8[reg] = val;
        }
    }

    /* Cycle cost: (HL) ops cost 4 extra T-cycles;
     * BIT (HL) = 12, others with (HL) = 16, without (HL) = 8 */
    if (hl_access) {
        return (grp == 1) ? 12 : 16;
    }
    return 8;
}

/* ── Interrupt handling ─────────────────────────────────────────────── */
static int handle_interrupts(GB *gb)
{
    uint8_t pending = gb->io[IO_IF] & gb->ie & 0x1F;
    if (!pending) return 0;

    if (gb->cpu.halted) {
        gb->cpu.halted = 0;
    }
    if (!gb->cpu.ime) return 0;

    gb->cpu.ime = 0;

    /* Find lowest-priority pending interrupt */
    uint8_t bit = 0;
    while (!(pending & (1 << bit))) bit++;

    gb->io[IO_IF] &= (uint8_t)~(1 << bit);

    static const uint16_t vectors[5] = {
        0x0040, 0x0048, 0x0050, 0x0058, 0x0060
    };
    PUSH16(rPC);
    rPC = vectors[bit];

    return 20;  /* ISR entry: 5 machine cycles = 20 T-cycles */
}

/* ── Main CPU step ──────────────────────────────────────────────────── */
int sm83_step(GB *gb)
{
    /* Handle pending EI */
    if (gb->cpu.ime == 2) gb->cpu.ime = 1;

    /* Service interrupts */
    int irq_cycles = handle_interrupts(gb);
    if (irq_cycles) return irq_cycles;

    /* HALT */
    if (gb->cpu.halted) {
        /* Check if any interrupt is pending to wake CPU */
        if (gb->io[IO_IF] & gb->ie & 0x1F) {
            gb->cpu.halted = 0;
        }
        return 4;
    }

    uint8_t op = RDPC();
    int cycles = 4;

    switch (op) {
        /* ── 0x00: NOP ─────────────────────────────────────────────── */
        case 0x00: break;

        /* ── 0x01/0x11/0x21/0x31: LD rr, nn ─────────────────────── */
        case 0x01: { uint8_t lo=RDPC(), hi=RDPC(); setBC((hi<<8)|lo); cycles=12; break; }
        case 0x11: { uint8_t lo=RDPC(), hi=RDPC(); setDE((hi<<8)|lo); cycles=12; break; }
        case 0x21: { uint8_t lo=RDPC(), hi=RDPC(); setHL((hi<<8)|lo); cycles=12; break; }
        case 0x31: { uint8_t lo=RDPC(), hi=RDPC(); rSP=(uint16_t)((hi<<8)|lo); cycles=12; break; }

        /* ── 0x02/0x12/0x22/0x32: LD (rr), A ─────────────────────── */
        case 0x02: WR(rBC, gb->cpu.a); cycles=8; break;
        case 0x12: WR(rDE, gb->cpu.a); cycles=8; break;
        case 0x22: WR(rHL, gb->cpu.a); setHL(rHL+1); cycles=8; break;
        case 0x32: WR(rHL, gb->cpu.a); setHL(rHL-1); cycles=8; break;

        /* ── 0x03/0x13/0x23/0x33: INC rr ─────────────────────────── */
        case 0x03: setBC(rBC+1); cycles=8; break;
        case 0x13: setDE(rDE+1); cycles=8; break;
        case 0x23: setHL(rHL+1); cycles=8; break;
        case 0x33: rSP++; cycles=8; break;

        /* ── 0x04/0x0C/… INC r ──────────────────────────────────── */
        case 0x04: gb->cpu.b = alu_inc(gb, gb->cpu.b); break;
        case 0x0C: gb->cpu.c = alu_inc(gb, gb->cpu.c); break;
        case 0x14: gb->cpu.d = alu_inc(gb, gb->cpu.d); break;
        case 0x1C: gb->cpu.e = alu_inc(gb, gb->cpu.e); break;
        case 0x24: gb->cpu.h = alu_inc(gb, gb->cpu.h); break;
        case 0x2C: gb->cpu.l = alu_inc(gb, gb->cpu.l); break;
        case 0x34: { uint8_t v=alu_inc(gb, RD(rHL)); WR(rHL,v); cycles=12; break; }
        case 0x3C: gb->cpu.a = alu_inc(gb, gb->cpu.a); break;

        /* ── 0x05/0x0D/… DEC r ──────────────────────────────────── */
        case 0x05: gb->cpu.b = alu_dec(gb, gb->cpu.b); break;
        case 0x0D: gb->cpu.c = alu_dec(gb, gb->cpu.c); break;
        case 0x15: gb->cpu.d = alu_dec(gb, gb->cpu.d); break;
        case 0x1D: gb->cpu.e = alu_dec(gb, gb->cpu.e); break;
        case 0x25: gb->cpu.h = alu_dec(gb, gb->cpu.h); break;
        case 0x2D: gb->cpu.l = alu_dec(gb, gb->cpu.l); break;
        case 0x35: { uint8_t v=alu_dec(gb, RD(rHL)); WR(rHL,v); cycles=12; break; }
        case 0x3D: gb->cpu.a = alu_dec(gb, gb->cpu.a); break;

        /* ── 0x06/0x0E/… LD r, n ────────────────────────────────── */
        case 0x06: gb->cpu.b = RDPC(); cycles=8; break;
        case 0x0E: gb->cpu.c = RDPC(); cycles=8; break;
        case 0x16: gb->cpu.d = RDPC(); cycles=8; break;
        case 0x1E: gb->cpu.e = RDPC(); cycles=8; break;
        case 0x26: gb->cpu.h = RDPC(); cycles=8; break;
        case 0x2E: gb->cpu.l = RDPC(); cycles=8; break;
        case 0x36: { uint8_t n=RDPC(); WR(rHL, n); cycles=12; break; }
        case 0x3E: gb->cpu.a = RDPC(); cycles=8; break;

        /* ── 0x07: RLCA ─────────────────────────────────────────── */
        case 0x07: {
            uint8_t c = gb->cpu.a >> 7;
            gb->cpu.a = (uint8_t)((gb->cpu.a << 1) | c);
            SET_FLAGS(0, 0, 0, c);
            break;
        }
        /* ── 0x0F: RRCA ─────────────────────────────────────────── */
        case 0x0F: {
            uint8_t c = gb->cpu.a & 1;
            gb->cpu.a = (uint8_t)((gb->cpu.a >> 1) | (c << 7));
            SET_FLAGS(0, 0, 0, c);
            break;
        }
        /* ── 0x17: RLA ──────────────────────────────────────────── */
        case 0x17: {
            uint8_t c = gb->cpu.a >> 7;
            gb->cpu.a = (uint8_t)((gb->cpu.a << 1) | (CF ? 1 : 0));
            SET_FLAGS(0, 0, 0, c);
            break;
        }
        /* ── 0x1F: RRA ──────────────────────────────────────────── */
        case 0x1F: {
            uint8_t c = gb->cpu.a & 1;
            gb->cpu.a = (uint8_t)((gb->cpu.a >> 1) | (CF ? 0x80 : 0));
            SET_FLAGS(0, 0, 0, c);
            break;
        }

        /* ── 0x08: LD (nn), SP ──────────────────────────────────── */
        case 0x08: {
            uint8_t lo=RDPC(), hi=RDPC();
            uint16_t addr = (uint16_t)((hi<<8)|lo);
            WR(addr, (uint8_t)rSP);
            WR(addr+1, (uint8_t)(rSP>>8));
            cycles=20; break;
        }

        /* ── 0x09/0x19/0x29/0x39: ADD HL, rr ───────────────────── */
        case 0x09: add_hl(gb, rBC); cycles=8; break;
        case 0x19: add_hl(gb, rDE); cycles=8; break;
        case 0x29: add_hl(gb, rHL); cycles=8; break;
        case 0x39: add_hl(gb, rSP); cycles=8; break;

        /* ── 0x0A/0x1A/0x2A/0x3A: LD A, (rr) ──────────────────── */
        case 0x0A: gb->cpu.a = RD(rBC); cycles=8; break;
        case 0x1A: gb->cpu.a = RD(rDE); cycles=8; break;
        case 0x2A: gb->cpu.a = RD(rHL); setHL(rHL+1); cycles=8; break;
        case 0x3A: gb->cpu.a = RD(rHL); setHL(rHL-1); cycles=8; break;

        /* ── 0x0B/0x1B/0x2B/0x3B: DEC rr ───────────────────────── */
        case 0x0B: setBC(rBC-1); cycles=8; break;
        case 0x1B: setDE(rDE-1); cycles=8; break;
        case 0x2B: setHL(rHL-1); cycles=8; break;
        case 0x3B: rSP--; cycles=8; break;

        /* ── 0x10: STOP ─────────────────────────────────────────── */
        case 0x10: {
            /* Read and discard next byte (STOP is 2 bytes) */
            RDPC();
            /* CGB: speed switch if KEY1 bit 0 set */
            if (gb->cgb_mode && (gb->io[IO_KEY1] & 0x01)) {
                gb->double_speed ^= 1;
                gb->io[IO_KEY1] = (uint8_t)((gb->double_speed ? 0x80 : 0x00));
            }
            cycles=4; break;
        }

        /* ── 0x18: JR e ─────────────────────────────────────────── */
        case 0x18: { int8_t e=(int8_t)RDPC(); rPC=(uint16_t)(rPC+e); cycles=12; break; }

        /* ── 0x20/0x28/0x30/0x38: JR cc, e ─────────────────────── */
        case 0x20: { int8_t e=(int8_t)RDPC(); if (!ZF){rPC=(uint16_t)(rPC+e);cycles=12;}else cycles=8; break; }
        case 0x28: { int8_t e=(int8_t)RDPC(); if ( ZF){rPC=(uint16_t)(rPC+e);cycles=12;}else cycles=8; break; }
        case 0x30: { int8_t e=(int8_t)RDPC(); if (!CF){rPC=(uint16_t)(rPC+e);cycles=12;}else cycles=8; break; }
        case 0x38: { int8_t e=(int8_t)RDPC(); if ( CF){rPC=(uint16_t)(rPC+e);cycles=12;}else cycles=8; break; }

        /* ── 0x27: DAA ──────────────────────────────────────────── */
        case 0x27: {
            uint8_t a = gb->cpu.a;
            if (!NF) {
                if (HF || (a & 0x0F) > 9)  a += 0x06;
                if (CF || a > 0x99)         { a += 0x60; F |= FLAG_C; }
            } else {
                if (HF) a -= 0x06;
                if (CF) a -= 0x60;
            }
            gb->cpu.a = a;
            F = (uint8_t)((a ? 0 : FLAG_Z) | (F & FLAG_N) | (F & FLAG_C));
            break;
        }
        /* ── 0x2F: CPL ──────────────────────────────────────────── */
        case 0x2F: gb->cpu.a ^= 0xFF; F |= FLAG_N | FLAG_H; break;
        /* ── 0x37: SCF ──────────────────────────────────────────── */
        case 0x37: F = (uint8_t)((F & FLAG_Z) | FLAG_C); break;
        /* ── 0x3F: CCF ──────────────────────────────────────────── */
        case 0x3F: F = (uint8_t)((F & FLAG_Z) | (CF ? 0 : FLAG_C)); break;

        /* ── 0x40–0x7F: LD r, r' — Block 01 ───────────────────── */
        /* This 64-opcode block: dst = (op>>3)&7, src = op&7
         * dst/src 6 = (HL); 0x76 = HALT */
        case 0x76: {
            uint8_t pending = gb->io[IO_IF] & gb->ie & 0x1F;
            if (!gb->cpu.ime && pending) {
                gb->cpu.halt_bug = 1;  /* HALT bug */
            }
            gb->cpu.halted = 1;
            break;
        }
        /* All LD r,r' — 63 remaining (hand-unrolled for speed) */
        case 0x40: break; /* LD B,B */
        case 0x41: gb->cpu.b = gb->cpu.c; break;
        case 0x42: gb->cpu.b = gb->cpu.d; break;
        case 0x43: gb->cpu.b = gb->cpu.e; break;
        case 0x44: gb->cpu.b = gb->cpu.h; break;
        case 0x45: gb->cpu.b = gb->cpu.l; break;
        case 0x46: gb->cpu.b = RD(rHL); cycles=8; break;
        case 0x47: gb->cpu.b = gb->cpu.a; break;

        case 0x48: gb->cpu.c = gb->cpu.b; break;
        case 0x49: break; /* LD C,C */
        case 0x4A: gb->cpu.c = gb->cpu.d; break;
        case 0x4B: gb->cpu.c = gb->cpu.e; break;
        case 0x4C: gb->cpu.c = gb->cpu.h; break;
        case 0x4D: gb->cpu.c = gb->cpu.l; break;
        case 0x4E: gb->cpu.c = RD(rHL); cycles=8; break;
        case 0x4F: gb->cpu.c = gb->cpu.a; break;

        case 0x50: gb->cpu.d = gb->cpu.b; break;
        case 0x51: gb->cpu.d = gb->cpu.c; break;
        case 0x52: break; /* LD D,D */
        case 0x53: gb->cpu.d = gb->cpu.e; break;
        case 0x54: gb->cpu.d = gb->cpu.h; break;
        case 0x55: gb->cpu.d = gb->cpu.l; break;
        case 0x56: gb->cpu.d = RD(rHL); cycles=8; break;
        case 0x57: gb->cpu.d = gb->cpu.a; break;

        case 0x58: gb->cpu.e = gb->cpu.b; break;
        case 0x59: gb->cpu.e = gb->cpu.c; break;
        case 0x5A: gb->cpu.e = gb->cpu.d; break;
        case 0x5B: break; /* LD E,E */
        case 0x5C: gb->cpu.e = gb->cpu.h; break;
        case 0x5D: gb->cpu.e = gb->cpu.l; break;
        case 0x5E: gb->cpu.e = RD(rHL); cycles=8; break;
        case 0x5F: gb->cpu.e = gb->cpu.a; break;

        case 0x60: gb->cpu.h = gb->cpu.b; break;
        case 0x61: gb->cpu.h = gb->cpu.c; break;
        case 0x62: gb->cpu.h = gb->cpu.d; break;
        case 0x63: gb->cpu.h = gb->cpu.e; break;
        case 0x64: break; /* LD H,H */
        case 0x65: gb->cpu.h = gb->cpu.l; break;
        case 0x66: gb->cpu.h = RD(rHL); cycles=8; break;
        case 0x67: gb->cpu.h = gb->cpu.a; break;

        case 0x68: gb->cpu.l = gb->cpu.b; break;
        case 0x69: gb->cpu.l = gb->cpu.c; break;
        case 0x6A: gb->cpu.l = gb->cpu.d; break;
        case 0x6B: gb->cpu.l = gb->cpu.e; break;
        case 0x6C: gb->cpu.l = gb->cpu.h; break;
        case 0x6D: break; /* LD L,L */
        case 0x6E: gb->cpu.l = RD(rHL); cycles=8; break;
        case 0x6F: gb->cpu.l = gb->cpu.a; break;

        case 0x70: WR(rHL, gb->cpu.b); cycles=8; break;
        case 0x71: WR(rHL, gb->cpu.c); cycles=8; break;
        case 0x72: WR(rHL, gb->cpu.d); cycles=8; break;
        case 0x73: WR(rHL, gb->cpu.e); cycles=8; break;
        case 0x74: WR(rHL, gb->cpu.h); cycles=8; break;
        case 0x75: WR(rHL, gb->cpu.l); cycles=8; break;
        case 0x77: WR(rHL, gb->cpu.a); cycles=8; break;

        case 0x78: gb->cpu.a = gb->cpu.b; break;
        case 0x79: gb->cpu.a = gb->cpu.c; break;
        case 0x7A: gb->cpu.a = gb->cpu.d; break;
        case 0x7B: gb->cpu.a = gb->cpu.e; break;
        case 0x7C: gb->cpu.a = gb->cpu.h; break;
        case 0x7D: gb->cpu.a = gb->cpu.l; break;
        case 0x7E: gb->cpu.a = RD(rHL); cycles=8; break;
        case 0x7F: break; /* LD A,A */

        /* ── 0x80–0xBF: ALU A, r — Block 10 ───────────────────── */
#define ALU_OP(base, fn, hl_cy, imm_cy) \
        case (base)+0: fn(gb, gb->cpu.b); break; \
        case (base)+1: fn(gb, gb->cpu.c); break; \
        case (base)+2: fn(gb, gb->cpu.d); break; \
        case (base)+3: fn(gb, gb->cpu.e); break; \
        case (base)+4: fn(gb, gb->cpu.h); break; \
        case (base)+5: fn(gb, gb->cpu.l); break; \
        case (base)+6: fn(gb, RD(rHL)); cycles=(hl_cy); break; \
        case (base)+7: fn(gb, gb->cpu.a); break;

        ALU_OP(0x80, alu_add0, 8, 8)  /* ADD A,r */
        ALU_OP(0x90, alu_sub0, 8, 8)  /* SUB r */
        ALU_OP(0xA0, alu_and, 8, 8)  /* AND r */
        ALU_OP(0xA8, alu_xor, 8, 8)  /* XOR r */
        ALU_OP(0xB0, alu_or,  8, 8)  /* OR r */
        ALU_OP(0xB8, alu_cp,  8, 8)  /* CP r */

        /* ADC and SBC need carry flag from F */
        case 0x88: alu_add(gb, gb->cpu.b, CF?1:0); break;
        case 0x89: alu_add(gb, gb->cpu.c, CF?1:0); break;
        case 0x8A: alu_add(gb, gb->cpu.d, CF?1:0); break;
        case 0x8B: alu_add(gb, gb->cpu.e, CF?1:0); break;
        case 0x8C: alu_add(gb, gb->cpu.h, CF?1:0); break;
        case 0x8D: alu_add(gb, gb->cpu.l, CF?1:0); break;
        case 0x8E: alu_add(gb, RD(rHL), CF?1:0); cycles=8; break;
        case 0x8F: alu_add(gb, gb->cpu.a, CF?1:0); break;

        case 0x98: alu_sub(gb, gb->cpu.b, CF?1:0); break;
        case 0x99: alu_sub(gb, gb->cpu.c, CF?1:0); break;
        case 0x9A: alu_sub(gb, gb->cpu.d, CF?1:0); break;
        case 0x9B: alu_sub(gb, gb->cpu.e, CF?1:0); break;
        case 0x9C: alu_sub(gb, gb->cpu.h, CF?1:0); break;
        case 0x9D: alu_sub(gb, gb->cpu.l, CF?1:0); break;
        case 0x9E: alu_sub(gb, RD(rHL), CF?1:0); cycles=8; break;
        case 0x9F: alu_sub(gb, gb->cpu.a, CF?1:0); break;

        /* ── 0xC0–0xFF: Control flow — Block 11 ────────────────── */
        /* RET cc */
        case 0xC0: if (!ZF) { POP16(rPC); cycles=20; } else cycles=8; break;
        case 0xC8: if ( ZF) { POP16(rPC); cycles=20; } else cycles=8; break;
        case 0xD0: if (!CF) { POP16(rPC); cycles=20; } else cycles=8; break;
        case 0xD8: if ( CF) { POP16(rPC); cycles=20; } else cycles=8; break;

        /* POP rr */
        case 0xC1: { uint16_t v; POP16(v); setBC(v); cycles=12; break; }
        case 0xD1: { uint16_t v; POP16(v); setDE(v); cycles=12; break; }
        case 0xE1: { uint16_t v; POP16(v); setHL(v); cycles=12; break; }
        case 0xF1: { uint16_t v; POP16(v); setAF(v); cycles=12; break; }

        /* JP cc, nn */
        case 0xC2: { uint8_t l=RDPC(),h=RDPC(); if(!ZF){rPC=(uint16_t)((h<<8)|l);cycles=16;}else cycles=12; break; }
        case 0xCA: { uint8_t l=RDPC(),h=RDPC(); if( ZF){rPC=(uint16_t)((h<<8)|l);cycles=16;}else cycles=12; break; }
        case 0xD2: { uint8_t l=RDPC(),h=RDPC(); if(!CF){rPC=(uint16_t)((h<<8)|l);cycles=16;}else cycles=12; break; }
        case 0xDA: { uint8_t l=RDPC(),h=RDPC(); if( CF){rPC=(uint16_t)((h<<8)|l);cycles=16;}else cycles=12; break; }

        /* JP nn */
        case 0xC3: { uint8_t l=RDPC(),h=RDPC(); rPC=(uint16_t)((h<<8)|l); cycles=16; break; }

        /* CB prefix */
        case 0xCB: cycles = exec_cb(gb); break;

        /* CALL cc, nn */
        case 0xC4: { uint8_t l=RDPC(),h=RDPC(); if(!ZF){PUSH16(rPC);rPC=(uint16_t)((h<<8)|l);cycles=24;}else cycles=12; break; }
        case 0xCC: { uint8_t l=RDPC(),h=RDPC(); if( ZF){PUSH16(rPC);rPC=(uint16_t)((h<<8)|l);cycles=24;}else cycles=12; break; }
        case 0xD4: { uint8_t l=RDPC(),h=RDPC(); if(!CF){PUSH16(rPC);rPC=(uint16_t)((h<<8)|l);cycles=24;}else cycles=12; break; }
        case 0xDC: { uint8_t l=RDPC(),h=RDPC(); if( CF){PUSH16(rPC);rPC=(uint16_t)((h<<8)|l);cycles=24;}else cycles=12; break; }

        /* PUSH rr */
        case 0xC5: PUSH16(rBC); cycles=16; break;
        case 0xD5: PUSH16(rDE); cycles=16; break;
        case 0xE5: PUSH16(rHL); cycles=16; break;
        case 0xF5: PUSH16(rAF); cycles=16; break;

        /* ALU A, n (immediate) */
        case 0xC6: alu_add(gb, RDPC(), 0); cycles=8; break;
        case 0xCE: { uint8_t c=CF?1:0; alu_add(gb, RDPC(), c); cycles=8; break; }
        case 0xD6: alu_sub(gb, RDPC(), 0); cycles=8; break;
        case 0xDE: { uint8_t c=CF?1:0; alu_sub(gb, RDPC(), c); cycles=8; break; }
        case 0xE6: alu_and(gb, RDPC()); cycles=8; break;
        case 0xEE: alu_xor(gb, RDPC()); cycles=8; break;
        case 0xF6: alu_or(gb, RDPC()); cycles=8; break;
        case 0xFE: alu_cp(gb, RDPC()); cycles=8; break;

        /* RST */
        case 0xC7: PUSH16(rPC); rPC=0x0000; cycles=16; break;
        case 0xCF: PUSH16(rPC); rPC=0x0008; cycles=16; break;
        case 0xD7: PUSH16(rPC); rPC=0x0010; cycles=16; break;
        case 0xDF: PUSH16(rPC); rPC=0x0018; cycles=16; break;
        case 0xE7: PUSH16(rPC); rPC=0x0020; cycles=16; break;
        case 0xEF: PUSH16(rPC); rPC=0x0028; cycles=16; break;
        case 0xF7: PUSH16(rPC); rPC=0x0030; cycles=16; break;
        case 0xFF: PUSH16(rPC); rPC=0x0038; cycles=16; break;

        /* RET */
        case 0xC9: POP16(rPC); cycles=16; break;

        /* RETI */
        case 0xD9: POP16(rPC); gb->cpu.ime=1; cycles=16; break;

        /* JP (HL) */
        case 0xE9: rPC = rHL; cycles=4; break;

        /* CALL nn */
        case 0xCD: { uint8_t l=RDPC(),h=RDPC(); PUSH16(rPC); rPC=(uint16_t)((h<<8)|l); cycles=24; break; }

        /* LD (0xFF00+n), A */
        case 0xE0: { uint8_t n=RDPC(); WR(0xFF00u+n, gb->cpu.a); cycles=12; break; }
        /* LD A, (0xFF00+n) */
        case 0xF0: { uint8_t n=RDPC(); gb->cpu.a=RD(0xFF00u+n); cycles=12; break; }
        /* LD (0xFF00+C), A */
        case 0xE2: WR(0xFF00u+gb->cpu.c, gb->cpu.a); cycles=8; break;
        /* LD A, (0xFF00+C) */
        case 0xF2: gb->cpu.a = RD(0xFF00u+gb->cpu.c); cycles=8; break;

        /* ADD SP, e */
        case 0xE8: {
            int8_t e = (int8_t)RDPC();
            uint32_t r = (uint32_t)(rSP + e);
            SET_FLAGS(0, 0,
                ((rSP ^ e ^ (r & 0xFFFF)) & 0x10) != 0,
                ((rSP ^ e ^ (r & 0xFFFF)) & 0x100) != 0);
            rSP = (uint16_t)r;
            cycles=16; break;
        }
        /* LD HL, SP+e */
        case 0xF8: {
            int8_t e = (int8_t)RDPC();
            uint32_t r = (uint32_t)(rSP + e);
            SET_FLAGS(0, 0,
                ((rSP ^ e ^ (r & 0xFFFF)) & 0x10) != 0,
                ((rSP ^ e ^ (r & 0xFFFF)) & 0x100) != 0);
            setHL((uint16_t)r);
            cycles=12; break;
        }

        /* LD (nn), A */
        case 0xEA: { uint8_t l=RDPC(),h=RDPC(); WR((uint16_t)((h<<8)|l), gb->cpu.a); cycles=16; break; }
        /* LD A, (nn) */
        case 0xFA: { uint8_t l=RDPC(),h=RDPC(); gb->cpu.a=RD((uint16_t)((h<<8)|l)); cycles=16; break; }

        /* LD SP, HL */
        case 0xF9: rSP = rHL; cycles=8; break;

        /* DI */
        case 0xF3: gb->cpu.ime = 0; break;
        /* EI */
        case 0xFB: if (gb->cpu.ime == 0) gb->cpu.ime = 2; break;

        /* Undefined/illegal — treat as NOP to avoid crash */
        default: break;
    }

    return cycles;
}
