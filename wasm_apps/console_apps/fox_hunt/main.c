/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fox_hunt.c
 * @brief Sub-GHz signal direction finder ("fox hunt") on the CC1121.
 *
 * Locks a single frequency and turns live RSSI into a geiger-style meter: a
 * big numeric dBm readout, a signal bar, a peak-hold marker, and a blinking
 * indicator whose flash rate speeds up as the signal gets stronger. Walk
 * around and follow the rising meter to home in on a transmitter.
 *
 *   LEFT/RIGHT — change frequency preset
 *   UP/DOWN    — fine tune +/- 100 kHz
 *   A          — reset peak-hold
 *   B          — exit
 *
 * Capabilities: display.write, input.read, rf.transceive
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

typedef struct {
    const char *name;
    uint32_t    hz;
} preset_t;

static const preset_t PRESETS[] = {
    { "315.00", 315000000u },
    { "433.92", 433920000u },
    { "868.00", 868000000u },
    { "915.00", 915000000u },
};
#define NPRESETS  ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define FINE_STEP 100000u   /* 100 kHz */

#define RSSI_FLOOR (-110)
#define RSSI_CEIL  (-30)

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* "MHz.dd" formatter (no libc). */
static void fmt_mhz(char *buf, uint32_t hz)
{
    uint32_t mhz  = hz / 1000000u;
    uint32_t frac = (hz % 1000000u) / 10000u;
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

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

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

    int      preset = 1;
    uint32_t freq   = PRESETS[preset].hz;
    rf_set_frequency(freq);

    int smooth = RSSI_FLOOR; /* exponential moving average of RSSI */
    int peak   = RSSI_FLOOR;
    int blink  = 0;
    unsigned frame = 0;

    int prev = input_get_buttons();

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            return 0;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
            peak = RSSI_FLOOR;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT)) {
            preset = (preset + NPRESETS - 1) % NPRESETS;
            freq   = PRESETS[preset].hz;
            rf_set_frequency(freq);
            peak = RSSI_FLOOR;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT)) {
            preset = (preset + 1) % NPRESETS;
            freq   = PRESETS[preset].hz;
            rf_set_frequency(freq);
            peak = RSSI_FLOOR;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP)) {
            freq += FINE_STEP;
            rf_set_frequency(freq);
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN)) {
            freq -= FINE_STEP;
            rf_set_frequency(freq);
        }

        int r = clampi(rf_get_rssi(), RSSI_FLOOR, RSSI_CEIL);
        smooth = smooth + (r - smooth) / 3; /* EMA, ~1/3 weight */
        if (smooth > peak) {
            peak = smooth;
        }

        /* strength 0..100 across the displayable window */
        int strength = (smooth - RSSI_FLOOR) * 100 / (RSSI_CEIL - RSSI_FLOOR);
        strength = clampi(strength, 0, 100);

        /* Geiger blink: stronger signal -> shorter period -> faster flash. */
        int period = 12 - strength / 10; /* 12 frames (weak) .. 2 frames (hot) */
        if (period < 2) {
            period = 2;
        }
        if ((frame % (unsigned)period) == 0) {
            blink = !blink;
        }
        frame++;

        /* ---------- draw ---------- */
        display_clear(COLOR_BLACK);

        int32_t hdr_h = SCR_H / 8;
        display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
        display_text(6, hdr_h / 2 - 4, "FOX HUNT", COLOR_BLACK);
        char fbuf[12];
        fmt_mhz(fbuf, freq);
        display_text(SCR_W - 84, hdr_h / 2 - 4, fbuf, COLOR_BLACK);

        /* Big RSSI number. */
        display_text(SCR_W / 2 - 54, hdr_h + 14, "RSSI dBm", COLOR_WHITE);
        display_text_huge(SCR_W / 2 - 40, hdr_h + 30, "     ", COLOR_BLACK);
        display_number(SCR_W / 2 - 40, hdr_h + 30, smooth, COLOR_WHITE);

        /* Signal bar + peak marker. */
        int32_t bar_x = SCR_W / 10;
        int32_t bar_w = SCR_W - 2 * bar_x;
        int32_t bar_y = SCR_H / 2 + 10;
        int32_t bar_h = SCR_H / 6;
        display_progress_bar(bar_x, bar_y, bar_w, bar_h, strength, 100,
                             COLOR_WHITE, COLOR_BLACK);
        int peak_pct = clampi((peak - RSSI_FLOOR) * 100 / (RSSI_CEIL - RSSI_FLOOR), 0, 100);
        int32_t px   = bar_x + bar_w * peak_pct / 100;
        display_vline(px, bar_y - 4, bar_h + 8, COLOR_WHITE);
        display_text(bar_x, bar_y + bar_h + 6, "PEAK", COLOR_WHITE);
        display_number(bar_x + 40, bar_y + bar_h + 6, peak, COLOR_WHITE);

        /* Geiger blink dot. */
        int32_t dot_y = SCR_H - SCR_H / 8 - 10;
        if (blink && strength > 0) {
            display_circle_fill(SCR_W / 2, dot_y, 8, COLOR_WHITE);
        } else {
            display_circle(SCR_W / 2, dot_y, 8, COLOR_WHITE);
        }

        int32_t ftr_h = SCR_H / 10;
        display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
        display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                     "L/R:freq U/D:tune A:peak B:exit", COLOR_BLACK);

        display_flush();
        delay(40000); /* ~25 fps; blink timing tuned to this cadence */
    }
    return 0;
}
