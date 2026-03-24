/**
 * @file mapper.c
 * @brief NES ROM mapper implementation
 *
 * Mapper  0 (NROM)  — no banking; up to 32 KB PRG + 8 KB CHR
 * Mapper  1 (MMC1)  — 5-bit serial shift register; 16/32 KB PRG, 4/8 KB CHR
 * Mapper  2 (UxROM) — switchable lower 16 KB PRG, fixed upper 16 KB
 * Mapper  3 (CNROM) — switchable 8 KB CHR bank
 * Mapper  4 (MMC3)  — 8 KB PRG / 1+2 KB CHR banks, scanline IRQ counter
 * Mapper  7 (AxROM) — 32 KB PRG bank, single-screen mirroring
 * Mapper  9 (MMC2)  — 8 KB PRG, latch-triggered 4 KB CHR banks (Punch-Out!!)
 * Mapper 66 (GxROM) — dual 32 KB PRG + 8 KB CHR bank select
 * Mapper206 (Namco) — simplified MMC3 (no IRQ, fixed mirroring)
 *
 * @license Apache-2.0
 */
#include "mapper.h"

/* ── iNES header constants ────────────────────────────────────────────*/
#define INES_HDR   16
#define TRAINER   512

/* ── Bank pointer helpers ─────────────────────────────────────────────
 * All helpers wrap the bank index modulo the available bank count to
 * avoid out-of-bounds accesses when a game writes an out-of-range value.
 */

static inline const uint8_t *prg16(const Mapper *m, int b)
{
    return m->prg_rom + (b % m->prg_banks) * 16384;
}

static inline const uint8_t *prg8(const Mapper *m, int b)
{
    return m->prg_rom + (b % (m->prg_banks * 2)) * 8192;
}

static inline const uint8_t *chr4(const Mapper *m, int b)
{
    return m->chr_rom + (b % (m->chr_banks * 2)) * 4096;
}

static inline const uint8_t *chr8(const Mapper *m, int b)
{
    return m->chr_rom + (b % m->chr_banks) * 8192;
}

/* ── Pre-computed PRG page pointers ───────────────────────────────────
 * Called from mapper_init and every PRG bank-switch write to update
 * the 4 × 8 KB page pointers (prg_page[0..3]).  Reads from $8000+
 * then just index: m->prg_page[(addr >> 13) & 3][addr & 0x1FFF].   */

static void update_prg_pages(Mapper *m)
{
    switch (m->id) {
    case 0:
    case 3: /* CNROM only switches CHR, PRG is NROM */
        m->prg_page[0] = prg16(m, 0);
        m->prg_page[1] = prg16(m, 0) + 0x2000;
        if (m->prg_banks <= 1) {
            m->prg_page[2] = m->prg_page[0];
            m->prg_page[3] = m->prg_page[1];
        } else {
            m->prg_page[2] = prg16(m, 1);
            m->prg_page[3] = prg16(m, 1) + 0x2000;
        }
        break;
    case 1: {
        int mode = (m->s.mmc1.ctrl >> 2) & 3;
        int bank = m->s.mmc1.prg & 0x0F;
        if (mode <= 1) {
            int b = bank & ~1;
            m->prg_page[0] = prg16(m, b);
            m->prg_page[1] = prg16(m, b) + 0x2000;
            m->prg_page[2] = prg16(m, b + 1);
            m->prg_page[3] = prg16(m, b + 1) + 0x2000;
        } else if (mode == 2) {
            m->prg_page[0] = prg16(m, 0);
            m->prg_page[1] = prg16(m, 0) + 0x2000;
            m->prg_page[2] = prg16(m, bank);
            m->prg_page[3] = prg16(m, bank) + 0x2000;
        } else { /* mode 3 — most common */
            m->prg_page[0] = prg16(m, bank);
            m->prg_page[1] = prg16(m, bank) + 0x2000;
            m->prg_page[2] = prg16(m, m->prg_banks - 1);
            m->prg_page[3] = prg16(m, m->prg_banks - 1) + 0x2000;
        }
        break;
    }
    case 2:
        m->prg_page[0] = prg16(m, m->s.uxrom.prg_bank);
        m->prg_page[1] = prg16(m, m->s.uxrom.prg_bank) + 0x2000;
        m->prg_page[2] = prg16(m, m->prg_banks - 1);
        m->prg_page[3] = prg16(m, m->prg_banks - 1) + 0x2000;
        break;
    case 4:
    case 206: {
        int banks8   = m->prg_banks * 2;
        int prg_mode = (m->s.mmc3.bank_sel >> 6) & 1;
        int r6 = m->s.mmc3.regs[6] % banks8;
        int r7 = m->s.mmc3.regs[7] % banks8;
        if (prg_mode == 0) {
            m->prg_page[0] = prg8(m, r6);
            m->prg_page[1] = prg8(m, r7);
            m->prg_page[2] = prg8(m, banks8 - 2);
            m->prg_page[3] = prg8(m, banks8 - 1);
        } else {
            m->prg_page[0] = prg8(m, banks8 - 2);
            m->prg_page[1] = prg8(m, r7);
            m->prg_page[2] = prg8(m, r6);
            m->prg_page[3] = prg8(m, banks8 - 1);
        }
        break;
    }
    case 7: {
        int banks32 = m->prg_banks / 2;
        int bank = (banks32 > 0) ? (m->s.axrom.prg_bank % banks32) : 0;
        const uint8_t *base = m->prg_rom + bank * 32768;
        m->prg_page[0] = base;
        m->prg_page[1] = base + 0x2000;
        m->prg_page[2] = base + 0x4000;
        m->prg_page[3] = base + 0x6000;
        break;
    }
    case 9: {
        int banks8 = m->prg_banks * 2;
        m->prg_page[0] = prg8(m, m->s.mmc2.prg_bank);
        m->prg_page[1] = prg8(m, banks8 - 3);
        m->prg_page[2] = prg8(m, banks8 - 2);
        m->prg_page[3] = prg8(m, banks8 - 1);
        break;
    }
    case 66: {
        int banks32 = m->prg_banks / 2;
        int bank = (banks32 > 0) ? (m->s.gxrom.prg_bank % banks32) : 0;
        const uint8_t *base = m->prg_rom + bank * 32768;
        m->prg_page[0] = base;
        m->prg_page[1] = base + 0x2000;
        m->prg_page[2] = base + 0x4000;
        m->prg_page[3] = base + 0x6000;
        break;
    }
    }
}

/* ── Pre-computed CHR page pointers ───────────────────────────────────
 * 8 × 1 KB page pointers covering $0000–$1FFF.  Reads from pattern
 * tables then just index: m->chr_page[addr >> 10][addr & 0x3FF].
 * Called from mapper_init and every CHR bank-switch write.
 * Mapper 9 (MMC2) also updates here on latch changes.              */

static void update_chr_pages(Mapper *m)
{
    const uint8_t *base;

    switch (m->id) {
    case 0:
    case 2:
        base = m->chr_banks ? m->chr_rom : m->chr_ram;
        for (int i = 0; i < 8; i++) m->chr_page[i] = base + i * 1024;
        break;

    case 1:
        if (m->chr_banks == 0) {
            for (int i = 0; i < 8; i++) m->chr_page[i] = m->chr_ram + i * 1024;
            break;
        }
        if ((m->s.mmc1.ctrl >> 4) & 1) {
            const uint8_t *c0 = chr4(m, m->s.mmc1.chr0);
            const uint8_t *c1 = chr4(m, m->s.mmc1.chr1);
            for (int i = 0; i < 4; i++) m->chr_page[i]     = c0 + i * 1024;
            for (int i = 0; i < 4; i++) m->chr_page[4 + i] = c1 + i * 1024;
        } else {
            const uint8_t *c = chr8(m, m->s.mmc1.chr0 >> 1);
            for (int i = 0; i < 8; i++) m->chr_page[i] = c + i * 1024;
        }
        break;

    case 3:
        if (m->chr_banks == 0) {
            for (int i = 0; i < 8; i++) m->chr_page[i] = m->chr_ram + i * 1024;
        } else {
            const uint8_t *c = chr8(m, m->s.cnrom.chr_bank);
            for (int i = 0; i < 8; i++) m->chr_page[i] = c + i * 1024;
        }
        break;

    case 4:
    case 206: {
        if (m->chr_banks == 0) {
            for (int i = 0; i < 8; i++) m->chr_page[i] = m->chr_ram + i * 1024;
            break;
        }
        int cm   = (m->s.mmc3.bank_sel >> 7) & 1;
        int t1k  = m->chr_banks * 8;
        int r0   = (m->s.mmc3.regs[0] & 0xFE) % t1k;
        int r1   = (m->s.mmc3.regs[1] & 0xFE) % t1k;
        if (cm == 0) {
            m->chr_page[0] = m->chr_rom + r0       * 1024;
            m->chr_page[1] = m->chr_rom + (r0 + 1) * 1024;
            m->chr_page[2] = m->chr_rom + r1       * 1024;
            m->chr_page[3] = m->chr_rom + (r1 + 1) * 1024;
            m->chr_page[4] = m->chr_rom + (m->s.mmc3.regs[2] % t1k) * 1024;
            m->chr_page[5] = m->chr_rom + (m->s.mmc3.regs[3] % t1k) * 1024;
            m->chr_page[6] = m->chr_rom + (m->s.mmc3.regs[4] % t1k) * 1024;
            m->chr_page[7] = m->chr_rom + (m->s.mmc3.regs[5] % t1k) * 1024;
        } else {
            m->chr_page[0] = m->chr_rom + (m->s.mmc3.regs[2] % t1k) * 1024;
            m->chr_page[1] = m->chr_rom + (m->s.mmc3.regs[3] % t1k) * 1024;
            m->chr_page[2] = m->chr_rom + (m->s.mmc3.regs[4] % t1k) * 1024;
            m->chr_page[3] = m->chr_rom + (m->s.mmc3.regs[5] % t1k) * 1024;
            m->chr_page[4] = m->chr_rom + r0       * 1024;
            m->chr_page[5] = m->chr_rom + (r0 + 1) * 1024;
            m->chr_page[6] = m->chr_rom + r1       * 1024;
            m->chr_page[7] = m->chr_rom + (r1 + 1) * 1024;
        }
        break;
    }

    case 7:
        for (int i = 0; i < 8; i++) m->chr_page[i] = m->chr_ram + i * 1024;
        break;

    case 9: {
        int t4k = m->chr_banks * 2;
        uint8_t b0 = m->s.mmc2.latch[0] ? m->s.mmc2.chr_bank_fe[0]
                                         : m->s.mmc2.chr_bank_fd[0];
        uint8_t b1 = m->s.mmc2.latch[1] ? m->s.mmc2.chr_bank_fe[1]
                                         : m->s.mmc2.chr_bank_fd[1];
        const uint8_t *c0 = m->chr_rom + (b0 % t4k) * 4096;
        const uint8_t *c1 = m->chr_rom + (b1 % t4k) * 4096;
        for (int i = 0; i < 4; i++) m->chr_page[i]     = c0 + i * 1024;
        for (int i = 0; i < 4; i++) m->chr_page[4 + i] = c1 + i * 1024;
        break;
    }

    case 66:
        if (m->chr_banks == 0) {
            for (int i = 0; i < 8; i++) m->chr_page[i] = m->chr_ram + i * 1024;
        } else {
            const uint8_t *c = chr8(m, m->s.gxrom.chr_bank);
            for (int i = 0; i < 8; i++) m->chr_page[i] = c + i * 1024;
        }
        break;
    }
}

/* ── iNES header parser ───────────────────────────────────────────────*/

static int parse_ines(Mapper *m, const uint8_t *rom, int rom_size)
{
    if (rom_size < INES_HDR) return -1;
    if (rom[0] != 'N' || rom[1] != 'E' || rom[2] != 'S' || rom[3] != 0x1A)
        return -1;

    uint8_t f6 = rom[6], f7 = rom[7];

    m->prg_banks = rom[4];
    m->chr_banks = rom[5];
    m->id        = (f6 >> 4) | (f7 & 0xF0);
    m->mirroring = (f6 & 0x01) ? MIRROR_V : MIRROR_H;
    /* Bit 3 of f6 = four-screen VRAM; treat as vertical for simplicity */

    int offset = INES_HDR + ((f6 & 0x04) ? TRAINER : 0);
    m->prg_rom  = rom + offset;
    m->chr_rom  = (m->chr_banks > 0) ? (rom + offset + m->prg_banks * 16384) : 0;

    /* Zero CHR-RAM and mapper-private state */
    for (int i = 0; i < 8192;            i++) m->chr_ram[i]           = 0;
    for (int i = 0; i < (int)sizeof(m->s); i++) ((uint8_t *)&m->s)[i] = 0;
    m->irq_pending = 0;

    return 0;
}


/* ════════════════════════════════════════════════════════════════════ *
 * Mapper 0 — NROM                                                      *
 * ════════════════════════════════════════════════════════════════════ */

static uint8_t m0_ppu_read(Mapper *m, uint16_t addr)
{
    addr &= 0x1FFFu;
    return m->chr_banks ? m->chr_rom[addr] : m->chr_ram[addr];
}

static void m0_ppu_write(Mapper *m, uint16_t addr, uint8_t val)
{
    if (m->chr_banks == 0) m->chr_ram[addr & 0x1FFFu] = val;
}


/* ════════════════════════════════════════════════════════════════════ *
 * Mapper 1 — MMC1 / SxROM                                             *
 * ════════════════════════════════════════════════════════════════════ */

static void m1_write(Mapper *m, uint16_t addr, uint8_t val)
{
    static const int mir_lut[4] = { MIRROR_1A, MIRROR_1B, MIRROR_V, MIRROR_H };

    if (val & 0x80u) {
        /* Reset: clear shift register, force PRG mode 3 (fix last bank) */
        m->s.mmc1.shift = 0;
        m->s.mmc1.count = 0;
        m->s.mmc1.ctrl |= 0x0Cu;
        return;
    }

    /* Shift bit 0 of the written byte into the shift register LSB-first:
     * after 5 writes, shift[4:0] = [5th bit, 4th, 3rd, 2nd, 1st].     */
    m->s.mmc1.shift = (uint8_t)((m->s.mmc1.shift >> 1) | ((val & 1u) << 4));
    if (++m->s.mmc1.count < 5) return;

    uint8_t data = m->s.mmc1.shift;
    m->s.mmc1.shift = 0;
    m->s.mmc1.count = 0;

    switch ((addr >> 13) & 3u) {
    case 0:
        m->s.mmc1.ctrl = data;
        m->mirroring   = mir_lut[data & 3u];
        break;
    case 1: m->s.mmc1.chr0 = data; break;
    case 2: m->s.mmc1.chr1 = data; break;
    case 3: m->s.mmc1.prg  = data; break;
    }
    update_prg_pages(m);
    update_chr_pages(m);
}

static uint8_t m1_ppu_read(Mapper *m, uint16_t addr)
{
    addr &= 0x1FFFu;
    if (m->chr_banks == 0) return m->chr_ram[addr]; /* full 8 KB CHR-RAM */
    if ((m->s.mmc1.ctrl >> 4) & 1) {
        /* 4 KB CHR mode */
        return (addr < 0x1000u) ? chr4(m, m->s.mmc1.chr0)[addr]
                                : chr4(m, m->s.mmc1.chr1)[addr - 0x1000u];
    }
    /* 8 KB CHR mode: chr0 bit 0 ignored */
    return chr8(m, m->s.mmc1.chr0 >> 1)[addr];
}

static void m1_ppu_write(Mapper *m, uint16_t addr, uint8_t val)
{
    if (m->chr_banks == 0) m->chr_ram[addr & 0x1FFFu] = val;
}


/* ════════════════════════════════════════════════════════════════════ *
 * Mapper 2 — UxROM                                                     *
 * ════════════════════════════════════════════════════════════════════ */

static void m2_write(Mapper *m, uint16_t addr, uint8_t val)
{
    (void)addr;
    m->s.uxrom.prg_bank = val;
    update_prg_pages(m);
}

static uint8_t m2_ppu_read(Mapper *m, uint16_t addr)
{
    addr &= 0x1FFFu;
    return m->chr_banks ? m->chr_rom[addr] : m->chr_ram[addr];
}

static void m2_ppu_write(Mapper *m, uint16_t addr, uint8_t val)
{
    if (m->chr_banks == 0) m->chr_ram[addr & 0x1FFFu] = val;
}


/* ════════════════════════════════════════════════════════════════════ *
 * Mapper 3 — CNROM                                                     *
 * ════════════════════════════════════════════════════════════════════ */

static void m3_write(Mapper *m, uint16_t addr, uint8_t val)
{
    (void)addr;
    m->s.cnrom.chr_bank = val & 3u;
    update_chr_pages(m);
}

static uint8_t m3_ppu_read(Mapper *m, uint16_t addr)
{
    if (m->chr_banks == 0) return m->chr_ram[addr & 0x1FFFu];
    return chr8(m, m->s.cnrom.chr_bank)[addr & 0x1FFFu];
}


/* ════════════════════════════════════════════════════════════════════ *
 * Mapper 4 — MMC3   (Mapper 206 reuses this with has_irq=0)           *
 * ════════════════════════════════════════════════════════════════════ */

static void m4_write(Mapper *m, uint16_t addr, uint8_t val, int has_irq)
{
    int even = !(addr & 1u);

    if (addr < 0xA000u) {
        if (even) m->s.mmc3.bank_sel = val;
        else      m->s.mmc3.regs[m->s.mmc3.bank_sel & 7u] = val;
        update_prg_pages(m);
        update_chr_pages(m);
    } else if (addr < 0xC000u) {
        if (even && has_irq)
            m->mirroring = (val & 1u) ? MIRROR_H : MIRROR_V;
        /* odd: PRG-RAM write protect — not needed, stub */
    } else if (addr < 0xE000u) {
        if (even) m->s.mmc3.irq_latch = val;
        else    { m->s.mmc3.irq_cnt = 0; m->s.mmc3.irq_reload = 1; }
    } else {
        if (even) { m->s.mmc3.irq_en = 0; m->irq_pending = 0; }
        else        m->s.mmc3.irq_en = 1;
    }
}

static uint8_t m4_ppu_read(Mapper *m, uint16_t addr)
{
    addr &= 0x1FFFu;
    if (m->chr_banks == 0) return m->chr_ram[addr];

    int chr_mode = (m->s.mmc3.bank_sel >> 7) & 1;
    int t1k      = m->chr_banks * 8; /* total 1 KB CHR pages */
    int off;

    if (chr_mode == 0) {
        /* 2KB banks at $0000–$0FFF, 1 KB banks at $1000–$1FFF */
        if (addr < 0x0800u)
            off = ((m->s.mmc3.regs[0] & 0xFEu) % t1k) * 1024 + (addr & 0x7FFu);
        else if (addr < 0x1000u)
            off = ((m->s.mmc3.regs[1] & 0xFEu) % t1k) * 1024 + (addr & 0x7FFu);
        else {
            int slot = (addr - 0x1000u) >> 10; /* 0–3 for R2–R5 */
            off = (m->s.mmc3.regs[2 + slot] % t1k) * 1024 + (addr & 0x3FFu);
        }
    } else {
        /* 1 KB banks at $0000–$0FFF, 2 KB banks at $1000–$1FFF */
        if (addr < 0x0400u)
            off = (m->s.mmc3.regs[2] % t1k) * 1024 + (addr & 0x3FFu);
        else if (addr < 0x0800u)
            off = (m->s.mmc3.regs[3] % t1k) * 1024 + (addr & 0x3FFu);
        else if (addr < 0x0C00u)
            off = (m->s.mmc3.regs[4] % t1k) * 1024 + (addr & 0x3FFu);
        else if (addr < 0x1000u)
            off = (m->s.mmc3.regs[5] % t1k) * 1024 + (addr & 0x3FFu);
        else if (addr < 0x1800u)
            off = ((m->s.mmc3.regs[0] & 0xFEu) % t1k) * 1024 + (addr & 0x7FFu);
        else
            off = ((m->s.mmc3.regs[1] & 0xFEu) % t1k) * 1024 + (addr & 0x7FFu);
    }
    return m->chr_rom[off];
}

static void m4_ppu_write(Mapper *m, uint16_t addr, uint8_t val)
{
    if (m->chr_banks == 0) m->chr_ram[addr & 0x1FFFu] = val;
}

static void m4_scanline(Mapper *m)
{
    if (m->s.mmc3.irq_cnt == 0 || m->s.mmc3.irq_reload) {
        m->s.mmc3.irq_cnt    = m->s.mmc3.irq_latch;
        m->s.mmc3.irq_reload = 0;
    } else {
        m->s.mmc3.irq_cnt--;
    }
    if (m->s.mmc3.irq_cnt == 0 && m->s.mmc3.irq_en)
        m->irq_pending = 1;
}


/* ════════════════════════════════════════════════════════════════════ *
 * Mapper 7 — AxROM                                                     *
 * ════════════════════════════════════════════════════════════════════ */

static void m7_write(Mapper *m, uint16_t addr, uint8_t val)
{
    (void)addr;
    m->s.axrom.prg_bank = val & 7u;
    m->mirroring = (val & 0x10u) ? MIRROR_1B : MIRROR_1A;
    update_prg_pages(m);
}

/* AxROM always uses CHR-RAM */
static uint8_t m7_ppu_read(Mapper *m, uint16_t addr)
{
    return m->chr_ram[addr & 0x1FFFu];
}

static void m7_ppu_write(Mapper *m, uint16_t addr, uint8_t val)
{
    m->chr_ram[addr & 0x1FFFu] = val;
}


/* ════════════════════════════════════════════════════════════════════ *
 * Mapper 9 — MMC2 / PxROM (Punch-Out!!)                               *
 *                                                                      *
 * PRG layout (8 KB banks):                                             *
 *   $8000–$9FFF  switchable 8 KB (prg_bank register)                  *
 *   $A000–$BFFF  fixed: second-to-last-3 8 KB bank                    *
 *   $C000–$DFFF  fixed: second-to-last-2 8 KB bank                    *
 *   $E000–$FFFF  fixed: last 8 KB bank                                 *
 *                                                                      *
 * CHR layout (4 KB windows, latch-controlled):                         *
 *   $0000–$0FFF  chr_bank_fd[0] or chr_bank_fe[0] per latch[0]        *
 *   $1000–$1FFF  chr_bank_fd[1] or chr_bank_fe[1] per latch[1]        *
 *   Latch updates on PPU reads of tile $FD/$FE in each nametable half. *
 * ════════════════════════════════════════════════════════════════════ */

static void m9_write(Mapper *m, uint16_t addr, uint8_t val)
{
    if      (addr < 0xB000u) { m->s.mmc2.prg_bank      = val & 0x0Fu; update_prg_pages(m); }
    else if (addr < 0xC000u) { m->s.mmc2.chr_bank_fd[0] = val & 0x1Fu; update_chr_pages(m); }
    else if (addr < 0xD000u) { m->s.mmc2.chr_bank_fe[0] = val & 0x1Fu; update_chr_pages(m); }
    else if (addr < 0xE000u) { m->s.mmc2.chr_bank_fd[1] = val & 0x1Fu; update_chr_pages(m); }
    else if (addr < 0xF000u) { m->s.mmc2.chr_bank_fe[1] = val & 0x1Fu; update_chr_pages(m); }
    else                     m->mirroring = (val & 1u) ? MIRROR_H : MIRROR_V;
}

static uint8_t m9_ppu_read(Mapper *m, uint16_t addr)
{
    addr &= 0x1FFFu;
    int  half   = (addr >= 0x1000u) ? 1 : 0;
    int  latch  = m->s.mmc2.latch[half];
    uint8_t bank = latch ? m->s.mmc2.chr_bank_fe[half]
                         : m->s.mmc2.chr_bank_fd[half];
    int t4k = m->chr_banks * 2;
    uint8_t result = m->chr_rom[(bank % t4k) * 4096 + (addr & 0x0FFFu)];

    /* Update latch based on tile address within this 4 KB window */
    uint16_t rel = addr & 0x0FFFu;
    if (rel >= 0xFD8u && rel <= 0xFDFu) m->s.mmc2.latch[half] = 0; /* $FD */
    if (rel >= 0xFE8u && rel <= 0xFEFu) m->s.mmc2.latch[half] = 1; /* $FE */

    return result;
}


/* ════════════════════════════════════════════════════════════════════ *
 * Mapper 66 — GxROM                                                    *
 * ════════════════════════════════════════════════════════════════════ */

static void m66_write(Mapper *m, uint16_t addr, uint8_t val)
{
    (void)addr;
    m->s.gxrom.prg_bank = (val >> 4) & 3u;
    m->s.gxrom.chr_bank = val & 3u;
    update_prg_pages(m);
    update_chr_pages(m);
}

static uint8_t m66_ppu_read(Mapper *m, uint16_t addr)
{
    if (m->chr_banks == 0) return m->chr_ram[addr & 0x1FFFu];
    return chr8(m, m->s.gxrom.chr_bank)[addr & 0x1FFFu];
}


/* ════════════════════════════════════════════════════════════════════ *
 * Public API                                                           *
 * ════════════════════════════════════════════════════════════════════ */

int mapper_init(Mapper *m, const uint8_t *rom, int rom_size)
{
    if (parse_ines(m, rom, rom_size) != 0) return -1;

    switch (m->id) {
    case 0:                               break;
    case 1:  m->s.mmc1.ctrl = 0x0Cu;    break; /* PRG mode 3 on reset */
    case 2:                               break;
    case 3:                               break;
    case 4:  /* fall-through */
    case 206:                             break;
    case 7:  m->mirroring = MIRROR_1A;   break;
    case 9:  m->mirroring = MIRROR_V;    break;
    case 66:                              break;
    default:                              return -1;
    }
    update_prg_pages(m);
    update_chr_pages(m);
    return 0;
}

uint8_t mapper_cpu_read(const Mapper *m, uint16_t addr)
{
    if (addr >= 0x8000u)
        return m->prg_page[(addr >> 13) & 3][addr & 0x1FFFu];
    return 0xFF; /* $4020-$7FFF: open bus / unmapped */
}

void mapper_cpu_write(Mapper *m, uint16_t addr, uint8_t val)
{
    switch (m->id) {
    case 0:                                                       break;
    case 1:   if (addr >= 0x8000u) m1_write(m, addr, val);       break;
    case 2:   if (addr >= 0x8000u) m2_write(m, addr, val);       break;
    case 3:   if (addr >= 0x8000u) m3_write(m, addr, val);       break;
    case 4:   if (addr >= 0x8000u) m4_write(m, addr, val, 1);    break;
    case 206: if (addr >= 0x8000u) m4_write(m, addr, val, 0);    break;
    case 7:   if (addr >= 0x8000u) m7_write(m, addr, val);       break;
    case 9:   if (addr >= 0xA000u) m9_write(m, addr, val);       break;
    case 66:  if (addr >= 0x8000u) m66_write(m, addr, val);      break;
    }
}

uint8_t mapper_ppu_read(Mapper *m, uint16_t addr)
{
    switch (m->id) {
    case 0:   return m0_ppu_read(m, addr);
    case 1:   return m1_ppu_read(m, addr);
    case 2:   return m2_ppu_read(m, addr);
    case 3:   return m3_ppu_read(m, addr);
    case 4:
    case 206: return m4_ppu_read(m, addr);
    case 7:   return m7_ppu_read(m, addr);
    case 9:   return m9_ppu_read(m, addr);
    case 66:  return m66_ppu_read(m, addr);
    default:  return 0;
    }
}

void mapper_ppu_write(Mapper *m, uint16_t addr, uint8_t val)
{
    switch (m->id) {
    case 0:   m0_ppu_write(m, addr, val);              break;
    case 1:   m1_ppu_write(m, addr, val);              break;
    case 2:   m2_ppu_write(m, addr, val);              break;
    case 3:
        if (m->chr_banks == 0) m->chr_ram[addr & 0x1FFFu] = val;
        break;
    case 4:
    case 206: m4_ppu_write(m, addr, val);              break;
    case 7:   m7_ppu_write(m, addr, val);              break;
    case 9:   /* CHR-ROM only; PPU writes are no-ops */ break;
    case 66:
        if (m->chr_banks == 0) m->chr_ram[addr & 0x1FFFu] = val;
        break;
    }
}

void mapper_scanline(Mapper *m)
{
    if (m->id == 4) m4_scanline(m);
    /* Mapper 206: no IRQ; other mappers: no scanline counter */
}

int mapper_irq_pending(Mapper *m)
{
    if (m->irq_pending) { m->irq_pending = 0; return 1; }
    return 0;
}
