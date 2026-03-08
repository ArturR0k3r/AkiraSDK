/**
 * @file inclinometer.c
 * @brief WASM app: tilt direction + altitude/pressure/temperature HUD.
 *
 * Uses the accelerometer gravity vector to determine which direction the board
 * is leaning and how much.  The result is displayed on a compass-style rose:
 *   - Needle points toward the low side (direction of tilt)
 *   - Needle length scales with tilt angle (0 = flat → full ring = 90°)
 *   - Inner "bubble" ring shows the flat zone (needle hides when nearly level)
 *
 * No magnetometer required — pure accelerometer.
 *
 * Required capabilities: display.write, sensor.read
 *
 * Build: ./build_wasm_apps.sh inclinometer
 */

#include "include/akira_api.h"

/* ---------------------------------------------------------------------------
 * Q14 sin/cos lookup table  (64 entries, 5.625° per step)
 * -------------------------------------------------------------------------*/
static const int16_t sin64[64] = {
        0,  1606,  3196,  4756,  6270,  7723,  9102, 10394,
    11585, 12665, 13623, 14449, 15137, 15679, 16069, 16305,
    16384, 16305, 16069, 15679, 15137, 14449, 13623, 12665,
    11585, 10394,  9102,  7723,  6270,  4756,  3196,  1606,
        0, -1606, -3196, -4756, -6270, -7723, -9102,-10394,
   -11585,-12665,-13623,-14449,-15137,-15679,-16069,-16305,
   -16384,-16305,-16069,-15679,-15137,-14449,-13623,-12665,
   -11585,-10394, -9102, -7723, -6270, -4756, -3196, -1606
};
#define SIN64(i) sin64[(i) & 63]
#define COS64(i) sin64[((i) + 16) & 63]

/* ---------------------------------------------------------------------------
 * Integer helpers
 * -------------------------------------------------------------------------*/
static int32_t isqrt64(int64_t n)
{
    if (n <= 0) return 0;
    int64_t x = n, y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return (int32_t)x;
}

/* Integer atan2 → tenths of degrees CCW from +X, result in [0, 3600) */
static int atan2_dd(int y, int x)
{
    if (x == 0 && y == 0) return 0;
    int ay = y < 0 ? -y : y;
    int ax = x < 0 ? -x : x;
    int ang;
    if (ax >= ay)
        ang = (int)((int64_t)10314 * ay * ax
                    / ((int64_t)18 * ax * ax + (int64_t)5 * ay * ay + 1));
    else
        ang = 900 - (int)((int64_t)10314 * ax * ay
                    / ((int64_t)18 * ay * ay + (int64_t)5 * ax * ax + 1));
    if (x < 0) ang = 1800 - ang;
    if (y < 0) ang = 3600 - ang;
    if (ang >= 3600) ang -= 3600;
    return ang;
}

static int buf_str(char *buf, int pos, const char *s)
{
    while (*s) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

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

/* ---------------------------------------------------------------------------
 * Draw compass rose
 * -------------------------------------------------------------------------*/
static void draw_rose(int cx, int cy, int r)
{
    display_circle(cx, cy, r,     COLOR_LIGHT_GRAY);
    display_circle(cx, cy, r - 2, COLOR_DARK_GRAY);

    /* Flat zone ring at 10° tilt — visual dead-band indicator */
    int flat_r = r / 9;
    display_circle(cx, cy, flat_r, 0x4208);

    /* Tick marks */
    for (int i = 0; i < 8; i++) {
        int idx = i * 8;
        int is_cardinal = (i % 2 == 0);
        int tick = is_cardinal ? 9 : 5;
        int ox = cx + (int)((int64_t)SIN64(idx) *  r          >> 14);
        int oy = cy - (int)((int64_t)COS64(idx) *  r          >> 14);
        int ix = cx + (int)((int64_t)SIN64(idx) * (r - tick)  >> 14);
        int iy = cy - (int)((int64_t)COS64(idx) * (r - tick)  >> 14);
        display_line(ix, iy, ox, oy, is_cardinal ? COLOR_WHITE : COLOR_GRAY);
    }

    /* Cardinal labels */
    int lr = r - 16;
    display_text(cx + (int)((int64_t)SIN64( 0)*lr>>14) - 3,
                 cy - (int)((int64_t)COS64( 0)*lr>>14) - 4, "N", COLOR_WHITE);
    display_text(cx + (int)((int64_t)SIN64(16)*lr>>14) - 3,
                 cy - (int)((int64_t)COS64(16)*lr>>14) - 4, "E", COLOR_LIGHT_GRAY);
    display_text(cx + (int)((int64_t)SIN64(32)*lr>>14) - 3,
                 cy - (int)((int64_t)COS64(32)*lr>>14) - 4, "S", COLOR_LIGHT_GRAY);
    display_text(cx + (int)((int64_t)SIN64(48)*lr>>14) - 3,
                 cy - (int)((int64_t)COS64(48)*lr>>14) - 4, "W", COLOR_LIGHT_GRAY);
}

/* ---------------------------------------------------------------------------
 * Draw tilt needle — length proportional to tilt_q14 (Q14 sin of tilt angle)
 * -------------------------------------------------------------------------*/
static void draw_needle(int cx, int cy, int dir_dd, int32_t tilt_q14, int r)
{
    /* Scale needle length: full ring radius when tilt = 90° (Q14 = 16384) */
    int needle_r = (int)((int64_t)tilt_q14 * (r - 10) >> 14);
    if (needle_r < 4) {
        /* Nearly flat — just draw the jewel */
        display_circle_fill(cx, cy, 5, COLOR_ORANGE);
        return;
    }

    int idx = dir_dd * 64 / 3600;

    int tip_x  = cx + (int)((int64_t)SIN64(idx) *  needle_r     >> 14);
    int tip_y  = cy - (int)((int64_t)COS64(idx) *  needle_r     >> 14);
    int tail_x = cx - (int)((int64_t)SIN64(idx) * (needle_r/3)  >> 14);
    int tail_y = cy + (int)((int64_t)COS64(idx) * (needle_r/3)  >> 14);

    int base_w = 5;
    int pi = (idx + 16) & 63;
    int bx = (int)((int64_t)SIN64(pi) * base_w >> 14);
    int by = (int)((int64_t)COS64(pi) * base_w >> 14);

    /* Orange needle toward low side */
    display_triangle_fill(tip_x, tip_y,
                          cx + bx, cy - by,
                          cx - bx, cy + by,
                          COLOR_ORANGE);
    /* Gray counter tail */
    display_triangle_fill(tail_x, tail_y,
                          cx + bx, cy - by,
                          cx - bx, cy + by,
                          COLOR_DARK_GRAY);

    display_circle_fill(cx, cy, 4, COLOR_WHITE);
}

/* ---------------------------------------------------------------------------
 * Main loop
 * -------------------------------------------------------------------------*/
int main(void)
{
    int32_t sw = 240, sh = 320;
    display_get_size(&sw, &sh);

    int cx = (int)sw / 2;
    int rose_r = 80;
    int cy = rose_r + 12;

    int hud_y = cy + rose_r + 6;
    int hud_h = (int)sh - hud_y - 4;

    char buf[32];

    while (1) {
        /* ---- Sensors ----------------------------------------------------- */
        int32_t ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int32_t ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int32_t az = sensor_read(SENSOR_CHAN_ACCEL_Z);
        if (ax == AKIRA_SENSOR_ERROR || ay == AKIRA_SENSOR_ERROR ||
            az == AKIRA_SENSOR_ERROR) {
            ax = 0; ay = 0; az = 9812;
        }

        int32_t alt  = sensor_read(SENSOR_CHAN_ALTITUDE);
        int32_t pres = sensor_read(SENSOR_CHAN_PRESS);
        int32_t temp = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);

        /* ---- Tilt direction + magnitude from gravity vector -------------- */
        /*
         * The horizontal components of gravity (ax, ay) point toward the low
         * side of the board in the board's XY plane.
         * tilt_dir  = atan2(ax, ay)  → which direction is "down" on the rose
         * tilt_sin  = sqrt(ax²+ay²) / |a|  = sin(tilt_angle) in Q14
         * tilt_deg  = approx tilt_sin * 900 >> 14  (decideg, 900 = 90°)
         */
        int32_t mag = isqrt64((int64_t)ax*ax + (int64_t)ay*ay + (int64_t)az*az);
        if (mag < 500) mag = 9812;

        int32_t horiz = isqrt64((int64_t)ax*ax + (int64_t)ay*ay);
        /* sin(tilt) in Q14 */
        int32_t tilt_q14 = (int32_t)((int64_t)horiz * 16384 / mag);
        /* tilt in whole degrees (0–90) */
        int32_t tilt_deg = (int32_t)((int64_t)tilt_q14 * 90 >> 14);

        /* Direction: atan2(ax, ay) — ax pushes East, ay pushes North */
        int dir_dd = atan2_dd((int)ax, (int)ay);  /* CCW from +Y */

        /* ---- Render ------------------------------------------------------ */
        display_clear(COLOR_BLACK);

        /* Title */
        display_text_large(cx - 52, 2, "INCLINOMETER", COLOR_ORANGE);

        draw_rose(cx, cy, rose_r);
        draw_needle(cx, cy, dir_dd, tilt_q14, rose_r);

        /* Tilt angle below the rose */
        buf[0] = '\0';
        int p = buf_int(buf, 0, tilt_deg);
        buf_str(buf, p, " deg tilt");
        display_text(cx - 30, cy + rose_r - 12, buf, COLOR_ORANGE);

        /* ---- HUD panel --------------------------------------------------- */
        int hud_w = (int)sw - 4;
        int row_h = (hud_h > 0 ? hud_h / 3 : 38);
        if (row_h < 14) row_h = 14;

        display_rounded_rect_fill(2, hud_y, hud_w, hud_h, 5, 0x1082);
        display_rounded_rect(2, hud_y, hud_w, hud_h, 5, 0x4208);

        /* Altitude */
        {
            int ry = hud_y + 4;
            display_text(8, ry, "ALT", COLOR_YELLOW);
            if (alt != AKIRA_SENSOR_ERROR) {
                p = buf_int(buf, 0, alt / 1000);
                buf_str(buf, p, " m");
                display_text(34, ry, buf, COLOR_WHITE);
                int32_t alt_m = alt / 1000;
                if (alt_m < 0) alt_m = 0;
                if (alt_m > 5000) alt_m = 5000;
                display_progress_bar(8, ry + 11, hud_w - 12, 7,
                                     alt_m, 5000, COLOR_YELLOW, COLOR_BLACK);
            } else {
                display_text(34, ry, "-- m", COLOR_DARK_GRAY);
            }
        }

        /* Pressure */
        {
            int ry = hud_y + row_h + 4;
            display_text(8, ry, "PRE", COLOR_CYAN);
            if (pres != AKIRA_SENSOR_ERROR) {
                int32_t hpa = pres / 100;
                p = buf_int(buf, 0, hpa);
                buf_str(buf, p, " hPa");
                display_text(34, ry, buf, COLOR_WHITE);
                int32_t fill = hpa * 10 - 9000;
                if (fill < 0) fill = 0;
                if (fill > 1500) fill = 1500;
                display_progress_bar(8, ry + 11, hud_w - 12, 7,
                                     fill, 1500, COLOR_CYAN, COLOR_BLACK);
            } else {
                display_text(34, ry, "--- hPa", COLOR_DARK_GRAY);
            }
        }

        /* Temperature */
        {
            int ry = hud_y + 2 * row_h + 4;
            display_text(8, ry, "TMP", COLOR_ORANGE);
            if (temp != AKIRA_SENSOR_ERROR) {
                int32_t deg_c = temp / 1000;
                p = buf_int(buf, 0, deg_c);
                buf_str(buf, p, " C");
                display_text(34, ry, buf, COLOR_WHITE);
                int32_t fill = (deg_c + 20) * 10;
                if (fill < 0) fill = 0;
                if (fill > 800) fill = 800;
                uint32_t bar_col = (deg_c > 35) ? COLOR_RED
                                 : (deg_c < 10) ? COLOR_BLUE
                                                : COLOR_ORANGE;
                display_progress_bar(8, ry + 11, hud_w - 12, 7,
                                     fill, 800, bar_col, COLOR_BLACK);
            } else {
                display_text(34, ry, "-- C", COLOR_DARK_GRAY);
            }
        }

        display_flush();
        delay(50000);   /* 20 fps */
    }
    return 0;
}
