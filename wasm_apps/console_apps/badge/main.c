/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief akira.badge — conference badge identity display
 *
 * Reads identity from NVS settings and shows a static badge screen:
 *   badge/handle  — callsign / hacker name  (max 16 chars)
 *   badge/name    — real name               (max 32 chars)
 *   badge/org     — organization            (max 16 chars)
 *   badge/role    — role label              (max 16 chars)
 *
 * Text is drawn with a built-in 5x7 bitmap font scaled by a cell size,
 * giving true large glyphs without relying on the two built-in font sizes.
 *
 * Controls:
 *   Y   refresh identity from settings
 *   B   exit to launcher
 */

#include "akira_api.h"

/* ── Display dimensions (set at startup via display_get_size) ─────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Colors ───────────────────────────────────────────────────────────── */
#define COL_BG     0x0000
#define COL_BORDER 0x2945
#define COL_ACCENT 0x07FF
#define COL_WHITE  0xFFFF
#define COL_GRAY   0x7BEF
#define COL_DIM    0x39E7

/* ── 5×7 bitmap font — column-major, bit0=top, covers 0x20–0x5A ───────── */
#define FONT_FIRST 0x20
#define FONT_LAST  0x5A
#define FONT_W     5
#define FONT_H     7

static const uint8_t font5x7[][FONT_W] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x00,0x08,0x14,0x22,0x41}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x41,0x22,0x14,0x08,0x00}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x04,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
};

/* ── Scaled text primitives ───────────────────────────────────────────── */
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* Pixel width of string s at cell size sz (no trailing gap on last char) */
static int tbig_w(const char *s, int sz)
{
    int n = str_len(s);
    return n > 0 ? n * (FONT_W + 1) * sz - sz : 0;
}

/* Pixel height of one line at cell size sz */
static int tbig_h(int sz) { return FONT_H * sz; }

/* Draw one glyph; return x advanced past it (including inter-char gap) */
static int draw_glyph(int x, int y, char c, int sz, uint16_t col)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if ((uint8_t)c < FONT_FIRST || (uint8_t)c > FONT_LAST) c = '?';
    const uint8_t *g = font5x7[(uint8_t)c - FONT_FIRST];
    for (int cx = 0; cx < FONT_W; cx++) {
        uint8_t bits = g[cx];
        for (int cy = 0; cy < FONT_H; cy++)
            if (bits & (1u << cy))
                display_rect(x + cx * sz, y + cy * sz, sz, sz, col);
    }
    return x + (FONT_W + 1) * sz;
}

static void draw_tbig(int x, int y, const char *s, int sz, uint16_t col)
{
    while (*s) x = draw_glyph(x, y, *s++, sz, col);
}

/* ── Badge render ─────────────────────────────────────────────────────── */
static void render(const char *handle, const char *name,
                   const char *org,    const char *role)
{
    display_clear(COL_BG);

    /* Corner accents */
    int cl = 20;
    display_rect(0,         0,         cl, 2,  COL_ACCENT);
    display_rect(0,         0,         2,  cl, COL_ACCENT);
    display_rect(SCR_W - cl,0,         cl, 2,  COL_ACCENT);
    display_rect(SCR_W - 2, 0,         2,  cl, COL_ACCENT);
    display_rect(0,         SCR_H - 2, cl, 2,  COL_ACCENT);
    display_rect(0,         SCR_H - cl,2,  cl, COL_ACCENT);
    display_rect(SCR_W - cl,SCR_H - 2, cl, 2,  COL_ACCENT);
    display_rect(SCR_W - 2, SCR_H - cl,2,  cl, COL_ACCENT);

    /* "AKIRA BADGE" dim watermark — stays small */
    display_text(SCR_W / 2 - 38, 8, "AKIRA BADGE", COL_DIM);
    display_rect(24, 22, SCR_W - 48, 1, COL_BORDER);

    /* ── Handle — cell=4, auto-shrink to fit width ─────────────────── */
    int hsz = 4;
    while (hsz > 1 && tbig_w(handle, hsz) > SCR_W - 32) hsz--;
    draw_tbig(SCR_W / 2 - tbig_w(handle, hsz) / 2, 38, handle, hsz, COL_ACCENT);

    int sep_y = 38 + tbig_h(hsz) + 8;
    display_rect(24, sep_y, SCR_W - 48, 1, COL_BORDER);

    /* ── Name — cell=3 ─────────────────────────────────────────────── */
    int cy = sep_y + 12;
    if (str_len(name) > 0) {
        draw_tbig(SCR_W / 2 - tbig_w(name, 3) / 2, cy, name, 3, COL_WHITE);
        cy += tbig_h(3) + 14;
    }

    /* ── Org — cell=3 ──────────────────────────────────────────────── */
    if (str_len(org) > 0) {
        draw_tbig(SCR_W / 2 - tbig_w(org, 3) / 2, cy, org, 3, COL_GRAY);
        cy += tbig_h(3) + 14;
    }

    /* ── Role pill — cell=3 ─────────────────────────────────────────── */
    if (str_len(role) > 0) {
        int rw = tbig_w(role, 3) + 20;
        int rh = tbig_h(3) + 10;
        int rx = SCR_W / 2 - rw / 2;
        display_rounded_rect_fill(rx, cy, rw, rh, 5, COL_ACCENT);
        draw_tbig(rx + 10, cy + 5, role, 3, COL_BG);
    }

    /* Dim hint at bottom edge */
    display_text(SCR_W / 2 - 49, SCR_H - 12, "Y:REFRESH  B:EXIT", COL_BORDER);

    display_flush();
}

/* ── Entry point ──────────────────────────────────────────────────────── */
int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    char handle[17] = "ARTUR";
    char name[33]   = "R0k3r";
    char org[17]    = "PENENGINEERING";
    char role[17]   = "CEO";

    settings_get("badge/handle", handle, sizeof(handle));
    settings_get("badge/name",   name,   sizeof(name));
    settings_get("badge/org",    org,    sizeof(org));
    settings_get("badge/role",   role,   sizeof(role));

    render(handle, name, org, role);

    uint32_t prev_btns = 0;
    while (1) {
        uint32_t btns    = (uint32_t)input_get_buttons();
        uint32_t pressed = btns & ~prev_btns;
        prev_btns = btns;

        if (pressed & AKIRA_BTN_Y) {
            settings_get("badge/handle", handle, sizeof(handle));
            settings_get("badge/name",   name,   sizeof(name));
            settings_get("badge/org",    org,    sizeof(org));
            settings_get("badge/role",   role,   sizeof(role));
            render(handle, name, org, role);
        }

        if (pressed & AKIRA_BTN_B) {
            app_switch("supervisor");
            return 0;
        }

        delay(20000);
    }

    return 0;
}
