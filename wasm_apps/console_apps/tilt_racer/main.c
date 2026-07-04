/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file tilt_racer.c
 * @brief Motion game controller (USB HID gamepad).
 *
 * The accelerometer drives two analog axes — tilt left/right to steer, tilt
 * fore/aft for throttle — while the face buttons map to gamepad buttons. Plug
 * the console into a PC and it enumerates as a gamepad; use it as a motion
 * racing wheel.
 *
 *   TILT     — steer (X axis) + throttle (Y axis)
 *   A/X/Y    — gamepad buttons 1/2/3
 *   B        — exit
 *
 * Capabilities: display.write, input.read, sensor.read, hid
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

#define ONE_G      9810
#define AXIS_MAX   32767
#define AXIS_DEAD  1000   /* accel units treated as centred */

/* gamepad button bits */
#define GP_A 0x01
#define GP_B 0x02
#define GP_C 0x04

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* accel axis (m/s^2 x1000) -> gamepad analog axis (-32768..32767) */
static int accel_to_axis(int a)
{
    if (a > -AXIS_DEAD && a < AXIS_DEAD) {
        return 0;
    }
    return clampi(a * AXIS_MAX / ONE_G, -AXIS_MAX, AXIS_MAX);
}

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);
    hid_init(HID_TRANSPORT_USB, HID_DEVICE_GAMEPAD);

    int prev  = input_get_buttons();
    int gpmask = 0;

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            hid_gamepad_reset();
            hid_disable();
            return 0;
        }

        /* Buttons: diff current desired mask against last, press/release edges. */
        int want = 0;
        if (AKIRA_BTN_PRESSED(held, AKIRA_BTN_A)) want |= GP_A;
        if (AKIRA_BTN_PRESSED(held, AKIRA_BTN_X)) want |= GP_B;
        if (AKIRA_BTN_PRESSED(held, AKIRA_BTN_Y)) want |= GP_C;
        int newp = want & ~gpmask;
        int rel  = gpmask & ~want;
        if (newp) hid_gamepad_press(newp);
        if (rel)  hid_gamepad_release(rel);
        gpmask = want;

        /* Axes from tilt. */
        int ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int have_imu = (ax != AKIRA_SENSOR_ERROR && ay != AKIRA_SENSOR_ERROR);
        int steer = 0, gas = 0;
        if (have_imu) {
            steer = accel_to_axis(ax);
            gas   = accel_to_axis(ay);
            hid_gamepad_set_axis(0, steer);
            hid_gamepad_set_axis(1, gas);
        }

        /* ---------- draw ---------- */
        display_clear(COLOR_BLACK);

        int32_t hdr_h = SCR_H / 8;
        display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
        display_text(6, hdr_h / 2 - 4, "TILT RACER", COLOR_BLACK);
        display_text(SCR_W - 78, hdr_h / 2 - 4,
                     hid_is_connected() ? "USB OK" : "USB...", COLOR_BLACK);

        /* Steering wheel: a needle rotating with the steer axis. */
        int32_t cx = SCR_W / 2;
        int32_t cy = hdr_h + (SCR_H - hdr_h) / 3;
        int32_t r  = (SCR_H - hdr_h) / 4;
        display_circle(cx, cy, r, COLOR_WHITE);
        /* Horizontal offset of the wheel's top spoke encodes steering. */
        int32_t tipx = cx + steer * r / AXIS_MAX;
        int32_t tipy = cy - r;
        display_line(cx, cy, tipx, tipy, COLOR_WHITE);
        display_circle_fill(tipx, tipy, 4, COLOR_WHITE);

        /* Axis bars. */
        int32_t bx = SCR_W / 8;
        int32_t bw = SCR_W - 2 * bx;
        int32_t sy = cy + r + 10;
        display_text(bx, sy - 10, "STEER", COLOR_WHITE);
        display_progress_bar(bx, sy, bw, 12, steer + AXIS_MAX, 2 * AXIS_MAX,
                             COLOR_WHITE, COLOR_BLACK);
        display_vline(bx + bw / 2, sy - 2, 16, COLOR_WHITE); /* centre mark */
        int32_t gy = sy + 24;
        display_text(bx, gy - 10, "THROTTLE", COLOR_WHITE);
        display_progress_bar(bx, gy, bw, 12, gas + AXIS_MAX, 2 * AXIS_MAX,
                             COLOR_WHITE, COLOR_BLACK);
        display_vline(bx + bw / 2, gy - 2, 16, COLOR_WHITE);

        if (!have_imu) {
            display_text(cx - 24, cy - 4, "NO IMU", COLOR_WHITE);
        }

        /* Button pips. */
        int32_t py = SCR_H - SCR_H / 10 - 22;
        const char *lbl[3] = { "A", "X", "Y" };
        int bits[3] = { GP_A, GP_B, GP_C };
        for (int i = 0; i < 3; i++) {
            int32_t px = bx + i * 34;
            if (gpmask & bits[i]) display_rect(px, py, 26, 16, COLOR_WHITE);
            else                  display_rect_outline(px, py, 26, 16, COLOR_WHITE);
            display_text(px + 9, py + 4, lbl[i],
                         (gpmask & bits[i]) ? COLOR_BLACK : COLOR_WHITE);
        }

        int32_t ftr_h = SCR_H / 10;
        display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
        display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                     "Tilt=steer/gas A/X/Y=btn B:exit", COLOR_BLACK);

        display_flush();
        delay(15000);
    }
    return 0;
}
