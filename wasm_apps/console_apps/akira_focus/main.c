/**
 * @file main.c
 * @brief AkiraFocus — Pomodoro timer for AkiraOS
 *
 * Controls:
 *   A        = Start / Pause
 *   LEFT     = Reset current session
 *   SETTINGS = Skip to next phase
 *
 * Flow: 4 × FOCUS (25 min) → LONG BREAK (15 min).
 * Short break (5 min) between each FOCUS session.
 * Pomodoro dots at the bottom track progress in the current cycle.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Screen ──────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;
static int32_t CX;
static int32_t CY;

/* ── Ring geometry ───────────────────────────────────────────────────── */
static int32_t OUTER_R;
static int32_t INNER_R;

/* ── Buttons ─────────────────────────────────────────────────────────── */
#define BTN_A         15
#define BTN_LEFT       6
#define BTN_SETTINGS   0

/* ── Phases ──────────────────────────────────────────────────────────── */
#define PHASE_FOCUS        0
#define PHASE_SHORT_BREAK  1
#define PHASE_LONG_BREAK   2

/* ── Durations (seconds) ─────────────────────────────────────────────── */
#define DUR_FOCUS        (25 * 60)
#define DUR_SHORT_BREAK  ( 5 * 60)
#define DUR_LONG_BREAK   (15 * 60)

/* ── Colors ──────────────────────────────────────────────────────────── */
#define COL_BG        0x0000
#define COL_RING_DARK 0x2945
#define COL_FOCUS     0xFD20   /* orange */
#define COL_SHORT     0x07E0   /* green  */
#define COL_LONG      0x07FF   /* cyan   */
#define COL_WHITE     0xFFFF
#define COL_DIM       0x39E7

/*
 * sin/cos for k*6° (k = 0..59), clockwise from 12 o'clock, scaled ×100.
 * Screen convention: x = CX + sin*R/100,  y = CY - cos*R/100
 */
static const int8_t SIN60[60] = {
     0,  10,  21,  31,  41,  50,  59,  67,  74,  81,
    87,  91,  95,  98,  99, 100,  99,  98,  95,  91,
    87,  81,  74,  67,  59,  50,  41,  31,  21,  10,
     0, -10, -21, -31, -41, -50, -59, -67, -74, -81,
   -87, -91, -95, -98, -99,-100, -99, -98, -95, -91,
   -87, -81, -74, -67, -59, -50, -41, -31, -21, -10
};
static const int8_t COS60[60] = {
   100,  99,  98,  95,  91,  87,  81,  74,  67,  59,
    50,  41,  31,  21,  10,   0, -10, -21, -31, -41,
   -50, -59, -67, -74, -81, -87, -91, -95, -98, -99,
  -100, -99, -98, -95, -91, -87, -81, -74, -67, -59,
   -50, -41, -31, -21, -10,   0,  10,  21,  31,  41,
    50,  59,  67,  74,  81,  87,  91,  95,  98,  99
};

/* ── State ───────────────────────────────────────────────────────────── */
static uint8_t  g_phase;
static uint8_t  g_pom_count;   /* FOCUS sessions completed this cycle (0–3) */
static uint32_t g_total_ms;
static uint32_t g_elapsed_ms;
static uint8_t  g_running;

static int g_prev_a, g_prev_l, g_prev_s;
static int g_tmr;

/* ── Helpers ─────────────────────────────────────────────────────────── */

static uint32_t phase_duration_ms(uint8_t p)
{
    if (p == PHASE_FOCUS)       return (uint32_t)DUR_FOCUS       * 1000;
    if (p == PHASE_SHORT_BREAK) return (uint32_t)DUR_SHORT_BREAK * 1000;
    return (uint32_t)DUR_LONG_BREAK * 1000;
}

static uint32_t phase_color(uint8_t p)
{
    if (p == PHASE_FOCUS)       return COL_FOCUS;
    if (p == PHASE_SHORT_BREAK) return COL_SHORT;
    return COL_LONG;
}

static const char *phase_name(uint8_t p)
{
    if (p == PHASE_FOCUS)       return "FOCUS";
    if (p == PHASE_SHORT_BREAK) return "SHORT BREAK";
    return "LONG BREAK";
}

static const char *next_phase_name(uint8_t p, uint8_t pom)
{
    if (p == PHASE_FOCUS) {
        return (pom >= 3) ? "LONG BREAK" : "SHORT BREAK";
    }
    return "FOCUS";
}

static void fmt_time(char *buf, uint32_t sec)
{
    uint32_t m = sec / 60;
    uint32_t s = sec % 60;
    buf[0] = '0' + (char)(m / 10);
    buf[1] = '0' + (char)(m % 10);
    buf[2] = ':';
    buf[3] = '0' + (char)(s / 10);
    buf[4] = '0' + (char)(s % 10);
    buf[5] = '\0';
}

/* ── Ring renderer ───────────────────────────────────────────────────── */

/*
 * Draw remaining-time ring.
 * The gray base covers the full circle; accent sectors fill remaining time
 * starting from the point where elapsed time ends (clockwise from 12 o'clock).
 * Depletion advances clockwise from 12 o'clock as time passes.
 */
static void draw_ring(uint32_t remaining_ms, uint32_t total_ms, uint32_t accent)
{
    uint32_t fill_segs;
    if (total_ms == 0 || remaining_ms == 0) {
        fill_segs = 0;
    } else {
        fill_segs = remaining_ms * 60 / total_ms;
        if (fill_segs > 60) fill_segs = 60;
    }

    int elapsed_segs = 60 - (int)fill_segs;

    display_circle_fill(CX, CY, OUTER_R, COL_BG);

    for (int i = elapsed_segs; i < 60; i++) {
        int j  = (i + 1) % 60;
        int x1 = CX + (int)SIN60[i] * OUTER_R / 100;
        int y1 = CY - (int)COS60[i] * OUTER_R / 100;
        int x2 = CX + (int)SIN60[j] * OUTER_R / 100;
        int y2 = CY - (int)COS60[j] * OUTER_R / 100;
        display_triangle_fill(CX, CY, x1, y1, x2, y2, accent);
    }

    /* Hollow center */
    display_circle_fill(CX, CY, INNER_R, COL_BG);

    /* Thin separator ring between accent and dark — draws over any AA gaps */
    display_circle(CX, CY, INNER_R,  COL_RING_DARK);
    display_circle(CX, CY, OUTER_R,  COL_RING_DARK);
}

/* ── Pomodoro dots ───────────────────────────────────────────────────── */

static void draw_dots(uint8_t count, uint32_t accent)
{
    /* 4 dots, spaced 5% of screen width apart, centered at CX */
    int spacing = SCR_W * 5 / 100;
    int y       = SCR_H * 86 / 100;
    int x0      = CX - spacing * 3 / 2;
    for (int i = 0; i < 4; i++) {
        int x = x0 + i * spacing;
        if (i < (int)count) {
            display_circle_fill(x, y, 5, accent);
        } else {
            display_circle(x, y, 5, COL_DIM);
        }
    }
}

/* ── Full-frame render ───────────────────────────────────────────────── */

static void render(void)
{
    uint32_t remaining_ms  = (g_elapsed_ms < g_total_ms)
                             ? g_total_ms - g_elapsed_ms : 0;
    uint32_t remaining_sec = remaining_ms / 1000;
    uint32_t accent        = phase_color(g_phase);

    display_clear(COL_BG);

    /* Title bar */
    display_text(CX - 42, SCR_H * 2 / 100, "AKIRA FOCUS", COL_DIM);
    display_hline(0, SCR_H * 7 / 100, SCR_W, COL_RING_DARK);

    /* Progress ring */
    draw_ring(remaining_ms, g_total_ms, accent);

    /* Phase label inside ring */
    const char *pname = phase_name(g_phase);
    int plen = strlen(pname);
    display_text(CX - plen * 4, CY - 22, pname, accent);

    /* MM:SS countdown */
    char tbuf[6];
    fmt_time(tbuf, remaining_sec);
    display_text_large(CX - 30, CY - 9, tbuf, COL_WHITE);

    /* Running / paused indicator */
    if (g_running) {
        display_text(CX - 28, CY + 13, "RUNNING", accent);
    } else {
        display_text(CX - 24, CY + 13, "PAUSED", COL_DIM);
    }

    /* Next phase hint inside ring */
    const char *nname = next_phase_name(g_phase, g_pom_count);
    int nlen = strlen(nname);
    display_text(CX - nlen * 3, CY + 26, nname, COL_RING_DARK);

    /* Pomodoro dots */
    draw_dots(g_pom_count, accent);

    /* Hint bar */
    int hint_y = SCR_H * 95 / 100;
    display_text(8,           hint_y, "A:Go/Pause", COL_DIM);
    display_text(CX - 16,     hint_y, "L:Reset",    COL_DIM);
    display_text(SCR_W - 72,  hint_y, "SET:Skip",   COL_DIM);

    display_flush();
}

/* ── Phase transitions ───────────────────────────────────────────────── */

static void start_phase(uint8_t p)
{
    g_phase      = p;
    g_total_ms   = phase_duration_ms(p);
    g_elapsed_ms = 0;
    g_running    = 0;
    timer_start(g_tmr);
}

static void advance_phase(void)
{
    if (g_phase == PHASE_FOCUS) {
        g_pom_count++;
        if (g_pom_count >= 4) {
            g_pom_count = 0;
            start_phase(PHASE_LONG_BREAK);
        } else {
            start_phase(PHASE_SHORT_BREAK);
        }
    } else {
        start_phase(PHASE_FOCUS);
    }
}

/* ── Completion flash ────────────────────────────────────────────────── */

static void flash_done(uint32_t accent)
{
    for (int i = 0; i < 6; i++) {
        display_clear((i & 1) ? accent : COL_BG);
        display_flush();
        delay(150000);
    }
}

/* ── Splash screen ───────────────────────────────────────────────────── */

static void draw_splash(void)
{
    display_clear(COL_BG);

    int ring_cy = SCR_H * 41 / 100;

    /* Decorative rings */
    display_circle(CX, ring_cy, SCR_H * 30 / 100, COL_RING_DARK);
    display_circle(CX, ring_cy, SCR_H * 28 / 100, COL_FOCUS);
    display_circle(CX, ring_cy, SCR_H * 21 / 100, COL_RING_DARK);

    /* App name */
    display_text_large(CX - 30, SCR_H * 30 / 100, "AKIRA", COL_FOCUS);
    display_text_large(CX - 30, SCR_H * 38 / 100, "FOCUS", COL_WHITE);

    /* Info */
    display_text(CX - 45, SCR_H * 63 / 100, "Pomodoro productivity", COL_DIM);
    display_text(CX - 42, SCR_H * 69 / 100, "25 min / 5 min / 15 min", COL_DIM);

    display_text(CX - 38, SCR_H * 87 / 100, "Press A to begin", COL_WHITE);
    display_flush();
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("AkiraFocus v1.0");

    display_get_size(&SCR_W, &SCR_H);
    CX = SCR_W / 2;
    CY = SCR_H * 48 / 100;
    OUTER_R = SCR_H * 33 / 100;
    INNER_R = SCR_H * 24 / 100;

    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    g_tmr = timer_create();
    timer_start(g_tmr);

    draw_splash();
    while (!gpio_read(BTN_A)) delay(20000);
    delay(80000);

    g_pom_count = 0;
    start_phase(PHASE_FOCUS);
    g_prev_a = 1;  /* consume the splash-exit press */
    g_prev_l = 0;
    g_prev_s = 0;

    while (1) {
        /* Accumulate elapsed time */
        int delta = timer_elapsed(g_tmr);
        timer_start(g_tmr);

        if (g_running) {
            g_elapsed_ms += (uint32_t)delta;
        }

        /* Phase complete? */
        if (g_running && g_elapsed_ms >= g_total_ms) {
            g_elapsed_ms = g_total_ms;
            g_running    = 0;
            flash_done(phase_color(g_phase));
            advance_phase();
            continue;
        }

        /* Button input (edge-triggered) */
        int a = gpio_read(BTN_A);
        int l = gpio_read(BTN_LEFT);
        int s = gpio_read(BTN_SETTINGS);

        if (a && !g_prev_a) {
            g_running = !g_running;
            timer_start(g_tmr);
        }
        if (l && !g_prev_l) {
            g_elapsed_ms = 0;
            g_running    = 0;
            timer_start(g_tmr);
        }
        if (s && !g_prev_s) {
            flash_done(phase_color(g_phase));
            advance_phase();
            g_prev_s = 1;
            continue;
        }

        g_prev_a = a;
        g_prev_l = l;
        g_prev_s = s;

        /* Render at ~10 fps */
        render();

        int used = timer_elapsed(g_tmr);
        timer_start(g_tmr);
        if (used < 100) {
            delay((uint32_t)(100 - used) * 1000);
        }
    }

    return 0;
}
