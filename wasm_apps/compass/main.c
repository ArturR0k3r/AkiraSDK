/**
 * @file compass.c
 * @brief WASM app: tilt-compensated compass + altitude/pressure/temperature HUD.
 *
 * Reads the magnetometer, accelerometer, and barometric sensor channels to
 * display:
 *   - A tilt-compensated compass rose with animated needle pointing North
 *   - Live altitude (m), pressure (hPa), and temperature (°C) readouts
 *
 * Tilt compensation rotates the magnetometer vector into the horizontal plane
 * using the pitch/roll derived from the accelerometer — no floating-point,
 * all Q14 fixed-point integer arithmetic.
 *
 * Graceful fallback: if no magnetometer is present the needle is hidden and
 * the altitude/pressure/temperature HUD is still shown.
 *
 * Required capabilities: display.write, sensor.read
 *
 * Build: ./build_wasm_apps.sh compass
 */

#include "include/akira_api.h"

/* ---------------------------------------------------------------------------
 * Q14 sin/cos lookup table
 *
 * 64 entries, step = 360/64 = 5.625° per index.
 * Values in Q14 format (16384 = 1.0, -16384 = -1.0).
 * cos64(i) = sin64[(i + 16) & 63]  (cos leads sin by 90° = 16 steps)
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
 * Integer square root
 * -------------------------------------------------------------------------*/
static int32_t isqrt64(int64_t n)
{
    if (n <= 0) return 0;
    int64_t x = n;
    int64_t y = 1;
    while (x > y) {
        x = (x + y) / 2;
        y = n / x;
    }
    return (int32_t)x;
}

/* ---------------------------------------------------------------------------
 * Integer atan2 returning CCW angle in tenths of degrees [0, 3599].
 * 0° = +X direction. Accuracy ~1.5° max error.
 *
 * Formula: atan(t) ≈ 572.9 · t / (1 + 0.2785·t²) in degrees
 *   scaled as decideg: 10314 · ay · ax / (18·ax² + 5·ay²)
 * -------------------------------------------------------------------------*/
static int atan2_dd(int y, int x)
{
    if (x == 0 && y == 0) return 0;

    int ay = y < 0 ? -y : y;
    int ax = x < 0 ? -x : x;
    int ang;

    if (ax >= ay) {
        /* First octant: result in [0, 450] decideg */
        ang = (int)((int64_t)10314 * ay * ax
                    / ((int64_t)18 * ax * ax + (int64_t)5 * ay * ay + 1));
    } else {
        /* Second octant: result in [450, 900] decideg */
        ang = 900 - (int)((int64_t)10314 * ax * ay
                    / ((int64_t)18 * ay * ay + (int64_t)5 * ax * ax + 1));
    }

    /* Quadrant adjustment to [0, 3600) */
    if (x < 0) ang = 1800 - ang;   /* left half */
    if (y < 0) ang = 3600 - ang;   /* lower half */

    if (ang >= 3600) ang -= 3600;
    return ang;
}

/* ---------------------------------------------------------------------------
 * Normalise raw sensor reading into Q14 given the magnitude (same units).
 * -------------------------------------------------------------------------*/
static int32_t to_q14(int32_t raw, int32_t mag)
{
    if (mag == 0) return 0;
    return (int32_t)((int64_t)raw * 16384 / mag);
}

/* ---------------------------------------------------------------------------
 * Draw compass rose (outline circle + 8 tick marks + cardinal labels)
 * -------------------------------------------------------------------------*/
static void draw_rose(int cx, int cy, int r)
{
    /* Outer ring */
    display_circle(cx, cy, r, COLOR_LIGHT_GRAY);
    display_circle(cx, cy, r - 2, 0x4208 /* dark medium-gray */);

    /* Tick marks: 8 directions, every 8 LUT steps (45°)
     * Long ticks at cardinal (N/E/S/W), short at ordinals */
    for (int i = 0; i < 8; i++) {
        int idx = i * 8;    /* LUT index for this tick (0, 8, 16, ..., 56) */
        int is_cardinal = (i % 2 == 0);
        int tick_len = is_cardinal ? 9 : 5;

        /* Inner and outer endpoints of tick */
        int ox = cx + (int)((int64_t)SIN64(idx) * r        >> 14);
        int oy = cy - (int)((int64_t)COS64(idx) * r        >> 14);
        int ix = cx + (int)((int64_t)SIN64(idx) * (r - tick_len) >> 14);
        int iy = cy - (int)((int64_t)COS64(idx) * (r - tick_len) >> 14);

        display_line(ix, iy, ox, oy, is_cardinal ? COLOR_WHITE : COLOR_GRAY);
    }

    /* Cardinal labels: N/E/S/W inside the ring, slightly offset inward */
    int label_r = r - 16;
    /* N */ display_text(cx + (int)((int64_t)SIN64( 0) * label_r >> 14) - 3,
                         cy - (int)((int64_t)COS64( 0) * label_r >> 14) - 4,
                         "N", COLOR_WHITE);
    /* E */ display_text(cx + (int)((int64_t)SIN64(16) * label_r >> 14) - 3,
                         cy - (int)((int64_t)COS64(16) * label_r >> 14) - 4,
                         "E", COLOR_LIGHT_GRAY);
    /* S */ display_text(cx + (int)((int64_t)SIN64(32) * label_r >> 14) - 3,
                         cy - (int)((int64_t)COS64(32) * label_r >> 14) - 4,
                         "S", COLOR_LIGHT_GRAY);
    /* W */ display_text(cx + (int)((int64_t)SIN64(48) * label_r >> 14) - 3,
                         cy - (int)((int64_t)COS64(48) * label_r >> 14) - 4,
                         "W", COLOR_LIGHT_GRAY);
}

/* ---------------------------------------------------------------------------
 * Draw the compass needle at the given heading (decideg, 0 = North).
 * The needle is two filled triangles sharing a pivot at (cx, cy):
 *   Red   → North tip  (heading direction, long)
 *   Gray  → South tail (opposite, shorter)
 * -------------------------------------------------------------------------*/
static void draw_needle(int cx, int cy, int heading_dd, int r)
{
    /* Convert heading (clockwise from North) to LUT index */
    int idx = heading_dd * 64 / 3600;

    /* Tip and tail */
    int tip_r  = r - 10;   /* how far the red tip extends */
    int tail_r = r / 3;    /* how far the gray tail extends */

    int tip_x  = cx + (int)((int64_t)SIN64(idx) * tip_r  >> 14);
    int tip_y  = cy - (int)((int64_t)COS64(idx) * tip_r  >> 14);
    int tail_x = cx - (int)((int64_t)SIN64(idx) * tail_r >> 14);
    int tail_y = cy + (int)((int64_t)COS64(idx) * tail_r >> 14);

    /* Perpendicular vector for the needle base width (~6 px) */
    /* Perpendicular to heading: (cos, sin) = (COS64, SIN64) rotated 90° */
    int base_w = 5;
    int perp_idx = (idx + 16) & 63;   /* +90° in LUT */
    int bx = (int)((int64_t)SIN64(perp_idx) * base_w >> 14);
    int by = (int)((int64_t)COS64(perp_idx) * base_w >> 14); /* note: already accounts for Y-flip */

    /* Red North half */
    display_triangle_fill(tip_x, tip_y,
                          cx + bx, cy - by,
                          cx - bx, cy + by,
                          COLOR_RED);
    /* Gray South half */
    display_triangle_fill(tail_x, tail_y,
                          cx + bx, cy - by,
                          cx - bx, cy + by,
                          COLOR_DARK_GRAY);

    /* Centre jewel */
    display_circle_fill(cx, cy, 4, COLOR_WHITE);
}

/* ---------------------------------------------------------------------------
 * Append a short string to buf starting at offset *pos.
 * Returns new *pos after appending (does not NUL-terminate mid-write).
 * -------------------------------------------------------------------------*/
static int buf_str(char *buf, int pos, const char *s)
{
    while (*s) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

/* Append integer to buf/pos */
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
 * Main loop
 * -------------------------------------------------------------------------*/

int main(void)
{
    int32_t sw = 240, sh = 320;
    display_get_size(&sw, &sh);

    /* Compass rose geometry — rose_r=80 leaves a comfortable HUD below */
    int cx = (int)sw / 2;
    int rose_r = 80;
    int cy = rose_r + 12;            /* centre: rose_r px from top edge + margin */

    /* HUD starts below the rose */
    int hud_y = cy + rose_r + 6;
    int hud_h = (int)sh - hud_y - 4;

    char buf[32];

    while (1) {
        /* ---- Read sensors ------------------------------------------------ */

        /* Accelerometer for tilt compensation */
        int32_t ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int32_t ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int32_t az = sensor_read(SENSOR_CHAN_ACCEL_Z);
        if (ax == AKIRA_SENSOR_ERROR || ay == AKIRA_SENSOR_ERROR ||
            az == AKIRA_SENSOR_ERROR) {
            ax = 0; ay = 0; az = 9812;
        }

        /* Magnetometer */
        int32_t mx = sensor_read(SENSOR_CHAN_MAGN_X);
        int32_t my = sensor_read(SENSOR_CHAN_MAGN_Y);
        int32_t mz = sensor_read(SENSOR_CHAN_MAGN_Z);
        int mag_ok = (mx != AKIRA_SENSOR_ERROR &&
                      my != AKIRA_SENSOR_ERROR &&
                      mz != AKIRA_SENSOR_ERROR);

        /* Barometric / environmental */
        int32_t alt  = sensor_read(SENSOR_CHAN_ALTITUDE);
        int32_t pres = sensor_read(SENSOR_CHAN_PRESS);
        int32_t temp = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);

        /* ---- Tilt compensation ------------------------------------------ */
        int32_t mag_a  = isqrt64((int64_t)ax*ax + (int64_t)ay*ay + (int64_t)az*az);
        if (mag_a < 500) mag_a = 9812;
        int32_t cross  = isqrt64((int64_t)ay*ay + (int64_t)az*az);
        if (cross == 0) cross = 1;

        int32_t sp = to_q14(-ax, mag_a);    /* sin(pitch) */
        int32_t cp = to_q14(cross, mag_a);  /* cos(pitch) */
        int32_t sr = to_q14( ay, cross);    /* sin(roll)  */
        int32_t cr = to_q14( az, cross);    /* cos(roll)  */

        int heading_dd = 0;
        if (mag_ok) {
            /* Rotate magnetometer into horizontal plane:
             *   mh_x = mx·cos(p) + my·sin(p)·sin(r) + mz·sin(p)·cos(r)
             *   mh_y = my·cos(r) − mz·sin(r)
             * (values in raw sensor units after the Q14 >> shift) */
            int32_t sp_sr = (int32_t)((int64_t)sp * sr >> 14);
            int32_t sp_cr = (int32_t)((int64_t)sp * cr >> 14);

            int32_t mh_x = (int32_t)(((int64_t)mx * cp
                                     + (int64_t)my * sp_sr
                                     + (int64_t)mz * sp_cr) >> 14);
            int32_t mh_y = (int32_t)(((int64_t)my * cr
                                     - (int64_t)mz * sr) >> 14);

            /* Compass heading: 0 = North (+Y horizontal component).
             * atan2(East, North) = atan2(mh_x, mh_y) in our body frame.
             * Convention: heading increases CW.
             * atan2_dd(y, x) returns CCW from +X → convert to CW from North:
             *   heading = atan2_dd(-mh_y, mh_x)   (rotates frame 90° CW) */
            heading_dd = atan2_dd((int)-mh_y, (int)mh_x);
        }

        /* ---- Render ------------------------------------------------------ */
        display_clear(COLOR_BLACK);

        /* Title */
        display_text_large(cx - 40, 2, "COMPASS", COLOR_CYAN);

        /* Compass rose */
        draw_rose(cx, cy, rose_r);

        if (mag_ok) {
            draw_needle(cx, cy, heading_dd, rose_r);

            /* Heading value below the rose */
            buf[0] = '\0';
            int p = buf_int(buf, 0, heading_dd / 10);
            buf_str(buf, p, " deg");
            display_text(cx - 18, cy + rose_r - 12, buf, COLOR_CYAN);
        }

        /* HUD panel */
        int row_h = (hud_h > 0 ? hud_h / 3 : 38);
        if (row_h < 14) row_h = 14;
        int hud_w = (int)sw - 4;

        display_rounded_rect_fill(2, hud_y, hud_w, hud_h, 5, 0x1082 /* dark panel */);
        display_rounded_rect(2, hud_y, hud_w, hud_h, 5, 0x4208 /* panel border */);

        /* Altitude row */
        {
            int ry = hud_y + 4;
            display_text(8, ry, "ALT", COLOR_YELLOW);
            if (alt != AKIRA_SENSOR_ERROR) {
                int p2 = buf_int(buf, 0, alt / 1000);
                buf_str(buf, p2, " m");
                display_text(34, ry, buf, COLOR_WHITE);
            } else {
                display_text(34, ry, "-- m", COLOR_DARK_GRAY);
            }

            /* Altitude bar clamped to 0–5000 m */
            int32_t alt_m = (alt != AKIRA_SENSOR_ERROR) ? alt / 1000 : 0;
            if (alt_m < 0) alt_m = 0;
            if (alt_m > 5000) alt_m = 5000;
            display_progress_bar(8, ry + 11, hud_w - 12, 7,
                                 alt_m, 5000, COLOR_YELLOW, COLOR_BLACK);
        }

        /* Pressure row */
        {
            int ry = hud_y + row_h + 4;
            display_text(8, ry, "PRE", COLOR_CYAN);
            if (pres != AKIRA_SENSOR_ERROR) {
                /* pres raw = kPa * 1000 → hPa = kPa * 10 = raw/100 */
                int32_t hpa = pres / 100;
                int p2 = buf_int(buf, 0, hpa);
                buf_str(buf, p2, " hPa");
                display_text(34, ry, buf, COLOR_WHITE);
            } else {
                display_text(34, ry, "--- hPa", COLOR_DARK_GRAY);
            }

            /* Pressure bar: typical range 900–1050 hPa */
            if (pres != AKIRA_SENSOR_ERROR) {
                int32_t hpa  = pres / 100;
                int32_t base = 9000;                        /* 900 hPa */
                int32_t span = 1500;                        /* up to 1050 hPa */
                int32_t fill = hpa * 10 - base;
                if (fill < 0) fill = 0;
                if (fill > span) fill = span;
                display_progress_bar(8, ry + 11, hud_w - 12, 7,
                                     fill, span, COLOR_CYAN, COLOR_BLACK);
            }
        }

        /* Temperature row */
        {
            int ry = hud_y + 2 * row_h + 4;
            display_text(8, ry, "TMP", COLOR_ORANGE);
            if (temp != AKIRA_SENSOR_ERROR) {
                int32_t deg_c = temp / 1000;
                int p2 = buf_int(buf, 0, deg_c);
                buf_str(buf, p2, " C");
                display_text(34, ry, buf, COLOR_WHITE);
            } else {
                display_text(34, ry, "-- C", COLOR_DARK_GRAY);
            }

            /* Temperature bar: -20 °C to +60 °C */
            if (temp != AKIRA_SENSOR_ERROR) {
                int32_t deg_m = temp / 1000;    /* degrees Celsius */
                int32_t fill = (deg_m + 20) * 10;  /* shift to [0..800] for -20..60 */
                if (fill < 0) fill = 0;
                if (fill > 800) fill = 800;
                uint32_t bar_col = (deg_m > 35) ? COLOR_RED
                                 : (deg_m < 10) ? COLOR_BLUE
                                                : COLOR_ORANGE;
                display_progress_bar(8, ry + 11, hud_w - 12, 7,
                                     fill, 800, bar_col, COLOR_BLACK);
            }
        }

        display_flush();
        delay(100000);   /* ~10 fps — compass doesn't need fast refresh */
    }

    return 0;
}
