/*
 * Copyright (c) 2026 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file logic_analyzer.c
 * @brief 4-channel GPIO logic analyzer — real-time scrolling waveform
 *
 * Probe channels (floating/external signal input):
 *   CH1 → GPIO42   CH2 → GPIO21   CH3 → GPIO20   CH4 → GPIO19
 *
 * Controls (akiraconsole buttons, active-HIGH, pull-down):
 *   UP (4)                  increase sample rate
 *   DOWN (5)                decrease sample rate
 *   A (15) / B (16)         pause / resume capture
 *   SETTINGS (2)            pause / resume capture
 */

#include "akira_api.h"

/* ── Probe pins ──────────────────────────────────────────────────────── */
#define CH_COUNT  4
static const uint32_t CH_PIN[CH_COUNT] = { 42, 21, 20, 19 };

/* ── Button pins (akiraconsole, active-HIGH, pull-down) ──────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_A        15
#define BTN_B        16
#define BTN_SETTINGS 2

/* ── Display geometry (landscape 320×240) ────────────────────────────── *
 *   y:0-17   Header bar  (title + rate + status)
 *   y:18-217 Channels    (4 × 50px each)
 *   y:218-239 Footer     (controls hint)
 *   x:0-47   Label column  (channel name, level, pin)
 *   x:48-319 Waveform area (272px = 272 samples wide)
 * ─────────────────────────────────────────────────────────────────────── */
#define SCR_W    320
#define SCR_H    240
#define HDR_H     18
#define FTR_H     22
#define LBL_W     48
#define WAVE_X    LBL_W
#define WAVE_W   (SCR_W - LBL_W)          /* 272 pixels           */
#define WAVE_Y    HDR_H
#define CHART_H  (SCR_H - HDR_H - FTR_H)  /* 200px                */
#define CH_H     (CHART_H / CH_COUNT)      /* 50px per channel     */

/* y-offsets from the top of a CH_H row */
#define Y_HI   10   /* top of HIGH signal bar  */
#define Y_LO   38   /* top of LOW  signal bar  */
#define BAR_T   2   /* signal bar thickness    */
#define Y_MID  24   /* dotted centre reference */

/* ── Colours ─────────────────────────────────────────────────────────── */
#define COL_BG      0x0000
#define COL_HDR     0x000F   /* navy                   */
#define COL_FTR     0x0009   /* dark navy              */
#define COL_TITLE   0x07FF   /* cyan                   */
#define COL_LABEL   0x8410   /* dim grey               */
#define COL_VALUE   0xFFFF   /* white                  */
#define COL_GRID    0x18C3   /* dark guide dots        */
#define COL_SEP     0x2945   /* separator lines        */
#define COL_CURSOR  0x3186   /* write-cursor column    */
#define COL_PAUSED  0xFFE0   /* yellow PAUSED notice   */
#define COL_HINT    0x4208   /* footer hint text       */

static const uint16_t CH_COLOR[CH_COUNT] = {
    0x07E0,  /* CH1 — green   */
    0x07FF,  /* CH2 — cyan    */
    0xFFE0,  /* CH3 — yellow  */
    0xF81F,  /* CH4 — magenta */
};

/* ── Sample ring buffer ──────────────────────────────────────────────── *
 * One entry per screen pixel column (272 samples).
 * wr  = index of the column that will receive the NEXT sample.
 * prv = last displayed level per channel (used for transition drawing).
 * ─────────────────────────────────────────────────────────────────────── */
#define BUF_W  272   /* == WAVE_W; plain literal required for array decl */

static uint8_t  smp[CH_COUNT][BUF_W];
static uint8_t  prv[CH_COUNT];
static int      wr     = 0;
static int      paused = 0;

/* ── Sample rate ─────────────────────────────────────────────────────── */
static const uint32_t RATE_US[] = {
    100, 200, 500, 1000, 2000, 5000, 10000, 20000
};
static const char *RATE_STR[] = {
    "100us", "200us", "500us", "1ms", "2ms", "5ms", "10ms", "20ms"
};
#define N_RATES 8
static int rate_idx = 3;   /* default: 1 ms / 1 kHz */

/* ── Button edge state ───────────────────────────────────────────────── */
static int pup = 0, pdn = 0, pa = 0, pb = 0, pcfg = 0;

/* ── Helpers ─────────────────────────────────────────────────────────── */
static int ch_y(int ch) { return WAVE_Y + ch * CH_H; }

/* ── Header ──────────────────────────────────────────────────────────── */
static void draw_header(void) {
    display_rect(0, 0, SCR_W, HDR_H, COL_HDR);
    display_text(4,   4, "LOGIC ANALYZER",      COL_TITLE);
    display_text(216, 4, RATE_STR[rate_idx],    COL_VALUE);
    if (paused)
        display_text(272, 4, "PAUSED", COL_PAUSED);
}

static void update_header_rate(void) {
    display_rect(216, 2, 52, 14, COL_HDR);
    display_text(216, 4, RATE_STR[rate_idx], COL_VALUE);
}

static void update_header_status(void) {
    display_rect(272, 2, 46, 14, COL_HDR);
    if (paused)
        display_text(272, 4, "PAUSED", COL_PAUSED);
}

/* ── Footer ──────────────────────────────────────────────────────────── */
static void draw_footer(void) {
    display_rect(0, SCR_H - FTR_H, SCR_W, FTR_H, COL_FTR);
    display_text(4, SCR_H - FTR_H + 5,
                 "UP:FASTER  DN:SLOWER  A/B/SET:PAUSE", COL_HINT);
}

/* ── Grid (drawn once at startup) ────────────────────────────────────── */
static void draw_grid(void) {
    for (int ch = 0; ch < CH_COUNT; ch++) {
        int y = ch_y(ch);
        display_rect(0, y, SCR_W, 1, COL_SEP);
        for (int x = WAVE_X; x < SCR_W; x += 32)
            display_rect(x, y + Y_MID, 2, 1, COL_GRID);
    }
    display_rect(0, ch_y(CH_COUNT), SCR_W, 1, COL_SEP);
    display_rect(LBL_W - 1, WAVE_Y, 1, CHART_H, COL_SEP);
}

/* ── Channel label ───────────────────────────────────────────────────── */
static const char *ch_name[CH_COUNT] = { "CH1", "CH2", "CH3", "CH4" };
static const char *ch_pin [CH_COUNT] = { "G42", "G21", "G20", "G19" };

static void draw_label(int ch, int level) {
    int y = ch_y(ch);
    display_rect(0, y + 1, LBL_W - 2, CH_H - 2, COL_BG);
    display_text(2, y +  5, ch_name[ch],         CH_COLOR[ch]);
    display_text(2, y + 19, level ? " H" : " L",
                             level ? CH_COLOR[ch] : COL_LABEL);
    display_text(2, y + 33, ch_pin[ch],           COL_LABEL);
}

/* ── Waveform column ─────────────────────────────────────────────────── *
 * draw_cursor = 1  →  paint the write-cursor marker (no signal drawn)
 * draw_cursor = 0  →  paint the actual sample (signal + transition)
 * ─────────────────────────────────────────────────────────────────────── */
static void draw_col(int col, int draw_cursor) {
    int sx = WAVE_X + col;

    for (int ch = 0; ch < CH_COUNT; ch++) {
        int yt = ch_y(ch);

        /* Clear channel column */
        display_rect(sx, yt + 1, 1, CH_H - 2, COL_BG);

        /* Restore guide dot if this falls on a grid boundary */
        if ((col & 31) == 0)
            display_rect(sx, yt + Y_MID, 1, 1, COL_GRID);

        if (draw_cursor) {
            display_rect(sx, yt + 2, 1, CH_H - 4, COL_CURSOR);
        } else {
            int level = (int)smp[ch][col];
            int prev  = (int)prv[ch];
            uint16_t c = CH_COLOR[ch];

            /* Vertical transition connector */
            if (level != prev) {
                int y1 = yt + (prev  ? Y_HI : Y_LO) + BAR_T / 2;
                int y2 = yt + (level ? Y_HI : Y_LO) + BAR_T / 2;
                if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
                display_rect(sx, y1, 1, y2 - y1 + 1, c);
            }

            /* Horizontal signal bar */
            display_rect(sx, yt + (level ? Y_HI : Y_LO), 1, BAR_T, c);
        }
    }
}

/* ── Buttons ─────────────────────────────────────────────────────────── */
static void handle_buttons(void) {
    int up  = gpio_read(BTN_UP);
    int dn  = gpio_read(BTN_DOWN);
    int a   = gpio_read(BTN_A);
    int b   = gpio_read(BTN_B);
    int cfg = gpio_read(BTN_SETTINGS);

    if (up && !pup && rate_idx > 0) {
        rate_idx--;
        update_header_rate();
        display_flush();
    }
    if (dn && !pdn && rate_idx < N_RATES - 1) {
        rate_idx++;
        update_header_rate();
        display_flush();
    }
    if ((a && !pa) || (b && !pb) || (cfg && !pcfg)) {
        paused = !paused;
        update_header_status();
        display_flush();
    }

    pup = up; pdn = dn; pa = a; pb = b; pcfg = cfg;
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_gpio(void) {
    for (int ch = 0; ch < CH_COUNT; ch++)
        gpio_configure(CH_PIN[ch], GPIO_INPUT | GPIO_PULL_DOWN);

    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_DOWN);
}

static void init_screen(void) {
    display_clear(COL_BG);
    draw_header();
    draw_grid();
    draw_footer();

    for (int ch = 0; ch < CH_COUNT; ch++) {
        prv[ch] = 0;
        draw_label(ch, 0);
        for (int i = 0; i < BUF_W; i++) smp[ch][i] = 0;
    }

    /* Draw initial flat LOW waveform */
    for (int col = 0; col < BUF_W; col++)
        draw_col(col, 0);

    /* Show cursor at write position 0 */
    draw_col(0, 1);
    display_flush();
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Logic Analyzer v2.0");

    init_gpio();
    init_screen();

    while (1) {
        handle_buttons();

        if (!paused) {
            /* 1. Sample */
            for (int ch = 0; ch < CH_COUNT; ch++)
                smp[ch][wr] = (uint8_t)gpio_read(CH_PIN[ch]);

            /* 2. Draw new column (uses prv[] for transition detection) */
            draw_col(wr, 0);

            /* 3. Update labels + prv[] for channels that changed */
            for (int ch = 0; ch < CH_COUNT; ch++) {
                if (smp[ch][wr] != prv[ch]) {
                    prv[ch] = smp[ch][wr];
                    draw_label(ch, prv[ch]);
                }
            }

            /* 4. Advance write pointer */
            wr = (wr + 1) % BUF_W;

            /* 5. Mark next write position with cursor */
            draw_col(wr, 1);

            display_flush();
            delay(RATE_US[rate_idx]);
        } else {
            delay(20000);  /* poll at ~50 Hz while paused */
        }
    }

    return 0;
}
