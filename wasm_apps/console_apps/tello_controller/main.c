/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file tello_controller.c
 * @brief Tello drone gamepad controller via BLE HID (Xbox profile).
 *
 *   D-PAD     — left stick:  throttle (up/down) + yaw (turn left/right)
 *   IMU TILT  — right stick: pitch (fwd/back) + roll (left/right)
 *   A         — R2+Y combo (takeoff/land toggle)
 *   B         — unused
 *   B+Y hold  — exit app
 *
 * Capabilities: display.write, gpio.read, sensor.read, hid
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

#define ONE_G      9810
#define AXIS_MAX   32767
#define AXIS_DEAD  2500   /* accel deadzone */

#define CAL_SAMPLES 8   /* startup IMU zero-calibration samples */

/* Gamepad constants (not in akira_api.h — must match hid_common.h) */
#define GP_BTN_Y       0x0008
#define GP_AXIS_RT     5

/* Raw GPIO pin numbers (akiraconsole_prod_esp32s3_procpu.dts gpio_keys) —
 * read directly like gravity_dash/main.c instead of input_get_buttons(). */
#define BTN_UP    4
#define BTN_DOWN  5
#define BTN_LEFT  6
#define BTN_RIGHT 7
#define BTN_A     15
#define BTN_B     16
#define BTN_Y     41  /* gpio1 pin9 = 32+9 */

/* D-pad dirs for hid_gamepad_set_dpad() */
#define DPAD_NONE  0
#define DPAD_UP    1
#define DPAD_DOWN  5
#define DPAD_LEFT  7
#define DPAD_RIGHT 3

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Accel raw (mm/s²) → gamepad axis (-32768..32767) */
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

    /* Init HID: BLE transport, gamepad device type */
    hid_init(HID_TRANSPORT_BLE, HID_DEVICE_GAMEPAD);

    /* Idle reads HIGH (PCB 10k pull-up dominates), pressed shorts to GND */
    gpio_configure(BTN_UP,    GPIO_INPUT | GPIO_PULL_UP);
    gpio_configure(BTN_DOWN,  GPIO_INPUT | GPIO_PULL_UP);
    gpio_configure(BTN_LEFT,  GPIO_INPUT | GPIO_PULL_UP);
    gpio_configure(BTN_RIGHT, GPIO_INPUT | GPIO_PULL_UP);
    gpio_configure(BTN_A,     GPIO_INPUT | GPIO_PULL_UP);
    gpio_configure(BTN_B,     GPIO_INPUT | GPIO_PULL_UP);
    gpio_configure(BTN_Y,     GPIO_INPUT | GPIO_PULL_UP);

    /* Zero-calibrate the IMU: whatever tilt the console is held at when the
     * app starts becomes the new "level" — avoids a constant stuck-tilt
     * reading if it's not held perfectly flat. */
    int ax_zero = 0, ay_zero = 0;
    int cal_n = 0;
    for (int i = 0; i < CAL_SAMPLES; i++) {
        int sx = sensor_read(SENSOR_CHAN_ACCEL_X);
        int sy = sensor_read(SENSOR_CHAN_ACCEL_Y);
        if (sx != AKIRA_SENSOR_ERROR && sy != AKIRA_SENSOR_ERROR) {
            ax_zero += sx;
            ay_zero += sy;
            cal_n++;
        }
        delay(15000);
    }
    if (cal_n > 0) {
        ax_zero /= cal_n;
        ay_zero /= cal_n;
    }

    while (1) {
        int b_held = gpio_read(BTN_B) == 0;
        int y_held = gpio_read(BTN_Y) == 0;

        /* ── Exit: hold B + Y ── */
        if (b_held && y_held) {
            hid_gamepad_reset();
            hid_set_transport(HID_TRANSPORT_NONE);
            return 0;
        }

        /* ── A button → R2+Y combo (takeoff/land toggle in Tello app) ── */
        int a_held = gpio_read(BTN_A) == 0;
        int buttons = a_held ? GP_BTN_Y : 0;
        int rt_axis = a_held ? AXIS_MAX : -32768;

        /* ── IMU Tilt → Left Stick (pitch + roll: smooth positioning) ── */
        int ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int imu_ok = (ax != AKIRA_SENSOR_ERROR && ay != AKIRA_SENSOR_ERROR);

        int roll = 0, pitch = 0;
        if (imu_ok) {
            roll  = -accel_to_axis(ax - ax_zero); /* tilt left/right  → roll */
            pitch = accel_to_axis(ay - ay_zero);   /* tilt forward/back → pitch */
        }

        /* ── D-Pad → Right Stick (yaw + throttle: discrete altitude/rotation) ── */
        int dpad = DPAD_NONE;
        int yaw = 0, throttle = 0;

        int up    = gpio_read(BTN_UP) == 0;
        int down  = gpio_read(BTN_DOWN) == 0;
        int left  = gpio_read(BTN_LEFT) == 0;
        int right = gpio_read(BTN_RIGHT) == 0;

        if (up && !down)    { throttle = -AXIS_MAX; dpad = DPAD_UP; }
        if (down && !up)    { throttle =  AXIS_MAX; dpad = DPAD_DOWN; }
        if (left && !right) { yaw      = -AXIS_MAX; dpad = DPAD_LEFT; }
        if (right && !left) { yaw      =  AXIS_MAX; dpad = DPAD_RIGHT; }

        /* Diagonal combos */
        if (up && right)    { throttle = -AXIS_MAX; yaw =  AXIS_MAX; }
        if (up && left)     { throttle = -AXIS_MAX; yaw = -AXIS_MAX; }
        if (down && right)  { throttle =  AXIS_MAX; yaw =  AXIS_MAX; }
        if (down && left)   { throttle =  AXIS_MAX; yaw = -AXIS_MAX; }

        /* One notify per frame — separate set_axis/set_dpad calls each send
         * their own BLE notify and congest the queue when several inputs
         * change in the same frame.
         *
         * Tello follows RC Mode 2: left stick (axis0/1) = yaw+throttle,
         * right stick (axis2/3) = roll+pitch — confirmed empirically now
         * that the HID transport itself is verified correct. */
        hid_gamepad_send_report(buttons, dpad, yaw, throttle, roll, pitch, -32768, rt_axis);

        /* ── Draw HUD ── */
        int32_t hdr_h = SCR_H / 8;
        display_clear(COLOR_BLACK);
        display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);

        int connected = hid_is_connected();
        display_text(6, hdr_h / 2 - 4, "TELLO CONTROLLER", COLOR_BLACK);
        display_text(SCR_W - 72, hdr_h / 2 - 4,
                     connected ? "BLE OK" : "BLE...", COLOR_BLACK);

        int32_t cy = hdr_h + 20;
        int32_t bar_w = SCR_W / 3;

        /* Left stick (D-pad → yaw + throttle) */
        display_text(10, cy, "LEFT STK", COLOR_GRAY);
        display_text(10, cy + 10, "D-PAD", COLOR_GRAY);
        display_progress_bar(10, cy + 24, bar_w, 8, yaw + AXIS_MAX,
                             2 * AXIS_MAX, COLOR_CYAN, COLOR_BLACK);
        display_progress_bar(10, cy + 38, bar_w, 8, throttle + AXIS_MAX,
                             2 * AXIS_MAX, COLOR_CYAN, COLOR_BLACK);

        /* Right stick (IMU tilt → pitch + roll) */
        int32_t rx = SCR_W - bar_w - 10;
        display_text(rx, cy, "RIGHT STK", COLOR_GRAY);
        display_text(rx, cy + 10, imu_ok ? "IMU" : "NO IMU", imu_ok ? COLOR_GREEN : COLOR_RED);
        display_progress_bar(rx, cy + 24, bar_w, 8, roll + AXIS_MAX,
                             2 * AXIS_MAX, COLOR_YELLOW, COLOR_BLACK);
        display_progress_bar(rx, cy + 38, bar_w, 8, pitch + AXIS_MAX,
                             2 * AXIS_MAX, COLOR_YELLOW, COLOR_BLACK);

        /* Button indicators */
        int32_t by = SCR_H - 40;
        int32_t bx = 10;
        display_text(bx, by, "A:TAKEOFF", a_held ? COLOR_GREEN : COLOR_GRAY);
        display_text(bx + 90, by, "B:LAND", b_held ? COLOR_RED : COLOR_GRAY);
        display_text(bx + 170, by, "B+Y:EXIT", COLOR_GRAY);

        display_flush();
        delay(15000); /* ~66 fps — throttle BLE gamepad report rate */
    }

    return 0;
}
