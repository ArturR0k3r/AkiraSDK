/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file akira_game.h
 * @brief Shared helpers for AkiraConsole games (header-only).
 *
 * Input with D-pad auto-repeat on top of the edge queue, a small PRNG, a
 * date-seeded "daily" helper, integer settings, text layout helpers, and a
 * modal menu / message box.  Everything is static inline: include it once,
 * from the app's single translation unit, after akira_api.h.
 *
 * Capabilities the helpers use (declare them in the app manifest):
 *   display.write, input.read   — always
 *   rtc.read                    — g_now(), key repeat, g_today()
 *   crypto                      — g_seed_random()
 *   settings.*                  — g_load_int() / g_save_int()
 */

#ifndef AKIRA_GAME_H
#define AKIRA_GAME_H

#include "akira_api.h"

/* ── Screen (filled by g_init) ─────────────────────────────────────────── */
static int32_t G_W = 320;
static int32_t G_H = 240;

/* ── Keys ──────────────────────────────────────────────────────────────── */
#define K_UP    0x001u
#define K_DOWN  0x002u
#define K_LEFT  0x004u
#define K_RIGHT 0x008u
#define K_A     0x010u
#define K_B     0x020u
#define K_X     0x040u
#define K_Y     0x080u
#define K_HOME  0x100u /* short press; a long press is the shell's */
#define K_DIRS  (K_UP | K_DOWN | K_LEFT | K_RIGHT)

#define G_REPEAT_DELAY_MS 300
#define G_REPEAT_RATE_MS  90

static uint32_t g__held;   /* K_* currently held, from the edge queue */
static uint32_t g__rep;    /* direction key being auto-repeated        */
static uint32_t g__rep_at; /* uptime of the next repeat                */

static inline uint32_t g_now(void) { return (uint32_t)rtc_get_uptime_ms(); }

static inline uint32_t g__key_of(uint32_t id)
{
    switch (id) {
    case 1:                  return K_HOME;
    case AKIRA_BTN_ID_UP:    return K_UP;
    case AKIRA_BTN_ID_DOWN:  return K_DOWN;
    case AKIRA_BTN_ID_LEFT:  return K_LEFT;
    case AKIRA_BTN_ID_RIGHT: return K_RIGHT;
    case AKIRA_BTN_ID_A:     return K_A;
    case AKIRA_BTN_ID_B:     return K_B;
    case AKIRA_BTN_ID_X:     return K_X;
    case AKIRA_BTN_ID_Y:     return K_Y;
    default:                 return 0;
    }
}

static inline uint32_t g__hw_bit(uint32_t k)
{
    switch (k) {
    case K_UP:    return AKIRA_BTN_UP;
    case K_DOWN:  return AKIRA_BTN_DOWN;
    case K_LEFT:  return AKIRA_BTN_LEFT;
    case K_RIGHT: return AKIRA_BTN_RIGHT;
    default:      return 0;
    }
}

/** Drop queued presses (e.g. the A that started the game). */
static inline void g_flush_input(void)
{
    akira_input_event_t ev;
    while (input_poll_event(&ev, sizeof(ev)) == 1) {
    }
    g__held = 0;
    g__rep = 0;
}

/**
 * Next key press as one K_* bit, or 0.  Non-blocking.  A held D-pad
 * direction repeats after G_REPEAT_DELAY_MS, every G_REPEAT_RATE_MS.
 */
static inline uint32_t g_key(void)
{
    akira_input_event_t ev;

    while (input_poll_event(&ev, sizeof(ev)) == 1) {
        uint32_t k = g__key_of(ev.button_id);
        if (!k) {
            continue;
        }
        if (ev.pressed) {
            g__held |= k;
            if (k & K_DIRS) {
                g__rep = k;
                g__rep_at = g_now() + G_REPEAT_DELAY_MS;
            }
            return k;
        }
        g__held &= ~k;
        if (g__rep == k) {
            g__rep = 0;
        }
    }
    if (g__rep && (int32_t)(g_now() - g__rep_at) >= 0) {
        /* The queue can drop a release; trust the live mask for repeats. */
        if (!((uint32_t)input_get_buttons() & g__hw_bit(g__rep))) {
            g__held &= ~g__rep;
            g__rep = 0;
            return 0;
        }
        g__rep_at = g_now() + G_REPEAT_RATE_MS;
        return g__rep;
    }
    return 0;
}

/** Block until a key is pressed. */
static inline uint32_t g_wait_key(void)
{
    for (;;) {
        uint32_t k = g_key();
        if (k) {
            return k;
        }
        delay(10000);
    }
}

/** Sleep, but return early (with the key) if one is pressed. */
static inline uint32_t g_sleep_or_key(uint32_t ms)
{
    uint32_t until = g_now() + ms;
    while ((int32_t)(g_now() - until) < 0) {
        uint32_t k = g_key();
        if (k) {
            return k;
        }
        delay(10000);
    }
    return 0;
}

static inline void g_init(void)
{
    display_get_size(&G_W, &G_H);
    if (G_W <= 0 || G_H <= 0) {
        G_W = 320;
        G_H = 240;
    }
    g_flush_input();
}

/* ── Random ────────────────────────────────────────────────────────────── */
static uint32_t g__rng = 0x6D2B79F5u;

static inline void g_seed(uint32_t s) { g__rng = s ? s : 0x6D2B79F5u; }

static inline uint32_t g_rand(void)
{
    uint32_t x = g__rng; /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g__rng = x;
}

static inline uint32_t g_rand_n(uint32_t n) { return n ? g_rand() % n : 0; }

/** Seed from the hardware RNG (falls back to the uptime). */
static inline void g_seed_random(void)
{
    uint32_t s = 0;
    if (crypto_random(&s, sizeof(s)) != 0 || s == 0) {
        s = g_now() * 2654435761u + 1u;
    }
    g_seed(s);
}

/** Seed deterministically from a number (a day, a level...). */
static inline void g_seed_from(uint32_t n)
{
    uint32_t s = n * 2654435761u ^ 0xA341316Cu;
    s ^= s >> 15;
    s *= 0x2C1B3C6Du;
    s ^= s >> 12;
    g_seed(s);
    for (int i = 0; i < 8; i++) {
        (void)g_rand();
    }
}

/* ── Date ──────────────────────────────────────────────────────────────── */

/** Days since 1970-01-01 (UTC) from the RTC, or -1 if the clock isn't set. */
static inline int g_today(void)
{
    int t = rtc_get_unix_time();
    return t > 1700000000 ? t / 86400 : -1;
}

/* ── Strings ───────────────────────────────────────────────────────────── */

/** Append @p s to the NUL-terminated @p d; returns @p d. */
static inline char *g_cat(char *d, const char *s)
{
    char *p = d;
    while (*p) {
        p++;
    }
    while (*s) {
        *p++ = *s++;
    }
    *p = '\0';
    return d;
}

static inline char *g_cat_int(char *d, int v)
{
    char t[12];
    itoa(v, t);
    return g_cat(d, t);
}

/** Append a duration as m:ss (or h:mm:ss). */
static inline char *g_cat_time(char *d, int secs)
{
    if (secs < 0) {
        secs = 0;
    }
    int h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
    if (h) {
        g_cat_int(d, h);
        g_cat(d, m < 10 ? ":0" : ":");
    }
    g_cat_int(d, m);
    g_cat(d, s < 10 ? ":0" : ":");
    return g_cat_int(d, s);
}

/** Append day @p day (days since epoch) as YYYY-MM-DD. */
static inline char *g_cat_date(char *d, int day)
{
    /* Civil-from-days (H. Hinnant), valid for any day >= 0. */
    int z = day + 719468;
    int era = z / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int dd = doy - (153 * mp + 2) / 5 + 1;
    int mm = mp < 10 ? mp + 3 : mp - 9;
    if (mm <= 2) {
        y++;
    }
    g_cat_int(d, y);
    g_cat(d, mm < 10 ? "-0" : "-");
    g_cat_int(d, mm);
    g_cat(d, dd < 10 ? "-0" : "-");
    return g_cat_int(d, dd);
}

/* ── Settings ──────────────────────────────────────────────────────────── */

static inline int g_load_int(const char *key, int def)
{
    char b[16];
    if (settings_get(key, b, sizeof(b)) != 0) {
        return def;
    }
    int v = 0, neg = 0, i = 0, any = 0;
    if (b[0] == '-') {
        neg = 1;
        i = 1;
    }
    for (; b[i] >= '0' && b[i] <= '9'; i++) {
        v = v * 10 + (b[i] - '0');
        any = 1;
    }
    return any ? (neg ? -v : v) : def;
}

static inline void g_save_int(const char *key, int v)
{
    char b[12];
    itoa(v, b);
    (void)settings_set(key, b);
}

/* ── Text layout (small font 7x10, large 11x18, 1px tracking) ─────────── */
#define G_FONT_ADV  8
#define G_FONT_H    10
#define G_LARGE_ADV 12
#define G_LARGE_H   18

static inline int g_tw(const char *s)
{
    int n = strlen(s);
    return n ? n * G_FONT_ADV - 1 : 0;
}

static inline int g_tw_large(const char *s)
{
    int n = strlen(s);
    return n ? n * G_LARGE_ADV - 1 : 0;
}

static inline void g_text_c(int cx, int y, const char *s, uint32_t c)
{
    display_text(cx - g_tw(s) / 2, y, s, c);
}

static inline void g_text_r(int rx, int y, const char *s, uint32_t c)
{
    display_text(rx - g_tw(s), y, s, c);
}

static inline void g_text_large_c(int cx, int y, const char *s, uint32_t c)
{
    display_text_large(cx - g_tw_large(s) / 2, y, s, c);
}

/* ── Panels, menus, message boxes ──────────────────────────────────────── */
typedef struct {
    uint16_t bg, border, text, dim, sel_bg, sel_text, title, shadow;
} g_theme_t;

static g_theme_t g_theme = {
    .bg = 0x10A4,       /* deep navy        */
    .border = 0x065B,   /* cyan             */
    .text = 0xFFFF,
    .dim = 0x8CB5,
    .sel_bg = 0x03D1,   /* teal bar         */
    .sel_text = 0xFFFF,
    .title = 0xFE87,    /* warm yellow      */
    .shadow = 0x0000,
};

static inline void g_panel(int x, int y, int w, int h)
{
    display_rounded_rect_fill(x + 3, y + 3, w, h, 6, g_theme.shadow);
    display_rounded_rect_fill(x, y, w, h, 6, g_theme.bg);
    display_rounded_rect(x, y, w, h, 6, g_theme.border);
}

#define G_MENU_LEFT  0x100 /* OR-ed into g_menu()'s result */
#define G_MENU_RIGHT 0x200

/**
 * Modal list.  Returns the chosen index, -1 on B/HOME.  LEFT/RIGHT return
 * index | G_MENU_LEFT / G_MENU_RIGHT when @p lr is set (for value cycling).
 * Draws over the current frame; the caller redraws its screen afterwards.
 */
static inline int g_menu(const char *title, const char *const *items, int n, int sel, int lr)
{
    int w = title ? g_tw_large(title) + 32 : 0;
    for (int i = 0; i < n; i++) {
        int iw = g_tw(items[i]) + 40;
        if (iw > w) {
            w = iw;
        }
    }
    if (w < 170) {
        w = 170;
    }
    if (w > G_W - 16) {
        w = G_W - 16;
    }
    int top = title ? 32 : 10;
    int h = top + n * 20 + 8;
    int x = (G_W - w) / 2, y = (G_H - h) / 2;

    if (sel < 0 || sel >= n) {
        sel = 0;
    }
    for (;;) {
        g_panel(x, y, w, h);
        if (title) {
            g_text_large_c(x + w / 2, y + 8, title, g_theme.title);
        }
        for (int i = 0; i < n; i++) {
            int iy = y + top + i * 20;
            if (i == sel) {
                display_rounded_rect_fill(x + 6, iy, w - 12, 18, 4, g_theme.sel_bg);
                display_text(x + 12, iy + 4, ">", g_theme.title);
            }
            display_text(x + 24, iy + 4, items[i], i == sel ? g_theme.sel_text : g_theme.text);
        }
        display_flush();

        uint32_t k = g_wait_key();
        if (k == K_UP) {
            sel = (sel + n - 1) % n;
        } else if (k == K_DOWN) {
            sel = (sel + 1) % n;
        } else if (k == K_A) {
            return sel;
        } else if (k == K_B || k == K_HOME) {
            return -1;
        } else if (lr && k == K_LEFT) {
            return sel | G_MENU_LEFT;
        } else if (lr && k == K_RIGHT) {
            return sel | G_MENU_RIGHT;
        }
    }
}

/** Modal text box; '\n' separates lines.  Waits for any key. */
static inline uint32_t g_message(const char *title, const char *body)
{
    int lines = 1, cols = 0, c = 0;
    for (const char *p = body; *p; p++) {
        if (*p == '\n') {
            lines++;
            c = 0;
        } else if (++c > cols) {
            cols = c;
        }
    }
    int w = cols * G_FONT_ADV + 28;
    if (title && g_tw_large(title) + 28 > w) {
        w = g_tw_large(title) + 28;
    }
    if (w > G_W - 12) {
        w = G_W - 12;
    }
    int top = title ? 32 : 12;
    int h = top + lines * (G_FONT_H + 2) + 26;
    if (h > G_H - 8) {
        h = G_H - 8;
    }
    int x = (G_W - w) / 2, y = (G_H - h) / 2;

    g_panel(x, y, w, h);
    if (title) {
        g_text_large_c(x + w / 2, y + 8, title, g_theme.title);
    }
    display_text(x + 14, y + top, body, g_theme.text);
    g_text_c(x + w / 2, y + h - 16, "press any button", g_theme.dim);
    display_flush();
    return g_wait_key();
}

#endif /* AKIRA_GAME_H */
