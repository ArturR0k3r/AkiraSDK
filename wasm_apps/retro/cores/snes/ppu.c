/**
 * @file ppu.c
 * @brief SNES PPU: scanline renderer (BG modes 0-4 and 7, sprites,
 *        windows, color math, brightness).
 *
 * Not implemented: hi-res modes 5/6, mosaic, offset-per-tile, EXTBG,
 * direct color, interlace, sprite time/range-over flags.
 *
 * @license Apache-2.0
 */
#include <string.h>
#include "snes.h"

/* ── Register access ─────────────────────────────────────────────────── */
/* Inexact speed-ups (0 = bit-exact reference). 1: draw odd lines as a copy of the line above
 * (half vertical resolution). 2: ignore colour maths (no translucency). */
#ifndef SNES_APPROX
#define SNES_APPROX 2
#endif
static int pal_br = -1;     /* brightness pal565[] was built for; -1 = stale */
static uint32_t spread[256]; /* plane byte -> one bit per nibble, leftmost pixel in nibble 0 */
static int spr_dirty = 1;   /* OAM changed: decoded sprite table is stale */
static struct { uint32_t ver; uint16_t key; uint8_t ty; int8_t blank; } bc[4];   /* bg_blank() cache */

void ppu_reset(PPU *p)
{
    int i, k;
    memset(p, 0, sizeof(*p));
    pal_br = -1;
    spr_dirty = 1;
    for (i = 0; i < 4; i++) bc[i].blank = -1;
    for (i = 0; i < 256; i++) {
        uint32_t v = 0;
        for (k = 0; k < 8; k++) v |= (uint32_t)((i >> (7 - k)) & 1) << (4 * k);
        spread[i] = v;
    }
    p->inidisp = 0x80;
}

static inline uint16_t vram_xlate(PPU *p, uint16_t a)
{
    switch ((p->vmain >> 2) & 3) {
    case 1: return (a & 0xFF00) | ((a & 0xE0) >> 5) | ((a & 0x1F) << 3);
    case 2: return (a & 0xFE00) | ((a & 0x1C0) >> 6) | ((a & 0x3F) << 3);
    case 3: return (a & 0xFC00) | ((a & 0x380) >> 7) | ((a & 0x7F) << 3);
    default: return a;
    }
}

static inline uint16_t vram_inc(PPU *p)
{
    static const uint16_t inc[4] = {1, 32, 128, 128};
    return inc[p->vmain & 3];
}

static inline int16_t sext13(uint16_t v) { return (int16_t)(v << 3) >> 3; }

uint8_t ppu_read(SNES *s, uint16_t reg)
{
    PPU *p = &s->ppu;
    uint8_t v;
    switch (reg) {
    case 0x2134: return p->mpy;
    case 0x2135: return p->mpy >> 8;
    case 0x2136: return p->mpy >> 16;
    case 0x2137:
        p->hcount = s->hc / 4; p->vcount = s->line;
        p->ophct_flip = p->opvct_flip = 0;
        return s->open_bus;
    case 0x2138: {
        uint16_t a = p->oamaddr & 0x3FF;
        v = p->oam[a < 512 ? a : 512 + (a & 31)];
        p->oamaddr = (p->oamaddr + 1) & 0x3FF;
        return v;
    }
    case 0x2139:
        v = p->vram_prefetch;
        if (!(p->vmain & 0x80)) {
            p->vram_prefetch = p->vram[vram_xlate(p, p->vmaddr) & 0x7FFF];
            p->vmaddr += vram_inc(p);
        }
        return v;
    case 0x213A:
        v = p->vram_prefetch >> 8;
        if (p->vmain & 0x80) {
            p->vram_prefetch = p->vram[vram_xlate(p, p->vmaddr) & 0x7FFF];
            p->vmaddr += vram_inc(p);
        }
        return v;
    case 0x213B:
        if (!p->cg_flip) v = p->cgram[p->cgadd];
        else { v = (p->cgram[p->cgadd] >> 8) & 0x7F; p->cgadd++; }
        p->cg_flip ^= 1;
        return v;
    case 0x213C:
        v = p->ophct_flip ? (p->hcount >> 8) & 1 : p->hcount;
        p->ophct_flip ^= 1;
        return v;
    case 0x213D:
        v = p->opvct_flip ? (p->vcount >> 8) & 1 : p->vcount;
        p->opvct_flip ^= 1;
        return v;
    case 0x213E: return 0x01;
    case 0x213F: p->ophct_flip = p->opvct_flip = 0; return 0x03;
    }
    return s->open_bus;
}

void ppu_write(SNES *s, uint16_t reg, uint8_t v)
{
    PPU *p = &s->ppu;
    uint16_t a;
    switch (reg) {
    case 0x2100: p->inidisp = v; break;
    case 0x2101: p->obsel = v; break;
    case 0x2102: p->oamadd = (p->oamadd & 0x100) | v; p->oamaddr = (p->oamadd & 0x1FF) << 1; break;
    case 0x2103: p->oamadd = (p->oamadd & 0xFF) | ((v & 1) << 8); p->oamaddr = (p->oamadd & 0x1FF) << 1; break;
    case 0x2104:
        a = p->oamaddr & 0x3FF;
        if (a >= 512) { p->oam[512 + (a & 31)] = v; spr_dirty = 1; }
        else if (!(a & 1)) p->oam_latch = v;
        else { p->oam[a - 1] = p->oam_latch; p->oam[a] = v; spr_dirty = 1; }
        p->oamaddr = (p->oamaddr + 1) & 0x3FF;
        break;
    case 0x2105: p->bgmode = v; break;
    case 0x2106: p->mosaic = v; break;
    case 0x2107: case 0x2108: case 0x2109: case 0x210A: p->bgsc[reg - 0x2107] = v; break;
    case 0x210B: p->bg12nba = v; break;
    case 0x210C: p->bg34nba = v; break;
    case 0x210D: case 0x210F: case 0x2111: case 0x2113: {
        int bg = (reg - 0x210D) >> 1;
        if (bg == 0) { p->m7hofs = sext13((v << 8) | p->m7_latch); p->m7_latch = v; }
        p->hofs[bg] = (v << 8) | (p->bgofs_latch & ~7) | ((p->hofs[bg] >> 8) & 7);
        p->bgofs_latch = v;
        break;
    }
    case 0x210E: case 0x2110: case 0x2112: case 0x2114: {
        int bg = (reg - 0x210E) >> 1;
        if (bg == 0) { p->m7vofs = sext13((v << 8) | p->m7_latch); p->m7_latch = v; }
        p->vofs[bg] = (v << 8) | p->bgofs_latch;
        p->bgofs_latch = v;
        break;
    }
    case 0x2115: p->vmain = v; break;
    case 0x2116: p->vmaddr = (p->vmaddr & 0xFF00) | v; p->vram_prefetch = p->vram[vram_xlate(p, p->vmaddr) & 0x7FFF]; break;
    case 0x2117: p->vmaddr = (p->vmaddr & 0x00FF) | (v << 8); p->vram_prefetch = p->vram[vram_xlate(p, p->vmaddr) & 0x7FFF]; break;
    case 0x2118:
        a = vram_xlate(p, p->vmaddr) & 0x7FFF;
        p->vram[a] = (p->vram[a] & 0xFF00) | v;
        p->vram_ver++;
        if (!(p->vmain & 0x80)) p->vmaddr += vram_inc(p);
        break;
    case 0x2119:
        a = vram_xlate(p, p->vmaddr) & 0x7FFF;
        p->vram[a] = (p->vram[a] & 0x00FF) | (v << 8);
        p->vram_ver++;
        if (p->vmain & 0x80) p->vmaddr += vram_inc(p);
        break;
    case 0x211A: p->m7sel = v; break;
    case 0x211B: case 0x211C: case 0x211D: case 0x211E:
        p->m7[reg - 0x211B] = (v << 8) | p->m7_latch;
        p->m7_latch = v;
        if (reg == 0x211C) p->mpy = (int32_t)p->m7[0] * (int8_t)v;
        break;
    case 0x211F: p->m7x = sext13((v << 8) | p->m7_latch); p->m7_latch = v; break;
    case 0x2120: p->m7y = sext13((v << 8) | p->m7_latch); p->m7_latch = v; break;
    case 0x2121: p->cgadd = v; p->cg_flip = 0; break;
    case 0x2122:
        if (!p->cg_flip) p->cg_latch = v;
        else { p->cgram[p->cgadd] = ((v & 0x7F) << 8) | p->cg_latch; p->cgadd++; pal_br = -1; }
        p->cg_flip ^= 1;
        break;
    case 0x2123: p->w12sel = v; break;
    case 0x2124: p->w34sel = v; break;
    case 0x2125: p->wobjsel = v; break;
    case 0x2126: case 0x2127: case 0x2128: case 0x2129: p->wh[reg - 0x2126] = v; break;
    case 0x212A: p->wbglog = v; break;
    case 0x212B: p->wobjlog = v; break;
    case 0x212C: p->tm = v; break;
    case 0x212D: p->ts = v; break;
    case 0x212E: p->tmw = v; break;
    case 0x212F: p->tsw = v; break;
    case 0x2130: p->cgwsel = v; break;
    case 0x2131: p->cgadsub = v; break;
    case 0x2132:
        if (v & 0x20) p->fix_r = v & 0x1F;
        if (v & 0x40) p->fix_g = v & 0x1F;
        if (v & 0x80) p->fix_b = v & 0x1F;
        break;
    case 0x2133: p->setini = v; break;
    }
}

void ppu_vblank_start(SNES *s)
{
    PPU *p = &s->ppu;
    if (!(p->inidisp & 0x80)) p->oamaddr = (p->oamadd & 0x1FF) << 1;
}

/* ── Line buffers ────────────────────────────────────────────────────── */
/* Layer pixel: 0 = transparent, otherwise (z << 8) | CGRAM index, where z (1..12)
 * is the stacking rank of that (layer, priority) in the current mode; higher is
 * on top. Compositing a screen is then a max() over the enabled layers. */
static uint16_t lay[5][SNES_W];     /* 0-3 BG, 4 OBJ */
static uint8_t  used[5];            /* layer has an opaque pixel on this line */
static uint8_t  rank[5][4];         /* [layer][priority] -> z */
static uint8_t  zlayer[16];         /* z -> layer */
static uint8_t  wmask[6][SNES_W];   /* 1 = inside window (BG1-4, OBJ, COL) */
static uint16_t pal565[256];
static uint32_t cgx[256];      /* CGRAM expanded to 3 lanes for packed colour maths */

/* Packed colour lanes: r at bits 0-4, b at 10-14, g at 21-25; gaps hold carries. */
#define LANES 0x03E07C1Fu
#define OVF   0x04008020u
static inline uint32_t expand555(uint16_t c) { return (c & 0x7C1F) | ((uint32_t)(c & 0x03E0) << 16); }

/* ── Backgrounds ─────────────────────────────────────────────────────── */
static int render_bg(PPU *p, int bg, int y, int bpp, uint16_t *out)
{
    const uint16_t *vram = p->vram;
    int big = (p->bgmode >> (4 + bg)) & 1;
    int tsz = big ? 16 : 8;
    int sc = p->bgsc[bg];
    int wide = sc & 1, tall = sc & 2;
    int mapw = wide ? 64 : 32, maph = tall ? 64 : 32;
    uint16_t map = (sc & 0xFC) << 8;
    uint8_t nba = bg < 2 ? p->bg12nba : p->bg34nba;
    uint16_t chr = ((bg & 1) ? (nba >> 4) : (nba & 0xF)) << 12;
    int hofs = p->hofs[bg] & 0x3FF, vofs = p->vofs[bg] & 0x3FF;
    int yy = (y + vofs) & 0x3FF;
    int ty = (yy / tsz) & (maph - 1);
    int bg_off = (p->bgmode & 7) == 0 ? bg * 32 : 0;
    int nw = bpp / 2, tbytes = bpp * 4;
    int x = 0, any = 0;
    /* line constants: map row (including the 32x64 / 64x64 screen block), z by priority */
    int row_base = map + (tall ? ((ty >> 5) & 1) * (wide ? 2 : 1) * 0x400 : 0) + (ty & 31) * 32;
    int zb0 = rank[bg][0] << 8, zb1 = rank[bg][1] << 8;

    while (x < SNES_W) {
        int px = (x + hofs) & 0x3FF;
        int col8 = px >> 3, xo0 = px & 7;
        int n = 8 - xo0, k;
        int tx = (big ? col8 >> 1 : col8) & (mapw - 1);
        uint16_t e = vram[(row_base + (wide ? ((tx >> 5) & 1) * 0x400 : 0) + (tx & 31)) & 0x7FFF];
        int tile = e & 0x3FF, yo = yy & (tsz - 1);
        int pal = (e >> 10) & 7, hf = (e >> 14) & 1;
        uint16_t base, w0, w1 = 0, w2 = 0, w3 = 0;
        uint32_t rlo, rhi = 0;
        int zbits = (e & 0x2000) ? zb1 : zb0;
        if (n > SNES_W - x) n = SNES_W - x;
        if (e >> 15) yo = tsz - 1 - yo;
        if (big) tile += ((col8 & 1) ^ hf) + (yo >> 3) * 16;
        base = chr + (tile & 0x3FF) * tbytes + (yo & 7);
        w0 = vram[base & 0x7FFF];
        if (nw > 1) { w1 = vram[(base + 8) & 0x7FFF]; }
        if (nw > 2) { w2 = vram[(base + 16) & 0x7FFF]; w3 = vram[(base + 24) & 0x7FFF]; }

        /* one nibble per pixel: plane p contributes bit p of the nibble */
        rlo = spread[w0 & 255] | (spread[w0 >> 8] << 1);
        if (bpp >= 4) rlo |= (spread[w1 & 255] << 2) | (spread[w1 >> 8] << 3);
        if (bpp == 8) rhi = spread[w2 & 255] | (spread[w2 >> 8] << 1) |
                            (spread[w3 & 255] << 2) | (spread[w3 >> 8] << 3);

        if (!(rlo | rhi)) {
            for (k = 0; k < n; k++) out[x + k] = 0;
        } else if (n == 8 && !hf && bpp != 8) {
            /* full unflipped column: unrolled, palette base hoisted */
            int zp = zbits | (bpp == 2 ? bg_off + pal * 4 : pal * 16), c;
#define PXL(K) c = (rlo >> (4 * (K))) & 15; out[x + (K)] = c ? zp + c : 0;
            PXL(0) PXL(1) PXL(2) PXL(3) PXL(4) PXL(5) PXL(6) PXL(7)
#undef PXL
            any = 1;
        } else {
            for (k = 0; k < n; k++) {
                int xo = xo0 + k, c, sh;
                if (hf) xo = 7 - xo;
                sh = xo * 4;
                c = (rlo >> sh) & 15;
                if (bpp == 8) c |= ((rhi >> sh) & 15) << 4;
                if (c) {
                    out[x + k] = zbits | (bpp == 2 ? bg_off + pal * 4 + c : bpp == 4 ? pal * 16 + c : c);
                    any = 1;
                } else out[x + k] = 0;
            }
        }
        x += n;
    }
    return any;
}

/* Is every tile referenced by this BG's map row completely empty (all planes, all 8 rows)?
 * Cached per BG and tile row; any VRAM write or a change of map/char/size settings invalidates
 * it. Lets a mostly blank layer (e.g. the HUD layer) cost almost nothing on most lines. */
static int bg_blank(PPU *p, int bg, int y, int bpp)
{
    int big = (p->bgmode >> (4 + bg)) & 1;
    int tsz = big ? 16 : 8;
    int sc = p->bgsc[bg];
    int wide = sc & 1, tall = sc & 2;
    uint8_t nba = bg < 2 ? p->bg12nba : p->bg34nba;
    int vofs = p->vofs[bg] & 0x3FF;
    int ty = (((y + vofs) & 0x3FF) / tsz) & (tall ? 63 : 31);
    uint16_t key = (uint16_t)(sc | ((bg & 1 ? nba >> 4 : nba & 15) << 8) | (big << 12) | ((bpp >> 2) << 13));
    int tx, mapw = wide ? 64 : 32, tbytes = bpp * 4;
    uint16_t map = (sc & 0xFC) << 8, chr = ((bg & 1) ? (nba >> 4) : (nba & 0xF)) << 12;
    int row_base = map + (tall ? ((ty >> 5) & 1) * (wide ? 2 : 1) * 0x400 : 0) + (ty & 31) * 32;

    if (bc[bg].blank >= 0 && bc[bg].ver == p->vram_ver && bc[bg].key == key && bc[bg].ty == ty) return bc[bg].blank;

    for (tx = 0; tx < mapw; tx++) {
        uint16_t e = p->vram[(row_base + (wide ? ((tx >> 5) & 1) * 0x400 : 0) + (tx & 31)) & 0x7FFF];
        int tile = e & 0x3FF, nt = big ? 4 : 1, q, w;
        for (q = 0; q < nt; q++) {
            int tn = (tile + (q & 1) + (q >> 1) * 16) & 0x3FF;
            uint16_t base = chr + tn * tbytes;
            for (w = 0; w < tbytes; w++)
                if (p->vram[(base + w) & 0x7FFF]) goto nonblank;
        }
    }
    bc[bg].ver = p->vram_ver; bc[bg].key = key; bc[bg].ty = (uint8_t)ty; bc[bg].blank = 1;
    return 1;
nonblank:
    bc[bg].ver = p->vram_ver; bc[bg].key = key; bc[bg].ty = (uint8_t)ty; bc[bg].blank = 0;
    return 0;
}

static int render_mode7(PPU *p, int y, uint16_t *out)
{
    int hoff = p->m7hofs - p->m7x, voff = p->m7vofs - p->m7y;
    int yy = (p->m7sel & 2) ? 255 - y : y;
    int32_t A = p->m7[0], B = p->m7[1], C = p->m7[2], D = p->m7[3];
    int32_t ox, oy, x;
    int any = 0, zbits = rank[0][0] << 8;
    hoff = (hoff & 0x2000) ? (hoff | ~1023) : (hoff & 1023);
    voff = (voff & 0x2000) ? (voff | ~1023) : (voff & 1023);
    ox = ((A * hoff) & ~63) + ((B * voff) & ~63) + ((B * yy) & ~63) + (p->m7x << 8);
    oy = ((C * hoff) & ~63) + ((D * voff) & ~63) + ((D * yy) & ~63) + (p->m7y << 8);
    for (x = 0; x < SNES_W; x++) {
        int xx = (p->m7sel & 1) ? 255 - x : x;
        int32_t px = (ox + A * xx) >> 8, py = (oy + C * xx) >> 8;
        int tile, c, outside = ((px | py) & ~1023) != 0;
        if (outside && (p->m7sel >> 6) == 2) { out[x] = 0; continue; }
        if (outside && (p->m7sel >> 6) == 3) tile = 0;
        else tile = p->vram[((py >> 3) & 127) * 128 + ((px >> 3) & 127)] & 0xFF;
        c = p->vram[(tile * 64 + (py & 7) * 8 + (px & 7)) & 0x7FFF] >> 8;
        if (c) { out[x] = zbits | c; any = 1; } else out[x] = 0;
    }
    return any;
}

/* ── Sprites ─────────────────────────────────────────────────────────── */
static int render_obj(PPU *p, int y)
{
    static const uint8_t sizes[8][2] = {{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
    static uint8_t  spr_y1[128], spr_sz[128];
    static int16_t  spr_x[128];
    static uint8_t  spr_obsel = 0xFF;
    uint16_t *out = lay[4];
    int list[32], n = 0, i, k, any = 0;
    int nsz[2] = { sizes[p->obsel >> 5][0], sizes[p->obsel >> 5][1] };
    uint16_t base0 = (p->obsel & 7) << 13;
    uint16_t base1 = base0 + ((((p->obsel >> 3) & 3) + 1) << 12);

    if (spr_dirty || spr_obsel != (p->obsel >> 5)) {
        for (i = 0; i < 128; i++) {
            uint8_t hi = p->oam[512 + (i >> 2)] >> ((i & 3) * 2);
            int x = p->oam[i * 4] | ((hi & 1) << 8);
            spr_y1[i] = (uint8_t)(p->oam[i * 4 + 1] + 1);
            spr_sz[i] = (uint8_t)nsz[(hi >> 1) & 1];
            spr_x[i] = (int16_t)(x >= 256 ? x - 512 : x);
        }
        spr_dirty = 0;
        spr_obsel = p->obsel >> 5;
    }

    memset(out, 0, SNES_W * sizeof(uint16_t));
    for (i = 0; i < 128 && n < 32; i++) {
        int sz = spr_sz[i], x = spr_x[i];
        if (((y - spr_y1[i]) & 0xFF) >= sz) continue;
        if (x + sz <= 0 || x >= 256) continue;
        list[n++] = i;
    }
    for (k = n - 1; k >= 0; k--) {
        int o = list[k];
        uint8_t hi = p->oam[512 + (o >> 2)] >> ((o & 3) * 2);
        uint8_t tile = p->oam[o * 4 + 2], attr = p->oam[o * 4 + 3];
        int sz = nsz[(hi >> 1) & 1];
        int x = p->oam[o * 4] | ((hi & 1) << 8);
        int row = (y - (p->oam[o * 4 + 1] + 1)) & 0xFF;
        int hf = (attr >> 6) & 1, vf = attr >> 7;
        int pal = (attr >> 1) & 7, prio = (attr >> 4) & 3;
        int zbits = rank[4][prio] << 8;
        uint16_t base = (attr & 1) ? base1 : base0;
        int tc;
        if (x >= 256) x -= 512;
        if (vf) row = sz - 1 - row;
        for (tc = 0; tc < sz / 8; tc++) {
            int tcol = hf ? (sz / 8 - 1 - tc) : tc;
            int tn = ((tile + ((row >> 3) << 4)) & 0xF0) | ((tile + tcol) & 0x0F);
            uint16_t addr = base + tn * 16 + (row & 7);
            uint16_t w0 = p->vram[addr & 0x7FFF], w1 = p->vram[(addr + 8) & 0x7FFF];
            int px;
            if (!(w0 | w1)) continue;
            for (px = 0; px < 8; px++) {
                int sx = x + tc * 8 + px;
                int b = 7 - (hf ? 7 - px : px);
                int c;
                if (sx < 0 || sx >= 256) continue;
                c = ((w0 >> b) & 1) | (((w0 >> (8 + b)) & 1) << 1) |
                    (((w1 >> b) & 1) << 2) | (((w1 >> (8 + b)) & 1) << 3);
                if (c) { out[sx] = zbits | (128 + pal * 16 + c); any = 1; }
            }
        }
    }
    return any;
}

/* ── Windows ─────────────────────────────────────────────────────────── */
static void make_window(PPU *p, int nib, int logic, uint8_t *out)
{
    int e1 = (nib >> 1) & 1, i1 = nib & 1, e2 = (nib >> 3) & 1, i2 = (nib >> 2) & 1;
    int x;
    if (!e1 && !e2) { memset(out, 0, SNES_W); return; }
    for (x = 0; x < SNES_W; x++) {
        int a = (x >= p->wh[0] && x <= p->wh[1]) ^ i1;
        int b = (x >= p->wh[2] && x <= p->wh[3]) ^ i2;
        int r;
        if (e1 && !e2) r = a;
        else if (!e1 && e2) r = b;
        else switch (logic) {
            case 0: r = a | b; break;
            case 1: r = a & b; break;
            case 2: r = a ^ b; break;
            default: r = !(a ^ b); break;
        }
        out[x] = r;
    }
}

/* ── Composition ─────────────────────────────────────────────────────── */
typedef struct { uint8_t layer, prio; } Ent;   /* layer 0-3 BG, 4 OBJ */

static void setup_rank(PPU *p)
{
    Ent e[12];
    int mode = p->bgmode & 7, n = 0, i;
#define ADD(l, pr) do { e[n].layer = (l); e[n].prio = (pr); n++; } while (0)
    switch (mode) {
    case 0:
        ADD(4,3); ADD(0,1); ADD(1,1); ADD(4,2); ADD(0,0); ADD(1,0);
        ADD(4,1); ADD(2,1); ADD(3,1); ADD(4,0); ADD(2,0); ADD(3,0);
        break;
    case 1:
        if (p->bgmode & 8) ADD(2,1);
        ADD(4,3); ADD(0,1); ADD(1,1); ADD(4,2); ADD(0,0); ADD(1,0);
        ADD(4,1);
        if (!(p->bgmode & 8)) ADD(2,1);
        ADD(4,0); ADD(2,0);
        break;
    case 7:
        ADD(4,3); ADD(4,2); ADD(4,1); ADD(0,0); ADD(4,0);
        break;
    default:
        ADD(4,3); ADD(0,1); ADD(4,2); ADD(1,1); ADD(4,1); ADD(0,0); ADD(4,0); ADD(1,0);
        break;
    }
#undef ADD
    memset(rank, 0, sizeof(rank));
    memset(zlayer, 0, sizeof(zlayer));
    for (i = 0; i < n; i++) {
        rank[e[i].layer][e[i].prio] = n - i;
        zlayer[n - i] = e[i].layer;
    }
}

/* out[x] = topmost enabled layer pixel (z<<8 | CGRAM index), 0 = backdrop. */
static void compose(uint8_t mask, uint8_t wmsk, uint16_t *out)
{
    static uint16_t tmp[5][SNES_W];
    const uint16_t *src[5];
    int ns = 0, l, x, i;

    for (l = 0; l < 5; l++) {
        if (!((mask >> l) & 1) || !used[l]) continue;
        if ((wmsk >> l) & 1) {
            for (x = 0; x < SNES_W; x++) tmp[l][x] = wmask[l][x] ? 0 : lay[l][x];
            src[ns++] = tmp[l];
        } else src[ns++] = lay[l];
    }
    if (ns == 0) { memset(out, 0, SNES_W * sizeof(uint16_t)); return; }
    if (ns == 1) { memcpy(out, src[0], SNES_W * sizeof(uint16_t)); return; }
    for (x = 0; x < SNES_W; x++) {
        uint16_t m = src[0][x];
        for (i = 1; i < ns; i++) if (src[i][x] > m) m = src[i][x];
        out[x] = m;
    }
}

static inline uint16_t to565(int r, int g, int b)
{
    uint16_t c = (r << 11) | (((g << 1) | (g >> 4)) << 5) | b;
#ifdef SNES_SWAP565
    c = (c >> 8) | (c << 8);
#endif
    return c;
}

static void build_pal565(PPU *p, int br)
{
    int i;
    for (i = 0; i < 256; i++) {
        int r = p->cgram[i] & 31, g = (p->cgram[i] >> 5) & 31, b = (p->cgram[i] >> 10) & 31;
        if (br != 15) { r = r * br / 15; g = g * br / 15; b = b * br / 15; }
        pal565[i] = to565(r, g, b);
        cgx[i] = expand555(p->cgram[i]);
    }
    pal_br = br;
}

void ppu_render_line(SNES *s, int y)
{
    static uint16_t mainz[SNES_W], subz[SNES_W];
    PPU *p = &s->ppu;
    uint16_t *dst = &s->fb[y * SNES_W];
    int mode = p->bgmode & 7;
    uint8_t layers = p->tm | p->ts;
    int x, bg, br = p->inidisp & 15, complex_, cw_active, sub_ready;
    uint32_t fixx;

#if SNES_APPROX & 1
    if (y & 1) { memcpy(dst, dst - SNES_W, SNES_W * 2); return; }
#endif
    if (p->inidisp & 0x80) { memset(dst, 0, SNES_W * 2); return; }

    setup_rank(p);
    memset(used, 0, sizeof(used));
    if (mode == 7) {
        if (layers & 1) used[0] = render_mode7(p, y, lay[0]);
    } else if (mode <= 4) {
        static const uint8_t bpp_tab[5][4] = {{2,2,2,2},{4,4,2,0},{4,4,0,0},{8,4,0,0},{8,2,0,0}};
        for (bg = 0; bg < 4; bg++)
            if (((layers >> bg) & 1) && bpp_tab[mode][bg])
                used[bg] = bg_blank(p, bg, y, bpp_tab[mode][bg]) ? 0 : render_bg(p, bg, y, bpp_tab[mode][bg], lay[bg]);
    }
    if (layers & 0x10) used[4] = render_obj(p, y);

    {
        uint8_t wm = p->tmw | p->tsw;
        if (wm & 1) make_window(p, p->w12sel & 0xF, p->wbglog & 3, wmask[0]);
        if (wm & 2) make_window(p, p->w12sel >> 4, (p->wbglog >> 2) & 3, wmask[1]);
        if (wm & 4) make_window(p, p->w34sel & 0xF, (p->wbglog >> 4) & 3, wmask[2]);
        if (wm & 8) make_window(p, p->w34sel >> 4, (p->wbglog >> 6) & 3, wmask[3]);
        if (wm & 16) make_window(p, p->wobjsel & 0xF, p->wobjlog & 3, wmask[4]);
    }

    compose(p->tm, p->tmw, mainz);
    if (pal_br != br) build_pal565(p, br);

    complex_ = (p->cgadsub & 0x3F) || (p->cgwsel & 0xC0);
#if SNES_APPROX & 2
    complex_ = 0;
#endif
    if (!complex_) {
        for (x = 0; x < SNES_W; x++) dst[x] = pal565[mainz[x] & 0xFF];
        return;
    }

    /* Colour window only matters when clipping or prevention is configured. */
    {
        int clip = (p->cgwsel >> 6) & 3, prevent = (p->cgwsel >> 4) & 3;
        if ((p->wobjsel >> 4) & 0x0A)               /* colour window enabled: varies per pixel */
            cw_active = clip || prevent;
        else                                        /* disabled: mask is 0 everywhere */
            cw_active = clip == 1 || clip == 3 || prevent == 1 || prevent == 3;
    }
    if (cw_active) make_window(p, p->wobjsel >> 4, (p->wobjlog >> 2) & 3, wmask[5]);
    sub_ready = 0;
    fixx = expand555((uint16_t)(p->fix_r | (p->fix_g << 5) | (p->fix_b << 10)));
    {
        /* per-line invariants in locals: stores to dst could alias struct fields, so the
         * compiler would otherwise reload them for every pixel */
        const uint8_t cadd = p->cgadsub;
        const int use_sub = (p->cgwsel & 2) != 0;
        const int sub_op = (cadd & 0x80) != 0;
        const int halve_flag = (cadd >> 6) & 1;
        const int clip = (p->cgwsel >> 6) & 3, prevent = (p->cgwsel >> 4) & 3;
        const uint8_t ts = p->ts, tsw = p->tsw;

        for (x = 0; x < SNES_W; x++) {
            uint16_t m = mainz[x];
            int ml = m ? zlayer[m >> 8] : 5;
            int inmask = ((cadd >> ml) & 1) && (ml != 4 || (m & 0xFF) >= 192);
            int do_clip = 0, do_prev = 0;
            uint32_t a, v;

            if (!cw_active) {
                /* No clip/prevent window and this layer is not in the math mask: plain colour. */
                if (!inmask) { dst[x] = pal565[m & 0xFF]; continue; }
            } else {
                int cw = wmask[5][x];
                do_clip = clip == 3 || (clip == 2 && cw) || (clip == 1 && !cw);
                do_prev = prevent == 3 || (prevent == 2 && cw) || (prevent == 1 && !cw);
            }
            if (!sub_ready && use_sub) { compose(ts, tsw, subz); sub_ready = 1; }

            a = do_clip ? 0 : cgx[m & 0xFF];
            if (!do_prev && inmask) {
                uint32_t b, t2;
                int halve = halve_flag && !do_clip;
                if (use_sub) {
                    uint16_t sv = subz[x];
                    if (!sv) {
                        /* transparent sub-screen pixel + black fixed colour: maths changes nothing */
                        if (!fixx && !do_clip) { dst[x] = pal565[m & 0xFF]; continue; }
                        b = fixx; halve = 0;
                    } else b = cgx[sv & 0xFF];
                } else b = fixx;
                if (sub_op) {                                  /* subtract, floor at 0 */
                    v = (a | OVF) - b;
                    t2 = v & OVF;
                    v &= t2 - (t2 >> 5);
                    if (halve) v = (v >> 1) & LANES;
                } else if (halve) {                            /* (a+b)/2 never overflows a lane */
                    v = ((a + b) >> 1) & LANES;
                } else {                                       /* add, saturate at 31 */
                    v = a + b;
                    t2 = v & OVF;
                    v = (v | (t2 - (t2 >> 5))) & LANES;
                }
                a = v;
            }
            {
                int r = a & 31, g = (a >> 21) & 31, bl = (a >> 10) & 31;
                if (br != 15) { r = r * br / 15; g = g * br / 15; bl = bl * br / 15; }
                dst[x] = to565(r, g, bl);
            }
        }
    }
}
