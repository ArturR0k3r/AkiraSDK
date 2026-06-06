/*
 * main.c — AkiraConsole bubble level + environmental HUD
 *
 * Hardware (AkiraConsole Prod):
 *   LSM6DS3 (I2C 0x6A) : accelerometer → pitch / roll angles
 *   BME280  (I2C 0x77) : altitude / pressure / temperature
 *
 * No magnetometer fitted — heading replaced with a 2-D bubble level.
 * The bubble floats opposite to the gravity vector projected onto the
 * screen plane; reference rings mark 10°, 20°, 30° tilt.
 *
 * Controls: SETTINGS = exit
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "akira_api.h"

/* ── Display geometry ────────────────────────────────────────────── */
static int32_t SCR_W = 320, SCR_H = 240;

/* Sharp LS027B7DH01: INVERT_COLORS=y → 0x0000 renders white, 0xFFFF black */
#define C_BG  0x0000u
#define C_FG  0xFFFFu

/* ── Button pins ─────────────────────────────────────────────────── */
#define PIN_SET 0   /* active-low pull-up */

/* ── Integer helpers ─────────────────────────────────────────────── */
static int32_t isqrt64(int64_t n)
{
    if (n <= 0) return 0;
    int64_t x = n, y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return (int32_t)x;
}

/* atan2 → CCW decideg [0, 3600) */
static int atan2_dd(int y, int x)
{
    if (x == 0 && y == 0) return 0;
    int ay = y < 0 ? -y : y;
    int ax = x < 0 ? -x : x;
    int ang;
    if (ax >= ay)
        ang = (int)((int64_t)10314 * ay * ax
                    / ((int64_t)18*ax*ax + (int64_t)5*ay*ay + 1));
    else
        ang = 900 - (int)((int64_t)10314 * ax * ay
                    / ((int64_t)18*ay*ay + (int64_t)5*ax*ax + 1));
    if (x < 0) ang = 1800 - ang;
    if (y < 0) ang = 3600 - ang;
    if (ang >= 3600) ang -= 3600;
    return ang;
}

/* Fold decideg [0,3600) into signed (-1800,1800] */
static int signed_dd(int dd) { return dd > 1800 ? dd - 3600 : dd; }

static int abs_i(int v) { return v < 0 ? -v : v; }

/* ── String helpers ─────────────────────────────────────────────── */
static int buf_int(char *buf, int pos, int32_t v)
{
    if (v < 0) { buf[pos++] = '-'; v = -v; }
    char tmp[12]; int n = 0;
    if (v == 0) { buf[pos++] = '0'; buf[pos] = '\0'; return pos; }
    while (v) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    while (n > 0) buf[pos++] = tmp[--n];
    buf[pos] = '\0';
    return pos;
}

static int buf_str(char *buf, int pos, const char *s)
{
    while (*s) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

/* ── Bubble level drawing ────────────────────────────────────────── */

/*
 * Draw the level circle: outer double ring, concentric reference rings
 * (10°, 20°, 30°), crosshair, and the tilt bubble.
 *
 * ax, ay, az   raw accelerometer in sensor units (≈ mm/s², 1 g ≈ 9812)
 * cx, cy       centre on screen
 * R            outer radius in pixels
 */
static void draw_level(int cx, int cy, int R,
                        int32_t ax, int32_t ay, int32_t az)
{
    int32_t mag = isqrt64((int64_t)ax*ax + (int64_t)ay*ay + (int64_t)az*az);
    if (mag < 500) mag = 9812;

    /* Outer ring — two circles for a 2 px stroke */
    display_circle(cx, cy, R,     C_FG);
    display_circle(cx, cy, R + 1, C_FG);

    /* Reference rings: sin(10°)·R ≈ 0.174·R, sin(20°)≈0.342·R, sin(30°)=0.5·R */
    int r10 = (int)((int64_t)R * 2850 / 16384);
    int r20 = (int)((int64_t)R * 5606 / 16384);
    int r30 = R / 2;
    display_circle(cx, cy, r10, C_FG);
    display_circle(cx, cy, r20, C_FG);
    display_circle(cx, cy, r30, C_FG);

    /* Crosshair — skip 4 px near edges so ticks are visible */
    int arm = R - 5;
    display_line(cx - arm, cy, cx - 3, cy, C_FG);
    display_line(cx + 3,   cy, cx + arm, cy, C_FG);
    display_line(cx, cy - arm, cx, cy - 3, C_FG);
    display_line(cx, cy + 3,   cx, cy + arm, C_FG);

    /* Bubble position: opposite to gravity component in XY plane */
    int bx = cx + (int)((int64_t)(-ax) * (R - 8) / mag);
    int by = cy + (int)((int64_t)( ay) * (R - 8) / mag);

    /* Clamp bubble centre inside outer ring */
    int dx = bx - cx, dy = by - cy;
    int dbr = (int)isqrt64((int64_t)dx*dx + (int64_t)dy*dy);
    int max_r = R - 9;
    if (dbr > max_r && dbr > 0) {
        bx = cx + dx * max_r / dbr;
        by = cy + dy * max_r / dbr;
    }

    /* Bubble: filled circle with hollow centre → ring shape */
    display_circle_fill(bx, by, 9, C_FG);
    display_circle_fill(bx, by, 4, C_BG);
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    gpio_configure(PIN_SET, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* Geometry — smaller radius on tall/narrow displays */
    int R  = (SCR_W >= 380) ? 68 : 78;
    int cx = SCR_W / 2;
    int cy = R + 18;          /* leave room for header + top margin */
    int hud_y = cy + R + 10;  /* HUD panel starts below circle     */
    int hud_h = SCR_H - hud_y - 3;

    char buf[32];
    int prev_set = 0;

    while (1) {
        /* ── Read sensors ── */
        int32_t ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int32_t ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int32_t az = sensor_read(SENSOR_CHAN_ACCEL_Z);
        if (ax == AKIRA_SENSOR_ERROR) ax = 0;
        if (ay == AKIRA_SENSOR_ERROR) ay = 0;
        if (az == AKIRA_SENSOR_ERROR) az = 9812;

        int32_t pres = sensor_read(SENSOR_CHAN_PRESS);
        int32_t temp = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
        /* Altitude computed from pressure (BME280 has no ALTITUDE channel).
         * Approximation: alt_m ≈ (101325 - P_Pa) / 12  (valid ±2000 m ASL) */
        int32_t alt = (pres != AKIRA_SENSOR_ERROR) ? (101325 - pres) / 12
                                                   : AKIRA_SENSOR_ERROR;

        /* ── Exit ── */
        int s = gpio_read(PIN_SET);
        if (s && !prev_set) break;
        prev_set = s;

        /* ── Pitch / roll from accelerometer ── */
        /* pitch: forward/back tilt (rotation around Y axis) */
        int pitch_dd = signed_dd(atan2_dd(-ax, az));
        /* roll:  left/right tilt (rotation around X axis)  */
        int roll_dd  = signed_dd(atan2_dd( ay, az));

        int level = (abs_i(pitch_dd) < 25 && abs_i(roll_dd) < 25); /* within 2.5° */

        /* ── Render ── */
        display_clear(C_BG);

        /* Header */
        int title_x = cx - 60;
        display_text_large(title_x, 2, "BUBBLE LEVEL", C_FG);
        display_hline(0, 16, SCR_W, C_FG);

        /* Bubble level indicator */
        draw_level(cx, cy, R, ax, ay, az);

        /* "LEVEL" banner when centred */
        if (level) {
            display_text_large(cx - 22, cy - 7, "LEVEL", C_FG);
        }

        /* Pitch / roll readout below circle */
        {
            int ry = cy + R + 3;
            int pp = 0;
            pp = buf_str(buf, pp, "P:");
            pp = buf_int(buf, pp, pitch_dd / 10);
            buf_str(buf, pp, "\xb0");  /* degree sign U+00B0 */
            display_text(cx - 70, ry, buf, C_FG);

            pp = 0;
            pp = buf_str(buf, pp, "R:");
            pp = buf_int(buf, pp, roll_dd / 10);
            buf_str(buf, pp, "\xb0");
            display_text(cx + 8, ry, buf, C_FG);
        }

        /* ── Environmental HUD ── */
        if (hud_h >= 14) {
            int row_h = hud_h / 3;
            if (row_h < 14) row_h = 14;

            /* panel border */
            display_rect(2, hud_y, SCR_W - 4, hud_h, C_FG);

            /* ALT */
            {
                int ry = hud_y + 3;
                display_text(6, ry, "ALT", C_FG);
                if (alt != AKIRA_SENSOR_ERROR) {
                    int pp = buf_int(buf, 0, alt / 1000);
                    buf_str(buf, pp, " m");
                } else {
                    buf_str(buf, 0, "-- m");
                }
                display_text(36, ry, buf, C_FG);
            }

            /* PRE */
            {
                int ry = hud_y + row_h + 3;
                display_text(6, ry, "PRE", C_FG);
                if (pres != AKIRA_SENSOR_ERROR) {
                    int pp = buf_int(buf, 0, pres / 100);
                    buf_str(buf, pp, " hPa");
                } else {
                    buf_str(buf, 0, "--- hPa");
                }
                display_text(36, ry, buf, C_FG);
            }

            /* TMP */
            {
                int ry = hud_y + 2*row_h + 3;
                display_text(6, ry, "TMP", C_FG);
                if (temp != AKIRA_SENSOR_ERROR) {
                    int pp = buf_int(buf, 0, temp / 1000);
                    buf_str(buf, pp, " C");
                } else {
                    buf_str(buf, 0, "-- C");
                }
                display_text(36, ry, buf, C_FG);
            }
        }

        display_flush();
        delay(80000);   /* ~12 fps */
    }

    return 0;
}
