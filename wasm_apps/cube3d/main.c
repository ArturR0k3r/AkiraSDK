/**
 * @file cube3d.c
 * @brief WASM app: real-time 3D flat-shaded spinning cube.
 *
 * Perspective projection, back-face culling, painter's algorithm depth sort,
 * and per-face diffuse shading with a slowly rotating light source.
 *
 * The gyroscope directly controls spin speed — hold the board steady for
 * gentle auto-rotation; flick your wrist to send it spinning.
 *
 * Required capabilities: display.write, sensor.read
 *
 * Build: ./build_wasm_apps.sh cube3d
 */

#include "include/akira_api.h"

/* ---------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------*/
#define Q14          16384      /* 1.0 in Q14 fixed-point                   */
#define SCALE        62         /* cube half-size in screen pixels           */
#define FOCAL        260        /* perspective focal length (pixels)         */
/* Angle accumulator: ANG_FULL = one full rotation.
 * One LUT step = ANG_FULL/64 = 64 units = 5.625°.
 * Fractional part  = ang & 63  (sub-step, for interpolation).              */
#define ANG_FULL     4096
#define ANG_MASK     (ANG_FULL - 1)
#define ANG_90       (ANG_FULL / 4)   /* 90° in angle units                 */
/* Auto-spin: ~2.5° per frame on each axis (coprime multiples of 16).        */
#define AUTO_X       16
#define AUTO_Y       26
#define AUTO_Z        9
/* Gyro: raw mrad/s → angle units/frame.  Lower = more sensitive.           */
#define GYRO_SCALE   10
/* Target frame period in ms */
#define FRAME_MS     28         /* ~35 fps */

/* ---------------------------------------------------------------------------
 * 64-entry Q14 sin/cos LUT  (5.625° per step)
 * -------------------------------------------------------------------------*/
static const int16_t s64[64] = {
        0,  1606,  3196,  4756,  6270,  7723,  9102, 10394,
    11585, 12665, 13623, 14449, 15137, 15679, 16069, 16305,
    16384, 16305, 16069, 15679, 15137, 14449, 13623, 12665,
    11585, 10394,  9102,  7723,  6270,  4756,  3196,  1606,
        0, -1606, -3196, -4756, -6270, -7723, -9102,-10394,
   -11585,-12665,-13623,-14449,-15137,-15679,-16069,-16305,
   -16384,-16305,-16069,-15679,-15137,-14449,-13623,-12665,
   -11585,-10394, -9102, -7723, -6270, -4756, -3196, -1606
};

/*
 * Interpolated sin/cos — accepts raw angle [0, ANG_FULL).
 * Linearly interpolates between adjacent LUT entries using the 6 fractional
 * bits (ang & 63), eliminating the step-stutter from direct LUT indexing.
 */
static int32_t sin_a(int ang)
{
    ang &= ANG_MASK;
    int idx  = ang >> 6;          /* integer LUT index [0..63]  */
    int frac = ang & 63;          /* fractional sub-step [0..63] */
    int32_t s0 = s64[idx];
    int32_t s1 = s64[(idx + 1) & 63];
    return s0 + ((s1 - s0) * frac >> 6);
}

static int32_t cos_a(int ang)
{
    return sin_a(ang + ANG_90);
}

/* ---------------------------------------------------------------------------
 * Cube geometry
 *
 *   7──────6        Y
 *  /|     /|        |
 * 3──────2 |        |
 * | 4────|─5        +──── X
 * |/     |/        /
 * 0──────1        Z
 *
 * Vertices in Q14 (unit cube ±1):
 * -------------------------------------------------------------------------*/
static const int16_t V[8][3] = {
    {-Q14, -Q14, -Q14},  /* 0 */
    { Q14, -Q14, -Q14},  /* 1 */
    { Q14,  Q14, -Q14},  /* 2 */
    {-Q14,  Q14, -Q14},  /* 3 */
    {-Q14, -Q14,  Q14},  /* 4 */
    { Q14, -Q14,  Q14},  /* 5 */
    { Q14,  Q14,  Q14},  /* 6 */
    {-Q14,  Q14,  Q14},  /* 7 */
};

/* 6 quad faces: CCW vertex order when viewed from outside */
static const uint8_t F[6][4] = {
    {4, 5, 6, 7},  /* front  +Z */
    {1, 0, 3, 2},  /* back   -Z */
    {5, 1, 2, 6},  /* right  +X */
    {0, 4, 7, 3},  /* left   -X */
    {7, 6, 2, 3},  /* top    +Y */
    {0, 1, 5, 4},  /* bottom -Y */
};

/* Unit face normals in Q14 */
static const int16_t FN[6][3] = {
    { 0,     0,   Q14},  /* front  */
    { 0,     0,  -Q14},  /* back   */
    { Q14,   0,     0},  /* right  */
    {-Q14,   0,     0},  /* left   */
    { 0,   Q14,    0},   /* top    */
    { 0,  -Q14,    0},   /* bottom */
};

/* Face base colors (RGB565) */
static const uint16_t FC[6] = {
    0xF800,  /* front  — red     */
    0x07FF,  /* back   — cyan    */
    0x07E0,  /* right  — green   */
    0xF81F,  /* left   — magenta */
    0xFFE0,  /* top    — yellow  */
    0x001F,  /* bottom — blue    */
};

/* ---------------------------------------------------------------------------
 * Rotation helpers
 *
 * rotate3(x,y,z, ix,iy,iz, out[3])
 *   Applies Rz → Rx → Ry (intrinsic, gives natural tumble).
 *   All inputs and outputs in Q14.
 * -------------------------------------------------------------------------*/
static void rotate3(int32_t x, int32_t y, int32_t z,
                    int ang_x, int ang_y, int ang_z,
                    int32_t out[3])
{
    /* Step 1: Rz */
    int32_t ax = (x * cos_a(ang_z) - y * sin_a(ang_z)) >> 14;
    int32_t ay = (x * sin_a(ang_z) + y * cos_a(ang_z)) >> 14;
    int32_t az = z;

    /* Step 2: Rx */
    int32_t bx = ax;
    int32_t by = (ay * cos_a(ang_x) - az * sin_a(ang_x)) >> 14;
    int32_t bz = (ay * sin_a(ang_x) + az * cos_a(ang_x)) >> 14;

    /* Step 3: Ry */
    out[0] = (bx * cos_a(ang_y) + bz * sin_a(ang_y)) >> 14;
    out[1] = by;
    out[2] = (-bx * sin_a(ang_y) + bz * cos_a(ang_y)) >> 14;
}

/* ---------------------------------------------------------------------------
 * Perspective projection: world Q14 → screen pixel
 * sx = cx + wx * SCALE * FOCAL / (FOCAL * Q14 + wz * SCALE)
 * -------------------------------------------------------------------------*/
static int proj_x(int32_t wx, int32_t wz, int cx)
{
    int32_t denom = FOCAL * Q14 + wz * SCALE;
    if (denom <= 0) denom = 1;
    return cx + (int)(((int64_t)wx * SCALE * FOCAL) / denom);
}

static int proj_y(int32_t wy, int32_t wz, int cy)
{
    int32_t denom = FOCAL * Q14 + wz * SCALE;
    if (denom <= 0) denom = 1;
    return cy - (int)(((int64_t)wy * SCALE * FOCAL) / denom);
}

/* ---------------------------------------------------------------------------
 * Shade a base RGB565 color by a 0–255 brightness value.
 * -------------------------------------------------------------------------*/
static uint32_t shade_color(uint16_t base, int bright)
{
    if (bright < 0)   bright = 0;
    if (bright > 255) bright = 255;
    int r = ((base >> 11) & 0x1F) * bright >> 8;
    int g = ((base >>  5) & 0x3F) * bright >> 8;
    int b = ( base        & 0x1F) * bright >> 8;
    return (uint32_t)((r << 11) | (g << 5) | b);
}

/* ---------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------*/
int main(void)
{
    int32_t sw = 240, sh = 320;
    display_get_size(&sw, &sh);
    int cx = (int)(sw / 2);
    int cy = (int)(sh / 2) - 10;

    /* Angle accumulators [0, ANG_FULL) */
    int ang_x = 0, ang_y = 0, ang_z = 0, ang_l = 0;

    /* Frame pacer — measures actual render time and sleeps only the rest */
    int th = timer_create();

    while (1) {
        timer_start(th);

        /* ---- Read gyro --------------------------------------------------- */
        int32_t gx = sensor_read(SENSOR_CHAN_GYRO_X);
        int32_t gy = sensor_read(SENSOR_CHAN_GYRO_Y);
        int32_t gz = sensor_read(SENSOR_CHAN_GYRO_Z);
        int dx = (gx != AKIRA_SENSOR_ERROR) ? gx / GYRO_SCALE : 0;
        int dy = (gy != AKIRA_SENSOR_ERROR) ? gy / GYRO_SCALE : 0;
        int dz = (gz != AKIRA_SENSOR_ERROR) ? gz / GYRO_SCALE : 0;

        ang_x = (ang_x + AUTO_X + dx) & ANG_MASK;
        ang_y = (ang_y + AUTO_Y + dy) & ANG_MASK;
        ang_z = (ang_z + AUTO_Z + dz) & ANG_MASK;
        ang_l = (ang_l + 3)           & ANG_MASK;

        /* ---- Rotate all 8 vertices --------------------------------------- */
        int32_t rv[8][3];
        for (int i = 0; i < 8; i++)
            rotate3(V[i][0], V[i][1], V[i][2],
                    ang_x, ang_y, ang_z, rv[i]);

        /* ---- Project to screen ------------------------------------------ */
        int sx[8], sy[8];
        for (int i = 0; i < 8; i++) {
            sx[i] = proj_x(rv[i][0], rv[i][2], cx);
            sy[i] = proj_y(rv[i][1], rv[i][2], cy);
        }

        /* ---- Light direction (orbits in XZ plane) ------------------------ */
        int32_t lx = cos_a(ang_l);
        int32_t ly = 13107;          /* 0.8 * Q14 */
        int32_t lz = sin_a(ang_l);

        /* ---- Per-face: cull + shade + depth ------------------------------ */
        typedef struct {
            int     face;
            int32_t depth;
            uint32_t color;
        } DrawCmd;

        DrawCmd cmds[6];
        int n_cmds = 0;

        for (int f = 0; f < 6; f++) {
            int32_t rn[3];
            rotate3(FN[f][0], FN[f][1], FN[f][2],
                    ang_x, ang_y, ang_z, rn);

            if (rn[2] < 0) continue;  /* back-face cull */

            int32_t dot = ((int64_t)rn[0]*lx + (int64_t)rn[1]*ly
                           + (int64_t)rn[2]*lz) >> 14;
            int bright = 89 + (int)(((int64_t)dot * 166) >> 14);

            int32_t depth = (rv[F[f][0]][2] + rv[F[f][1]][2]
                           + rv[F[f][2]][2] + rv[F[f][3]][2]) / 4;

            cmds[n_cmds].face  = f;
            cmds[n_cmds].depth = depth;
            cmds[n_cmds].color = shade_color(FC[f], bright);
            n_cmds++;
        }

        /* ---- Painter's sort: back-to-front ------------------------------- */
        for (int i = 1; i < n_cmds; i++) {
            DrawCmd tmp = cmds[i];
            int j = i - 1;
            while (j >= 0 && cmds[j].depth > tmp.depth) {
                cmds[j + 1] = cmds[j];
                j--;
            }
            cmds[j + 1] = tmp;
        }

        /* ---- Render ------------------------------------------------------ */
        display_clear(COLOR_BLACK);

        for (int ci = 0; ci < n_cmds; ci++) {
            int f = cmds[ci].face;
            uint32_t col = cmds[ci].color;
            int v0 = F[f][0], v1 = F[f][1], v2 = F[f][2], v3 = F[f][3];

            display_triangle_fill(sx[v0], sy[v0],
                                  sx[v1], sy[v1],
                                  sx[v2], sy[v2], col);
            display_triangle_fill(sx[v0], sy[v0],
                                  sx[v2], sy[v2],
                                  sx[v3], sy[v3], col);

            uint32_t edge = shade_color(FC[f], 60);
            display_line(sx[v0], sy[v0], sx[v1], sy[v1], edge);
            display_line(sx[v1], sy[v1], sx[v2], sy[v2], edge);
            display_line(sx[v2], sy[v2], sx[v3], sy[v3], edge);
            display_line(sx[v3], sy[v3], sx[v0], sy[v0], edge);
        }

        display_text(4, (int)sh - 12, "CUBE3D", COLOR_DARK_GRAY);
        display_flush();

        /* ---- Frame pacer: sleep only whatever is left of FRAME_MS -------- */
        int elapsed = timer_elapsed(th);   /* milliseconds */
        int rest = FRAME_MS - elapsed;
        if (rest > 2)
            delay(rest * 1000);            /* delay takes microseconds */
    }

    timer_free(th);
    return 0;
}
