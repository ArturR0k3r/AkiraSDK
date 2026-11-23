/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file akira_spr.h
 * @brief Indexed pixel-art sprites for AkiraConsole games (header-only).
 *
 * Art is written one character per pixel ('.' = clear) and expanded once at
 * start-up into RGB565 caches drawn with display_bitmap_transparent() — one
 * native call per sprite.  Every palette entry also carries a black/white
 * value, so the same art renders on the 1-bit 400x240 Sharp panel (where any
 * non-zero pixel is white).
 *
 * Palette: the fixed letters below plus any characters a game defines with
 * spr_pal_set() (digits are a good choice for per-level "slot" colours).
 *
 * Include once, from the app's single translation unit, after akira_api.h
 * and akira_game.h.
 */

#ifndef AKIRA_SPR_H
#define AKIRA_SPR_H

#include "akira_api.h"

#define SPR_KEY 0x0821u /* transparent colour key */
#define SPR_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static int spr_mono;            /* 1 on the 1-bit Sharp panel */
static uint16_t spr__pal[128];
static uint8_t spr__bw[128];

typedef struct { char ch; uint8_t r, g, b; } spr_pal_t;

/* The shared fixed palette (same letters as Pixel Dungeon). */
static const spr_pal_t SPR_PAL[] = {
    {'k', 0, 0, 0},       {'K', 40, 36, 48},    {'d', 84, 80, 96},
    {'g', 140, 136, 150}, {'G', 196, 192, 204}, {'w', 255, 255, 255},
    {'r', 222, 48, 48},   {'R', 128, 22, 34},   {'p', 255, 140, 160},
    {'o', 250, 146, 40},  {'O', 160, 82, 30},   {'y', 255, 222, 70},
    {'Y', 186, 138, 24},  {'n', 146, 94, 52},   {'N', 82, 50, 30},
    {'s', 250, 204, 164}, {'S', 200, 142, 110}, {'l', 160, 234, 90},
    {'e', 62, 170, 72},   {'E', 30, 92, 52},    {'c', 130, 232, 255},
    {'t', 40, 150, 172},  {'b', 74, 116, 232},  {'B', 40, 52, 132},
    {'u', 176, 96, 232},  {'U', 92, 42, 132},   {'m', 240, 86, 200},
    {'v', 240, 232, 210}, {'V', 176, 166, 138}, {'z', 176, 186, 206},
    {'Z', 100, 110, 132}, {'i', 206, 238, 255},
};

static inline int spr_lum(int r, int g, int b) { return (r * 77 + g * 150 + b * 29) >> 8; }

/** Define (or redefine) a palette character.  mono: 1 white, 0 black. */
static inline void spr_pal_set(char ch, int r, int g, int b, int mono)
{
    uint16_t c = SPR_RGB(r, g, b);
    if (c == SPR_KEY) c ^= 0x20;
    spr__pal[(uint8_t)ch & 127] = c;
    spr__bw[(uint8_t)ch & 127] = (uint8_t)mono;
}
/** Same, with the black/white value taken from luminance. */
static inline void spr_pal_rgb(char ch, int r, int g, int b)
{
    spr_pal_set(ch, r, g, b, spr_lum(r, g, b) >= 100);
}

static inline void spr_init(int mono)
{
    spr_mono = mono;
    for (unsigned i = 0; i < sizeof(SPR_PAL) / sizeof(SPR_PAL[0]); i++)
        spr_pal_rgb(SPR_PAL[i].ch, SPR_PAL[i].r, SPR_PAL[i].g, SPR_PAL[i].b);
}

/** A UI/drawing colour that degrades sensibly on the 1-bit panel. */
static inline uint16_t spr_col(int r, int g, int b)
{
    if (spr_mono) return spr_lum(r, g, b) >= 100 ? 0xFFFF : 0x0000;
    uint16_t c = SPR_RGB(r, g, b);
    return c == SPR_KEY ? (uint16_t)(c ^ 0x20) : c;
}

static inline uint16_t spr_px(char ch)
{
    uint8_t u = (uint8_t)ch & 127;
    if (spr_mono) return spr__bw[u] ? 0xFFFF : 0x0000;
    return spr__pal[u];
}

/** Expand @p n pixels of art; '.' becomes SPR_KEY or the pixel of @p under. */
static inline void spr_build(uint16_t *dst, const char *src, int n, const uint16_t *under)
{
    for (int i = 0; i < n; i++)
        dst[i] = (src[i] == '.') ? (under ? under[i] : SPR_KEY) : spr_px(src[i]);
}

/** Horizontal mirror of a w x h sprite (for facing left/right). */
static inline void spr_mirror(uint16_t *dst, const uint16_t *src, int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) dst[y * w + x] = src[y * w + (w - 1 - x)];
}

static inline void spr_blit(int x, int y, int w, int h, const uint16_t *px)
{
    display_bitmap_transparent(x, y, w, h, px, (uint32_t)(w * h * 2), SPR_KEY);
}
static inline void spr_blit16(int x, int y, const uint16_t *px) { spr_blit(x, y, 16, 16, px); }
static inline void spr_blit_opaque16(int x, int y, const uint16_t *px)
{
    display_bitmap(x, y, 16, 16, px, 512);
}

/** Integer blow-up through a caller scratch buffer of (n*s)^2 pixels. */
static inline void spr_blit_scaled(int x, int y, const uint16_t *src, int w, int h, int s,
                                   uint16_t *scratch)
{
    int W = w * s, H = h * s;
    for (int yy = 0; yy < H; yy++)
        for (int xx = 0; xx < W; xx++) scratch[yy * W + xx] = src[(yy / s) * w + xx / s];
    display_bitmap_transparent(x, y, W, H, scratch, (uint32_t)(W * H * 2), SPR_KEY);
}

/** Solid-colour silhouette (hit flash, shadows, unknown fish). */
static inline void spr_blit_tint(int x, int y, const uint16_t *src, int w, int h, uint16_t col,
                                 uint16_t *scratch)
{
    for (int i = 0; i < w * h; i++) scratch[i] = (src[i] == SPR_KEY) ? SPR_KEY : col;
    display_bitmap_transparent(x, y, w, h, scratch, (uint32_t)(w * h * 2), SPR_KEY);
}

/** 16x16 ordered-dither veil: pct of the pixels set to @p col. */
static inline void spr_dither16(uint16_t *dst, int pct, uint16_t col)
{
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int t = pct * 16 / 100;
    for (int i = 0; i < 256; i++)
        dst[i] = bayer[((i >> 4) & 3) * 4 + (i & 3)] < t ? col : SPR_KEY;
}

/* ── Text with a 1px black outline: readable over any art ───────────── */
static inline void spr_text_out(int x, int y, const char *s, uint16_t c)
{
    display_text(x - 1, y, s, 0x0000); display_text(x + 1, y, s, 0x0000);
    display_text(x, y - 1, s, 0x0000); display_text(x, y + 1, s, 0x0000);
    display_text(x, y, s, c);
}

/* ── Game UI theme (colour; collapses to black/white on the Sharp) ──── */
typedef struct {
    uint16_t panel, edge, accent, text, dim, sel, seltext, barbg;
    uint16_t red, green, yellow, cyan, orange, violet, gold;
} spr_theme_t;
static spr_theme_t SPRT;

static inline void spr_theme_init(int ar, int ag, int ab)
{
    if (spr_mono) {
        SPRT.panel = SPRT.barbg = 0x0000;
        SPRT.edge = SPRT.accent = SPRT.text = SPRT.dim = SPRT.sel = 0xFFFF;
        SPRT.seltext = 0x0000;
        SPRT.red = SPRT.green = SPRT.yellow = SPRT.cyan = SPRT.orange = SPRT.violet = SPRT.gold = 0xFFFF;
        return;
    }
    SPRT.panel = SPR_RGB(18, 16, 30);   SPRT.edge = SPR_RGB(110, 96, 140);
    SPRT.accent = SPR_RGB(ar, ag, ab);  SPRT.text = 0xFFFF;
    SPRT.dim = SPR_RGB(150, 146, 170);  SPRT.sel = SPR_RGB(64, 52, 110);
    SPRT.seltext = 0xFFFF;              SPRT.barbg = SPR_RGB(40, 34, 52);
    SPRT.red = SPR_RGB(255, 70, 70);    SPRT.green = SPR_RGB(110, 230, 100);
    SPRT.yellow = SPR_RGB(255, 222, 80); SPRT.cyan = SPR_RGB(110, 220, 255);
    SPRT.orange = SPR_RGB(255, 150, 50); SPRT.violet = SPR_RGB(200, 130, 255);
    SPRT.gold = SPR_RGB(255, 200, 60);
}

static inline void spr_panel(int x, int y, int w, int h, const char *title)
{
    if (!spr_mono) display_rounded_rect_fill(x + 3, y + 3, w, h, 6, 0x0000);
    display_rounded_rect_fill(x, y, w, h, 6, SPRT.panel);
    display_rounded_rect(x, y, w, h, 6, SPRT.accent);
    if (title) {
        int n = strlen(title), tw = n ? n * 12 - 1 : 0;
        display_text_large(x + w / 2 - tw / 2, y + 6, title, SPRT.yellow);
        display_hline(x + 8, y + 27, w - 16, SPRT.edge);
    }
}

static inline void spr_sel_row(int x, int y, int w, int h)
{
    display_rounded_rect_fill(x, y, w, h, 4, SPRT.sel);
    if (!spr_mono) display_rounded_rect(x, y, w, h, 4, SPRT.accent);
}

static inline void spr_bar(int x, int y, int w, int h, int v, int max, uint16_t col)
{
    int fw = max > 0 ? w * (v < 0 ? 0 : (v > max ? max : v)) / max : 0;
    display_rect(x, y, w, h, SPRT.barbg);
    if (fw > 0) {
        display_rect(x, y, fw, h, col);
        if (!spr_mono && h > 3) display_hline(x, y, fw, 0xFFFF);
    }
    display_rect_outline(x - 1, y - 1, w + 2, h + 2, spr_mono ? 0xFFFF : 0x0000);
}

#endif /* AKIRA_SPR_H */
