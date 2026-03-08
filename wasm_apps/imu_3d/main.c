/**
 * @file imu_3d.c
 * @brief WASM app: 3D board orientation visualizer using IMU accelerometer.
 *
 * Reads the accelerometer X/Y/Z channels and renders three coloured orthographic
 * axis arrows representing the board's current orientation in space:
 *   X-axis → red    Y-axis → green    Z-axis → blue
 *
 * No floating-point, no atan2, no sin/cos tables are required.  Pitch and roll
 * are derived directly from the normalised accelerometer ratios, which already
 * encode sin/cos of the tilt angles.  The rotation matrix is built from those
 * Q14 fixed-point values.
 *
 * Required capabilities: display.write, sensor.read
 *
 * Build: ./build_wasm_apps.sh imu_3d
 */

#include "include/akira_api.h"

/* ---------------------------------------------------------------------------
 * Fixed-point helpers
 * -------------------------------------------------------------------------*/

/* Q14 constant (1.0 in fixed-point) */
#define Q14 16384

/* Integer square root via Newton–Raphson.  Safe for n up to ~INT64_MAX/2. */
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

/* Clamp v to [lo, hi] */
static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Normalise raw sensor reading into Q14 given the magnitude (same units). */
static int32_t to_q14(int32_t raw, int32_t mag)
{
    if (mag == 0) return 0;
    return (int32_t)((int64_t)raw * Q14 / mag);
}

/* ---------------------------------------------------------------------------
 * Arrow renderer
 * -------------------------------------------------------------------------*/

/*
 * Draw an axis arrow from (cx, cy) to (tx, ty).
 * dim != 0 renders in a halved-brightness version of color (depth cue).
 */
static void draw_arrow(int cx, int cy, int tx, int ty, uint32_t color, int dim)
{
    /* Halve each RGB565 channel by masking and shifting to avoid bleed */
    uint32_t draw_color = dim
        ? (((color & 0xF800U) >> 1) & 0x7800U) |
          (((color & 0x07E0U) >> 1) & 0x03E0U) |
          (((color & 0x001FU) >> 1) & 0x000FU)
        : color;

    display_line(cx, cy, tx, ty, draw_color);

    /* Arrowhead: small filled triangle at tip */
    int dx = tx - cx;
    int dy = ty - cy;
    int32_t len = isqrt64((int64_t)dx * dx + (int64_t)dy * dy);
    if (len == 0) return;

    /* Perpendicular vector scaled to half arrowhead width */
    int hw = 5;   /* half-width of arrowhead base in pixels */
    int px =  (int)((int64_t)(-dy) * hw / len);
    int py =  (int)((int64_t)  dx  * hw / len);

    /* Arrow base is 1/4 back from the tip */
    int bx = tx - dx / 4;
    int by = ty - dy / 4;

    display_triangle_fill(tx, ty, bx + px, by + py, bx - px, by - py, draw_color);
}

/* ---------------------------------------------------------------------------
 * Main loop
 * -------------------------------------------------------------------------*/

int main(void)
{
    int32_t sw = 240, sh = 320;
    display_get_size(&sw, &sh);

    int cx = (int)sw / 2;
    int cy = (int)sh / 2 - 10;   /* slightly above centre to leave HUD room */

    /* Axis arrow length in pixels */
    int scale = 60;

    while (1) {
        int32_t ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int32_t ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int32_t az = sensor_read(SENSOR_CHAN_ACCEL_Z);

        /* If any sensor is unavailable, fall back to "flat board" pose */
        if (ax == AKIRA_SENSOR_ERROR || ay == AKIRA_SENSOR_ERROR ||
            az == AKIRA_SENSOR_ERROR) {
            ax = 0; ay = 0; az = 9812;
        }

        /* ---- Normalised trig values in Q14 --------------------------------
         *
         * When the board is tilted by pitch p and roll r:
         *   ax / |a| = -sin(p)
         *   ay / |a| =  cos(p) · sin(r)
         *   az / |a| =  cos(p) · cos(r)
         *
         * So we can get sin/cos without any atan+trig table:
         *   cross = sqrt(ay²+az²) = |a| · cos(p)
         *
         *   sin_p = -ax / mag   cos_p = cross / mag
         *   sin_r =  ay / cross  cos_r = az / cross
         */
        int32_t mag   = isqrt64((int64_t)ax*ax + (int64_t)ay*ay + (int64_t)az*az);
        if (mag < 500) mag = 9812;   /* less than ~0.05 g — use safe default */

        int32_t cross = isqrt64((int64_t)ay*ay + (int64_t)az*az);
        if (cross == 0) cross = 1;

        int32_t sp = to_q14(-ax, mag);   /* sin(pitch) in Q14 */
        int32_t cp = to_q14(cross, mag); /* cos(pitch) in Q14 */
        int32_t sr = to_q14(ay, cross);  /* sin(roll)  in Q14 */
        int32_t cr = to_q14(az, cross);  /* cos(roll)  in Q14 */

        /* ---- Rotation matrix R = Ry(pitch) · Rx(roll) in Q14 --------------
         *
         *       [ cp      sp·sr/q   sp·cr/q ]
         *   R = [ 0       cr        -sr      ]
         *       [ -sp     cp·sr/q   cp·cr/q  ]
         *
         * The three columns are the body-frame axes expressed in world-frame:
         *   col0 (body X in world): (cp,  0,   -sp)
         *   col1 (body Y in world): (sp·sr, cr, cp·sr)  [>>14 for cross term]
         *   col2 (body Z in world): (sp·cr, -sr, cp·cr) [>>14 for cross term]
         *
         * World frame: X→screen-right, Y→depth, Z→screen-up.
         */

        /* World-frame components of each body axis */
        int32_t rx_wx = cp,   rx_wy = 0,  rx_wz = -sp;
        int32_t ry_wx = (int32_t)((int64_t)sp * sr >> 14);
        int32_t ry_wy = cr;
        int32_t ry_wz = (int32_t)((int64_t)cp * sr >> 14);
        int32_t rz_wx = (int32_t)((int64_t)sp * cr >> 14);
        int32_t rz_wy = -sr;
        int32_t rz_wz = (int32_t)((int64_t)cp * cr >> 14);

        /* ---- Orthographic projection onto screen (X→right, Z→up) ----------
         *   screen_x = cx + wx * scale / Q14
         *   screen_y = cy - wz * scale / Q14   (screen Y grows downward)
         */
        int x_tx = cx + (int)((int64_t)rx_wx * scale >> 14);
        int x_ty = cy - (int)((int64_t)rx_wz * scale >> 14);

        int y_tx = cx + (int)((int64_t)ry_wx * scale >> 14);
        int y_ty = cy - (int)((int64_t)ry_wz * scale >> 14);

        int z_tx = cx + (int)((int64_t)rz_wx * scale >> 14);
        int z_ty = cy - (int)((int64_t)rz_wz * scale >> 14);

        /* ---- Painter's algorithm: sort axes back-to-front by depth (wy) ----
         * Negative wy = axis pointing away from viewer → draw first (behind). */
        typedef struct { int tx, ty; uint32_t color; int32_t depth; } Axis;
        Axis axes[3] = {
            { x_tx, x_ty, COLOR_RED,   rx_wy },
            { y_tx, y_ty, COLOR_GREEN, ry_wy },
            { z_tx, z_ty, COLOR_BLUE,  rz_wy },
        };

        /* Bubble sort ascending by depth */
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2 - i; j++) {
                if (axes[j].depth > axes[j+1].depth) {
                    Axis tmp = axes[j]; axes[j] = axes[j+1]; axes[j+1] = tmp;
                }
            }
        }

        /* ---- HUD: pitch and roll angles -----------------------------------
         * sin values give the angle directly:
         *   |sin(pitch)| = |sp| / Q14 → pitch_deg ≈ arcsin(sp/Q14) in decideg
         * For <45° the approximation pitch ≈ sp * 900 / Q14 is within 10%.
         * We show the exact magnitude in progress bars (which saturate at 90°).
         */
        int32_t pitch_abs = sp < 0 ? -sp : sp;
        int32_t roll_abs  = sr < 0 ? -sr : sr;
        /* Convert Q14 magnitude to tenths of degrees (900 decideg = 90°) */
        int32_t pitch_dd = clamp32((int32_t)((int64_t)pitch_abs * 900 >> 14), 0, 900);
        int32_t roll_dd  = clamp32((int32_t)((int64_t)roll_abs  * 900 >> 14), 0, 900);

        /* ---- Render frame ------------------------------------------------- */
        display_clear(COLOR_BLACK);

        /* Title / legend */
        display_text(4, 4, "IMU 3D", COLOR_LIGHT_GRAY);
        display_text((int)sw - 30, 4, "X",  COLOR_RED);
        display_text((int)sw - 20, 4, "Y",  COLOR_GREEN);
        display_text((int)sw - 10, 4, "Z",  COLOR_BLUE);

        /* Axes (back-to-front) */
        for (int i = 0; i < 3; i++) {
            draw_arrow(cx, cy, axes[i].tx, axes[i].ty,
                       axes[i].color, axes[i].depth < 0);
        }

        /* Centre jewel */
        display_circle_fill(cx, cy, 4, COLOR_LIGHT_GRAY);

        /* HUD panel at bottom */
        int hud_y = (int)sh - 56;
        display_rounded_rect_fill(2, hud_y, (int)sw - 4, 54, 6, 0x2945 /* dark blue-gray */);

        /* Pitch row */
        display_text(8,  hud_y + 4,  "PITCH", COLOR_RED);
        display_number(56, hud_y + 4, pitch_dd / 10, COLOR_WHITE);
        display_text(78,  hud_y + 4,  "deg",  COLOR_LIGHT_GRAY);
        display_progress_bar(8, hud_y + 18, (int)sw - 16, 12,
                             pitch_dd, 900, COLOR_RED, 0x1082 /* very dark */);

        /* Roll row */
        display_text(8,  hud_y + 33, "ROLL ", COLOR_GREEN);
        display_number(56, hud_y + 33, roll_dd / 10, COLOR_WHITE);
        display_text(78,  hud_y + 33, "deg",  COLOR_LIGHT_GRAY);
        display_progress_bar(8, hud_y + 47, (int)sw - 16, 12,
                             roll_dd, 900, COLOR_GREEN, 0x1082);

        display_flush();
        delay(33000);   /* ~30 fps */
    }

    return 0;
}
