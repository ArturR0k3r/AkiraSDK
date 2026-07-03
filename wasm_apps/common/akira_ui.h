/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file akira_ui.h
 * @brief AkiraConsole shared UI kit — WASM-app implementation (header-only).
 *
 * WASM-side mirror of the native src/console_shell/ui/akira_ui.h. Same design
 * system, same four primitives (outline < solid fill < dither < 3px heavy
 * stroke), same strict 1-bit palette (AKIRA_UI_INK / AKIRA_UI_PAPER only).
 *
 * Header-only + `static inline` on purpose: the console SDK's akira_api.h
 * *defines* non-static helpers (printf, itoa), so it may only be pulled into a
 * single translation unit per app. Apps are single-file (main.c); including
 * this header there compiles the kit inline with zero duplicate symbols.
 *
 * Built against the v1.6.x console API (display_* / input_get_buttons / delay,
 * akira_console.h buttons + geometry). Icons use the akira_icons.h format:
 * MSB-first, row-major uint8_t arrays, (w+7)/8 bytes per row.
 *
 * Usage:
 *   #include "akira_api.h"
 *   #include "akira_ui.h"
 */

#ifndef AKIRA_UI_WASM_H
#define AKIRA_UI_WASM_H

#include <stdint.h>
#include <stdbool.h>

#include "akira_api.h"     /* provides strlen(), itoa(), display_*, input_* */
#include "akira_console.h" /* CONSOLE_WIDTH/HEIGHT, AKIRA_BTN_* masks        */

/* The only two colours the kit permits (RGB565; black/white on the colour
 * console, on/off on the 1-bit Sharp LCD). */
#define AKIRA_UI_INK   0x0000u
#define AKIRA_UI_PAPER 0xFFFFu

#define AKIRA_UI_CELL_W 8
#define AKIRA_UI_CELL_H 13
#define AKIRA_UI_STATUSBAR_H 20 /* matches the console header/footer height */

typedef enum {
    AKIRA_UI_SOLID,
    AKIRA_UI_TRANSPARENT,
    AKIRA_UI_DITHER,
} akira_ui_fill_t;

typedef struct {
    const char    *title;
    const char    *clock;
    int            battery_pct;
    bool           show_wifi;
    bool           wifi_on;
    bool           show_bt;
    bool           bt_on;
    const uint8_t *icon_wifi; /* akira_icons.h format, or NULL */
    const uint8_t *icon_bt;
} akira_ui_status_t;

/* ---- internal helpers ------------------------------------------------- */

static inline int akira_ui__tw(const char *s)
{
    return (int)strlen(s) * AKIRA_UI_CELL_W;
}

static inline int akira_ui__strcpy(char *dst, const char *src)
{
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return i;
}

static inline void akira_ui__size(int *w, int *h)
{
    int32_t ww = CONSOLE_WIDTH, hh = CONSOLE_HEIGHT;
    display_get_size(&ww, &hh);
    if (ww <= 0) ww = CONSOLE_WIDTH;
    if (hh <= 0) hh = CONSOLE_HEIGHT;
    *w = ww;
    *h = hh;
}

static inline void akira_ui__dither(int x, int y, int w, int h, uint16_t color)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (((i + j) & 1) == 0) {
                display_pixel(x + i, y + j, color);
            }
        }
    }
}

/* ---- icon blit (akira_icons.h format: MSB-first uint8_t rows) --------- */

static inline void akira_ui_icon_1bpp(int x, int y, int scale, const uint8_t *icon,
                                      int w, int rows, uint16_t fg, uint16_t bg,
                                      akira_ui_fill_t fill)
{
    if (!icon || scale < 1) {
        return;
    }
    int stride = (w + 7) / 8;
    for (int r = 0; r < rows; r++) {
        const uint8_t *row = icon + r * stride;
        for (int c = 0; c < w; c++) {
            bool set = (row[c >> 3] & (0x80u >> (c & 7))) != 0;
            int px = x + c * scale;
            int py = y + r * scale;

            switch (fill) {
            case AKIRA_UI_SOLID:
                display_rect(px, py, scale, scale, set ? fg : bg);
                break;
            case AKIRA_UI_TRANSPARENT:
                if (set) {
                    display_rect(px, py, scale, scale, fg);
                }
                break;
            case AKIRA_UI_DITHER:
                if (set && ((c + r) & 1) == 0) {
                    display_rect(px, py, scale, scale, fg);
                } else {
                    display_rect(px, py, scale, scale, bg);
                }
                break;
            }
        }
    }
}

/* ---- status bar ------------------------------------------------------- */

static inline void akira_ui_status_bar(const akira_ui_status_t *s)
{
    int W, H;
    akira_ui__size(&W, &H);
    const int bar_h = AKIRA_UI_STATUSBAR_H;
    int txt_y = (bar_h - 10) / 2;
    if (txt_y < 0) txt_y = 0;

    display_rect(0, 0, W, bar_h, AKIRA_UI_PAPER);

    if (s->title && s->title[0]) {
        display_text(6, txt_y, s->title, AKIRA_UI_INK);
    } else if (s->battery_pct >= 0) {
        int by = txt_y;
        display_rect_outline(4, by, 22, 10, AKIRA_UI_INK);
        display_rect(26, by + 3, 2, 4, AKIRA_UI_INK);
        int pct = s->battery_pct > 100 ? 100 : s->battery_pct;
        int segs = (pct * 5 + 50) / 100;
        for (int i = 0; i < 5; i++) {
            display_rect(6 + i * 4, by + 2, 3, 6,
                         (i < segs) ? AKIRA_UI_INK : AKIRA_UI_PAPER);
        }
        char buf[8];
        itoa(pct, buf);
        int bl = (int)strlen(buf);
        buf[bl] = '%';
        buf[bl + 1] = '\0';
        display_text(30, txt_y, buf, AKIRA_UI_INK);
    }

    int right = W - 4;
    if (s->clock && s->clock[0]) {
        display_text(W - akira_ui__tw(s->clock) - 4, txt_y, s->clock, AKIRA_UI_INK);
        right = W - akira_ui__tw(s->clock) - 8;
    }
    int icon_y = (bar_h - 16) / 2;
    if (icon_y < 0) icon_y = 0;
    if (s->show_bt && s->icon_bt) {
        right -= 24;
        akira_ui_icon_1bpp(right, icon_y, 1, s->icon_bt, 24, 16,
                           AKIRA_UI_INK, AKIRA_UI_PAPER,
                           s->bt_on ? AKIRA_UI_TRANSPARENT : AKIRA_UI_DITHER);
    }
    if (s->show_wifi && s->icon_wifi) {
        right -= 24;
        akira_ui_icon_1bpp(right, icon_y, 1, s->icon_wifi, 24, 16,
                           AKIRA_UI_INK, AKIRA_UI_PAPER,
                           s->wifi_on ? AKIRA_UI_TRANSPARENT : AKIRA_UI_DITHER);
    }
}

/* ---- list row --------------------------------------------------------- */

static inline void akira_ui_list_row(int y, int h, const char *text, const char *meta,
                                     int meter_0_5, bool selected)
{
    int W, H;
    akira_ui__size(&W, &H);
    uint16_t bg = selected ? AKIRA_UI_PAPER : AKIRA_UI_INK;
    uint16_t fg = selected ? AKIRA_UI_INK : AKIRA_UI_PAPER;
    int ty = y + (h - 10) / 2;

    display_rect(0, y, W, h, bg);
    if (text) {
        display_text(6, ty, text, fg);
    }

    int rx = W - 6;
    if (meter_0_5 >= 0) {
        const int nblk = 5, bw = 6, bh = 8, gap = 2;
        int mx = W - 6 - nblk * (bw + gap);
        int my = y + (h - bh) / 2;
        for (int i = 0; i < nblk; i++) {
            int cx = mx + i * (bw + gap);
            if (i < meter_0_5) {
                display_rect(cx, my, bw, bh, fg);
            } else {
                display_rect_outline(cx, my, bw, bh, fg);
            }
        }
        rx = mx - 6;
    }
    if (meta && meta[0]) {
        display_text(rx - akira_ui__tw(meta), ty, meta, fg);
    }
}

/* ---- dither-shadow card (signature elevation primitive) --------------- */

/* 50% checkerboard fill clipped to a rounded-rect mask (the "shadow"). No
 * pattern-fill primitive exists on-device — composed from display_pixel. The
 * 2x2 checker is pure parity, so no cache is needed; the corner distance test
 * is the only cost. If card-dense screens lag, pre-render into a display_bitmap
 * and blit instead. */
static inline void akira_ui__dither_rounded(int x, int y, int w, int h, int r,
                                            uint16_t color)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int cx = -1, cy = -1;
            if (i < r && j < r)                { cx = r - 1 - i; cy = r - 1 - j; }
            else if (i >= w - r && j < r)      { cx = i - (w - r); cy = r - 1 - j; }
            else if (i < r && j >= h - r)      { cx = r - 1 - i; cy = j - (h - r); }
            else if (i >= w - r && j >= h - r) { cx = i - (w - r); cy = j - (h - r); }
            if (cx >= 0 && cx * cx + cy * cy > r * r) {
                continue;
            }
            if (((i + j) & 1) == 0) {
                display_pixel(x + i, y + j, color);
            }
        }
    }
}

/* The signature Playdate-style card: rounded body + dithered offset shadow.
 * Selected = paper-on-ink fill; idle = ink body + paper outline. Larger
 * shadow_offset = more elevated (the confirm dialog uses the largest). */
static inline void akira_ui_dither_card(int x, int y, int w, int h, int radius,
                                        bool selected, int shadow_offset)
{
    if (shadow_offset > 0) {
        akira_ui__dither_rounded(x + shadow_offset, y + shadow_offset, w, h,
                                 radius, AKIRA_UI_PAPER);
    }
    display_rounded_rect_fill(x, y, w, h, radius,
                              selected ? AKIRA_UI_PAPER : AKIRA_UI_INK);
    if (!selected) {
        display_rounded_rect(x, y, w, h, radius, AKIRA_UI_PAPER);
    }
}

/* ---- grid tile -------------------------------------------------------- */

static inline void akira_ui_grid_tile(int x, int y, int w, int h, int scale,
                                      const uint8_t *icon, int icon_w, int icon_rows,
                                      const char *label, bool selected)
{
    uint16_t bg = selected ? AKIRA_UI_PAPER : AKIRA_UI_INK;
    uint16_t fg = selected ? AKIRA_UI_INK : AKIRA_UI_PAPER;

    /* Playdate-style rounded dither-shadow card instead of a square outline. */
    akira_ui_dither_card(x, y, w, h, /*radius=*/10, selected, /*shadow=*/4);

    if (icon) {
        int sw = icon_w * scale;
        akira_ui_icon_1bpp(x + (w - sw) / 2, y + 12, scale, icon,
                           icon_w, icon_rows, fg, bg, AKIRA_UI_TRANSPARENT);
    }
    if (label && label[0]) {
        display_text(x + (w - akira_ui__tw(label)) / 2, y + h - 16, label, fg);
    }
}

/* ---- button ----------------------------------------------------------- */

static inline void akira_ui_button(int x, int y, int w, int h, const char *label,
                                   bool pressed)
{
    uint16_t bg = pressed ? AKIRA_UI_PAPER : AKIRA_UI_INK;
    uint16_t fg = pressed ? AKIRA_UI_INK : AKIRA_UI_PAPER;

    display_rect(x, y, w, h, bg);
    if (!pressed) {
        display_rect_outline(x, y, w, h, fg);
    }
    if (label) {
        display_text(x + (w - akira_ui__tw(label)) / 2, y + (h - 10) / 2, label, fg);
    }
}

/* ---- toggle ----------------------------------------------------------- */

static inline void akira_ui_toggle(int x, int y, const char *label, bool on)
{
    if (label) {
        display_text(x, y + 2, label, AKIRA_UI_PAPER);
    }
    int tx = x + (label ? akira_ui__tw(label) + 8 : 0);
    const int tw = 26, th = 14;

    if (on) {
        display_rect(tx, y, tw, th, AKIRA_UI_PAPER);
        display_rect(tx + tw - 12, y + 2, 10, 10, AKIRA_UI_INK);
    } else {
        display_rect_outline(tx, y, tw, th, AKIRA_UI_PAPER);
        display_rect(tx + 2, y + 2, 10, 10, AKIRA_UI_PAPER);
    }
}

/* ---- meter ------------------------------------------------------------ */

static inline void akira_ui_meter(int x, int y, int w, int h, int value, int max)
{
    display_progress_bar(x, y, w, h, value, max, AKIRA_UI_PAPER, AKIRA_UI_INK);
}

/* ---- tag -------------------------------------------------------------- */

static inline void akira_ui_tag(int x, int y, const char *text, bool dithered)
{
    int w = akira_ui__tw(text) + 8, h = 14;
    if (dithered) {
        akira_ui__dither(x, y, w, h, AKIRA_UI_PAPER);
        display_rect_outline(x, y, w, h, AKIRA_UI_PAPER);
        display_text(x + 4, y + 2, text, AKIRA_UI_INK);
    } else {
        display_rect_outline(x, y, w, h, AKIRA_UI_PAPER);
        display_text(x + 4, y + 2, text, AKIRA_UI_PAPER);
    }
}

/* ---- alert ------------------------------------------------------------ */

static inline void akira_ui_alert(const char *title, const char *subtitle)
{
    int W, H;
    akira_ui__size(&W, &H);
    int cx = W / 2;

    display_clear(AKIRA_UI_INK);

    int apex_y = H / 2 - 54, ts = 22;
    display_triangle(cx, apex_y, cx - ts, apex_y + ts * 2,
                     cx + ts, apex_y + ts * 2, AKIRA_UI_PAPER);
    display_vline(cx, apex_y + 12, ts - 6, AKIRA_UI_PAPER);
    display_rect(cx - 1, apex_y + ts + 4, 2, 2, AKIRA_UI_PAPER);

    if (title && title[0]) {
        display_text(cx - akira_ui__tw(title) / 2, H / 2 + 4, title, AKIRA_UI_PAPER);
    }
    if (subtitle && subtitle[0]) {
        display_text(cx - akira_ui__tw(subtitle) / 2, H / 2 + 20, subtitle, AKIRA_UI_PAPER);
    }
    display_flush();
}

/* ---- confirm dialog (the only 3px element) ---------------------------- */

static inline void akira_ui__confirm_render(int bx, int by, int bw, int bh,
                                            const char *capability_name,
                                            const char *question, int sel)
{
    int cx = bx + bw / 2;

    display_clear(AKIRA_UI_INK);

    /* The reserved Capability-Guard marker: the system's largest dither-shadow
     * offset (6px) + a rounded card + a heavy 3px outline. Weight = stakes. */
    akira_ui__dither_rounded(bx + 6, by + 6, bw, bh, 14, AKIRA_UI_PAPER);
    display_rounded_rect_fill(bx, by, bw, bh, 14, AKIRA_UI_INK);
    for (int i = 0; i < 3; i++) {
        display_rounded_rect(bx + i, by + i, bw - 2 * i, bh - 2 * i, 14 - i,
                             AKIRA_UI_PAPER);
    }

    if (question && question[0]) {
        display_text(cx - akira_ui__tw(question) / 2, by + 40, question, AKIRA_UI_PAPER);
    }
    char cap[80];
    int n = akira_ui__strcpy(cap, "capability: ");
    n += akira_ui__strcpy(cap + n, capability_name ? capability_name : "?");
    akira_ui__strcpy(cap + n, " - restricted");
    display_text(cx - akira_ui__tw(cap) / 2, by + 60, cap, AKIRA_UI_PAPER);

    int btn_y = by + bh - 44;
    /* cancel = plain text (focus ring when selected). */
    const char *cancel = "cancel";
    int cxl = bx + bw / 4 - akira_ui__tw(cancel) / 2;
    if (sel == 0) {
        display_rounded_rect(cxl - 10, btn_y - 4, akira_ui__tw(cancel) + 20, 28, 8,
                             AKIRA_UI_PAPER);
    }
    display_text(cxl, btn_y + 5, cancel, AKIRA_UI_PAPER);

    /* confirm = filled rounded button; elevates (gains a shadow) when focused. */
    int cbx = bx + bw / 2 + 8, cbw = bw / 2 - 24, cbh = 28;
    akira_ui_dither_card(cbx, btn_y, cbw, cbh, 8, /*selected=*/true,
                         /*shadow=*/sel == 1 ? 3 : 0);
    display_text(cbx + (cbw - akira_ui__tw("confirm")) / 2, btn_y + 5, "confirm",
                 AKIRA_UI_INK);

    display_flush();
}

static inline bool akira_ui_confirm_dialog(const char *capability_name,
                                           const char *question)
{
    int W, H;
    akira_ui__size(&W, &H);
    int bw = W - 60, bh = 150;
    int bx = (W - bw) / 2, by = (H - bh) / 2;

    int sel = 0; /* default cancel — safest for a guarded action */
    akira_ui__confirm_render(bx, by, bw, bh, capability_name, question, sel);

    uint32_t prev = (uint32_t)input_get_buttons();
    while (true) {
        delay(20000); /* 20 ms */
        uint32_t now = (uint32_t)input_get_buttons();
        uint32_t pressed = now & ~prev; /* rising edges */
        prev = now;

        if (pressed & (AKIRA_BTN_LEFT | AKIRA_BTN_RIGHT)) {
            sel ^= 1;
            akira_ui__confirm_render(bx, by, bw, bh, capability_name, question, sel);
        }
        if (pressed & AKIRA_BTN_A) {
            return sel == 1;
        }
        if (pressed & AKIRA_BTN_B) {
            return false;
        }
    }
}

#endif /* AKIRA_UI_WASM_H */
