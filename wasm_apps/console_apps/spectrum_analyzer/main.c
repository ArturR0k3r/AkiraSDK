/*
 * spectrum_analyzer/main.c — RF spectrum analyzer
 * 64-bin RSSI sweep · monochrome · 1px thin-line bars · scrolling waterfall
 *
 * Controls:
 *   LEFT / RIGHT   shift center freq by span/2
 *   UP   / DOWN    zoom in / zoom out (halve / double span)
 *   A              reset to 433.920 MHz, 2 MHz span
 *   B              pause / resume
 *   X              toggle waterfall
 *   Y              toggle peak hold
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "akira_api.h"

/* ── Monochrome palette ─────────────────────────────────────────────── */
#define BLK  0x0000u
#define WHT  0xFFFFu

/* ── Display ────────────────────────────────────────────────────────── */
static int32_t GW = 0, GH = 0;

/* ── Layout (all derived at runtime from GW / GH) ───────────────────── */
#define HDR_H          16
#define FTR_H          12
/* Bar-chart area: 55% of content height */
#define CONTENT_H()    (GH - HDR_H - FTR_H)
#define BAR_Y          HDR_H
#define BAR_H()        (CONTENT_H() * 55 / 100)
#define WF_Y()         (HDR_H + BAR_H())
#define WF_H()         (CONTENT_H() - BAR_H())

/* ── Spectrum config ────────────────────────────────────────────────── */
#define BINS            64
#define SETTLE_US       800u

/* ── RF defaults ────────────────────────────────────────────────────── */
#define DEF_CENTER  433920000u
#define DEF_SPAN      2000000u
#define MIN_SPAN        50000u
#define MAX_SPAN    200000000u

/* ── RSSI range (dBm) ───────────────────────────────────────────────── */
#define RSSI_FLOOR  (-120)
#define RSSI_CEIL   ( -20)

/* ── Peak hold ──────────────────────────────────────────────────────── */
#define PEAK_HOLD_SWEEPS  60

/* ── Waterfall ──────────────────────────────────────────────────────── */
#define WF_ROWS  96

/* ── Buttons ────────────────────────────────────────────────────────── */
#define BTN_UP     0
#define BTN_DOWN   1
#define BTN_LEFT   2
#define BTN_RIGHT  3
#define BTN_A      4
#define BTN_B      5
#define BTN_X      6
#define BTN_Y      7
#define BTN_COUNT  8

static const int BTN_PINS[BTN_COUNT] = { 4, 5, 6, 7, 15, 16, 17, 40 };

#define DB_FRAMES 3

static int btn_cnt[BTN_COUNT];
static int btn_st[BTN_COUNT];
static int btn_prev[BTN_COUNT];
static int btn_hold[BTN_COUNT];

/* ─────────────────────────────────────────────────────────────────────
 * State
 * ───────────────────────────────────────────────────────────────────── */
static uint32_t g_center   = DEF_CENTER;
static uint32_t g_span     = DEF_SPAN;
static int      g_paused   = 0;
static int      g_hold     = 1;
static int      g_wf_on    = 1;


static int8_t   g_rssi[BINS];
static int8_t   g_peak[BINS];
static int      g_peak_ttl[BINS];

static uint8_t  g_wf[WF_ROWS][BINS];  /* 0=noise  1=signal */
static int      g_wf_head = 0;

static int8_t   g_noise   = -110;     /* tracked noise floor */

/* ─────────────────────────────────────────────────────────────────────
 * Buttons — same debounce pattern as rf_chat
 * ───────────────────────────────────────────────────────────────────── */
static void btns_init(void)
{
    for (int i = 0; i < BTN_COUNT; i++)
        gpio_configure(BTN_PINS[i], GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
}

static void btns_poll(void)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        int raw    = gpio_read(BTN_PINS[i]);
        btn_prev[i] = btn_st[i];
        btn_cnt[i]  = raw ? btn_cnt[i] + 1 : 0;
        btn_st[i]   = (btn_cnt[i] >= DB_FRAMES) ? 1 : 0;
        btn_hold[i] = btn_st[i] ? btn_hold[i] + 1 : 0;
    }
}

static int rose(int i) { return  btn_st[i] && !btn_prev[i]; }
static void handle_btns(void); /* forward decl — defined after do_sweep */

/* ─────────────────────────────────────────────────────────────────────
 * String helpers (no libc)
 * ───────────────────────────────────────────────────────────────────── */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void fmt_freq(char *buf, uint32_t hz)
{
    uint32_t mhz = hz / 1000000u;
    uint32_t khz = (hz % 1000000u) / 1000u;
    int i = 0;
    buf[i++] = '0' + (char)(mhz / 100 % 10);
    buf[i++] = '0' + (char)(mhz /  10 % 10);
    buf[i++] = '0' + (char)(mhz       % 10);
    buf[i++] = '.';
    buf[i++] = '0' + (char)(khz / 100 % 10);
    buf[i++] = '0' + (char)(khz /  10 % 10);
    buf[i++] = '0' + (char)(khz       % 10);
    buf[i++] = 'M';
    buf[i]   = '\0';
}

static void fmt_span(char *buf, uint32_t hz)
{
    int i = 0;
    uint32_t k = hz / 1000u;
    if (k >= 1000u) {
        uint32_t m = k / 1000u;
        if (m >= 100) buf[i++] = '0' + (char)(m / 100 % 10);
        if (m >=  10) buf[i++] = '0' + (char)(m /  10 % 10);
        buf[i++] = '0' + (char)(m % 10);
        buf[i++] = 'M';
    } else {
        if (k >= 100) buf[i++] = '0' + (char)(k / 100 % 10);
        if (k >=  10) buf[i++] = '0' + (char)(k /  10 % 10);
        buf[i++] = '0' + (char)(k % 10);
        buf[i++] = 'k';
    }
    buf[i] = '\0';
}

static void fmt_dbm(char *buf, int v)
{
    int i = 0;
    if (v < 0) { buf[i++] = '-'; v = -v; }
    if (v >= 100) buf[i++] = '0' + (char)(v / 100 % 10);
    if (v >=  10) buf[i++] = '0' + (char)(v /  10 % 10);
    buf[i++] = '0' + (char)(v % 10);
    buf[i++] = 'd'; buf[i++] = 'B'; buf[i] = '\0';
}

/* ─────────────────────────────────────────────────────────────────────
 * Geometry
 * ───────────────────────────────────────────────────────────────────── */

/* X pixel for bin b — evenly spread across full width */
static int bin_x(int b)
{
    return (b * (GW - 1)) / (BINS - 1);
}

/* Y pixel from RSSI within a rect (area_y, area_h) — bottom = low level */
static int rssi_y(int8_t r, int area_y, int area_h)
{
    int h = ((int)r - RSSI_FLOOR) * area_h / (RSSI_CEIL - RSSI_FLOOR);
    if (h < 0)       h = 0;
    if (h > area_h)  h = area_h;
    return area_y + area_h - h;
}

/* ─────────────────────────────────────────────────────────────────────
 * Drawing
 * ───────────────────────────────────────────────────────────────────── */
static void draw_header(int sweep_bin)
{
    char fbuf[12], sbuf[8];
    fmt_freq(fbuf, g_center);
    fmt_span(sbuf, g_span);

    display_rect(0, 0, GW, HDR_H, WHT);

    display_text(2, 3, "SPECTRUM", BLK);

    int fl = slen(fbuf) * 7;
    display_text((GW - fl) / 2, 3, fbuf, BLK);

    int sl = slen(sbuf) * 7;
    display_text(GW - sl - 2, 3, sbuf, BLK);

    /* Sweep-progress underline */
    if (!g_paused && sweep_bin > 0) {
        int prog = sweep_bin * GW / BINS;
        display_hline(0, HDR_H - 2, prog, BLK);
    }
    if (g_paused)
        display_text((GW - 6 * 7) / 2, 3, "PAUSED", BLK);
}

static void draw_bar_chart(void)
{
    int by = BAR_Y;
    int bh = BAR_H();

    display_rect(0, by, GW, bh, BLK);

    /* Dashed horizontal grid at 25/50/75 % */
    for (int p = 1; p <= 3; p++) {
        int gy = by + bh - bh * p / 4;
        for (int x = 0; x < GW; x += 4)
            display_pixel(x, gy, WHT);
    }

    /* Dashed vertical grid every 16 bins */
    for (int b = 16; b < BINS; b += 16) {
        int x = bin_x(b);
        for (int y = by; y < by + bh; y += 3)
            display_pixel(x, y, WHT);
    }

    /* dBm scale labels on left */
    char db[8];
    fmt_dbm(db, RSSI_FLOOR + (RSSI_CEIL - RSSI_FLOOR) * 3 / 4);
    display_text(2, by + bh / 4 - 5, db, WHT);
    fmt_dbm(db, RSSI_FLOOR + (RSSI_CEIL - RSSI_FLOOR) / 2);
    display_text(2, by + bh / 2 - 5, db, WHT);

    /* 1-px liner bars + peak dots */
    for (int b = 0; b < BINS; b++) {
        int x  = bin_x(b);
        int y0 = rssi_y(g_rssi[b], by, bh);
        int h  = by + bh - y0;
        if (h > 0)
            display_vline(x, y0, h, WHT);

        if (g_hold && (int)g_peak[b] > (int)g_rssi[b]) {
            int py = rssi_y(g_peak[b], by, bh);
            if (py >= by && py < by + bh)
                display_pixel(x, py, WHT);
        }
    }

    /* Bottom separator */
    display_hline(0, by + bh, GW, WHT);
}

static void draw_waterfall(void)
{
    int wy = WF_Y();
    int wh = WF_H();
    if (wh <= 0) return;

    display_rect(0, wy, GW, wh, BLK);

    int rows = wh < WF_ROWS ? wh : WF_ROWS;
    int bw   = GW / BINS;
    if (bw < 1) bw = 1;

    for (int row = 0; row < rows; row++) {
        int idx = (g_wf_head - row + WF_ROWS) % WF_ROWS;
        int y   = wy + row;
        for (int b = 0; b < BINS; b++) {
            if (g_wf[idx][b]) {
                int x = bin_x(b);
                display_rect(x, y, bw > 1 ? bw - 1 : 1, 1, WHT);
            }
        }
    }
}

static void draw_footer(void)
{
    display_rect(0, GH - FTR_H, GW, FTR_H, WHT);
    display_text(2, GH - FTR_H + 2,
                 "L/R:shift  U/D:zoom  A:rst  B:pause  X:wfall  Y:peak",
                 BLK);
}

static void full_redraw(int sweep_bin)
{
    draw_header(sweep_bin);
    draw_bar_chart();
    if (g_wf_on)
        draw_waterfall();
    else
        display_rect(0, WF_Y(), GW, WF_H(), BLK);
    draw_footer();
    display_flush();
}

/* ─────────────────────────────────────────────────────────────────────
 * RF sweep
 * ───────────────────────────────────────────────────────────────────── */
static void do_sweep(void)
{
    uint32_t step    = g_span / BINS;
    int      min_raw = RSSI_FLOOR;

    for (int b = 0; b < BINS; b++) {
        uint32_t freq = g_center - g_span / 2u + (uint32_t)b * step;
        rf_set_frequency(freq);
        delay(SETTLE_US);

        int raw = rf_get_rssi();
        if (raw < RSSI_FLOOR) raw = RSSI_FLOOR;
        if (raw > RSSI_CEIL)  raw = RSSI_CEIL;
        g_rssi[b] = (int8_t)raw;

        if (raw > min_raw) min_raw = raw;

        /* Peak hold */
        if (g_hold) {
            if ((int)g_rssi[b] >= (int)g_peak[b]) {
                g_peak[b]     = g_rssi[b];
                g_peak_ttl[b] = PEAK_HOLD_SWEEPS;
            } else if (g_peak_ttl[b] > 0) {
                g_peak_ttl[b]--;
            } else if (g_peak[b] > (int8_t)RSSI_FLOOR) {
                g_peak[b]--;  /* 1 dB/sweep decay */
            }
        }

        /* Poll buttons + partial refresh every 8 bins */
        if ((b & 7) == 7) {
            btns_poll();
            handle_btns();
            full_redraw(b + 1);
        }
    }

    /* Track noise floor (slow rise, fast fall) */
    if (min_raw < (int)g_noise)
        g_noise = (int8_t)min_raw;
    else if (g_noise < (int8_t)(RSSI_CEIL - 15))
        g_noise++;

    /* Append waterfall row */
    int8_t thresh = (int8_t)((int)g_noise + 10);
    g_wf_head = (g_wf_head + 1) % WF_ROWS;
    for (int b = 0; b < BINS; b++)
        g_wf[g_wf_head][b] = (g_rssi[b] > thresh) ? 1u : 0u;
}

/* ─────────────────────────────────────────────────────────────────────
 * Button handling
 * ───────────────────────────────────────────────────────────────────── */
static void handle_btns(void)
{
    uint32_t half = g_span / 2u;

    if (rose(BTN_LEFT)) {                            /* LEFT: shift down */
        if (g_center > half + MIN_SPAN)
            g_center -= half;
    }
    if (rose(BTN_RIGHT))                             /* RIGHT: shift up  */
        g_center += half;

    if (rose(BTN_UP)) {                              /* UP: zoom in      */
        uint32_t ns = g_span / 2u;
        if (ns >= MIN_SPAN) g_span = ns;
    }
    if (rose(BTN_DOWN)) {                            /* DOWN: zoom out   */
        uint32_t ns = g_span * 2u;
        if (ns <= MAX_SPAN) g_span = ns;
    }

    if (rose(BTN_A)) {                               /* A: reset         */
        g_center = DEF_CENTER;
        g_span   = DEF_SPAN;
        g_paused = 0;
        for (int b = 0; b < BINS; b++) {
            g_rssi[b] = (int8_t)RSSI_FLOOR;
            g_peak[b] = (int8_t)RSSI_FLOOR;
            g_peak_ttl[b] = 0;
        }
        g_wf_head = 0;
        for (int r = 0; r < WF_ROWS; r++)
            for (int b = 0; b < BINS; b++)
                g_wf[r][b] = 0;
    }

    if (rose(BTN_B))                                 /* B: pause         */
        g_paused = !g_paused;

    if (rose(BTN_X))                                 /* X: waterfall     */
        g_wf_on = !g_wf_on;

    if (rose(BTN_Y)) {                               /* Y: peak hold     */
        g_hold = !g_hold;
        if (!g_hold)
            for (int b = 0; b < BINS; b++) {
                g_peak[b]     = (int8_t)RSSI_FLOOR;
                g_peak_ttl[b] = 0;
            }
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * Entry point
 * ───────────────────────────────────────────────────────────────────── */
int main(void)
{
    display_get_size(&GW, &GH);
    display_clear(BLK);

    /* Splash */
    display_rect(0, 0, GW, 20, WHT);
    display_text(4, 6, "RF SPECTRUM ANALYZER", BLK);
    display_text(4, 28, "64-bin RSSI sweep", WHT);
    display_text(4, 42, "Default: 433.920 MHz  2 MHz span", WHT);
    display_text(4, 62, "L/R  shift center", WHT);
    display_text(4, 74, "U/D  zoom in/out", WHT);
    display_text(4, 86, "A    reset  B pause  X wfall  Y peak", WHT);
    display_rect(0, GH - 20, GW, 20, WHT);
    display_text(4, GH - 14, "Press A to start", BLK);
    display_flush();

    btns_init();

    for (int b = 0; b < BINS; b++) {
        g_rssi[b]     = (int8_t)RSSI_FLOOR;
        g_peak[b]     = (int8_t)RSSI_FLOOR;
        g_peak_ttl[b] = 0;
    }
    for (int r = 0; r < WF_ROWS; r++)
        for (int b = 0; b < BINS; b++)
            g_wf[r][b] = 0;

    /* Wait for A press + release */
    while (!gpio_read(BTN_A)) delay(10000u);
    while ( gpio_read(BTN_A)) delay(10000u);

    display_clear(BLK);
    full_redraw(0);

    while (1) {
        btns_poll();
        handle_btns();

        if (!g_paused) {
            do_sweep();
        } else {
            full_redraw(0);
            delay(50000u);
        }
    }

    return 0;
}
