/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file spectrum_sweep.c
 * @brief Sub-GHz RF spectrum analyzer on the CC1121.
 *
 * Steps the radio across an ISM band, sampling RSSI at each channel, and draws
 * the energy as a live bar graph with a peak-hold line. Handy for spotting an
 * active key fob / sensor / remote before diving in with a capture tool.
 *
 *   LEFT/RIGHT — change band
 *   A          — reset peak-hold
 *   B          — exit
 *
 * Note on RSSI: rf_get_rssi() returns dBm, and the transport shares the same
 * signed range with error codes, so we simply clamp the reading into the
 * displayable window rather than trying to distinguish a genuine strong signal
 * from an occasional I/O error — this is a visualizer, not a calibrated meter.
 *
 * Capabilities: display.write, input.read, rf.transceive
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

typedef struct {
    const char *name;
    uint32_t    start;
    uint32_t    stop;
} band_t;

static const band_t BANDS[] = {
    { "315 ISM",  314000000u, 316000000u },
    { "433 ISM",  433050000u, 434790000u },
    { "868 ISM",  868000000u, 870000000u },
    { "915 ISM",  902000000u, 928000000u },
    { "SUB-G ALL", 300000000u, 928000000u },
};
#define NBANDS ((int)(sizeof(BANDS) / sizeof(BANDS[0])))

#define NBINS     48
#define RSSI_MIN  (-120)
#define RSSI_MAX  (-20)

static int cur[NBINS];
static int peak[NBINS];

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t bin_freq(const band_t *b, int i)
{
    return b->start + (uint32_t)(((uint64_t)(b->stop - b->start) * (uint32_t)i) / (NBINS - 1));
}

/* Minimal "MHz.dd" formatter (no libc). */
static void fmt_mhz(char *buf, uint32_t hz)
{
    uint32_t mhz  = hz / 1000000u;
    uint32_t frac = (hz % 1000000u) / 10000u; /* two decimals */
    char tmp[12];
    int  i = 0;
    if (mhz == 0) {
        tmp[i++] = '0';
    }
    while (mhz > 0) {
        tmp[i++] = (char)('0' + mhz % 10);
        mhz /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j++] = '.';
    buf[j++] = (char)('0' + (frac / 10) % 10);
    buf[j++] = (char)('0' + frac % 10);
    buf[j]   = '\0';
}

static void reset_peaks(void)
{
    for (int i = 0; i < NBINS; i++) {
        peak[i] = RSSI_MIN;
        cur[i]  = RSSI_MIN;
    }
}

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    int32_t hdr_h = SCR_H / 8;
    int32_t ftr_h = SCR_H / 10;
    int32_t plot_y = hdr_h;
    int32_t plot_h = SCR_H - hdr_h - ftr_h;
    int32_t bar_w  = SCR_W / NBINS;
    if (bar_w < 1) {
        bar_w = 1;
    }

    if (rf_select(AKIRA_RF_CHIP_CC1121) < 0) {
        display_clear(COLOR_BLACK);
        display_text(8, SCR_H / 2 - 8, "CC1121 not available", COLOR_WHITE);
        display_flush();
        while (!AKIRA_BTN_PRESSED((uint32_t)input_get_buttons(), AKIRA_BTN_B)) {
            delay(50000);
        }
        return -1;
    }
    rf_set_modulation(RADIO_MOD_GFSK);
    rf_set_bandwidth(200000u);

    int band = 1; /* start on 433 ISM */
    reset_peaks();

    int prev = input_get_buttons();

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            return 0;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
            reset_peaks();
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT)) {
            band = (band + NBANDS - 1) % NBANDS;
            reset_peaks();
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT)) {
            band = (band + 1) % NBANDS;
            reset_peaks();
        }

        /* One full sweep across the band. */
        int max_i = 0;
        for (int i = 0; i < NBINS; i++) {
            rf_set_frequency(bin_freq(&BANDS[band], i));
            delay(500); /* let the PLL settle before sampling */
            int r  = clampi(rf_get_rssi(), RSSI_MIN, RSSI_MAX);
            cur[i] = r;
            if (r > peak[i]) {
                peak[i] = r;
            }
            if (peak[i] > peak[max_i]) {
                max_i = i;
            }
        }

        /* ---------- draw ---------- */
        display_clear(COLOR_BLACK);

        display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
        display_text(6, 4, BANDS[band].name, COLOR_BLACK);
        char lo[12], hi[12], pk[12];
        fmt_mhz(lo, BANDS[band].start);
        fmt_mhz(hi, BANDS[band].stop);
        display_text(6, hdr_h - 12, lo, COLOR_BLACK);
        display_text(SCR_W - 56, hdr_h - 12, hi, COLOR_BLACK);
        fmt_mhz(pk, bin_freq(&BANDS[band], max_i));
        display_text(SCR_W / 2 - 30, 4, pk, COLOR_BLACK);

        /* Bars (white on black) + peak-hold ticks above them. */
        for (int i = 0; i < NBINS; i++) {
            int h_cur  = (cur[i]  - RSSI_MIN) * plot_h / (RSSI_MAX - RSSI_MIN);
            int h_peak = (peak[i] - RSSI_MIN) * plot_h / (RSSI_MAX - RSSI_MIN);
            int x      = i * bar_w;
            if (h_cur > 0) {
                display_rect(x, plot_y + plot_h - h_cur, bar_w - 1 > 0 ? bar_w - 1 : 1,
                             h_cur, COLOR_WHITE);
            }
            int py = plot_y + plot_h - h_peak;
            display_hline(x, py, bar_w - 1 > 0 ? bar_w - 1 : 1, COLOR_WHITE);
        }

        display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
        display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                     "L/R:band A:reset-peak B:exit", COLOR_BLACK);

        display_flush();
    }
    return 0;
}
