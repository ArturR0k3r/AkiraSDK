/**
 * @file main.c
 * @brief Skyhop — an endless vertical jumper for AkiraOS
 *
 * Hopbot, a little robot on a pogo spring, bounces off every ledge it lands
 * on.  Steer it from ledge to ledge and climb from the garden through the
 * skies, dusk and night into space.  The column wraps around: leave on one
 * side and you come back on the other.
 *
 * Ledges: plain (one look per altitude), drifting, bobbing, puffs that
 * vanish after one hop, and cracked decoys that crumble under you.  Springs
 * throw you twice as high.  Critters can be stomped from above or zapped;
 * spike mines can only be zapped.  Balloons and rockets carry you up, a
 * bubble shield takes one hit.
 *
 * Controls:
 *   tilt          steer (LEFT / RIGHT override; D-pad-only in Options)
 *   A / UP        zap upward
 *   B / HOME      pause            X   show frame rate
 *
 * The first Play runs a short set-up (hold level, tilt right) that learns
 * which accelerometer axis runs across the screen and which way, so the
 * game does not depend on how the IMU is mounted.  Physics run in fixed
 * 1/60 s steps, decoupled from the frame rate.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"
#include "../../common/akira_spr.h"

/* ── Layout ──────────────────────────────────────────────────────────────
 * The play column is the same width on both panels, so the game plays the
 * same; the 400-px Sharp panel just gets wider side panels. */
#define PW 208
static int SW = 320, SH = 240, PX0 = 56;

/* ── Physics, per 1/60 s step, 8.8 fixed point ───────────────────────── */
#define FP        256
#define GRAV      92     /* 0.36 px/step^2                         */
#define JUMP_V    2170   /* 8.5 px/step: a ~100 px hop             */
#define SPRING_V  3150   /* ~210 px                                */
#define MAX_FALL  (-3072)
#define MAXV      830    /* 3.2 px/step sideways                   */
#define ACC       90
#define BALLOON_V 1000
#define BALLOON_T 150
#define ROCKET_V  2100
#define ROCKET_T  130
#define SHIELD_T  1200   /* 20 s                                   */
#define STEP3     50     /* one step in 1/3 ms                     */

#define PL_W   40
#define PL_H   10
#define HERO_W 16
#define HERO_H 20

/* Altitude zones (heights in px). */
#define N_ZONE 5
typedef struct {
    const char *name;
    uint8_t top[3], bot[3], acc[3];
    int h0;
} zone_t;
static const zone_t ZONES[N_ZONE] = {
    {"Garden", {92, 172, 248}, {196, 232, 255}, {130, 226, 90}, 0},
    {"Skies", {44, 112, 226}, {140, 200, 255}, {150, 220, 255}, 3200},
    {"Dusk", {58, 38, 118}, {250, 138, 88}, {255, 168, 96}, 8000},
    {"Night", {6, 10, 34}, {26, 34, 84}, {180, 186, 255}, 14000},
    {"Space", {0, 0, 0}, {16, 6, 34}, {226, 136, 255}, 21000},
};
#define H_FULL 26000 /* difficulty tops out here */
#define H_METER 28000

static int iabs(int v) { return v < 0 ? -v : v; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int lerp(int a, int b, int t, int one) { return a + (b - a) * t / one; }

static const int8_t SINQ[65] = {
    0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60, 63,
    65, 68, 71, 73, 76, 78, 81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 107,
    109, 111, 112, 113, 115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126,
    126, 126, 127, 127, 127, 127};
/* sin(a * 2pi / 256) * 127 */
static int isin(int a)
{
    a &= 255;
    if (a < 64) return SINQ[a];
    if (a < 128) return SINQ[128 - a];
    if (a < 192) return -SINQ[a - 128];
    return -SINQ[256 - a];
}

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* ── Art ─────────────────────────────────────────────────────────────────
 * One character per pixel, '.' clear.  Digits are game colours with an
 * explicit black/white value for the Sharp panel (see build_sprites). */
static const char A_HERO[] =
    ".......22......."
    ".......22......."
    "........k......."
    "........k......."
    "...kkkkkkkkkk..."
    "..kGGGGGGGGGGk.."
    ".kGwwGGGGGGGGGk."
    ".kGwGkkkkkkkkGk."
    ".kGwGk1w11w1kGk."
    ".kGGGk11111tkGk."
    ".kGGGkkkkkkkkGk."
    ".kGGGGGGGGGGGGk."
    ".kgGGGGGGGGGGgk."
    "..kggggggggggk.."
    "...kkkkkkkkkk..."
    ".......kk......."
    ".....kyyyyk....."
    "......kYYk......"
    ".....kyyyyk....."
    "....k333333k....";
_Static_assert(sizeof(A_HERO) == HERO_W * HERO_H + 1, "hero art");

static const char A_BUZZ[2][20 * 16 + 1] = {
    "......GGG..GGG......"
    ".....GiiiGGiiiG....."
    ".....GiiiiGiiiiG...."
    "......GiiiGiiiG....."
    ".......GGkkGG......."
    ".....kkkkkkkkkk....."
    "....kyyykyyykyyk...."
    "...kyyyykyyykwwwk..."
    "kk.kyyyykyyykwkkwk.."
    ".kkkyyyykyyykwkkwk.."
    "...kyyyykyyykwwwk..."
    "....kyyykyyykyyk...."
    ".....kkkkkkkkkk....."
    "......k...k..k......"
    "...................."
    "....................",
    "...................."
    "...................."
    "....GGGG..GGGG......"
    "...GiiiiGGiiiiG....."
    "....GGGGkkGGGG......"
    ".....kkkkkkkkkk....."
    "....kyyykyyykyyk...."
    "...kyyyykyyykwwwk..."
    "kk.kyyyykyyykwkkwk.."
    ".kkkyyyykyyykwkkwk.."
    "...kyyyykyyykwwwk..."
    "....kyyykyyykyyk...."
    ".....kkkkkkkkkk....."
    "......k...k..k......"
    "...................."
    "....................",
};

static const char A_UFO[2][20 * 12 + 1] = {
    "........kkkk........"
    ".......kiiiik......."
    "......kiwiiiik......"
    "......kiiiiiik......"
    "...kkkkkkkkkkkkkk..."
    "..kzzzzzzzzzzzzzzk.."
    ".kzGGGGGGGGGGGGGGzk."
    "kzz5zz5zz5zz5zz5zzzk"
    ".kZZZZZZZZZZZZZZZZk."
    "..kkkZZZZZZZZZZkkk.."
    ".....kkkkkkkkkk....."
    "......k........k....",
    "........kkkk........"
    ".......kiiiik......."
    "......kiwiiiik......"
    "......kiiiiiik......"
    "...kkkkkkkkkkkkkk..."
    "..kzzzzzzzzzzzzzzk.."
    ".kzGGGGGGGGGGGGGGzk."
    "kz5zz5zz5zz5zz5zz5zk"
    ".kZZZZZZZZZZZZZZZZk."
    "..kkkZZZZZZZZZZkkk.."
    ".....kkkkkkkkkk....."
    "......k........k....",
};

static const char A_MINE[] =
    ".......kk......."
    ".......Gk......."
    "..kk...Gk...kk.."
    "..kGk.kkkk.kGk.."
    "...kGk6666kGk..."
    "....k666666k...."
    "...k66w66666k..."
    "kGk66w6666666kGk"
    "kGk6666666666kGk"
    "...k666666R6k..."
    "....k666RR6k...."
    "...kGk6666kGk..."
    "..kGk.kkkk.kGk.."
    "..kk...kG...kk.."
    ".......kG......."
    ".......kk.......";
_Static_assert(sizeof(A_MINE) == 16 * 16 + 1, "mine art");

static const char A_SPRING[] =
    ".kkkkkkkkkk."
    ".kGGGGGGGGk."
    ".kkkkkkkkkk."
    "..kyyyyyyk.."
    "...kYYYYk..."
    "..kyyyyyyk.."
    "...kYYYYk..."
    "..kyyyyyyk.."
    ".kkkkkkkkkk."
    ".kggggggggk.";
_Static_assert(sizeof(A_SPRING) == 12 * 10 + 1, "spring art");

static const char A_BALLOON[] =
    "...kkkkkk..."
    "..krrrrrrk.."
    ".krwwrrrrrk."
    ".krwrrrrrrk."
    "krrrrrrrrrrk"
    "krrrrrrrrrrk"
    "krrrrrrrrrRk"
    ".krrrrrrrRk."
    ".kRrrrrrRRk."
    "..kRRRRRRk.."
    "...kkRRkk..."
    ".....kk....."
    "......k....."
    ".....k......"
    "......k....."
    ".......k...."
    "......k....."
    "......k.....";
_Static_assert(sizeof(A_BALLOON) == 12 * 18 + 1, "balloon art");

static const char A_ROCKET[] =
    ".....kk....."
    "....krrk...."
    "....krrk...."
    "...kGGGGk..."
    "...kG11Gk..."
    "...kG11Gk..."
    "...kGGGGk..."
    "...kGGGGk..."
    "..kkGGGGkk.."
    ".krkGGGGkrk."
    ".krkGGGGkrk."
    ".kkkkkkkkkk."
    "...koyyok..."
    "....kyyk...."
    ".....kk....."
    "............";
_Static_assert(sizeof(A_ROCKET) == 12 * 16 + 1, "rocket art");

/* Sprite caches (RGB565, SPR_KEY = clear). */
enum { HF_STAND, HF_CROUCH, HF_DEAD };
static uint16_t spr_hero[3][2][HERO_W * HERO_H]; /* [frame][0 right, 1 left] */
static uint16_t spr_buzz[2][2][20 * 16]; /* [frame][0 right, 1 left] */
static uint16_t spr_ufo[2][20 * 12];
static uint16_t spr_mine[16 * 16];
static uint16_t spr_spring[2][12 * 10];
static uint16_t spr_balloon[12 * 18];
static uint16_t spr_rocket[12 * 16];
#define N_PSPR 9
enum { PS_MOVE = 5, PS_VMOVE, PS_CLOUD, PS_CRACK };
static uint16_t spr_plat[N_PSPR][PL_W * PL_H];
static char art_tmp[HERO_W * HERO_H + 1];
static uint16_t scratch2x[HERO_W * HERO_H * 4];

static uint16_t C_PANEL, C_EDGE, C_TEXT, C_DIM, C_GOLD, C_RED;

typedef struct { uint8_t r, g, b; } rgb_t;

/* One platform look.  Colour: hl highlight row, top band, body, texture,
 * edge outline.  Mono: a distinct white pattern per kind. */
static void build_plat(uint16_t *d, int kind)
{
    static const rgb_t P[N_PSPR][5] = {
        /* hl              top              body             tex              edge   */
        {{176, 244, 112}, {84, 196, 64}, {150, 96, 52}, {104, 62, 32}, {40, 30, 18}},     /* garden */
        {{255, 255, 255}, {222, 230, 246}, {178, 190, 216}, {230, 190, 80}, {70, 80, 112}}, /* skies  */
        {{244, 186, 116}, {214, 138, 70}, {166, 98, 48}, {104, 56, 26}, {58, 30, 14}},    /* dusk   */
        {{206, 220, 240}, {140, 160, 192}, {78, 92, 124}, {190, 200, 220}, {18, 22, 40}},  /* night  */
        {{224, 255, 255}, {118, 238, 228}, {58, 146, 190}, {230, 255, 255}, {20, 38, 70}}, /* space  */
        {{170, 222, 255}, {70, 150, 240}, {30, 70, 160}, {255, 214, 70}, {10, 20, 52}},    /* move   */
        {{232, 192, 255}, {180, 110, 240}, {98, 48, 168}, {255, 214, 70}, {30, 10, 52}},   /* vmove  */
        {{255, 255, 255}, {246, 248, 255}, {206, 222, 244}, {176, 196, 228}, {150, 170, 204}}, /* cloud */
        {{150, 146, 140}, {120, 116, 112}, {84, 80, 78}, {20, 18, 18}, {30, 28, 28}},      /* crack  */
    };
    const rgb_t *p = P[kind];
    for (int y = 0; y < PL_H; y++) {
        for (int x = 0; x < PL_W; x++) {
            int edge = (x == 0 || x == PL_W - 1 || y == 0 || y == PL_H - 1);
            int corner = (x == 0 || x == PL_W - 1) && (y == 0 || y == PL_H - 1);
            int pick; /* 0 hl, 1 top, 2 body, 3 tex, 4 edge, -1 clear */
            if (corner) pick = -1;
            else if (edge) pick = 4;
            else if (y == 1) pick = 0;
            else if (y <= 3) pick = 1;
            else pick = 2;

            if (pick == 2 || pick == 1) {
                switch (kind) {
                case 0: /* grass fringe and pebbles */
                    if (y == 4 && (x % 5 == 1 || x % 5 == 2)) pick = 1;
                    else if (y >= 5 && (x * 7 + y * 3) % 11 == 0) pick = 3;
                    break;
                case 1: /* marble with a gold trim */
                    if (y == 3) pick = 3;
                    else if (y >= 5 && (x + y * 3) % 13 == 0) pick = 4;
                    break;
                case 2: /* planks */
                    if (y >= 2 && (x % 13 == 0)) pick = 3;
                    else if (y == 6 && x % 5 == 2) pick = 3;
                    break;
                case 3: /* girder: X bracing and rivets */
                    if (y >= 4 && ((x + y) % 10 == 0 || (x - y + 40) % 10 == 0)) pick = 3;
                    break;
                case 4: /* crystal facets */
                    if (y >= 4 && ((x * 3 + y * 5) % 17 == 0)) pick = 3;
                    else if (y >= 4 && (x % 9 == 4)) pick = 0;
                    break;
                case PS_MOVE: { /* > > chevrons */
                    int cx = (x - 6) % 14, cy = iabs(y - 5);
                    if (y >= 3 && y <= 7 && cx >= 0 && (cx == cy || cx == cy + 1)) pick = 3;
                    break;
                }
                case PS_VMOVE: { /* ^ v marks */
                    int cx = x % 13 - 6;
                    if ((y == 3 && iabs(cx) <= 1) || (y == 4 && iabs(cx) == 2)) pick = 3;
                    if ((y == 7 && iabs(cx) <= 1) || (y == 6 && iabs(cx) == 2)) pick = 3;
                    break;
                }
                case PS_CLOUD:
                    break;
                case PS_CRACK: /* zig-zag cracks */
                    if ((x == 13 + (y & 1) || x == 27 - (y & 1)) && y >= 1) pick = 3;
                    else if (y == 6 && (x == 6 || x == 33)) pick = 3;
                    break;
                }
            }
            if (kind == PS_CLOUD) {
                /* scalloped top, round ends */
                int bump = x % 10, top = (bump >= 3 && bump <= 6) ? 0 : (bump == 2 || bump == 7) ? 1 : 2;
                if (y < top) pick = -1;
                else if (y == top && pick != -1) pick = 4;
                if ((x <= 1 || x >= PL_W - 2) && (y <= 1 || y >= PL_H - 2)) pick = -1;
            }
            if (kind == PS_CRACK && (x == 13 + (y & 1) || x == 27 - (y & 1)) && y < PL_H - 2) pick = -1; /* split */

            uint16_t c;
            if (pick < 0) c = SPR_KEY;
            else if (!spr_mono) c = spr_col(p[pick].r, p[pick].g, p[pick].b);
            else {
                int on;
                switch (kind) {
                case PS_MOVE:  on = edge || (y % 2 == 1 && y > 1); break;
                case PS_VMOVE: on = edge || x % 3 == 0; break;
                case PS_CLOUD: on = pick == 4 || (pick >= 0 && (x * 5 + y * 3) % 7 == 0); break;
                case PS_CRACK: on = (edge && x % 4 < 2) || pick == 3; break;
                default:       on = edge || y <= 3 || ((x + (y & 1) * 2) % 4 == 0 && y % 2 == 1); break;
                }
                c = on ? 0xFFFF : 0x0000;
            }
            d[y * PL_W + x] = c;
        }
    }
}

static void build_sprites(void)
{
    /* Game colours with explicit black/white values for the Sharp. */
    spr_pal_set('1', 80, 214, 255, 0); /* visor             */
    spr_pal_set('2', 255, 96, 64, 1);  /* antenna tip       */
    spr_pal_set('3', 74, 78, 98, 1);   /* foot pad          */
    spr_pal_set('5', 255, 232, 90, 0); /* UFO lights        */
    spr_pal_set('6', 206, 44, 70, 1);  /* mine shell        */
    spr_pal_set('t', 40, 150, 190, 0); /* visor shade       */

    int n = HERO_W * HERO_H;
    spr_build(spr_hero[HF_STAND][0], A_HERO, n, 0);
    /* Crouch: squash the spring (drop two coil rows, shift down). */
    for (int i = 0; i < 2 * HERO_W; i++) art_tmp[i] = '.';
    for (int r = 0; r < 16; r++)
        for (int x = 0; x < HERO_W; x++) art_tmp[(r + 2) * HERO_W + x] = A_HERO[r * HERO_W + x];
    for (int x = 0; x < HERO_W; x++) {
        art_tmp[18 * HERO_W + x] = A_HERO[18 * HERO_W + x];
        art_tmp[19 * HERO_W + x] = A_HERO[19 * HERO_W + x];
    }
    spr_build(spr_hero[HF_CROUCH][0], art_tmp, n, 0);
    /* Zapped: upside down. */
    for (int r = 0; r < HERO_H; r++)
        for (int x = 0; x < HERO_W; x++) art_tmp[r * HERO_W + x] = A_HERO[(HERO_H - 1 - r) * HERO_W + x];
    spr_build(spr_hero[HF_DEAD][0], art_tmp, n, 0);
    for (int f = 0; f < 3; f++) spr_mirror(spr_hero[f][1], spr_hero[f][0], HERO_W, HERO_H);

    for (int f = 0; f < 2; f++) {
        spr_build(spr_buzz[f][0], A_BUZZ[f], 20 * 16, 0);
        spr_mirror(spr_buzz[f][1], spr_buzz[f][0], 20, 16);
        spr_build(spr_ufo[f], A_UFO[f], 20 * 12, 0);
    }
    spr_build(spr_mine, A_MINE, 16 * 16, 0);
    spr_build(spr_spring[0], A_SPRING, 12 * 10, 0);
    /* Compressed spring: four empty rows, then plate, coil, base. */
    static const int rows[] = {0, 1, 2, 7, 8, 9};
    for (int i = 0; i < 4 * 12; i++) art_tmp[i] = '.';
    for (int r = 0; r < 6; r++)
        for (int x = 0; x < 12; x++) art_tmp[(4 + r) * 12 + x] = A_SPRING[rows[r] * 12 + x];
    spr_build(spr_spring[1], art_tmp, 12 * 10, 0);
    spr_build(spr_balloon, A_BALLOON, 12 * 18, 0);
    spr_build(spr_rocket, A_ROCKET, 12 * 16, 0);
    for (int k = 0; k < N_PSPR; k++) build_plat(spr_plat[k], k);

    spr_theme_init(120, 200, 255);
    C_PANEL = spr_mono ? 0x0000 : SPR_RGB(16, 22, 48);
    C_EDGE = spr_mono ? 0xFFFF : SPR_RGB(80, 120, 190);
    C_TEXT = 0xFFFF;
    C_DIM = spr_mono ? 0xFFFF : SPR_RGB(140, 160, 200);
    C_GOLD = spr_mono ? 0xFFFF : SPR_RGB(255, 214, 80);
    C_RED = spr_mono ? 0xFFFF : SPR_RGB(255, 96, 90);

    if (spr_mono) {
        g_theme.bg = 0x0000;
        g_theme.border = g_theme.text = g_theme.dim = g_theme.title = 0xFFFF;
        g_theme.sel_bg = 0xFFFF;
        g_theme.sel_text = 0x0000;
        g_theme.shadow = 0x0000;
    } else {
        g_theme.bg = SPR_RGB(18, 26, 60);
        g_theme.border = SPR_RGB(120, 200, 255);
        g_theme.dim = SPR_RGB(150, 170, 205);
        g_theme.sel_bg = SPR_RGB(40, 104, 196);
        g_theme.title = SPR_RGB(255, 214, 80);
    }
}

/* ── Settings ────────────────────────────────────────────────────────── */
static int best, runs;
/* Tilt is the main control.  The IMU's mounting (which chip axis runs across
 * the screen, and which way) is learnt once by the set-up screen and saved;
 * the D-pad always works as an override. */
static int ctl_tilt = 1, ctl_axis, ctl_invert, ctl_sens = 3, ctl_cal;
static int tilt_zero; /* milli-m/s^2 offset for sensor bias */

static void save_ctl(void)
{
    g_save_int("skyhop/ctl", ctl_tilt | ctl_axis << 1 | ctl_invert << 2 | ctl_sens << 4 | ctl_cal << 7);
}
static void load_settings(void)
{
    best = g_load_int("skyhop/best", 0);
    runs = g_load_int("skyhop/runs", 0);
    int c = g_load_int("skyhop/ctl", 1 | 3 << 4); /* default: tilt, not set up yet */
    ctl_tilt = c & 1;
    ctl_axis = (c >> 1) & 1;
    ctl_invert = (c >> 2) & 1;
    ctl_sens = iclamp((c >> 4) & 7, 1, 5);
    ctl_cal = (c >> 7) & 1;
    tilt_zero = iclamp(g_load_int("skyhop/tz", 0), -4000, 4000);
}

/* ── Tilt ────────────────────────────────────────────────────────────── */
static int imu_ok = 1;

/* Both accelerometer axes across the screen plane; 0 if there is no IMU. */
static int read_axes(int *x, int *y)
{
    int a = sensor_read(SENSOR_CHAN_ACCEL_X);
    int b = sensor_read(SENSOR_CHAN_ACCEL_Y);
    if (a == AKIRA_SENSOR_ERROR || b == AKIRA_SENSOR_ERROR) {
        imu_ok = 0;
        return 0;
    }
    imu_ok = 1;
    *x = a;
    *y = b;
    return 1;
}

/* The steering axis, signed so that tilting right is positive. */
static int tilt_raw(void)
{
    int v = sensor_read(ctl_axis ? SENSOR_CHAN_ACCEL_Y : SENSOR_CHAN_ACCEL_X);
    if (v == AKIRA_SENSOR_ERROR) {
        imu_ok = 0;
        return 0;
    }
    imu_ok = 1;
    return ctl_invert ? -v : v;
}
/* Side-to-side tilt reads ~0 whenever the console is held level, however far
 * the screen is tipped towards the player, so there is no per-run zeroing:
 * only a saved offset for sensor bias, set while the player holds it level. */
static void tilt_calibrate(void)
{
    tilt_zero = tilt_raw();
    g_save_int("skyhop/tz", tilt_zero);
}

/* -1024..1024 relative to level, with a small dead zone. */
static int tilt_value(void)
{
    static const int full[5] = {6000, 4800, 3600, 2800, 2000}; /* ~38..12 degrees */
    int t = tilt_raw() - tilt_zero;
    if (!imu_ok || iabs(t) < 250) return 0;
    return iclamp(t * 1024 / full[ctl_sens - 1], -1024, 1024);
}

/* ── World ───────────────────────────────────────────────────────────── */
enum { PT_NONE, PT_GROUND, PT_STATIC, PT_MOVE, PT_VMOVE, PT_CLOUD, PT_CRACK };
typedef struct {
    int32_t x;  /* FP, left edge in the column  */
    int32_t y;  /* px, top surface height       */
    int32_t y0; /* px, centre of a bobbing ledge */
    int32_t py; /* px, top surface height one step ago */
    int16_t vx; /* FP/step                      */
    uint8_t type, skin;
    uint8_t spring; /* 0 none, else x offset + 1 */
    uint8_t sp_t, phase, amp;
} plat_t;
#define MAXP 48
static plat_t plats[MAXP];

enum { EN_NONE, EN_BUZZ, EN_UFO, EN_MINE };
typedef struct {
    int32_t x;      /* FP left */
    int32_t y;      /* px bottom */
    int16_t vx;
    uint8_t type, t, dying;
} enemy_t;
#define MAXE 6
static enemy_t enemies[MAXE];

enum { IT_NONE, IT_BALLOON, IT_ROCKET, IT_SHIELD };
typedef struct {
    int32_t x, y; /* px: left, bottom */
    uint8_t type;
} item_t;
#define MAXI 4
static item_t items[MAXI];

typedef struct { int32_t x, y; uint8_t on; } bolt_t;
#define MAXB 3
static bolt_t bolts[MAXB];

typedef struct {
    int32_t x, y; /* FP */
    int16_t vx, vy;
    uint16_t col;
    uint8_t life, sz;
} part_t;
#define MAXPART 28
static part_t parts[MAXPART];
static int part_next;

/* hero */
enum { HS_PLAY, HS_ZAP, HS_FALL };
static int32_t hx, hy, hvx, hvy; /* FP; hx left edge, hy feet height */
static int hstate, face, crouch, shoot_t, shoot_cd, inv_t;
static int power, power_t, shield;

/* run */
static int cam;       /* px height of the screen's bottom edge */
static int max_h, score, kills, zone, toast_t, over, over_t, new_best;
static int gen_y, last_enemy_y, last_item_y;
static uint32_t steps;
static int ctl_dir, tilt_v, want_shoot, want_pause, show_fps, fps;
static int title_mode; /* attract loop on the title screen: no input, no death */

static int sy_of(int h) { return SH - (h - cam); }
static int zone_of(int h)
{
    int z = 0;
    while (z + 1 < N_ZONE && h >= ZONES[z + 1].h0) z++;
    return z;
}
static int diff_of(int h) { return h >= H_FULL ? 1024 : (h > 0 ? h * 1024 / H_FULL : 0); }

static void burst(int32_t x, int32_t y, int n, uint16_t col, int spread, int up)
{
    for (int i = 0; i < n; i++) {
        part_t *p = &parts[part_next];
        part_next = (part_next + 1) % MAXPART;
        p->x = x;
        p->y = y;
        p->vx = (int16_t)((int)g_rand_n(spread * 2 + 1) - spread);
        p->vy = (int16_t)(up + (int)g_rand_n(spread + 1));
        p->col = col;
        p->life = (uint8_t)(18 + g_rand_n(14));
        p->sz = (uint8_t)(2 + (g_rand() & 1));
    }
}

static plat_t *plat_new(void)
{
    for (int i = 0; i < MAXP; i++)
        if (plats[i].type == PT_NONE) {
            plat_t *p = &plats[i];
            p->vx = 0;
            p->spring = p->sp_t = p->phase = p->amp = 0;
            p->skin = 0;
            return p;
        }
    return 0;
}

static void add_plat(int type, int x, int y)
{
    plat_t *p = plat_new();
    if (!p) return;
    p->type = (uint8_t)type;
    p->x = x * FP;
    p->y = p->y0 = p->py = y;
    p->skin = (uint8_t)zone_of(y);
}

/* Is the column x-range [a0,a1) overlapping [b0,b1), across the wrap? (FP) */
static int xover(int32_t a0, int32_t a1, int32_t b0, int32_t b1)
{
    for (int s = -1; s <= 1; s++) {
        int32_t o = s * PW * FP;
        if (a0 + o < b1 && a1 + o > b0) return 1;
    }
    return 0;
}

/* Lay the next "path" ledge plus decoys, fillers, critters and pickups.
 * Every path ledge is within reach of the one below (gap < a full hop), so
 * there is always a way up; decoys and puffs are extra. */
static void gen_row(void)
{
    int d = diff_of(gen_y);
    int gmin = lerp(20, 50, d, 1024), gmax = lerp(46, 84, d, 1024);
    int gap = gmin + (int)g_rand_n((uint32_t)(gmax - gmin + 1));
    if (gen_y - last_enemy_y < 120) gap = imin(gap, 64); /* breathe after a critter */

    int r = (int)g_rand_n(1024);
    int pmove = gen_y > 600 ? lerp(60, 300, d, 1024) : 0;
    int pv = gen_y > 6000 ? lerp(0, 170, d, 1024) : 0;
    int pcloud = gen_y > 1800 ? lerp(40, 170, d, 1024) : 0;
    int type = PT_STATIC;
    if (r < pmove) type = PT_MOVE;
    else if (r < pmove + pv) type = PT_VMOVE;
    else if (r < pmove + pv + pcloud) type = PT_CLOUD;

    int prev = gen_y;
    plat_t *p = plat_new();
    if (!p) {
        gen_y += gap;
        return;
    }
    int px = (int)g_rand_n(PW - PL_W + 1);
    int y = prev + gap;
    p->type = (uint8_t)type;
    p->x = px * FP;
    p->skin = (uint8_t)zone_of(y);
    if (type == PT_MOVE) {
        int v = lerp(140, 400, d, 1024) + (int)g_rand_n(64);
        p->vx = (int16_t)((g_rand() & 1) ? v : -v);
    }
    if (type == PT_VMOVE) {
        p->amp = 20;
        y = prev + imax(28, imin(gap, gmax - 20)) ; /* top of its swing stays in reach */
        p->phase = (uint8_t)g_rand();
        p->y0 = y;
        p->y = p->py = y + p->amp * isin(p->phase) / 127;
        gen_y = y - p->amp;                   /* the next ledge is measured from its low point */
    } else {
        p->y = p->y0 = p->py = y;
        gen_y = y;
    }
    if ((type == PT_STATIC || type == PT_MOVE) && (int)g_rand_n(1024) < lerp(70, 40, d, 1024))
        p->spring = (uint8_t)(1 + 4 + g_rand_n(PL_W - 20));

    /* Cracked decoy between the two path ledges. */
    if (gen_y > 900 && (int)g_rand_n(1024) < lerp(80, 280, d, 1024) && gap > 30) {
        int dx = (int)g_rand_n(PW - PL_W + 1);
        add_plat(PT_CRACK, dx, prev + gap / 2);
    }
    /* Filler ledges: plenty low down, rare up high. */
    if ((int)g_rand_n(1024) < lerp(520, 40, d, 1024) && gap > 26) {
        int fx = (px + PL_W + 12 + (int)g_rand_n(PW - 2 * PL_W - 24)) % (PW - PL_W);
        add_plat(PT_STATIC, fx, prev + 10 + (int)g_rand_n((uint32_t)(gap - 18)));
    }

    /* Critters. */
    if (!title_mode && y > 1800 && y - last_enemy_y > lerp(1500, 520, d, 1024) &&
        (int)g_rand_n(1024) < lerp(160, 420, d, 1024)) {
        for (int i = 0; i < MAXE; i++) {
            enemy_t *e = &enemies[i];
            if (e->type != EN_NONE) continue;
            int z = zone_of(y);
            e->dying = 0;
            e->t = (uint8_t)g_rand();
            if (y > 5000 && g_rand_n(100) < 30) {
                /* A mine sits off to the side of the ledge below it. */
                e->type = EN_MINE;
                int mx = (px + PL_W / 2 + PW / 2 - 8 + (int)g_rand_n(40) - 20 + PW) % PW;
                e->x = iclamp(mx, 0, PW - 16) * FP;
                e->y = y + 34;
                e->vx = 0;
            } else {
                e->type = z >= 3 ? EN_UFO : EN_BUZZ;
                e->x = (int)g_rand_n(PW - 20) * FP;
                e->y = y + 40;
                int v = lerp(120, 300, d, 1024) + (e->type == EN_UFO ? 80 : 0);
                e->vx = (int16_t)((g_rand() & 1) ? v : -v);
            }
            last_enemy_y = y;
            break;
        }
    }

    /* Pickups float above a path ledge. */
    if (!title_mode && y > 900 && y - last_item_y > 1600 && g_rand_n(1024) < 70) {
        for (int i = 0; i < MAXI; i++) {
            item_t *it = &items[i];
            if (it->type != IT_NONE) continue;
            int r2 = (int)g_rand_n(100);
            it->type = (uint8_t)(r2 < 40 ? IT_BALLOON : (r2 < 65 && y > 4000) ? IT_ROCKET : IT_SHIELD);
            it->x = px + PL_W / 2 - 6;
            it->y = (type == PT_VMOVE ? p->y0 + p->amp : y) + 24;
            last_item_y = y;
            break;
        }
    }
}

static void new_game(void)
{
    for (int i = 0; i < MAXP; i++) plats[i].type = PT_NONE;
    for (int i = 0; i < MAXE; i++) enemies[i].type = EN_NONE;
    for (int i = 0; i < MAXI; i++) items[i].type = IT_NONE;
    for (int i = 0; i < MAXB; i++) bolts[i].on = 0;
    for (int i = 0; i < MAXPART; i++) parts[i].life = 0;
    plat_t *g = plat_new();
    g->type = PT_GROUND;
    g->x = 0;
    g->y = g->y0 = g->py = 0;
    cam = -40; /* the lawn shows under Hopbot at the start */
    gen_y = 0;
    last_enemy_y = last_item_y = 0;
    while (gen_y < SH + 80) gen_row();

    hx = (PW / 2 - HERO_W / 2) * FP;
    hy = 0;
    hvx = 0;
    hvy = JUMP_V;
    hstate = HS_PLAY;
    face = 0;
    crouch = shoot_t = shoot_cd = inv_t = 0;
    power = power_t = shield = 0;
    max_h = score = kills = 0;
    zone = 0;
    toast_t = 0;
    over = over_t = 0;
    new_best = 0;
    steps = 0;
    want_shoot = want_pause = 0;
}

/* ── Simulation ──────────────────────────────────────────────────────── */
static void kill_enemy(enemy_t *e)
{
    e->dying = 1;
    e->t = 0;
    kills++;
    int w = e->type == EN_MINE ? 16 : 20;
    burst(e->x + w * FP / 2, (e->y + 8) * FP, 8, spr_mono ? 0xFFFF : SPR_RGB(255, 230, 120), 260, 100);
}

static int enemy_h(const enemy_t *e) { return e->type == EN_BUZZ ? 14 : e->type == EN_UFO ? 12 : 16; }
static int enemy_w(const enemy_t *e) { return e->type == EN_MINE ? 16 : 20; }
static int enemy_bob(const enemy_t *e)
{
    return e->type == EN_UFO ? isin(e->t * 3) * 6 / 127 : isin(e->t * 4) * 3 / 127;
}

static void wrap_hero(void)
{
    if (hx + HERO_W * FP / 2 < 0) hx += PW * FP;
    else if (hx + HERO_W * FP / 2 >= PW * FP) hx -= PW * FP;
}

static void hero_step(void)
{
    if (hstate != HS_PLAY) {
        hvy -= GRAV;
        if (hvy < MAX_FALL) hvy = MAX_FALL;
        hy += hvy;
        hx += hvx;
        wrap_hero();
        if (hy / FP + HERO_H < cam - 24) over = 1;
        return;
    }

    /* steering */
    if (ctl_dir) {
        hvx += ctl_dir * ACC;
        if (ctl_dir * hvx < 0) hvx += ctl_dir * ACC; /* turn quickly */
    } else if (ctl_tilt && imu_ok) {
        int target = tilt_v * MAXV / 1024;
        hvx += (target - hvx) / 4;
    } else {
        hvx = hvx * 13 / 16;
    }
    hvx = iclamp(hvx, -MAXV, MAXV);
    if (hvx > 60) face = 0;
    else if (hvx < -60) face = 1;
    hx += hvx;
    wrap_hero();

    int32_t prev = hy;
    if (power) {
        hvy = power == IT_ROCKET ? ROCKET_V : BALLOON_V;
        if (power == IT_ROCKET && (steps & 1))
            burst(hx + HERO_W * FP / 2, hy, 1, spr_mono ? 0xFFFF : ((steps & 2) ? SPR_RGB(255, 200, 60) : SPR_RGB(255, 110, 40)), 60, -300);
        if (--power_t <= 0) {
            power = 0;
            hvy = JUMP_V / 2;
        }
    } else {
        hvy -= GRAV;
        if (hvy < MAX_FALL) hvy = MAX_FALL;
    }
    hy += hvy;

    /* Landing: one-way ledges, only while falling.  A spring is a surface
     * of its own, 8 px above its ledge. */
    if (!power && hvy < 0) {
        plat_t *bestp = 0;
        int best_top = 0, on_spring = 0;
        int32_t f0 = hx + 3 * FP, f1 = hx + (HERO_W - 3) * FP;
        for (int i = 0; i < MAXP; i++) {
            plat_t *p = &plats[i];
            if (p->type == PT_NONE) continue;
            int w = p->type == PT_GROUND ? PW : PL_W;
            if (p->spring) {
                int32_t s0 = p->x + (p->spring - 1) * FP, s1 = s0 + 12 * FP;
                int st = p->y + 8;
                if (prev >= (p->py + 8) * FP && hy <= st * FP && xover(f0, f1, s0, s1) &&
                    (!bestp || st > best_top)) {
                    bestp = p;
                    best_top = st;
                    on_spring = 1;
                    continue;
                }
            }
            if (prev < p->py * FP || hy > p->y * FP) continue;
            if (!xover(f0, f1, p->x, p->x + w * FP)) continue;
            if (!bestp || p->y > best_top) {
                bestp = p;
                best_top = p->y;
                on_spring = 0;
            }
        }
        if (bestp) {
            plat_t *p = bestp;
            int pxc = p->x + PL_W * FP / 2;
            if (p->type == PT_CRACK) {
                uint16_t c = spr_mono ? 0xFFFF : SPR_RGB(150, 116, 80);
                burst(pxc, p->y * FP, 10, c, 220, 0);
                p->type = PT_NONE;
            } else {
                hy = best_top * FP;
                hvy = JUMP_V;
                crouch = 7;
                if (on_spring) {
                    hvy = SPRING_V;
                    p->sp_t = 12;
                }
                if (p->type == PT_CLOUD) {
                    burst(pxc, p->y * FP, 12, spr_mono ? 0xFFFF : SPR_RGB(240, 246, 255), 300, 40);
                    p->type = PT_NONE;
                } else {
                    burst(hx + HERO_W * FP / 2, hy, 3, spr_mono ? 0xFFFF : SPR_RGB(230, 230, 210), 120, 60);
                }
            }
        }
    }
    if (crouch) crouch--;
    if (shoot_t) shoot_t--;
    if (shoot_cd) shoot_cd--;
    if (inv_t) inv_t--;
    if (shield) shield--;

    /* Critters. */
    int32_t b0 = hx + 2 * FP, b1 = hx + (HERO_W - 2) * FP;
    int32_t bb = hy, bt = hy + (HERO_H - 2) * FP;
    for (int i = 0; i < MAXE && !title_mode; i++) {
        enemy_t *e = &enemies[i];
        if (e->type == EN_NONE || e->dying) continue;
        int ey = e->y + enemy_bob(e), eh = enemy_h(e), ew = enemy_w(e);
        int32_t e0 = e->x + 2 * FP, e1 = e->x + (ew - 2) * FP;
        if (!xover(b0, b1, e0, e1) || bb > (ey + eh) * FP || bt < (ey + 2) * FP) continue;
        if (power) {
            kill_enemy(e);
            continue;
        }
        if (e->type != EN_MINE && hvy < 0 && prev >= (ey + eh / 2) * FP) {
            kill_enemy(e);
            hvy = JUMP_V;
            crouch = 7;
            continue;
        }
        if (inv_t) continue;
        if (shield) {
            shield = 0;
            inv_t = 60;
            kill_enemy(e);
            continue;
        }
        hstate = HS_ZAP;
        hvy = 3 * FP;
        hvx = 0;
        burst(hx + HERO_W * FP / 2, hy + HERO_H * FP / 2, 10, spr_mono ? 0xFFFF : SPR_RGB(255, 240, 120), 300, 50);
        return;
    }

    /* Pickups. */
    for (int i = 0; i < MAXI; i++) {
        item_t *it = &items[i];
        if (it->type == IT_NONE) continue;
        int ih = it->type == IT_BALLOON ? 18 : it->type == IT_ROCKET ? 16 : 14;
        if (!xover(b0, b1, it->x * FP, (it->x + 12) * FP) || bb > (it->y + ih) * FP || bt < it->y * FP)
            continue;
        if (it->type == IT_SHIELD) {
            shield = SHIELD_T;
        } else {
            power = it->type;
            power_t = it->type == IT_ROCKET ? ROCKET_T : BALLOON_T;
        }
        burst((it->x + 6) * FP, (it->y + 8) * FP, 8, spr_mono ? 0xFFFF : SPR_RGB(255, 255, 255), 200, 80);
        it->type = IT_NONE;
    }

    if (hy / FP + HERO_H < cam) {
        if (title_mode) {
            hy = (cam + 40) * FP;
            hvy = JUMP_V;
        } else {
            hstate = HS_FALL;
        }
    }
}

static void step(void)
{
    steps++;

    for (int i = 0; i < MAXP; i++) {
        plat_t *p = &plats[i];
        if (p->type == PT_NONE) continue;
        if (p->type == PT_MOVE) {
            p->x += p->vx;
            if (p->x < 0) {
                p->x = -p->x;
                p->vx = (int16_t)-p->vx;
            } else if (p->x > (PW - PL_W) * FP) {
                p->x = 2 * (PW - PL_W) * FP - p->x;
                p->vx = (int16_t)-p->vx;
            }
        } else if (p->type == PT_VMOVE) {
            p->py = p->y;
            p->phase = (uint8_t)(p->phase + 2);
            p->y = p->y0 + p->amp * isin(p->phase) / 127;
        }
        if (p->sp_t) p->sp_t--;
        if (p->y < cam - 40 && p->type != PT_GROUND) p->type = PT_NONE;
        if (p->type == PT_GROUND && cam > 60) p->type = PT_NONE;
    }

    for (int i = 0; i < MAXE; i++) {
        enemy_t *e = &enemies[i];
        if (e->type == EN_NONE) continue;
        e->t++;
        if (e->dying) {
            e->y -= 4;
            if (e->t > 60) e->type = EN_NONE;
            continue;
        }
        e->x += e->vx;
        int w = enemy_w(e);
        if (e->x < 0) {
            e->x = 0;
            e->vx = (int16_t)-e->vx;
        } else if (e->x > (PW - w) * FP) {
            e->x = (PW - w) * FP;
            e->vx = (int16_t)-e->vx;
        }
        if (e->y < cam - 40) e->type = EN_NONE;
    }
    for (int i = 0; i < MAXI; i++)
        if (items[i].type && items[i].y < cam - 40) items[i].type = IT_NONE;

    /* Zap bolts. */
    if (want_shoot) {
        want_shoot = 0;
        if (hstate == HS_PLAY && !shoot_cd) {
            for (int i = 0; i < MAXB; i++)
                if (!bolts[i].on) {
                    bolts[i].on = 1;
                    bolts[i].x = hx / FP + 7;
                    if (bolts[i].x >= PW) bolts[i].x -= PW;
                    bolts[i].y = hy / FP + HERO_H;
                    shoot_t = 8;
                    shoot_cd = 9;
                    break;
                }
        }
    }
    for (int i = 0; i < MAXB; i++) {
        bolt_t *b = &bolts[i];
        if (!b->on) continue;
        b->y += 9;
        if (b->y > cam + SH + 10) {
            b->on = 0;
            continue;
        }
        for (int j = 0; j < MAXE; j++) {
            enemy_t *e = &enemies[j];
            if (e->type == EN_NONE || e->dying) continue;
            int ey = e->y + enemy_bob(e);
            int ex = e->x / FP;
            if (b->x + 2 > ex && b->x < ex + enemy_w(e) && b->y + 6 > ey && b->y - 9 < ey + enemy_h(e)) {
                kill_enemy(e);
                b->on = 0;
                break;
            }
        }
    }

    for (int i = 0; i < MAXPART; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        p->vy = (int16_t)(p->vy - 20);
    }

    hero_step();

    int top = hy / FP + HERO_H;
    if (hstate == HS_PLAY && top - SH * 58 / 100 > cam) cam = top - SH * 58 / 100;
    while (gen_y < cam + SH + 80) gen_row();

    if (hstate == HS_PLAY && hy / FP > max_h) {
        max_h = hy / FP;
        score = max_h / 4;
    }
    int z = zone_of(cam + SH / 2);
    if (z > zone) {
        zone = z;
        toast_t = 150;
    }
    if (toast_t) toast_t--;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static uint16_t mixc(const uint8_t *a, const uint8_t *b, int t, int one)
{
    return spr_col(lerp(a[0], b[0], t, one), lerp(a[1], b[1], t, one), lerp(a[2], b[2], t, one));
}

/* Sky colours for the middle of the screen, blended across zone borders. */
static void sky_at(int h, uint8_t *top, uint8_t *bot, int *zf)
{
    int z = zone_of(h);
    int t = 0;
    if (z + 1 < N_ZONE) {
        int from = ZONES[z + 1].h0 - 1400;
        if (h > from) t = (h - from) * 256 / 1400;
    }
    const zone_t *a = &ZONES[z], *b = &ZONES[z + 1 < N_ZONE ? z + 1 : z];
    for (int i = 0; i < 3; i++) {
        top[i] = (uint8_t)lerp(a->top[i], b->top[i], t, 256);
        bot[i] = (uint8_t)lerp(a->bot[i], b->bot[i], t, 256);
    }
    *zf = z * 256 + t;
}

static void draw_cloud(int x, int y, int s, uint16_t c)
{
    display_circle_fill(x + 8 * s, y, 7 * s, c);
    display_circle_fill(x + 18 * s, y - 4 * s, 9 * s, c);
    display_circle_fill(x + 28 * s, y, 7 * s, c);
    display_rect(x + 8 * s, y, 20 * s, 7 * s, c);
}

static void draw_sky(int x0, int w)
{
    uint8_t top[3], bot[3];
    int zf;
    sky_at(cam + SH / 2, top, bot, &zf);
    if (spr_mono) {
        display_rect(x0, 0, w, SH, 0x0000);
    } else {
        const int bands = 12;
        for (int i = 0; i < bands; i++) {
            int y0 = i * SH / bands, y1 = (i + 1) * SH / bands;
            display_rect(x0, y0, w, y1 - y0, mixc(top, bot, i, bands - 1));
        }
    }

    /* Stars from dusk on (parallax 1/4). */
    if (zf > 2 * 256 - 128) {
        int ps = cam / 4, cell = 22;
        int dense = zf >= 3 * 256 ? 2 : 1;
        for (int c = ps / cell - 1; c <= (ps + SH) / cell + 1; c++) {
            for (int k = 0; k < dense; k++) {
                uint32_t hh = hash32((uint32_t)c * 7u + (uint32_t)k * 131u + 17u);
                int x = (int)(hh % (uint32_t)w);
                int sy = SH - (c * cell + (int)((hh >> 12) % cell) - ps);
                if (sy < 0 || sy >= SH) continue;
                int bright = (hh >> 20) & 3;
                uint16_t col = spr_mono ? 0xFFFF : (bright == 0 ? SPR_RGB(255, 250, 210) : bright == 1 ? SPR_RGB(170, 180, 230) : SPR_RGB(110, 110, 160));
                if (((hh >> 24) & 7) == 0) display_rect(x0 + x, sy, 2, 2, col);
                else display_pixel(x0 + x, sy, col);
            }
        }
    }
    /* Moon at night, a ringed planet in space. */
    if (!spr_mono && zf >= 3 * 256 - 64 && zf < 4 * 256) {
        int my = 70 + (cam - 14000) / 10;
        if (my > -30 && my < SH + 30) {
            display_circle_fill(x0 + w - 50, my, 18, SPR_RGB(244, 238, 200));
            display_circle_fill(x0 + w - 56, my - 4, 4, SPR_RGB(214, 206, 170));
            display_circle_fill(x0 + w - 44, my + 7, 3, SPR_RGB(214, 206, 170));
        }
    }
    if (!spr_mono && zf >= 4 * 256 - 64) {
        int py = 60 + (cam - 21000) / 12;
        if (py > -40 && py < SH + 40) {
            int cx = x0 + 52;
            uint16_t ring = SPR_RGB(255, 214, 160), ring2 = SPR_RGB(190, 150, 120);
            /* tilted ring: back half, the planet, then the front half */
            display_line(cx - 34, py + 9, cx + 34, py - 9, ring2);
            display_line(cx - 34, py + 10, cx + 34, py - 8, ring2);
            display_circle_fill(cx, py, 20, SPR_RGB(206, 120, 230));
            display_circle_fill(cx - 6, py - 6, 9, SPR_RGB(226, 156, 244));
            display_rect(cx - 19, py + 6, 38, 3, SPR_RGB(160, 86, 200));
            display_line(cx - 34, py + 9, cx + 2, py - 1, ring);
            display_line(cx - 34, py + 10, cx + 2, py, ring);
        }
    }
    /* Clouds below the night (parallax 1/2). */
    if (!spr_mono && zf < 3 * 256) {
        int pc = cam / 2, cell = 96;
        uint16_t col = zf >= 2 * 256 - 128 ? SPR_RGB(250, 190, 170) : (zf >= 256 ? SPR_RGB(236, 244, 255) : SPR_RGB(255, 255, 255));
        for (int c = pc / cell - 1; c <= (pc + SH) / cell + 1; c++) {
            uint32_t hh = hash32((uint32_t)c * 2654435761u + 5u);
            if ((hh & 3) == 0) continue;
            int s = 1 + (int)((hh >> 8) & 1);
            int x = (int)((hh >> 10) % (uint32_t)(w + 20)) - 30;
            int sy = SH - (c * cell + (int)((hh >> 20) % 50u) - pc);
            if (sy < -20 || sy > SH + 20) continue;
            draw_cloud(x0 + x, sy, s, col);
        }
    }
    /* Hills and the lawn at the very start. */
    if (cam < 300) {
        int base = SH + cam / 2;
        uint16_t h1 = spr_col(120, 190, 120), h2 = spr_col(90, 160, 96);
        if (!spr_mono) {
            display_circle_fill(x0 + 30, base + 10, 60, h1);
            display_circle_fill(x0 + w - 40, base + 20, 70, h1);
            display_circle_fill(x0 + w / 2, base + 30, 64, h2);
        }
        int gy = sy_of(0);
        if (gy < SH) {
            display_rect(x0, gy, w, SH - gy, spr_mono ? 0x0000 : SPR_RGB(140, 92, 52));
            display_rect(x0, gy, w, 5, spr_mono ? 0xFFFF : SPR_RGB(84, 196, 64));
            display_hline(x0, gy, w, spr_mono ? 0xFFFF : SPR_RGB(176, 244, 112));
            if (!spr_mono)
                for (int x = 3; x < w; x += 9) display_rect(x0 + x, gy + 5, 3, 2, SPR_RGB(84, 196, 64));
        }
    }
}

static void text_large_out(int x, int y, const char *s, uint16_t c);

static void blit_wrap(int x, int y, int w, int h, const uint16_t *px)
{
    spr_blit(PX0 + x, y, w, h, px);
    if (x < 0) spr_blit(PX0 + x + PW, y, w, h, px);
    else if (x + w > PW) spr_blit(PX0 + x - PW, y, w, h, px);
}

static void draw_world(void)
{
    /* Best-height marker. */
    if (best > 0 && !title_mode) {
        int by = sy_of(best * 4);
        if (by > 0 && by < SH) {
            uint16_t c = spr_mono ? 0xFFFF : SPR_RGB(255, 220, 90);
            for (int x = 0; x < PW; x += 8) display_hline(PX0 + x, by, 4, c);
            display_text(PX0 + PW - 34, by - 11, "BEST", c);
        }
    }

    for (int i = 0; i < MAXP; i++) {
        plat_t *p = &plats[i];
        if (p->type == PT_NONE || p->type == PT_GROUND) continue;
        int sy = sy_of(p->y);
        if (sy < -12 || sy > SH) continue;
        int px = p->x / FP;
        int k = p->type == PT_STATIC ? p->skin : p->type == PT_MOVE ? PS_MOVE : p->type == PT_VMOVE ? PS_VMOVE
                : p->type == PT_CLOUD ? PS_CLOUD : PS_CRACK;
        spr_blit(PX0 + px, sy, PL_W, PL_H, spr_plat[k]);
        if (p->spring) spr_blit(PX0 + px + p->spring - 1, sy - 10, 12, 10, spr_spring[p->sp_t ? 1 : 0]);
    }

    for (int i = 0; i < MAXI; i++) {
        item_t *it = &items[i];
        if (it->type == IT_NONE) continue;
        int bob = isin((int)steps * 3 + i * 40) * 3 / 127;
        int sy = sy_of(it->y) + bob;
        if (it->type == IT_BALLOON) spr_blit(PX0 + it->x, sy - 18, 12, 18, spr_balloon);
        else if (it->type == IT_ROCKET) spr_blit(PX0 + it->x, sy - 16, 12, 16, spr_rocket);
        else {
            int cx = PX0 + it->x + 6, cy = sy - 7;
            display_circle_fill(cx, cy, 7, spr_mono ? 0x0000 : SPR_RGB(40, 120, 200));
            display_circle(cx, cy, 7, spr_mono ? 0xFFFF : SPR_RGB(140, 230, 255));
            display_circle(cx, cy, 5, spr_mono ? 0xFFFF : SPR_RGB(90, 190, 250));
            display_rect(cx - 3, cy - 4, 2, 2, 0xFFFF);
        }
    }

    for (int i = 0; i < MAXE; i++) {
        enemy_t *e = &enemies[i];
        if (e->type == EN_NONE) continue;
        int ey = e->y + enemy_bob(e);
        int sx = PX0 + e->x / FP, sy = sy_of(ey);
        if (sy < -4 || sy - 16 > SH) continue;
        if (e->dying && (e->t & 2)) continue;
        if (e->type == EN_BUZZ) {
            spr_blit(sx, sy - 16, 20, 16, spr_buzz[(e->t >> 2) & 1][e->vx < 0]);
        } else if (e->type == EN_UFO) {
            spr_blit(sx, sy - 12, 20, 12, spr_ufo[(e->t >> 3) & 1]);
        } else {
            spr_blit(sx, sy - 16, 16, 16, spr_mine);
        }
    }

    for (int i = 0; i < MAXB; i++) {
        bolt_t *b = &bolts[i];
        if (!b->on) continue;
        int sy = sy_of(b->y + 8);
        display_rect(PX0 + b->x, sy, 3, 8, spr_mono ? 0xFFFF : SPR_RGB(255, 220, 60));
        display_vline(PX0 + b->x + 1, sy + 1, 6, 0xFFFF);
    }

    for (int i = 0; i < MAXPART; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        int x = p->x / FP;
        if (x < 0) x += PW;
        if (x >= PW) x -= PW;
        display_rect(PX0 + x, sy_of(p->y / FP), p->sz, p->sz, p->col);
    }

    /* Hopbot. */
    int hxp = hx / FP, hyp = sy_of(hy / FP) - HERO_H;
    if (hyp < SH && hyp > -HERO_H - 30) {
        int blink = inv_t && (steps & 4);
        if (power == IT_BALLOON) {
            blit_wrap(hxp + 2, hyp - 20, 12, 18, spr_balloon);
        }
        if (!blink) {
            int f = hstate != HS_PLAY ? HF_DEAD : (crouch > 3 ? HF_CROUCH : HF_STAND);
            blit_wrap(hxp, hyp, HERO_W, HERO_H, spr_hero[f][face]);
        }
        if (power == IT_ROCKET) {
            int fl = 4 + (int)(steps & 3);
            display_rect(PX0 + hxp + 5, hyp + HERO_H, 6, fl, spr_mono ? 0xFFFF : SPR_RGB(255, 150, 40));
            display_rect(PX0 + hxp + 6, hyp + HERO_H, 4, fl - 2, spr_mono ? 0xFFFF : SPR_RGB(255, 236, 120));
        }
        if (shoot_t > 4) {
            display_rect(PX0 + hxp + 5, hyp - 4, 6, 4, spr_mono ? 0xFFFF : SPR_RGB(255, 240, 140));
        }
        if (shield && (shield > 120 || (steps & 8))) {
            int cx = PX0 + hxp + HERO_W / 2, cy = hyp + HERO_H / 2;
            display_circle(cx, cy, 15, spr_mono ? 0xFFFF : SPR_RGB(140, 230, 255));
            display_circle(cx, cy, 14, spr_mono ? 0xFFFF : SPR_RGB(70, 170, 240));
        }
    }

    if (toast_t && !title_mode) {
        const char *nm = ZONES[zone].name;
        int ty = 36;
        uint16_t c = spr_col(ZONES[zone].acc[0], ZONES[zone].acc[1], ZONES[zone].acc[2]);
        if (toast_t > 20 || (toast_t & 4)) {
            int tw = g_tw_large(nm);
            int tx = PX0 + PW / 2 - tw / 2;
            text_large_out(tx, ty, nm, c);
            const char *sub = "altitude reached";
            spr_text_out(PX0 + PW / 2 - g_tw(sub) / 2, ty + 22, sub, 0xFFFF);
        }
    }
}

static void text_large_out(int x, int y, const char *s, uint16_t c)
{
    if (!spr_mono) {
        display_text_large(x - 1, y, s, 0x0000);
        display_text_large(x + 1, y, s, 0x0000);
        display_text_large(x, y - 1, s, 0x0000);
        display_text_large(x, y + 1, s, 0x0000);
    }
    display_text_large(x, y, s, c);
}

static void meter_mark(int y, int x, uint16_t c)
{
    display_triangle_fill(x, y, x + 6, y - 4, x + 6, y + 4, c);
}

static void draw_panels(void)
{
    int rx = PX0 + PW;
    int rw = SW - rx;
    display_rect(0, 0, PX0, SH, C_PANEL);
    display_rect(rx, 0, rw, SH, C_PANEL);
    display_vline(PX0 - 1, 0, SH, C_EDGE);
    display_vline(rx, 0, SH, C_EDGE);

    /* Left: score, best, zone, power-ups. */
    char b[16];
    int cx = PX0 / 2;
    g_text_c(cx, 8, "SCORE", C_DIM);
    itoa(score, b);
    if (g_tw_large(b) <= PX0 - 6) display_text_large(cx - g_tw_large(b) / 2, 20, b, C_GOLD);
    else g_text_c(cx, 24, b, C_GOLD);
    g_text_c(cx, 48, "BEST", C_DIM);
    itoa(best > score ? best : score, b);
    g_text_c(cx, 60, b, C_TEXT);
    uint16_t zc = spr_col(ZONES[zone].acc[0], ZONES[zone].acc[1], ZONES[zone].acc[2]);
    g_text_c(cx, 84, ZONES[zone].name, zc);

    int y = 108;
    if (power) {
        if (power == IT_ROCKET) spr_blit(cx - 6, y, 12, 16, spr_rocket);
        else spr_blit(cx - 6, y, 12, 18, spr_balloon);
        spr_bar(8, y + 22, PX0 - 16, 4, power_t, power == IT_ROCKET ? ROCKET_T : BALLOON_T, SPRT.orange);
        y += 34;
    }
    if (shield) {
        display_circle(cx, y + 8, 7, spr_mono ? 0xFFFF : SPR_RGB(140, 230, 255));
        display_circle(cx, y + 8, 5, spr_mono ? 0xFFFF : SPR_RGB(90, 190, 250));
        spr_bar(8, y + 22, PX0 - 16, 4, shield, SHIELD_T, SPRT.cyan);
        y += 34;
    }
    if (kills) {
        itoa(kills, b);
        g_text_c(cx, SH - 62, b, C_TEXT);
        g_text_c(cx, SH - 50, "zapped", C_DIM);
    }
    if (show_fps) {
        b[0] = 0;
        g_cat_int(b, fps);
        g_cat(b, "fps");
        g_text_c(cx, SH - 76, b, C_RED);
    }
    if (ctl_tilt && imu_ok) {
        /* live tilt gauge */
        int gw = PX0 - 16, gx = 8, gy = SH - 18;
        display_rect_outline(gx, gy, gw, 7, C_EDGE);
        display_vline(gx + gw / 2, gy - 2, 11, C_DIM);
        int p = gx + gw / 2 + tilt_v * (gw / 2 - 3) / 1024;
        display_rect(p - 2, gy + 1, 4, 5, C_GOLD);
        g_text_c(cx, SH - 32, "TILT", C_DIM);
    } else {
        g_text_c(cx, SH - 22, ctl_tilt ? "D-pad" : "B:menu", C_DIM);
    }

    /* Right: altitude meter. */
    int mx = rx + rw / 2 - 3, m0 = 22, m1 = SH - 14, ml = m1 - m0;
    g_text_c(rx + rw / 2, 7, "ALT", C_DIM);
    for (int z = 0; z < N_ZONE; z++) {
        int h0 = ZONES[z].h0, h1 = z + 1 < N_ZONE ? ZONES[z + 1].h0 : H_METER;
        int y0 = m1 - h1 * ml / H_METER, y1 = m1 - h0 * ml / H_METER;
        uint16_t c = spr_mono ? 0x0000 : spr_col(ZONES[z].acc[0] / 2, ZONES[z].acc[1] / 2, ZONES[z].acc[2] / 2);
        display_rect(mx, y0, 6, y1 - y0, c);
        if (spr_mono && z) display_hline(mx - 2, y1, 10, 0xFFFF);
    }
    display_rect_outline(mx - 1, m0 - 1, 8, ml + 2, C_EDGE);
    int fill = imin(max_h, H_METER) * ml / H_METER;
    if (fill > 0) display_rect(mx + 1, m1 - fill, 4, fill, spr_mono ? 0xFFFF : zc);
    if (best > 0) {
        int by = m1 - imin(best * 4, H_METER) * ml / H_METER;
        display_hline(mx - 4, by, 14, C_GOLD);
    }
    int hy2 = m1 - iclamp(hy / FP, 0, H_METER) * ml / H_METER;
    meter_mark(hy2, mx + 8, C_TEXT);
}

static void render(void)
{
    draw_sky(PX0, PW);
    draw_world();
    draw_panels();
}

/* ── Screens ─────────────────────────────────────────────────────────── */
static void poll_input(void)
{
    uint32_t k;
    while ((k = g_key()) != 0) {
        if (k == K_A || k == K_UP) want_shoot = 1;
        else if (k == K_B || k == K_HOME) want_pause = 1;
        else if (k == K_X) show_fps ^= 1;
    }
    uint32_t held = (uint32_t)input_get_buttons();
    ctl_dir = ((held & AKIRA_BTN_RIGHT) ? 1 : 0) - ((held & AKIRA_BTN_LEFT) ? 1 : 0);
    /* light smoothing: 3/4 new reading, 1/4 previous */
    tilt_v = ctl_tilt ? (tilt_v + 3 * tilt_value()) / 4 : 0;
}

static int calib_step; /* set-up screen step, 0 when not shown */

/* Hopbot on a rail, following the tilt. */
static void draw_tilt_rail(int x, int w, int y, int v)
{
    display_rect_outline(x, y, w, 12, C_EDGE);
    display_vline(x + w / 2, y - 3, 17, C_DIM);
    int p = x + w / 2 + v * (w / 2 - 8) / 1024;
    spr_blit(p - HERO_W / 2, y - HERO_H - 2, HERO_W, HERO_H, spr_hero[HF_STAND][v < 0 ? 1 : 0]);
    display_rect(p - 2, y + 2, 4, 8, C_GOLD);
}

/* A console outline rotated by a (256 = full turn), clockwise = tilted right. */
static void draw_console_icon(int cx, int cy, int a, uint16_t body, uint16_t screen)
{
    int c = isin(a + 64), sn = isin(a);
    static const int8_t box[2][4][2] = {
        {{-36, -20}, {36, -20}, {36, 20}, {-36, 20}},
        {{-22, -14}, {22, -14}, {22, 10}, {-22, 10}},
    };
    for (int k = 0; k < 2; k++) {
        int px[4], py[4];
        for (int i = 0; i < 4; i++) {
            int x = box[k][i][0], y = box[k][i][1];
            px[i] = cx + (x * c - y * sn) / 127;
            py[i] = cy + (x * sn + y * c) / 127;
        }
        uint16_t col = k ? screen : body;
        display_triangle_fill(px[0], py[0], px[1], py[1], px[2], py[2], col);
        display_triangle_fill(px[0], py[0], px[2], py[2], px[3], py[3], col);
    }
    /* D-pad on the left, two buttons on the right */
    static const int8_t dots[3][2] = {{-29, 2}, {28, -3}, {31, 6}};
    uint16_t dc = spr_mono ? 0x0000 : SPR_RGB(40, 44, 70);
    for (int i = 0; i < 3; i++) {
        int x = dots[i][0], y = dots[i][1];
        int qx = cx + (x * c - y * sn) / 127, qy = cy + (x * sn + y * c) / 127;
        if (i == 0) {
            display_rect(qx - 4, qy - 1, 9, 3, dc);
            display_rect(qx - 1, qy - 4, 3, 9, dc);
        } else {
            display_circle_fill(qx, qy, 2, dc);
        }
    }
}

/* Learn which accelerometer axis runs across the screen, and which way:
 * hold level, press A, tilt right.  Returns 1 when set up. */
static int calibrate_wizard(void)
{
    int n0x = 0, n0y = 0, hold = 0, ax = 0, ay = 0, saved_cal = ctl_cal;
    uint32_t anim = 0;
    calib_step = 1;
    g_flush_input();
    for (;;) {
        uint32_t k = g_key();
        anim++;
        int have = read_axes(&ax, &ay);
        if (k == K_B || k == K_HOME) {
            if (calib_step == 3) { /* redo */
                calib_step = 1;
                continue;
            }
            ctl_cal = 1; /* skipped on purpose: don't ask again (Options can) */
            if (!saved_cal) save_ctl();
            calib_step = 0;
            g_flush_input();
            return 0;
        }
        if (calib_step == 1 && k == K_A && have) {
            /* average the level pose */
            int sx = 0, sy = 0, n = 0;
            for (int i = 0; i < 8; i++) {
                int x, y;
                if (read_axes(&x, &y)) {
                    sx += x;
                    sy += y;
                    n++;
                }
                delay(10000);
            }
            if (n) {
                n0x = sx / n;
                n0y = sy / n;
                hold = 0;
                calib_step = 2;
            }
        } else if (calib_step == 2 && have) {
            int dx = ax - n0x, dy = ay - n0y;
            int big = iabs(dx) > iabs(dy) ? dx : dy, small = iabs(dx) > iabs(dy) ? dy : dx;
            if (iabs(big) > 2200 && iabs(big) > 2 * iabs(small)) hold++;
            else hold = 0;
            if (hold >= 6) { /* steady for ~0.2 s */
                ctl_axis = iabs(dx) > iabs(dy) ? 0 : 1;
                ctl_invert = big < 0;
                tilt_zero = (ctl_axis ? n0y : n0x) * (ctl_invert ? -1 : 1);
                tilt_zero = iclamp(tilt_zero, -4000, 4000);
                ctl_cal = 1;
                ctl_tilt = 1;
                save_ctl();
                g_save_int("skyhop/tz", tilt_zero);
                calib_step = 3;
            }
        } else if (calib_step == 3 && k == K_A) {
            calib_step = 0;
            g_flush_input();
            return 1;
        }

        draw_sky(0, SW);
        int w = imin(SW - 24, 320), x = (SW - w) / 2, y = 22, h = 196;
        spr_panel(x, y, w, h, "TILT SET-UP");
        int cx = SW / 2;
        if (!have) {
            g_text_c(cx, y + 50, "No motion sensor found.", SPRT.red);
            g_text_c(cx, y + 66, "Steer with LEFT / RIGHT.", C_TEXT);
            g_text_c(cx, y + h - 20, "B: continue", C_DIM);
        } else if (calib_step == 1) {
            g_text_c(cx, y + 38, "Hold the console the way you", C_TEXT);
            g_text_c(cx, y + 50, "play, level, and press A.", C_TEXT);
            draw_console_icon(cx, y + 110, 0, spr_col(120, 130, 170), spr_mono ? 0x0000 : SPR_RGB(140, 220, 255));
            g_text_c(cx, y + h - 20, "A: ready   B: skip", C_DIM);
        } else if (calib_step == 2) {
            g_text_c(cx, y + 38, "Now tilt it to the RIGHT", C_GOLD);
            g_text_c(cx, y + 50, "and hold it there.", C_TEXT);
            int a = isin((int)anim * 6) * 12 / 127;
            if (a < 0) a = -a;
            draw_console_icon(cx, y + 110, a, spr_col(120, 130, 170), spr_mono ? 0x0000 : SPR_RGB(140, 220, 255));
            display_triangle_fill(cx + 60, y + 100, cx + 60, y + 120, cx + 74, y + 110, C_GOLD);
            g_text_c(cx, y + h - 20, "B: skip", C_DIM);
        } else {
            g_text_c(cx, y + 38, "Got it! Tilt left and right", SPRT.green);
            g_text_c(cx, y + 50, "to try it out.", C_TEXT);
            int v = tilt_value();
            draw_tilt_rail(x + 24, w - 48, y + 118, v);
            g_text_c(cx, y + h - 20, "A: done   B: redo", C_DIM);
        }
        display_flush();
        delay(30000);
    }
}

static void tilt_test(void)
{
    g_flush_input();
    for (;;) {
        uint32_t k = g_key();
        if (k == K_B || k == K_HOME) break;
        if (k == K_A) tilt_calibrate();
        int raw = tilt_raw();
        int v = tilt_value();
        draw_sky(0, SW);
        int w = SW - 40, x = 20, y = 40;
        spr_panel(x, y, w, 150, "TILT TEST");
        char b[40];
        if (!imu_ok) {
            g_text_c(SW / 2, y + 44, "No motion sensor found.", SPRT.red);
            g_text_c(SW / 2, y + 58, "Use the D-pad to steer.", C_TEXT);
        } else {
            b[0] = 0;
            g_cat(b, ctl_axis ? "axis Y" : "axis X");
            g_cat(b, ctl_invert ? "-  raw " : "+  raw ");
            g_cat_int(b, raw);
            g_text_c(SW / 2, y + 40, b, C_DIM);
            draw_tilt_rail(x + 20, w - 40, y + 80, v);
            g_text_c(SW / 2, y + 98, "Tilt right: Hopbot goes right.", C_TEXT);
            g_text_c(SW / 2, y + 110, "Backwards? Use Calibrate tilt.", C_DIM);
        }
        g_text_c(SW / 2, y + 132, "Hold level, A: re-centre  B: back", C_DIM);
        display_flush();
        delay(30000);
    }
}

static void options_menu(void)
{
    int sel = 0;
    for (;;) {
        char i0[24], i1[24];
        i0[0] = i1[0] = 0;
        g_cat(i0, ctl_tilt ? "Steer: tilt" : "Steer: D-pad");
        g_cat(i1, "Sensitivity: ");
        g_cat_int(i1, ctl_sens);
        const char *it[] = {i0, i1, "Calibrate tilt", "Tilt test", "Back"};
        render();
        int r = g_menu("OPTIONS", it, 5, sel, 1);
        if (r < 0) break;
        int lr = r & (G_MENU_LEFT | G_MENU_RIGHT);
        int dirv = (r & G_MENU_LEFT) ? -1 : 1;
        sel = r & 0xFF;
        if (sel == 0) ctl_tilt ^= 1;
        else if (sel == 1) ctl_sens = (ctl_sens - 1 + dirv + 5) % 5 + 1;
        else if (sel == 2) {
            if (!lr) calibrate_wizard();
        } else if (sel == 3) {
            if (!lr) tilt_test();
        } else if (!lr) break;
        save_ctl();
    }
    save_ctl();
    g_flush_input();
}

/* 0 resume, 1 restart, 2 quit to title */
static int pause_menu(void)
{
    static const char *const it[] = {"Resume", "Restart", "Options", "Quit to title"};
    int sel = 0;
    for (;;) {
        render();
        int r = g_menu("PAUSED", it, 4, sel, 0);
        g_flush_input();
        if (r < 0 || r == 0) break;
        if (r == 1) return 1;
        if (r == 3) return 2;
        if (r == 2) {
            options_menu();
            sel = 2;
        }
    }
    return 0;
}

/* 1 retry, 0 title */
static int game_over(void)
{
    runs++;
    g_save_int("skyhop/runs", runs);
    if (score > best) {
        best = score;
        new_best = 1;
        g_save_int("skyhop/best", best);
    }
    g_flush_input();
    uint32_t t0 = g_now();
    for (;;) {
        render();
        int w = PW - 16, h = 132, x = PX0 + 8, y = (SH - h) / 2;
        spr_panel(x, y, w, h, "FALLEN");
        char b[32];
        b[0] = 0;
        g_cat(b, "Score ");
        g_cat_int(b, score);
        display_text_large(PX0 + PW / 2 - g_tw_large(b) / 2, y + 36, b, C_GOLD);
        if (new_best) {
            if ((g_now() / 300) & 1) g_text_c(PX0 + PW / 2, y + 60, "NEW BEST!", SPRT.green);
        } else {
            b[0] = 0;
            g_cat(b, "Best ");
            g_cat_int(b, best);
            g_text_c(PX0 + PW / 2, y + 60, b, C_TEXT);
        }
        b[0] = 0;
        g_cat(b, "Reached ");
        g_cat(b, ZONES[zone_of(max_h)].name);
        g_text_c(PX0 + PW / 2, y + 76, b, C_DIM);
        b[0] = 0;
        g_cat_int(b, kills);
        g_cat(b, kills == 1 ? " critter zapped" : " critters zapped");
        g_text_c(PX0 + PW / 2, y + 90, b, C_DIM);
        g_text_c(PX0 + PW / 2, y + h - 18, "A: again   B: menu", C_TEXT);
        display_flush();
        uint32_t k = g_key();
        if (g_now() - t0 > 400) {
            if (k == K_A) return 1;
            if (k == K_B || k == K_HOME) return 0;
        }
        delay(40000);
    }
}

/* 1 play again, 0 back to title */
static int play(void)
{
    new_game();
    title_mode = 0;
    tilt_v = 0;
    g_flush_input();
    uint32_t last = g_now(), fps_t = last;
    int acc = 0, frames = 0;
    for (;;) {
        uint32_t now = g_now();
        int dt = (int)(now - last);
        last = now;
        if (dt > 100) dt = 100;
        if (dt < 0) dt = 0;
        acc += dt * 3;
        poll_input();
        if (want_pause) {
            want_pause = 0;
            int r = pause_menu();
            if (r == 1) return 1;
            if (r == 2) return 0;
            last = g_now();
            acc = 0;
            continue;
        }
        int n = 0;
        while (acc >= STEP3 && n < 4) {
            step();
            acc -= STEP3;
            n++;
            if (over) break;
        }
        if (acc >= STEP3) acc = 0;
        render();
        display_flush();
        frames++;
        if (now - fps_t >= 1000) {
            fps = frames * 1000 / (int)(now - fps_t);
            frames = 0;
            fps_t = now;
        }
        if (over) return game_over();
        uint32_t spent = g_now() - now;
        delay(spent < 15 ? (15 - spent) * 1000 : 1000);
    }
}

static const char HOWTO[] =
    "Hopbot bounces off every ledge it\n"
    "lands on. Climb as high as you can.\n"
    "\n"
    "Tilt the console     steer\n"
    "  (LEFT/RIGHT work too)\n"
    "A or UP              zap upward\n"
    "B or HOME            pause\n"
    "\n"
    "The sides wrap around.\n"
    "Puffs vanish after one hop,\n"
    "cracked ledges crumble under you.\n"
    "Stomp critters; zap spike mines.\n"
    "Balloons and rockets lift you,\n"
    "a bubble shield takes one hit.";

/* Title: Hopbot bounces on a ledge while the menu waits.  -1 quits. */
static int title_screen(void)
{
    static const char *const items_txt[] = {"Play", "Options", "How to play", "Quit"};
    int sel = 0;
    g_flush_input();
    for (;;) {
        /* A little attract scene: bounce on the spot. */
        title_mode = 1;
        new_game();
        for (int i = 0; i < MAXP; i++) /* only the lawn: no invisible ledges */
            if (plats[i].type != PT_GROUND) plats[i].type = PT_NONE;
        tilt_v = 0;
        uint32_t last = g_now();
        int acc = 0;
        for (;;) {
            uint32_t now = g_now();
            int dt = (int)(now - last);
            last = now;
            acc += iclamp(dt, 0, 100) * 3;
            while (acc >= STEP3) {
                acc -= STEP3;
                ctl_dir = 0;
                /* drift towards the middle ledge */
                int c = hx / FP + HERO_W / 2;
                if (c < PW / 2 - 30) ctl_dir = 1;
                else if (c > PW / 2 + 30) ctl_dir = -1;
                steps++;
                hero_step();
                if (hy / FP > 120) { /* keep it low on the title */
                    hy = 0;
                    hvy = JUMP_V;
                }
                for (int i = 0; i < MAXPART; i++) {
                    part_t *p = &parts[i];
                    if (!p->life) continue;
                    p->life--;
                    p->x += p->vx;
                    p->y += p->vy;
                    p->vy = (int16_t)(p->vy - 20);
                }
            }
            /* scene */
            int save_px0 = PX0;
            draw_sky(0, SW);
            PX0 = 52 - PW / 2; /* column centre at x = 52 */
            {
                int hxp = hx / FP, hyp = sy_of(hy / FP) - HERO_H;
                int f = crouch > 3 ? HF_CROUCH : HF_STAND;
                /* twice the size, feet on the lawn */
                spr_blit_scaled(PX0 + hxp - HERO_W / 2, hyp - HERO_H, spr_hero[f][face], HERO_W, HERO_H, 2, scratch2x);
                for (int i = 0; i < MAXPART; i++) {
                    part_t *p = &parts[i];
                    if (p->life) display_rect(PX0 + p->x / FP, sy_of(p->y / FP), p->sz, p->sz, p->col);
                }
            }
            PX0 = save_px0;

            /* logo */
            const char *logo = "SKYHOP";
            int lw = 6 * 17 - 1, lx = SW / 2 - lw / 2, ly = 18;
            uint16_t sh = spr_mono ? 0x0000 : SPR_RGB(20, 40, 100);
            display_text_huge(lx + 2, ly + 3, logo, sh);
            display_text_huge(lx, ly, logo, spr_mono ? 0xFFFF : SPR_RGB(255, 214, 80));
            g_text_c(SW / 2, ly + 34, "how high can Hopbot go?", spr_mono ? 0xFFFF : SPR_RGB(30, 60, 120));

            /* menu */
            int mw = 150, mh = 4 * 20 + 12, mx = SW / 2 - mw / 2, my = 76;
            g_panel(mx, my, mw, mh);
            for (int i = 0; i < 4; i++) {
                int iy = my + 6 + i * 20;
                if (i == sel) display_rounded_rect_fill(mx + 6, iy, mw - 12, 18, 4, g_theme.sel_bg);
                g_text_c(SW / 2, iy + 4, items_txt[i], i == sel ? g_theme.sel_text : g_theme.text);
            }
            if (best > 0) {
                char b[24];
                b[0] = 0;
                g_cat(b, "Best ");
                g_cat_int(b, best);
                int bw = g_tw(b) + 12;
                display_rounded_rect_fill(SW - bw - 6, 6, bw, 16, 4, spr_mono ? 0x0000 : SPR_RGB(18, 26, 60));
                display_text(SW - bw, 9, b, C_GOLD);
            }
            {
                const char *hint = ctl_tilt ? "tilt to steer  A: zap  B: pause" : "LEFT/RIGHT steer  A: zap  B: pause";
                spr_text_out(SW / 2 - g_tw(hint) / 2, SH - 16, hint, 0xFFFF);
            }
            display_flush();

            uint32_t k = g_key();
            if (k == K_UP) sel = (sel + 3) % 4;
            else if (k == K_DOWN) sel = (sel + 1) % 4;
            else if (k == K_HOME || k == K_B) return -1;
            else if (k == K_A) {
                title_mode = 0;
                if (sel == 0) {
                    if (ctl_tilt && !ctl_cal) calibrate_wizard();
                    return 0;
                }
                if (sel == 1) {
                    options_menu();
                    break;
                }
                if (sel == 2) {
                    g_message("HOW TO PLAY", HOWTO);
                    g_flush_input();
                    break;
                }
                if (sel == 3) return -1;
            }
            delay(16000);
        }
    }
}

int main(void)
{
    printf("AkiraOS Skyhop");
    g_init();
    SW = G_W;
    SH = G_H;
    PX0 = (SW - PW) / 2;
    spr_init(SW >= 400);
    build_sprites();
    load_settings();
    g_seed_random();
    for (;;) {
        if (title_screen() < 0) break;
        while (play()) {
        }
        title_mode = 0;
    }
    app_switch("supervisor");
    return 0;
}
