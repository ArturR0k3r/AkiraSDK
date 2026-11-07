/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file air_mouse.c
 * @brief Tilt-controlled HID air mouse.
 *
 * The console's accelerometer drives the host cursor: tilt the board and the
 * pointer glides in that direction. Buttons handle clicks and scroll. Works as
 * a USB HID mouse (plug in, instant pointer) or a BLE HID mouse (wireless).
 *
 *   A      — left button   (hold to drag)
 *   Y      — right button
 *   UP/DN  — scroll wheel  up / down
 *   X      — toggle transport USB <-> BLE
 *   B      — exit
 *
 * Capabilities: display.write, input.read, sensor.read, hid
 */

#include "akira_api.h"

/* Runtime display size — never hardcode 320x240. */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/*
 * Tilt -> motion tuning. sensor_read() returns m/s^2 x1000, so ~9810 == 1g
 * projected onto an axis when the board is fully tilted that way.
 *   DEADZONE — small tilts near level produce no motion (cursor holds still).
 *   DIVISOR  — sensitivity; smaller = faster cursor.
 *   MAX_STEP — clamp per-frame travel so a hard tilt can't fling the pointer.
 * SIGN_* let you flip an axis if it feels reversed for how the board is held.
 */
#define TILT_DEADZONE 1200
#define TILT_DIVISOR  380
#define TILT_MAX_STEP 22
#define SIGN_X        (+1)
#define SIGN_Y        (-1)
#define ONE_G         9810

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Map one accel axis (m/s^2 x1000) to a per-frame pixel delta. */
static int tilt_to_delta(int a)
{
    if (a > -TILT_DEADZONE && a < TILT_DEADZONE) {
        return 0;
    }
    return clampi(a / TILT_DIVISOR, -TILT_MAX_STEP, TILT_MAX_STEP);
}

static int g_transport = HID_TRANSPORT_USB;

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);
    hid_init(g_transport, HID_DEVICE_MOUSE);

    int32_t hdr_h = SCR_H / 9;
    int32_t ftr_h = SCR_H / 9;
    int32_t cx    = SCR_W / 2;
    int32_t cy    = hdr_h + (SCR_H - hdr_h - ftr_h) / 2;
    int32_t gauge_r = (SCR_H - hdr_h - ftr_h) / 2 - 6;
    if (gauge_r < 10) {
        gauge_r = 10;
    }

    /* Seed prev from current state so a button still held from launch (e.g. the
     * A-press that started this app) is not read as a fresh press. */
    int prev   = input_get_buttons();
    int l_down = 0;
    int r_down = 0;

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            hid_key_release_all();
            hid_disable();
            return 0;
        }

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_X)) {
            hid_disable();
            g_transport = (g_transport == HID_TRANSPORT_USB) ? HID_TRANSPORT_BLE
                                                             : HID_TRANSPORT_USB;
            hid_init(g_transport, HID_DEVICE_MOUSE);
        }

        /* Clicks are hold-to-hold so click-and-drag works. */
        int want_l = AKIRA_BTN_PRESSED(held, AKIRA_BTN_A);
        int want_r = AKIRA_BTN_PRESSED(held, AKIRA_BTN_Y);
        if (want_l && !l_down) { hid_mouse_btn_press(HID_MOUSE_BTN_LEFT);   l_down = 1; }
        if (!want_l && l_down) { hid_mouse_btn_release(HID_MOUSE_BTN_LEFT); l_down = 0; }
        if (want_r && !r_down) { hid_mouse_btn_press(HID_MOUSE_BTN_RIGHT);  r_down = 1; }
        if (!want_r && r_down) { hid_mouse_btn_release(HID_MOUSE_BTN_RIGHT); r_down = 0; }

        if (AKIRA_BTN_PRESSED(held, AKIRA_BTN_UP))   hid_mouse_scroll(+1);
        if (AKIRA_BTN_PRESSED(held, AKIRA_BTN_DOWN)) hid_mouse_scroll(-1);

        int ax       = sensor_read(SENSOR_CHAN_ACCEL_X);
        int ay       = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int have_imu = (ax != AKIRA_SENSOR_ERROR && ay != AKIRA_SENSOR_ERROR);
        if (have_imu) {
            int dx = SIGN_X * tilt_to_delta(ax);
            int dy = SIGN_Y * tilt_to_delta(ay);
            if (dx || dy) {
                hid_mouse_move(dx, dy);
            }
        }

        /* ---------- draw (strictly black/white: display is 2-state) ---------- */
        display_clear(COLOR_BLACK);

        display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
        display_text(6, hdr_h / 2 - 4, "AIR MOUSE", COLOR_BLACK);
        display_text(SCR_W - 78, hdr_h / 2 - 4,
                     g_transport == HID_TRANSPORT_USB ? "USB" : "BLE", COLOR_BLACK);
        if (hid_is_connected()) {
            display_circle_fill(SCR_W - 14, hdr_h / 2, 4, COLOR_BLACK);
        } else {
            display_circle(SCR_W - 14, hdr_h / 2, 4, COLOR_BLACK);
        }

        /* Tilt gauge: dot shows the direction gravity is pulling. */
        display_circle(cx, cy, gauge_r, COLOR_WHITE);
        display_circle_fill(cx, cy, 2, COLOR_WHITE);
        if (have_imu) {
            int gx = cx + clampi(SIGN_X * ax * gauge_r / ONE_G, -gauge_r, gauge_r);
            int gy = cy + clampi(SIGN_Y * ay * gauge_r / ONE_G, -gauge_r, gauge_r);
            display_line(cx, cy, gx, gy, COLOR_WHITE);
            display_circle_fill(gx, gy, 5, COLOR_WHITE);
        } else {
            display_text(cx - 24, cy - 4, "NO IMU", COLOR_WHITE);
        }

        /* Click-state indicators. */
        int32_t by = SCR_H - ftr_h - 22;
        if (l_down) { display_rect(6, by, 40, 16, COLOR_WHITE); }
        else        { display_rect_outline(6, by, 40, 16, COLOR_WHITE); }
        display_text(22, by + 4, "L", l_down ? COLOR_BLACK : COLOR_WHITE);
        if (r_down) { display_rect(SCR_W - 46, by, 40, 16, COLOR_WHITE); }
        else        { display_rect_outline(SCR_W - 46, by, 40, 16, COLOR_WHITE); }
        display_text(SCR_W - 30, by + 4, "R", r_down ? COLOR_BLACK : COLOR_WHITE);

        display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
        display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                     "A:L Y:R U/D:scroll X:link B:exit", COLOR_BLACK);

        display_flush();
        delay(15000); /* ~66 fps for a responsive pointer */
    }
    return 0;
}
