/**
 * @file snes.c
 * @brief SNES bus, cartridge, I/O registers, DMA/HDMA and frame scheduler
 *
 * @license Apache-2.0
 */
#include <string.h>
#include "snes.h"

#define LINE_CYCLES   1364
#define HBLANK_START  1096
#define LINES         262
#define VBLANK_LINE   225

/* ── Cartridge ───────────────────────────────────────────────────────── */
static int header_score(const uint8_t *rom, uint32_t size, uint32_t off, int want_hi)
{
    int sc = 0;
    if (size < off + 0x40) return -1;
    uint16_t chk  = rom[off + 0x1C] | (rom[off + 0x1D] << 8);
    uint16_t comp = rom[off + 0x1E] | (rom[off + 0x1F] << 8);
    uint16_t rst  = rom[off + 0x3C] | (rom[off + 0x3D] << 8);
    if ((uint16_t)(chk + comp) == 0xFFFF) sc += 4;
    if (rst >= 0x8000) sc += 2;
    if ((rom[off + 0x15] & 1) == want_hi) sc += 2;
    return sc;
}

int snes_init(SNES *s, const uint8_t *rom, uint32_t size)
{
    if ((size & 0x3FF) == 512) { rom += 512; size -= 512; }
    if (size < 0x8000) return -1;

    memset(s, 0, sizeof(*s));
    s->rom = rom;
    s->rom_size = size;
    s->rom_mask = (size & (size - 1)) == 0 ? size - 1 : 0;

    int lo = header_score(rom, size, 0x7FC0, 0);
    int hi = header_score(rom, size, 0xFFC0, 1);
    s->hirom = hi > lo;
    uint32_t off = s->hirom ? 0xFFC0 : 0x7FC0;

    uint8_t sram_n = rom[off + 0x18];
    s->sram_mask = 0;
    if (sram_n > 0 && sram_n <= 5) s->sram_mask = (1024u << sram_n) - 1;

    snes_reset(s);
    return 0;
}

void snes_reset(SNES *s)
{
    memset(s->wram, 0, sizeof(s->wram));
    memset(&s->dma, 0, sizeof(s->dma));
    memset(s->fb, 0, sizeof(s->fb));
    s->nmitimen = s->rdnmi = s->timeup = s->irq_line = 0;
    s->mdmaen = s->hdmaen = 0;
    s->htime = s->vtime = 0x1FF;
    s->hc = 0; s->line = 0; s->frame = 0;
    s->apu_out[0] = 0xAA; s->apu_out[1] = 0xBB; s->apu_out[2] = s->apu_out[3] = 0;
    s->apu_running = 0;
    ppu_reset(&s->ppu);
    snes_rebuild_pages(s);
    cpu65816_reset(s);
    s->hc = 0;
}

void snes_set_pad(SNES *s, int port, uint16_t mask) { s->pad[port & 1] = mask; }

static inline uint8_t cart_read(SNES *s, uint8_t bank, uint16_t o)
{
    uint32_t idx;
    if (!s->hirom) {
        if (o >= 0x8000) idx = ((uint32_t)(bank & 0x7F) * 0x8000) + (o & 0x7FFF);
        else if ((bank & 0x7F) >= 0x70 && (bank & 0x7F) < 0x7E) {
            if (!s->sram_mask) return s->open_bus;
            return s->sram[(((bank & 0x0F) * 0x8000u) + o) & s->sram_mask];
        } else return s->open_bus;
    } else {
        if ((bank & 0x7F) < 0x40) {
            if (o < 0x8000) {
                if (o >= 0x6000 && (bank & 0x7F) >= 0x20 && s->sram_mask)
                    return s->sram[(((bank & 0x1F) * 0x2000u) + (o - 0x6000)) & s->sram_mask];
                return s->open_bus;
            }
        }
        idx = ((uint32_t)(bank & 0x3F) * 0x10000) + o;
    }
    if (s->rom_mask) idx &= s->rom_mask; else idx %= s->rom_size;
    return s->rom[idx];
}

static inline void cart_write(SNES *s, uint8_t bank, uint16_t o, uint8_t v)
{
    if (!s->sram_mask) return;
    if (!s->hirom) {
        if (o < 0x8000 && (bank & 0x7F) >= 0x70 && (bank & 0x7F) < 0x7E)
            s->sram[(((bank & 0x0F) * 0x8000u) + o) & s->sram_mask] = v;
    } else if ((bank & 0x7F) >= 0x20 && (bank & 0x7F) < 0x40 && o >= 0x6000 && o < 0x8000) {
        s->sram[(((bank & 0x1F) * 0x2000u) + (o - 0x6000)) & s->sram_mask] = v;
    }
}

/* ── Access timing ───────────────────────────────────────────────────── */
static inline int speed(SNES *s, uint8_t bank, uint16_t o)
{
    if (bank >= 0x40 && bank < 0x80) return 8;
    if (bank >= 0xC0) return s->memsel ? 6 : 8;
    if (o < 0x2000) return 8;
    if (o < 0x4000) return 6;
    if (o < 0x4200) return 12;
    if (o < 0x6000) return 6;
    if (o < 0x8000) return 8;
    return (bank >= 0x80 && s->memsel) ? 6 : 8;
}

/* ── I/O register reads ──────────────────────────────────────────────── */
static uint8_t io_read(SNES *s, uint16_t o)
{
    uint8_t v;
    /* Everything except these reads can change with time or has side effects that
     * a repeating idle loop would observe, so it disqualifies idle-loop skipping. */
    if (!(o == 0x4210 || o == 0x4211 || (o >= 0x4214 && o <= 0x421F) || (o >= 0x4300 && o < 0x4380)))
        s->vol_reads++;
    if (o >= 0x2100 && o < 0x2140) return ppu_read(s, o);
    if (o >= 0x2140 && o < 0x2180) return s->apu_out[o & 3];
    if (o == 0x2180) { v = s->wram[s->wmadd & 0x1FFFF]; s->wmadd = (s->wmadd + 1) & 0x1FFFF; return v; }
    if (o == 0x4016 || o == 0x4017) {
        int p = o & 1;
        v = (s->joy_shift[p] >> 15) & 1;
        if (s->joy_strobe) s->joy_shift[p] = s->pad[p];
        else s->joy_shift[p] = (s->joy_shift[p] << 1) | 1;
        return v | (p ? 0x1C : (s->open_bus & 0xFC));
    }
    switch (o) {
    case 0x4210: v = (s->rdnmi & 0x80) | (s->open_bus & 0x70) | 0x02; s->rdnmi &= 0x7F; return v;
    case 0x4211: v = s->timeup << 7; s->timeup = 0; s->irq_line = 0; return v | (s->open_bus & 0x7F);
    case 0x4212:
        v = 0;
        if (s->line >= VBLANK_LINE) v |= 0x80;
        if (s->hc >= HBLANK_START || s->hc < 4) v |= 0x40;
        if (s->line >= VBLANK_LINE && s->line < VBLANK_LINE + 3) v |= 0x01;
        return v;
    case 0x4213: return 0xFF;
    case 0x4214: return s->rddiv;
    case 0x4215: return s->rddiv >> 8;
    case 0x4216: return s->rdmpy;
    case 0x4217: return s->rdmpy >> 8;
    case 0x4218: return s->joy_auto[0];
    case 0x4219: return s->joy_auto[0] >> 8;
    case 0x421A: return s->joy_auto[1];
    case 0x421B: return s->joy_auto[1] >> 8;
    case 0x421C: case 0x421D: case 0x421E: case 0x421F: return 0;
    }
    if (o >= 0x4300 && o < 0x4380) {
        DMACh *d = &s->dma[(o >> 4) & 7];
        switch (o & 0xF) {
        case 0x0: return d->dmap;
        case 0x1: return d->bbad;
        case 0x2: return d->a1t;
        case 0x3: return d->a1t >> 8;
        case 0x4: return d->a1b;
        case 0x5: return d->das;
        case 0x6: return d->das >> 8;
        case 0x7: return d->dasb;
        case 0x8: return d->a2a;
        case 0x9: return d->a2a >> 8;
        case 0xA: return d->ntrl;
        default:  return d->unused;
        }
    }
    return s->open_bus;
}

/* ── DMA / HDMA ──────────────────────────────────────────────────────── */
static const uint8_t dma_pat[8][4] = {
    {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1}
};
static const uint8_t dma_len[8] = {1, 2, 2, 4, 4, 4, 2, 4};

static void dma_run(SNES *s, uint8_t mask)
{
    int ch;
    s->hc += 8;
    for (ch = 0; ch < 8; ch++) {
        if (!(mask & (1 << ch))) continue;
        DMACh *d = &s->dma[ch];
        int mode = d->dmap & 7;
        int to_a = d->dmap & 0x80;
        int fixed = d->dmap & 0x08;
        int dec = d->dmap & 0x10;
        uint32_t len = d->das ? d->das : 0x10000;
        int idx = 0;
        s->hc += 8;
        while (len--) {
            uint16_t b = 0x2100 + ((d->bbad + dma_pat[mode][idx & 3]) & 0xFF);
            uint32_t a = ((uint32_t)d->a1b << 16) | d->a1t;
            int32_t h0 = s->hc;
            if (!to_a) {
                uint8_t v = snes_read(s, a);
                snes_write(s, b, v);
            } else {
                uint8_t v = snes_read(s, b);
                snes_write(s, a, v);
            }
            s->hc = h0 + 8;
            if (!fixed) d->a1t += dec ? -1 : 1;
            idx++;
        }
        d->das = 0;
    }
}

static void hdma_init(SNES *s)
{
    int ch;
    for (ch = 0; ch < 8; ch++) {
        DMACh *d = &s->dma[ch];
        d->hdma_term = 1;
        if (!(s->hdmaen & (1 << ch))) continue;
        d->a2a = d->a1t;
        d->ntrl = snes_read(s, ((uint32_t)d->a1b << 16) | d->a2a++);
        d->hdma_term = 0;
        if (d->ntrl == 0) d->hdma_term = 1;
        else if (d->dmap & 0x40) {
            d->das = snes_read(s, ((uint32_t)d->a1b << 16) | d->a2a++);
            d->das |= snes_read(s, ((uint32_t)d->a1b << 16) | d->a2a++) << 8;
        }
        d->hdma_do = 1;
    }
}

static void hdma_run(SNES *s)
{
    int ch;
    for (ch = 0; ch < 8; ch++) {
        DMACh *d = &s->dma[ch];
        if (!(s->hdmaen & (1 << ch)) || d->hdma_term) continue;
        int mode = d->dmap & 7;
        int indirect = d->dmap & 0x40;
        if (d->hdma_do) {
            int i, n = dma_len[mode];
            for (i = 0; i < n; i++) {
                uint16_t b = 0x2100 + ((d->bbad + dma_pat[mode][i & 3]) & 0xFF);
                uint32_t a;
                if (indirect) a = ((uint32_t)d->dasb << 16) | d->das++;
                else          a = ((uint32_t)d->a1b << 16) | d->a2a++;
                int32_t h0 = s->hc;
                uint8_t v = snes_read(s, a);
                snes_write(s, b, v);
                s->hc = h0 + 8;
            }
        }
        uint8_t cnt = (d->ntrl & 0x7F) - 1;
        d->ntrl = (d->ntrl & 0x80) | (cnt & 0x7F);
        d->hdma_do = d->ntrl >> 7;
        if ((d->ntrl & 0x7F) == 0) {
            d->ntrl = snes_read(s, ((uint32_t)d->a1b << 16) | d->a2a++);
            if (d->ntrl == 0) { d->hdma_term = 1; continue; }
            if (indirect) {
                d->das = snes_read(s, ((uint32_t)d->a1b << 16) | d->a2a++);
                d->das |= snes_read(s, ((uint32_t)d->a1b << 16) | d->a2a++) << 8;
            }
            d->hdma_do = 1;
        }
    }
}

/* ── APU stub (IPL handshake, then port echo) ────────────────────────── */
static void apu_write(SNES *s, int p, uint8_t v)
{
    s->apu_in[p] = v;
    if (!s->apu_running) {
        if (p == 0 && v == 0xCC) { s->apu_running = 1; s->apu_out[0] = 0xCC; }
    } else {
        s->apu_out[p] = v;
    }
}

/* ── I/O register writes ─────────────────────────────────────────────── */
static void io_write(SNES *s, uint16_t o, uint8_t v)
{
    if (o >= 0x2100 && o < 0x2140) { ppu_write(s, o, v); return; }
    if (o >= 0x2140 && o < 0x2180) { apu_write(s, o & 3, v); return; }
    switch (o) {
    case 0x2180: s->wram[s->wmadd & 0x1FFFF] = v; s->wmadd = (s->wmadd + 1) & 0x1FFFF; return;
    case 0x2181: s->wmadd = (s->wmadd & 0x1FF00) | v; return;
    case 0x2182: s->wmadd = (s->wmadd & 0x100FF) | (v << 8); return;
    case 0x2183: s->wmadd = (s->wmadd & 0x0FFFF) | ((v & 1) << 16); return;
    case 0x4016:
        s->joy_strobe = v & 1;
        if (s->joy_strobe) { s->joy_shift[0] = s->pad[0]; s->joy_shift[1] = s->pad[1]; }
        return;
    case 0x4200: {
        uint8_t old = s->nmitimen;
        s->nmitimen = v;
        if (!(old & 0x80) && (v & 0x80) && (s->rdnmi & 0x80)) s->cpu.nmi = 1;
        if (!(v & 0x30)) { s->timeup = 0; s->irq_line = 0; }
        return;
    }
    case 0x4202: s->wrmpya = v; return;
    case 0x4203: s->rdmpy = s->wrmpya * v; s->rddiv = v; return;
    case 0x4204: s->wrdiv_lo = v; return;
    case 0x4205: s->wrdiv_hi = v; return;
    case 0x4206: {
        uint16_t n = s->wrdiv_lo | (s->wrdiv_hi << 8);
        if (v) { s->rddiv = n / v; s->rdmpy = n % v; }
        else   { s->rddiv = 0xFFFF; s->rdmpy = n; }
        return;
    }
    case 0x4207: s->htime = (s->htime & 0x100) | v; return;
    case 0x4208: s->htime = (s->htime & 0x0FF) | ((v & 1) << 8); return;
    case 0x4209: s->vtime = (s->vtime & 0x100) | v; return;
    case 0x420A: s->vtime = (s->vtime & 0x0FF) | ((v & 1) << 8); return;
    case 0x420B: s->mdmaen = v; if (v) dma_run(s, v); return;
    case 0x420C: s->hdmaen = v; return;
    case 0x420D: s->memsel = v & 1; snes_rebuild_pages(s); return;
    }
    if (o >= 0x4300 && o < 0x4380) {
        DMACh *d = &s->dma[(o >> 4) & 7];
        switch (o & 0xF) {
        case 0x0: d->dmap = v; break;
        case 0x1: d->bbad = v; break;
        case 0x2: d->a1t = (d->a1t & 0xFF00) | v; break;
        case 0x3: d->a1t = (d->a1t & 0x00FF) | (v << 8); break;
        case 0x4: d->a1b = v; break;
        case 0x5: d->das = (d->das & 0xFF00) | v; break;
        case 0x6: d->das = (d->das & 0x00FF) | (v << 8); break;
        case 0x7: d->dasb = v; break;
        case 0x8: d->a2a = (d->a2a & 0xFF00) | v; break;
        case 0x9: d->a2a = (d->a2a & 0x00FF) | (v << 8); break;
        case 0xA: d->ntrl = v; break;
        default:  d->unused = v; break;
        }
    }
}

/* ── Page tables (bus fast path) ─────────────────────────────────────── */
void snes_rebuild_pages(SNES *s)
{
    int bank, pg;
    for (bank = 0; bank < 256; bank++) {
        for (pg = 0; pg < 8; pg++) {
            uint32_t o = (uint32_t)pg << 13;
            int i = (bank << 3) | pg;
            uint8_t *rp = 0, *wp = 0;
            int b7 = bank & 0x7F;

            s->spd_page[i] = speed(s, bank, o);
            if (bank == 0x7E || bank == 0x7F) {
                rp = wp = s->wram + ((bank & 1) << 16) + o;
            } else if (b7 < 0x40) {
                if (pg == 0) rp = wp = s->wram + o;
                if (!s->hirom) {
                    if (pg >= 4 && s->rom_mask)
                        rp = (uint8_t *)s->rom + ((((uint32_t)b7 * 0x8000) + (o & 0x7FFF)) & s->rom_mask);
                } else {
                    if (pg >= 4 && s->rom_mask)
                        rp = (uint8_t *)s->rom + ((((uint32_t)(bank & 0x3F) * 0x10000) + o) & s->rom_mask);
                    if (pg == 3 && b7 >= 0x20 && s->sram_mask >= 0x1FFF)
                        rp = wp = s->sram + ((((bank & 0x1F) * 0x2000u)) & s->sram_mask);
                }
            } else if (!s->hirom) {
                if (pg >= 4 && s->rom_mask)
                    rp = (uint8_t *)s->rom + ((((uint32_t)b7 * 0x8000) + (o & 0x7FFF)) & s->rom_mask);
                else if (pg < 4 && b7 >= 0x70 && b7 < 0x7E && s->sram_mask >= 0x1FFF)
                    rp = wp = s->sram + ((((bank & 0x0F) * 0x8000u) + o) & s->sram_mask);
            } else {
                if (s->rom_mask)
                    rp = (uint8_t *)s->rom + ((((uint32_t)(bank & 0x3F) * 0x10000) + o) & s->rom_mask);
            }
            s->rd_page[i] = rp;
            s->wr_page[i] = wp;
        }
    }
}

/* ── Bus ─────────────────────────────────────────────────────────────── */
uint8_t snes_read(SNES *s, uint32_t addr)
{
    uint32_t pg = addr >> 13;
    uint8_t *rp = s->rd_page[pg];
    if (rp) {
        uint8_t fv = rp[addr & 0x1FFF];
        s->hc += s->spd_page[pg];
        s->open_bus = fv;
        return fv;
    }
    uint8_t bank = addr >> 16;
    uint16_t o = addr;
    uint8_t v;
    s->hc += speed(s, bank, o);
    if (bank == 0x7E || bank == 0x7F) {
        v = s->wram[((uint32_t)(bank & 1) << 16) | o];
    } else if ((bank & 0x7F) < 0x40) {
        if (o < 0x2000)       v = s->wram[o];
        else if (o < 0x6000)  v = (o >= 0x2100) ? io_read(s, o) : s->open_bus;
        else                  v = cart_read(s, bank, o);
    } else {
        v = cart_read(s, bank, o);
    }
    s->open_bus = v;
    return v;
}

void snes_write(SNES *s, uint32_t addr, uint8_t v)
{
    uint32_t pg = addr >> 13;
    uint8_t *wp = s->wr_page[pg];
    s->wr_count++;
    if (wp) {
        s->hc += s->spd_page[pg];
        s->open_bus = v;
        wp[addr & 0x1FFF] = v;
        return;
    }
    uint8_t bank = addr >> 16;
    uint16_t o = addr;
    s->hc += speed(s, bank, o);
    s->open_bus = v;
    if (bank == 0x7E || bank == 0x7F) {
        s->wram[((uint32_t)(bank & 1) << 16) | o] = v;
    } else if ((bank & 0x7F) < 0x40) {
        if (o < 0x2000)       s->wram[o] = v;
        else if (o < 0x6000)  { if (o >= 0x2100) io_write(s, o, v); }
        else                  cart_write(s, bank, o, v);
    } else {
        cart_write(s, bank, o, v);
    }
}

/* ── Frame scheduler ─────────────────────────────────────────────────── */
/* Run the CPU until s->hc reaches target.
 *
 * Idle-loop skipping: games spend much of a frame spinning on a flag that only
 * an interrupt changes (e.g. LDA flag / BEQ -4). When a short backward jump
 * lands on the same PC with identical registers and no bus write or
 * time-varying I/O read since the last iteration, every further iteration is
 * identical, so whole iterations are skipped up to the end of this slice. The
 * machine state afterwards is exactly what executing them would give. */
static void run_cpu_to(SNES *s, int32_t target)
{
    uint32_t loop_pc = 0, snap_wr = 0, snap_vr = 0;
    int32_t snap_hc = 0;
    CPU65816 snap = s->cpu;
    uint16_t pc_prev = s->cpu.pc;
    int have = 0;

    while (s->hc < target) {
        uint16_t pc1;
        if (s->cpu.wai && !s->cpu.nmi && !s->irq_line) { s->hc = target; break; }
        cpu65816_step(s);
        pc1 = s->cpu.pc;
        /* short backward (or self) jump: possible idle loop. Sequential flow
         * gives a large wrapped difference and falls through. */
        if ((uint16_t)(pc_prev - pc1) <= 16) {
            const CPU65816 *c = &s->cpu;
            uint32_t pc24 = ((uint32_t)c->pb << 16) | pc1;
            if (have && pc24 == loop_pc && s->wr_count == snap_wr && s->vol_reads == snap_vr &&
                !c->nmi && !s->irq_line &&
                !((c->a ^ snap.a) | (c->x ^ snap.x) | (c->y ^ snap.y) | (c->s ^ snap.s) | (c->d ^ snap.d) |
                  (c->db ^ snap.db) | (c->fC ^ snap.fC) | (c->fZ ^ snap.fZ) | (c->fN ^ snap.fN) |
                  (c->fV ^ snap.fV) | (c->fI ^ snap.fI) | (c->fD ^ snap.fD) | (c->fX ^ snap.fX) |
                  (c->fM ^ snap.fM) | (c->fE ^ snap.fE))) {
                int32_t dc = s->hc - snap_hc, k;
                if (dc > 0 && (k = (target - s->hc - 1) / dc) > 0) s->hc += k * dc;
                snap_hc = s->hc;
            } else {
                loop_pc = pc24; snap = s->cpu; snap_hc = s->hc;
                snap_wr = s->wr_count; snap_vr = s->vol_reads; have = 1;
            }
        }
        pc_prev = pc1;
    }
}

static void run_line(SNES *s, int line)
{
    int32_t irq_at = 0x7FFFFFFF, render_at = 0x7FFFFFFF, next;
    int visible = line >= 1 && line <= SNES_H;
    uint8_t irqm = (s->nmitimen >> 4) & 3;

    s->line = line;
    if (line == 0) {
        s->rdnmi &= 0x7F;
        if (s->hdmaen) hdma_init(s);
    } else if (line == VBLANK_LINE) {
        s->rdnmi |= 0x80;
        if (s->nmitimen & 0x80) s->cpu.nmi = 1;
        ppu_vblank_start(s);
        if (s->nmitimen & 1) { s->joy_auto[0] = s->pad[0]; s->joy_auto[1] = s->pad[1]; }
    }

    if (irqm == 1) irq_at = s->htime * 4 + 14;
    else if (irqm == 2 && line == s->vtime) irq_at = 10;
    else if (irqm == 3 && line == s->vtime) irq_at = s->htime * 4 + 14;
    if (visible) render_at = HBLANK_START;

    for (;;) {
        next = LINE_CYCLES;
        if (irq_at < next) next = irq_at;
        if (render_at < next) next = render_at;
        run_cpu_to(s, next);
        if (next == irq_at && irq_at < LINE_CYCLES) {
            s->timeup = 1; s->irq_line = 1; irq_at = 0x7FFFFFFF;
        } else if (next == render_at) {
            if (!s->skip_render) ppu_render_line(s, line - 1);
            render_at = 0x7FFFFFFF;
        } else {
            break;
        }
    }
    if (line <= SNES_H && s->hdmaen) hdma_run(s);
    s->hc -= LINE_CYCLES;
}

void snes_run_frame(SNES *s)
{
    int line;
    for (line = 0; line < LINES; line++) run_line(s, line);
    s->frame++;
}
