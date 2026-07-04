/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file level_tool.c
 * @brief Bubble spirit level + magnetometer compass.
 *
 * Left half: a spirit level whose bubble drifts with the accelerometer — the
 * board is flat when the bubble sits in the centre ring. Right half: a compass
 * needle pointing along the horizontal magnetic-field vector from the
 * magnetometer. Two workshop tools in one, no radio needed.
 *
 *   B — exit
 *
 * Note: pitch/roll are a small-angle linear approximation of the gravity
 * projection (accurate near level, which is where it matters). The compass
 * shows raw field direction, not a tilt-compensated true heading.
 *
 * Capabilities: display.write, input.read, sensor.read
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

#define ONE_G 9810
#define LEVEL_TOL 400   /* accel units within which we call it "LEVEL" */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int iabs(int v) { return v < 0 ? -v : v; }

/* integer sqrt for vector magnitude */
static int isqrt(int v)
{
    if (v <= 0) return 0;
    int x = v, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return x;
}

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    int prev = input_get_buttons();

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            return 0;
        }

        int ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int mx = sensor_read(SENSOR_CHAN_MAGN_X);
        int my = sensor_read(SENSOR_CHAN_MAGN_Y);
        int have_acc = (ax != AKIRA_SENSOR_ERROR && ay != AKIRA_SENSOR_ERROR);
        int have_mag = (mx != AKIRA_SENSOR_ERROR && my != AKIRA_SENSOR_ERROR);

        /* ---------- draw ---------- */
        display_clear(COLOR_BLACK);

        int32_t hdr_h = SCR_H / 9;
        display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
        display_text(6, hdr_h / 2 - 4, "LEVEL & COMPASS", COLOR_BLACK);

        int32_t body_y = hdr_h;
        int32_t body_h = SCR_H - hdr_h - SCR_H / 10;
        int32_t half   = SCR_W / 2;

        /* ----- LEFT: spirit level ----- */
        int32_t lcx = half / 2;
        int32_t lcy = body_y + body_h / 2;
        int32_t lr  = (half < body_h ? half : body_h) / 2 - 10;
        if (lr < 10) lr = 10;
        display_circle(lcx, lcy, lr, COLOR_WHITE);
        display_circle(lcx, lcy, lr / 4, COLOR_WHITE); /* centre target ring */
        if (have_acc) {
            int bx = lcx + clampi(ax * lr / ONE_G, -lr, lr);
            int by = lcy + clampi(ay * lr / ONE_G, -lr, lr);
            display_circle_fill(bx, by, 6, COLOR_WHITE);
            int level = (iabs(ax) < LEVEL_TOL && iabs(ay) < LEVEL_TOL);
            display_text(lcx - 18, lcy + lr + 4, level ? "LEVEL" : "TILT", COLOR_WHITE);
            /* roll/pitch small-angle degrees */
            display_text(lcx - 30, lcy - lr - 12, "R", COLOR_WHITE);
            display_number(lcx - 18, lcy - lr - 12, ax * 90 / ONE_G, COLOR_WHITE);
            display_text(lcx + 8, lcy - lr - 12, "P", COLOR_WHITE);
            display_number(lcx + 20, lcy - lr - 12, ay * 90 / ONE_G, COLOR_WHITE);
        } else {
            display_text(lcx - 24, lcy - 4, "NO ACC", COLOR_WHITE);
        }

        /* divider */
        display_vline(half, body_y + 4, body_h - 8, COLOR_WHITE);

        /* ----- RIGHT: compass ----- */
        int32_t ccx = half + half / 2;
        int32_t ccy = body_y + body_h / 2;
        int32_t cr  = (half < body_h ? half : body_h) / 2 - 10;
        if (cr < 10) cr = 10;
        display_circle(ccx, ccy, cr, COLOR_WHITE);
        /* cardinal ticks */
        display_text(ccx - 3, ccy - cr - 10, "N", COLOR_WHITE);
        display_text(ccx - 3, ccy + cr + 2,  "S", COLOR_WHITE);
        display_text(ccx + cr + 2, ccy - 3,  "E", COLOR_WHITE);
        display_text(ccx - cr - 8, ccy - 3,  "W", COLOR_WHITE);
        if (have_mag) {
            int mag = isqrt(mx * mx + my * my);
            if (mag < 1) mag = 1;
            /* Screen up = North: point needle toward field vector. */
            int nx = ccx + mx * cr / mag;
            int ny = ccy - my * cr / mag;
            display_line(ccx, ccy, nx, ny, COLOR_WHITE);
            display_circle_fill(nx, ny, 5, COLOR_WHITE);
            display_circle_fill(ccx, ccy, 2, COLOR_WHITE);
        } else {
            display_text(ccx - 24, ccy - 4, "NO MAG", COLOR_WHITE);
        }

        int32_t ftr_h = SCR_H / 10;
        display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
        display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                     "Flatten bubble to level   B:exit", COLOR_BLACK);

        display_flush();
        delay(30000);
    }
    return 0;
}
