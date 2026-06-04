/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file spectrum_analyzer.c
 * @brief RF spectrum analyzer — sweeps 64 frequency bins across a user-
 *        configurable center + span, displays live RSSI bar chart and
 *        scrolling waterfall history.
 *
 * Controls (akiraconsole, active-HIGH, pull-down):
 *   LEFT  (6)  — shift center frequency down by half span
 *   RIGHT (7)  — shift center frequency up by half span
 *   UP    (4)  — zoom in  (halve span)
 *   DOWN  (5)  — zoom out (double span)
 *   A     (15) — reset to defaults (433.92 MHz, 2 MHz span)
 *   B     (16) — pause / resume sweep
 *
 * Layout (320x240 landscape):
 *   y:0-17    Header    -- center freq, span, sweep progress bar
 *   y:18-119  Bar chart -- 64 bins x BIN_PX wide, RSSI amplitude (grayscale)
 *   y:120-239 Waterfall -- 120 rows of RSSI history, newest at top (grayscale)
 *
 * Palette: 16-step linear grayscale (0=black, 15=white).
 * All UI elements are black & white only -- no color.
 */

#include "akira_api.h"

/* Button pins */
#define BTN_UP     4
#define BTN_DOWN   5
#define BTN_LEFT   6
#define BTN_RIGHT  7
#define BTN_A      15
#define BTN_B      16

/* Display */
static int32_t SCR_W = 320, SCR_H = 240;

/* Layout */
#define HDR_H        18
#define BAR_AREA_Y   HDR_H
#define BAR_AREA_H   102
#define WF_Y         (HDR_H + BAR_AREA_H)
#define WF_H         (SCR_H - WF_Y)

/* Spectrum bins */
#define BINS         64
#define BIN_PX       (SCR_W / BINS)

/* RF defaults */
#define DEFAULT_CENTER_HZ  433920000U
#define DEFAULT_SPAN_HZ      2000000U
#define SETTLE_US              1000U
#define MIN_SPAN_HZ            50000U
#define MAX_SPAN_HZ        200000000U

/* RSSI range */
#define RSSI_MIN   (-120)
#define RSSI_MAX   (  -20)

/*
 * 16-step linear grayscale palette (RGB565).
 * Level n: R5 = n*2, G6 = n*4, B5 = n*2  =>  0=black, 15=white
 */
static const uint16_t k_wf_palette[16] = {
    0x0000,  /*  0 -- black    */
    0x0841,  /*  1             */
    0x1082,  /*  2             */
    0x18C3,  /*  3             */
    0x2104,  /*  4             */
    0x2945,  /*  5             */
    0x3186,  /*  6             */
    0x39C7,  /*  7             */
    0x4208,  /*  8 -- mid gray */
    0x52CA,  /*  9             */
    0x630C,  /* 10             */
    0x738E,  /* 11             */
    0x8410,  /* 12             */
    0xAD75,  /* 13             */
    0xD6BA,  /* 14             */
    0xFFFF,  /* 15 -- white    */
};

/* State */
static uint32_t g_center_hz = DEFAULT_CENTER_HZ;
static uint32_t g_span_hz   = DEFAULT_SPAN_HZ;
static int      g_paused    = 0;

static int8_t  g_rssi[BINS];

#define WF_ROWS_MAX 120
static uint8_t g_wf[WF_ROWS_MAX][BINS];
static int     g_wf_head = 0;

/* Button edge-detection */
static uint8_t g_prev_btns = 0;

static uint8_t read_buttons(void)
{
    uint8_t b = 0;
    if (gpio_read(BTN_UP))    b |= (1u << 0);
    if (gpio_read(BTN_DOWN))  b |= (1u << 1);
    if (gpio_read(BTN_LEFT))  b |= (1u << 2);
    if (gpio_read(BTN_RIGHT)) b |= (1u << 3);
    if (gpio_read(BTN_A))     b |= (1u << 4);
    if (gpio_read(BTN_B))     b |= (1u << 5);
    return b;
}

static int btn_pressed(uint8_t cur, int bit)
{
    return (cur & (1u << bit)) && !(g_prev_btns & (1u << bit));
}

static int rssi_to_palette(int8_t rssi)
{
    int v   = (int)rssi - RSSI_MIN;
    int idx = v * 15 / (RSSI_MAX - RSSI_MIN);
    if (idx < 0)  idx = 0;
    if (idx > 15) idx = 15;
    return idx;
}

static int rssi_to_bar_h(int8_t rssi)
{
    int h = ((int)rssi - RSSI_MIN) * BAR_AREA_H / (RSSI_MAX - RSSI_MIN);
    if (h < 1)          h = 1;
    if (h > BAR_AREA_H) h = BAR_AREA_H;
    return h;
}

static void fmt_freq(char *buf, uint32_t hz)
{
    int pos = 0;
    uint32_t mhz = hz / 1000000U;
    uint32_t khz = (hz % 1000000U) / 1000U;
    buf[pos++] = '0' + (char)(mhz / 100 % 10);
    buf[pos++] = '0' + (char)(mhz /  10 % 10);
    buf[pos++] = '0' + (char)(mhz       % 10);
    buf[pos++] = '.';
    buf[pos++] = '0' + (char)(khz / 100 % 10);
    buf[pos++] = '0' + (char)(khz /  10 % 10);
    buf[pos++] = '0' + (char)(khz       % 10);
    buf[pos++] = 'M';
    buf[pos]   = '\0';
}

static void fmt_span(char *buf, uint32_t hz)
{
    int pos = 0;
    uint32_t k = hz / 1000U;
    if (k >= 1000) {
        uint32_t m = k / 1000U;
        buf[pos++] = '0' + (char)(m / 100 % 10);
        buf[pos++] = '0' + (char)(m /  10 % 10);
        buf[pos++] = '0' + (char)(m       % 10);
        buf[pos++] = 'M';
    } else {
        buf[pos++] = '0' + (char)(k / 1000 % 10);
        buf[pos++] = '0' + (char)(k /  100 % 10);
        buf[pos++] = '0' + (char)(k /   10 % 10);
        buf[pos++] = '0' + (char)(k        % 10);
        buf[pos++] = 'k';
    }
    buf[pos] = '\0';
}

static void draw_header(int sweep_idx)
{
    char buf[24];

    display_rect(0, 0, (int)SCR_W, HDR_H, COLOR_BLACK);

    display_text(2, 5, "RF", COLOR_GRAY);

    fmt_freq(buf, g_center_hz);
    display_text(18, 5, buf, COLOR_WHITE);

    fmt_span(buf, g_span_hz);
    display_text(104, 5, buf, COLOR_LIGHT_GRAY);
    display_text(136, 5, "span", COLOR_DARK_GRAY);

    if (g_paused) {
        display_rect(216, 4, 80, 10, COLOR_BLACK);
        display_text(220, 5, "[ PAUSED ]", COLOR_LIGHT_GRAY);
    } else {
        display_rect(216, 6, 88, 5, COLOR_DARK_GRAY);
        int fill = sweep_idx * 88 / BINS;
        if (fill > 0)
            display_rect(216, 6, fill, 5, COLOR_WHITE);
    }

    display_hline(0, HDR_H - 1, (int)SCR_W, COLOR_WHITE);
}

static void draw_bars(void)
{
    int bin, x, bar_h, bar_y;
    int sw = (int)SCR_W;

    display_rect(0, BAR_AREA_Y, sw, BAR_AREA_H, COLOR_BLACK);

    /* Noise floor reference at RSSI_MIN+10 dBm */
    int ref_h = 10 * BAR_AREA_H / (RSSI_MAX - RSSI_MIN);
    display_hline(0, BAR_AREA_Y + BAR_AREA_H - ref_h, sw, COLOR_DARK_GRAY);

    /* Vertical grid every 8 bins */
    for (bin = 8; bin < BINS; bin += 8) {
        x = bin * BIN_PX;
        display_line(x, BAR_AREA_Y, x, BAR_AREA_Y + BAR_AREA_H - 1, COLOR_DARK_GRAY);
    }

    /* dBm scale on right edge */
    display_text(sw - 22, BAR_AREA_Y + 2,              "-20",  COLOR_DARK_GRAY);
    display_text(sw - 28, BAR_AREA_Y + BAR_AREA_H - 9, "-120", COLOR_DARK_GRAY);

    /* Bars — clamp palette min to 2 so bars are always visible on black bg */
    for (bin = 0; bin < BINS; bin++) {
        x     = bin * BIN_PX;
        bar_h = rssi_to_bar_h(g_rssi[bin]);
        bar_y = BAR_AREA_Y + BAR_AREA_H - bar_h;
        int pal = rssi_to_palette(g_rssi[bin]);
        if (pal < 2) pal = 2;
        display_rect(x, bar_y, BIN_PX - 1, bar_h, k_wf_palette[pal]);
    }

    display_hline(0, WF_Y, sw, COLOR_WHITE);
}

static void draw_waterfall(void)
{
    int row, bin;
    int wf_rows = (int)WF_H;

    for (row = 0; row < wf_rows && row < WF_ROWS_MAX; row++) {
        int idx = (g_wf_head - row + WF_ROWS_MAX) % WF_ROWS_MAX;
        int y   = WF_Y + row;
        for (bin = 0; bin < BINS; bin++) {
            int x = bin * BIN_PX;
            display_rect(x, y, BIN_PX - 1, 1, k_wf_palette[g_wf[idx][bin]]);
        }
    }
}

static void do_sweep(void)
{
    int bin;
    uint32_t step = g_span_hz / BINS;

    for (bin = 0; bin < BINS; bin++) {
        uint32_t freq = (g_center_hz - g_span_hz / 2) + (uint32_t)bin * step;
        rf_set_frequency(freq);
        delay(SETTLE_US);
        int raw = rf_get_rssi();
        if (raw < RSSI_MIN) raw = RSSI_MIN;
        if (raw > RSSI_MAX) raw = RSSI_MAX;
        g_rssi[bin] = (int8_t)raw;

        if ((bin & 7) == 0) {
            draw_header(bin);
            display_flush();
        }
    }

    g_wf_head = (g_wf_head + 1) % WF_ROWS_MAX;
    for (bin = 0; bin < BINS; bin++) {
        int wp = rssi_to_palette(g_rssi[bin]);
        if (wp < 1) wp = 1;
        g_wf[g_wf_head][bin] = (uint8_t)wp;
    }
}

static void handle_buttons(uint8_t cur)
{
    uint32_t half = g_span_hz / 2;

    if (btn_pressed(cur, 2)) {
        if (g_center_hz > half + DEFAULT_SPAN_HZ)
            g_center_hz -= half;
    }
    if (btn_pressed(cur, 3))
        g_center_hz += half;

    if (btn_pressed(cur, 0)) {
        uint32_t ns = g_span_hz / 2;
        if (ns >= MIN_SPAN_HZ) g_span_hz = ns;
    }
    if (btn_pressed(cur, 1)) {
        uint32_t ns = g_span_hz * 2;
        if (ns <= MAX_SPAN_HZ) g_span_hz = ns;
    }

    if (btn_pressed(cur, 4)) {
        g_center_hz = DEFAULT_CENTER_HZ;
        g_span_hz   = DEFAULT_SPAN_HZ;
        g_paused    = 0;
    }
    if (btn_pressed(cur, 5))
        g_paused = !g_paused;
}

static void draw_start_screen(void)
{
    int i;
    int sw = (int)SCR_W, sh = (int)SCR_H;

    printf("[spectrum_analyzer] display: %dx%d\n", sw, sh);

    display_clear(COLOR_BLACK);

    /* White title bar across the top */
    display_rect(0, 0, sw, 20, COLOR_WHITE);
    display_text(4, 6, "SPECTRUM ANALYZER", COLOR_BLACK);

    /* Section: subtitle */
    display_text(4, 28, "64-bin RF scanner (RSSI sweep)", COLOR_LIGHT_GRAY);

    /* Grayscale ramp strip */
    for (i = 0; i < sw; i++) {
        int pal = (i * 15) / sw;
        display_rect(i, 40, 1, 6, k_wf_palette[pal]);
    }

    /* White divider */
    display_rect(0, 48, sw, 1, COLOR_DARK_GRAY);

    /* Controls */
    display_text(4, 54,  "LEFT/RIGHT   shift center freq",   COLOR_LIGHT_GRAY);
    display_text(4, 66,  "UP/DOWN      zoom span in/out",    COLOR_LIGHT_GRAY);
    display_text(4, 78,  "A            reset to default",    COLOR_LIGHT_GRAY);
    display_text(4, 90,  "B            pause / resume",      COLOR_LIGHT_GRAY);

    /* Default freq info */
    display_rect(0, 104, sw, 1, COLOR_DARK_GRAY);
    display_text(4, 108, "Default: 433.920 MHz  2 MHz span", COLOR_GRAY);

    /* Press A prompt — white box at bottom */
    display_rect(0, sh - 18, sw, 18, COLOR_WHITE);
    display_text(4, sh - 13, "Press A to start", COLOR_BLACK);

    display_flush();
}

int main(void)
{
    int i;

    printf("[spectrum_analyzer] starting\n");
    display_get_size(&SCR_W, &SCR_H);

    /* Black screen immediately */
    display_clear(COLOR_BLACK);
    display_flush();

    gpio_configure(BTN_UP,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,  GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,  GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,     GPIO_INPUT | GPIO_PULL_DOWN);

    for (i = 0; i < BINS; i++)
        g_rssi[i] = (int8_t)RSSI_MIN;

    for (i = 0; i < WF_ROWS_MAX; i++) {
        int b;
        for (b = 0; b < BINS; b++)
            g_wf[i][b] = 0;
    }

    draw_start_screen();
    while (!gpio_read(BTN_A))
        delay(10000);
    while (gpio_read(BTN_A))
        delay(10000);

    display_clear(COLOR_BLACK);
    draw_header(0);
    draw_bars();
    draw_waterfall();
    display_flush();

    while (1) {
        uint8_t cur = read_buttons();
        handle_buttons(cur);
        g_prev_btns = cur;

        if (!g_paused) {
            do_sweep();
            draw_bars();
            draw_waterfall();
            draw_header(BINS);
        } else {
            draw_header(0);
        }

        display_flush();
        delay(5000);
    }

    return 0;
}
