/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief akira.radio.scan — RF spectrum waterfall
 *
 * Sweeps the CC1121 transceiver across N_COLS frequency buckets and renders
 * a scrolling RSSI waterfall on the 320x240 ST7789V display.  Color encodes
 * signal strength: black -> blue -> cyan -> green -> yellow -> red.
 *
 * Display layout (landscape 320x240):
 *   y:  0-17   Header  — title, center freq, step, status
 *   y: 18-137  Waterfall — 120 rows of sweep history (oldest top, newest bottom)
 *   y:138-197  Spectrum  — live bar chart of current sweep + peak hold
 *   y:198-219  Info row  — peak frequency and estimated dBm
 *   y:220-239  Footer    — button hints
 *
 *   x:  0-47   Label column (freq range + dBm scale)
 *   x: 48-319  272 frequency buckets
 *
 * Controls (AkiraConsole input API):
 *   UP / DOWN    shift center frequency by half the current span
 *   LEFT / RIGHT zoom channel step ÷2 / x2
 *   A            pause / resume sweep
 *   B            toggle auto noise floor
 *   Y            reset to defaults
 */

#include "akira_api.h"

/* ── Display geometry ─────────────────────────────────────────────────── */
#define SCR_W     320
#define SCR_H     240
#define HDR_H      18
#define FTR_Y     220
#define FTR_H      20

#define LBL_W      48
#define WAVE_X     LBL_W
#define WAVE_W    (SCR_W - LBL_W)   /* 272 freq buckets */

#define FALL_Y     HDR_H
#define FALL_H    120
#define SPEC_Y    (FALL_Y + FALL_H)  /* 138 */
#define SPEC_H     60
#define INFO_Y    (SPEC_Y + SPEC_H)  /* 198 */
#define INFO_H     22

#define N_COLS    WAVE_W             /* 272 */
#define N_ROWS    FALL_H             /* 120 */

/* ── Colors ───────────────────────────────────────────────────────────── */
#define COL_BG     0x0000
#define COL_HDR    0x000F
#define COL_FTR    0x0009
#define COL_TITLE  0x07FF
#define COL_SEP    0x2945
#define COL_LABEL  0x7BEF
#define COL_WHITE  0xFFFF
#define COL_YELLOW 0xFFE0
#define COL_RED    0xF800
#define COL_GREEN  0x07E0
#define COL_PEAK   0xFFFF

/* RSSI-to-color palette: black -> dark-blue -> blue -> cyan -> green -> yellow -> orange -> red */
static const uint16_t PALETTE[8] = {
    0x0000,  /* [  0..31] black      */
    0x0009,  /* [ 32..63] dark blue  */
    0x001F,  /* [ 64..95] blue       */
    0x07FF,  /* [ 96..127] cyan      */
    0x07E0,  /* [128..159] green     */
    0xFFE0,  /* [160..191] yellow    */
    0xFD20,  /* [192..223] orange    */
    0xF800,  /* [224..255] red       */
};

/* ── RF state ─────────────────────────────────────────────────────────── */
#define DEFAULT_CENTER_HZ   433920000U
#define DEFAULT_STEP_IDX    1

static const uint32_t STEP_HZ[] = {
    5000, 10000, 25000, 50000, 100000, 200000, 500000, 1000000
};
static const char *STEP_LBL[] = {
    "5k", "10k", "25k", "50k", "100k", "200k", "500k", "1M"
};
#define N_STEPS 8

#define RSSI_FLOOR_DEFAULT  (-120)
#define RSSI_CEIL_DEFAULT   (-20)

/* ── App state ────────────────────────────────────────────────────────── */
static uint8_t  wfall[N_ROWS][N_COLS];  /* waterfall amplitude ring buffer */
static uint8_t  spectrum[N_COLS];       /* current sweep amplitudes        */
static uint8_t  peak_hold[N_COLS];      /* peak-hold per bucket            */
static uint16_t row_buf[N_COLS];        /* scratch RGB565 row for raw blit */

static uint32_t center_hz  = DEFAULT_CENTER_HZ;
static int      step_idx   = DEFAULT_STEP_IDX;
static int      rssi_floor = RSSI_FLOOR_DEFAULT;
static int      rssi_ceil  = RSSI_CEIL_DEFAULT;
static int      paused     = 0;
static int      auto_gain  = 0;
static int      wfall_wr   = 0;   /* next ring-buffer row to write */

static uint32_t prev_btns  = 0;

/* ── Integer helpers (no stdlib) ──────────────────────────────────────── */
static char *int_to_buf(char *p, int32_t v)
{
    if (v < 0) { *p++ = '-'; v = -v; }
    char tmp[12]; int n = 0;
    if (v == 0) { *p++ = '0'; *p = '\0'; return p; }
    while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    while (n > 0) *p++ = tmp[--n];
    *p = '\0';
    return p;
}

static char *uint_to_buf(char *p, uint32_t v)
{
    char tmp[12]; int n = 0;
    if (v == 0) { *p++ = '0'; *p = '\0'; return p; }
    while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    while (n > 0) *p++ = tmp[--n];
    *p = '\0';
    return p;
}

static char *str_put(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    *d = '\0';
    return d;
}

static void zero_pad3(char *p, uint32_t v)
{
    p[0] = '0' + (int)((v / 100) % 10);
    p[1] = '0' + (int)((v /  10) % 10);
    p[2] = '0' + (int)((v /   1) % 10);
    p[3] = '\0';
}

/* freq_to_buf: writes "###.###MHz" */
static char *freq_to_buf(char *p, uint32_t hz)
{
    p = uint_to_buf(p, hz / 1000000);
    *p++ = '.';
    uint32_t sub_khz = (hz % 1000000) / 1000;
    zero_pad3(p, sub_khz); p += 3;
    p = str_put(p, "MHz");
    return p;
}

/* ── Amplitude mapping ────────────────────────────────────────────────── */
static uint8_t rssi_to_amp(int rssi)
{
    int span = rssi_ceil - rssi_floor;
    if (span < 1) span = 1;
    int amp = (rssi - rssi_floor) * 255 / span;
    if (amp < 0)   amp = 0;
    if (amp > 255) amp = 255;
    return (uint8_t)amp;
}

static uint16_t amp_to_color(uint8_t amp)
{
    return PALETTE[amp >> 5];
}

/* ── Compute start frequency ──────────────────────────────────────────── */
static uint32_t start_hz(void)
{
    uint32_t half = (uint32_t)(N_COLS / 2) * STEP_HZ[step_idx];
    return (center_hz > half) ? (center_hz - half) : 0;
}

/* ── Draw header ──────────────────────────────────────────────────────── */
static void draw_header(void)
{
    display_rect(0, 0, SCR_W, HDR_H, COL_HDR);
    display_text(4, 4, "RF SCAN", COL_TITLE);

    char buf[64];
    char *p = buf;

    /* Center frequency */
    p = str_put(p, "CTR:");
    p = freq_to_buf(p, center_hz);
    display_text(64, 4, buf, COL_WHITE);

    /* Step */
    p = buf; p = str_put(p, "STP:"); p = str_put(p, STEP_LBL[step_idx]);
    display_text(214, 4, buf, COL_YELLOW);

    /* Status flags */
    if (paused)     display_text(278, 4, "PAUSED", COL_RED);
    else if (auto_gain) display_text(284, 4, "AUTO",  COL_GREEN);
}

/* ── Draw footer ──────────────────────────────────────────────────────── */
static void draw_footer(void)
{
    display_rect(0, FTR_Y, SCR_W, FTR_H, COL_FTR);
    display_text(4, FTR_Y + 5,
        "U/D:FREQ  L/R:ZOOM  A:PAUSE  B:AUTO  Y:RESET", 0x4208);
}

/* ── Frequency axis (left label column) ──────────────────────────────── */
static void draw_axis_labels(void)
{
    display_rect(0, FALL_Y, LBL_W, FALL_H + SPEC_H + INFO_H, COL_BG);

    /* Waterfall zone: start/end frequency */
    char buf[16];
    freq_to_buf(buf, start_hz());
    display_text(2, FALL_Y + 2,  buf, COL_LABEL);

    uint32_t end = start_hz() + (uint32_t)N_COLS * STEP_HZ[step_idx];
    freq_to_buf(buf, end);
    display_text(2, FALL_Y + FALL_H - 10, buf, COL_LABEL);

    /* Spectrum zone: dBm scale */
    int_to_buf(buf, rssi_ceil);
    display_text(2, SPEC_Y + 2,  buf, COL_LABEL);
    int_to_buf(buf, rssi_floor);
    display_text(2, SPEC_Y + SPEC_H - 10, buf, COL_LABEL);
}

/* ── Redraw full waterfall from ring buffer ───────────────────────────── */
static void redraw_waterfall(void)
{
    for (int row = 0; row < N_ROWS; row++) {
        int buf_idx = (wfall_wr + row) % N_ROWS;
        for (int col = 0; col < N_COLS; col++)
            row_buf[col] = amp_to_color(wfall[buf_idx][col]);
        display_raw_write(WAVE_X, FALL_Y + row, N_COLS, 1,
                          row_buf, (uint32_t)(N_COLS * 2));
    }
}

/* ── Draw spectrum bar chart ──────────────────────────────────────────── */
static void draw_spectrum(void)
{
    display_rect(WAVE_X, SPEC_Y, WAVE_W, SPEC_H, COL_BG);
    display_rect(WAVE_X, SPEC_Y, WAVE_W, 1, COL_SEP);

    int max_bar = SPEC_H - 2;
    for (int col = 0; col < N_COLS; col++) {
        uint8_t amp = spectrum[col];
        int bar_h = ((int)amp * max_bar) / 255;
        if (bar_h < 1) bar_h = 1;
        int bx = WAVE_X + col;
        int by = SPEC_Y + SPEC_H - bar_h;
        display_rect(bx, by, 1, bar_h, amp_to_color(amp));

        /* Peak-hold pixel */
        uint8_t pk = peak_hold[col];
        int pk_h = ((int)pk * max_bar) / 255;
        int pk_y = SPEC_Y + SPEC_H - pk_h - 1;
        if (pk_y >= SPEC_Y)
            display_rect(bx, pk_y, 1, 1, COL_PEAK);
    }
}

/* ── Info row: peak frequency + estimated dBm ────────────────────────── */
static void draw_info_row(void)
{
    display_rect(0, INFO_Y, SCR_W, INFO_H, COL_BG);
    display_rect(0, INFO_Y, SCR_W, 1, COL_SEP);

    /* Find column with highest amplitude */
    int pk_col = 0;
    uint8_t pk_amp = 0;
    for (int i = 0; i < N_COLS; i++) {
        if (spectrum[i] > pk_amp) { pk_amp = spectrum[i]; pk_col = i; }
    }

    uint32_t pk_freq = start_hz() + (uint32_t)pk_col * STEP_HZ[step_idx];
    int      pk_dbm  = rssi_floor +
                       ((int)pk_amp * (rssi_ceil - rssi_floor)) / 255;

    char buf[48];
    char *p = str_put(buf, "PEAK ");
    p = freq_to_buf(p, pk_freq);
    p = str_put(p, "  (");
    p = int_to_buf(p, pk_dbm);
    str_put(p, " dBm)");

    display_text(WAVE_X, INFO_Y + 5, buf, COL_TITLE);
}

/* ── Perform one full sweep ───────────────────────────────────────────── */
static void do_sweep(void)
{
    uint32_t f0 = start_hz();

    int auto_obs_min = 0;

    for (int col = 0; col < N_COLS; col++) {
        uint32_t freq = f0 + (uint32_t)col * STEP_HZ[step_idx];
        rf_set_frequency(freq);
        delay(300);  /* 300 µs settle */

        int16_t rssi_raw = 0;
        rf_get_rssi(&rssi_raw);

        uint8_t amp = rssi_to_amp((int)rssi_raw);
        spectrum[col] = amp;

        /* Peak hold with slow decay */
        if (amp > peak_hold[col]) peak_hold[col] = amp;
        else if (peak_hold[col] > 0) peak_hold[col]--;

        /* Track minimum for auto-gain */
        if (col == 0 || (int)rssi_raw < auto_obs_min)
            auto_obs_min = (int)rssi_raw;

        wfall[wfall_wr][col] = amp;
    }

    /* Auto noise floor: gentle IIR toward observed floor - 5 dBm headroom */
    if (auto_gain) {
        int target = auto_obs_min - 5;
        rssi_floor = (rssi_floor * 3 + target) / 4;
        if (rssi_floor > rssi_ceil - 10)
            rssi_floor = rssi_ceil - 10;
    }

    wfall_wr = (wfall_wr + 1) % N_ROWS;

    redraw_waterfall();
    draw_spectrum();
    draw_info_row();
    draw_axis_labels();
    display_flush();
}

/* ── Handle button edges ──────────────────────────────────────────────── */
static void handle_buttons(void)
{
    uint32_t btns    = (uint32_t)input_get_buttons();
    uint32_t pressed = btns & ~prev_btns;
    prev_btns = btns;

    if (!pressed) return;

    /* Frequency shift = half the current displayed span */
    uint32_t shift = (uint32_t)(N_COLS / 2) * STEP_HZ[step_idx];
    if (shift < 1000000) shift = 1000000;

    int changed = 0;

    if (pressed & AKIRA_BTN_UP) {
        center_hz += shift;
        changed = 1;
    }
    if (pressed & AKIRA_BTN_DOWN) {
        if (center_hz > shift) center_hz -= shift;
        else center_hz = (uint32_t)(N_COLS / 2) * STEP_HZ[step_idx];
        changed = 1;
    }
    if ((pressed & AKIRA_BTN_RIGHT) && step_idx < N_STEPS - 1) {
        step_idx++;
        changed = 1;
    }
    if ((pressed & AKIRA_BTN_LEFT) && step_idx > 0) {
        step_idx--;
        changed = 1;
    }
    if (pressed & AKIRA_BTN_A) {
        paused = !paused;
        changed = 1;
    }
    if (pressed & AKIRA_BTN_B) {
        auto_gain = !auto_gain;
        if (!auto_gain) {
            rssi_floor = RSSI_FLOOR_DEFAULT;
            rssi_ceil  = RSSI_CEIL_DEFAULT;
        }
        changed = 1;
    }
    if (pressed & AKIRA_BTN_Y) {
        center_hz  = DEFAULT_CENTER_HZ;
        step_idx   = DEFAULT_STEP_IDX;
        rssi_floor = RSSI_FLOOR_DEFAULT;
        rssi_ceil  = RSSI_CEIL_DEFAULT;
        auto_gain  = 0;
        paused     = 0;
        wfall_wr   = 0;
        for (int i = 0; i < N_COLS; i++) {
            spectrum[i]  = 0;
            peak_hold[i] = 0;
        }
        for (int r = 0; r < N_ROWS; r++)
            for (int c = 0; c < N_COLS; c++)
                wfall[r][c] = 0;
        redraw_waterfall();
        changed = 1;
    }

    if (changed) {
        draw_header();
        draw_axis_labels();
        display_flush();
    }
}

/* ── Entry point ──────────────────────────────────────────────────────── */
int main(void)
{
    printf("akira.radio.scan v1.0");

    /* Zero state */
    for (int i = 0; i < N_COLS; i++) { spectrum[i] = 0; peak_hold[i] = 0; }
    for (int r = 0; r < N_ROWS; r++)
        for (int c = 0; c < N_COLS; c++)
            wfall[r][c] = 0;

    /* Initial render */
    display_clear(COL_BG);
    draw_header();
    draw_footer();
    draw_axis_labels();
    display_rect(WAVE_X, FALL_Y, WAVE_W, FALL_H, COL_BG);
    display_rect(WAVE_X, SPEC_Y, WAVE_W, SPEC_H, COL_BG);
    display_rect(0,      INFO_Y, SCR_W,  INFO_H,  COL_BG);
    display_rect(WAVE_X, FALL_Y, WAVE_W, 1, COL_SEP);  /* waterfall top border */
    display_flush();

    while (1) {
        handle_buttons();
        if (!paused)
            do_sweep();
        else
            delay(20000);
    }

    return 0;
}
