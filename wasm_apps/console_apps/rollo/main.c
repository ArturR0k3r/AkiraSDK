/**
 * @file main.c
 * @brief Rollo — a rubber-ball platformer for AkiraOS
 *
 * Roll, jump and bounce Rollo through ten hand-built levels.  Light every
 * spark to open the gate; spikes, pits and urchins pop Rollo back to the
 * last beacon.  Pumps puff Rollo up into a big ball that floats on water but
 * no longer squeezes through narrow gaps; pins let the air back out.
 *
 * Controls:
 *   LEFT / RIGHT   roll (with momentum)      A / UP    jump (hold: higher)
 *   B / HOME       pause menu
 *
 * Physics run at a fixed 60 Hz in 1/256-pixel fixed point, decoupled from
 * the frame rate.  Each frame is drawn from pre-built RGB565 tile and sprite
 * caches (solid runs go out four tiles per blit) and presented with one
 * display_flush().  The 400x240 1-bit Sharp panel gets the same game with a
 * wider view: every palette entry carries a black/white value.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 * SPDX-License-Identifier: Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"
#include "../../common/akira_spr.h"

static int32_t SW = 320, SH = 240;

static inline int iabs(int v) { return v < 0 ? -v : v; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ── Levels ──────────────────────────────────────────────────────────────
 * One character per 16x16 tile:
 *   #  brick           %  stone block      ~  water
 *   ^  floor spikes    v  ceiling spikes   >  <  wall spikes (pointing)
 *   S  spring          +  pump (big)       -  pin (small)
 *   o  spark           C  beacon           E  gate (two tiles tall)
 *   H  urchin, patrols left/right          V  urchin, patrols up/down
 *   x  urchin stopper (invisible)          ?  sign (hints[] in order)
 *   P  start
 * Objects placed in water get water as their base tile. */
typedef struct {
    const char *name;
    const char *const *rows; /* NULL-terminated */
    const char *const *hints;
    uint8_t zone;            /* 0 meadow, 1 grotto, 2 dusk fort */
} level_t;

/* LEVELS-BEGIN */
static const char *const L1_ROWS[] = {
    "                                                                            ",
    "                                                                            ",
    "                                                                            ",
    "                                                                            ",
    "                                                                            ",
    "                                                                            ",
    "                                                                            ",
    "                                         o                     o            ",
    "                                oo      ###           o       ##            ",
    "                     o  o      %%%%     ###     o    %%%    ####            ",
    "                  o ##               ######               ######            ",
    "  P ?   o  o  ?  #####      ?        ###### ?             ######       E    ",
    "########################  #####################   ##########################",
    "########################  #####################   ##########################",
    "########################  #####################   ##########################",
    0,
};
static const char *const L1_HINTS[] = {
    "Hold LEFT or RIGHT to roll",
    "Press A or UP to jump",
    "Hold A longer to jump higher",
    "Light every spark to open the gate",
    0,
};
static const char *const L2_ROWS[] = {
    "                                                                                                ",
    "                                                                                                ",
    "                                                                                                ",
    "                                                                                                ",
    "                                                                                                ",
    "                                                                                                ",
    "                                                                                                ",
    "                                                 ############                                   ",
    "         o     o                                 vvvvvvvvvvvv              o                    ",
    "                         o   o ^ o     o                               o  ##   o                ",
    "                             #####                                 o  ##  ##                    ",
    "  P ?    ^    ^^   C ?       #####           C ?    ^ o ^ o    C  ##^^##^^##               E    ",
    "########################   ##########     ####################################    ##############",
    "########################   ##########^^^^^####################################    ##############",
    "########################   ###################################################    ##############",
    0,
};
static const char *const L2_HINTS[] = {
    "Spikes pop you. Jump over them!",
    "Beacons save your spot",
    "Low ceiling: hold A only briefly",
    0,
};
static const char *const L3_ROWS[] = {
    "                                                                                          ",
    "                                                                                          ",
    "                                                    oo                                    ",
    "                                                   %%%%                                   ",
    "                                                                                          ",
    "                                                                                          ",
    "            o         o o                                          o                      ",
    "                     %%%%%                                         ##                     ",
    "           ##                                   S                  ##                     ",
    "           ##                     o            %%%                 ##                     ",
    "           ##                                                      ##                     ",
    "           ##                                                      ##        o            ",
    "           ##                                                      ##       %%%           ",
    "           ##                                                      ##                     ",
    "  P ?   S  ##   o? S        S            C   S            o  o  S  ##    o          E     ",
    "##############################        ####################################################",
    "##############################^^^^^^^^####################################################",
    "##########################################################################################",
    0,
};
static const char *const L3_HINTS[] = {
    "Springs fling you sky-high",
    "Steer in the air to reach ledges",
    0,
};
static const char *const L4_ROWS[] = {
    "                                                                                                    ",
    "                                                                                                    ",
    "                                                                                                    ",
    "                                                                                                    ",
    "                                                                                         o          ",
    "                                                                                        %%%%        ",
    "                                                                                                    ",
    "                                                                                                    ",
    "                                                          o                                         ",
    "                                                          %%                    o                   ",
    "                         o    o    o                o     %%     o             ###                  ",
    "  P ?         ?  +                         C              %%               o   ###   S         E    ",
    "########~~o~~########~~~~~~~~~~~~~~~~~~~########~~~~~~~~~~~~~~~~~~~~~~##############################",
    "#####################^^^^^^^^^^^^^^^^^^^########~~~~~~~~~~~~~~~~~~~~~~##############################",
    "################################################~~~~~~~~~~~~~~~~~~~~~~##############################",
    0,
};
static const char *const L4_HINTS[] = {
    "Small balls sink in water",
    "Pumps puff you up. Big balls float!",
    0,
};
static const char *const L5_ROWS[] = {
    "########################################################################################################",
    "########################################################################################################",
    "                              ###     ##########             ###              ####                      ",
    "                              ###     ##########             ###              ####                      ",
    "                              ###     ##########             ###              ####                      ",
    "                              ###     ##########             ###              ####                      ",
    "                              ###     ##########             ###              ####                      ",
    "                              ###     ##########             ###              ####                      ",
    "                              ###     ##########             ###              ####                      ",
    "                              ###     ##########             ###              ####       o              ",
    "              o     o         ###                        o         o          ####      ###             ",
    "  P ?  +                 ? -   o   C      o       +           o            -    o   C   ###    o    E   ",
    "###########~~~~~~~~~~~~~~############################~~~~~~~~~~~~~~~~~~~################################",
    "###########^^^^^^^^^^^^^^############################~~~~~~~~~~~~~~~~~~~################################",
    "#####################################################~~~~~~~~~~~~~~~~~~~################################",
    0,
};
static const char *const L5_HINTS[] = {
    "Big balls float but can't fit gaps",
    "Pins let the air out",
    0,
};
static const char *const L6_ROWS[] = {
    "                                                                                                    ",
    "                                                                                                    ",
    "                                                                                                    ",
    "                                                                                                    ",
    "                                                         x   x                                      ",
    "                        x                                                                           ",
    "                                                             V                                      ",
    "                                                         V                                          ",
    "              o         V         #################        o                     o                  ",
    "                                               o       o   %%  o                %%%                 ",
    "                                          o            %%      %%                                   ",
    "  P ?   o x   H   x           C    x  H   x  H    x                    C x  o  H     x   o     E    ",
    "#######################   ############################              ################################",
    "#######################   ############################              ################################",
    "#######################   ############################              ################################",
    0,
};
static const char *const L6_HINTS[] = {
    "Urchins patrol. Time your jumps",
    0,
};
static const char *const L7_ROWS[] = {
    "#                        #",
    "#                        #",
    "#                        #",
    "#      o  E              #",
    "#    %%%%%%%%%%%%        #",
    "#                        #",
    "#                 o      #",
    "#                %%%%    #",
    "#                        #",
    "#    x  H   o  x        <#",
    "#          %%%          <#",
    "#                       <#",
    "#    o                   #",
    "#   %%%%%                #",
    "#                        #",
    "#>           o           #",
    "#>          %%%%         #",
    "#>                       #",
    "#                    o   #",
    "#                  %%%%% #",
    "#                        #",
    "#              o         #",
    "#            %%%%        #",
    "#                        #",
    "#       o              o #",
    "#      %%%%              #",
    "#                        #",
    "#  P ?                S  #",
    "##########################",
    "##########################",
    0,
};
static const char *const L7_HINTS[] = {
    "Climb! The gate is at the top",
    0,
};
static const char *const L8_ROWS[] = {
    "                                                                                                              ",
    "                                                                                                              ",
    "                                                                                                              ",
    "                                  ###                  ##                                                     ",
    "                                  ###                  ##                                                     ",
    "                                  ###      o+          ##                                            o        ",
    "                                  ###     %%%%         ##                                           %%%%      ",
    "            x      H      x       ###                  ##                                                     ",
    "                o                 ###                  ##                                                     ",
    "                %%                ###                  ##                ########         o                   ",
    "                %%                ###                  ##                vvvvvvvv        %%%                  ",
    "             o  %%   o            ###              o   o    o                                                 ",
    "  P ?  +        %%          C  -   o   S                            C -     ^ o     x   H    x    S       E   ",
    "##########~~~~~~~~~~~~~~~~######################~~~~~~~~~~~~~~~~~~############################################",
    "##########~~~~~~~~~~~~~~~~######################~~~~~~~~~~~~~~~~~~############################################",
    "##########~~~~~~~~~~~~~~~~######################~~~~~~~~~~~~~~~~~~############################################",
    0,
};
static const char *const L8_HINTS[] = {
    "Float, shrink, squeeze through!",
    0,
};
static const char *const L9_ROWS[] = {
    "                                                                                                        ",
    "                                                                                                        ",
    "                                                                                                        ",
    "                                  o                                                                     ",
    "                                 %%%                              %%%%                                  ",
    "                                                                  vvvv                                  ",
    "                                                  x  H    x                                             ",
    "                                                                    o                 o                 ",
    "                        o              C  ^                       %%%%               %%                 ",
    "                       %%%             %%%%%%    o           o            C     %%        %%            ",
    "            o    %%%                            %%%   %%%   %%%          %%%%                           ",
    "  P ?      %%%                S                                                                 o  E    ",
    "########                     %%%%                                                              #########",
    "########                                                                                       #########",
    "########                                                                                       #########",
    0,
};
static const char *const L9_HINTS[] = {
    "Watch your step up here",
    0,
};
static const char *const L10_ROWS[] = {
    "                                                                                                                            ",
    "                                                                                                                            ",
    "                                                                                                                            ",
    "                   oo                                      ###                                                              ",
    "                 %%%%%%                                    ###                        x           o                         ",
    "                                                           ###                    x                                         ",
    "                                                           ###                        V             ##                      ",
    "                                   x         H    x        ###                    V                 ##                      ",
    "                                        o                  ###                                      ##                      ",
    "          o                             %%                 ###  ##########          o               ##        o             ",
    "                                        %%                 ###  vvvvvvvvvv          %%              ##                      ",
    "                                    o   %%    o            ###                  %%      %%          ##                      ",
    "  P ?   ^  ^   S           C +          %%            C -   o      ^ o ^    C                S      ##  x   H    x     E    ",
    "#################        #######~~~~~~~~~~~~~~~~~~~~###########################            #################################",
    "#################^^^^^^^^#######~~~~~~~~~~~~~~~~~~~~###########################            #################################",
    "################################~~~~~~~~~~~~~~~~~~~~###########################            #################################",
    0,
};
static const char *const L10_HINTS[] = {
    "Last one. Everything you learned!",
    0,
};
static const level_t LV[] = {
    {"Roll Out", L1_ROWS, L1_HINTS, 0},
    {"Prickle Path", L2_ROWS, L2_HINTS, 0},
    {"Spring Hollow", L3_ROWS, L3_HINTS, 0},
    {"Puffer Pond", L4_ROWS, L4_HINTS, 1},
    {"Pinhole Caves", L5_ROWS, L5_HINTS, 1},
    {"Urchin Run", L6_ROWS, L6_HINTS, 1},
    {"The Well", L7_ROWS, L7_HINTS, 2},
    {"Tidal Fort", L8_ROWS, L8_HINTS, 2},
    {"Sky Steps", L9_ROWS, L9_HINTS, 2},
    {"Rollo's Gauntlet", L10_ROWS, L10_HINTS, 2},
};
/* LEVELS-END */

#define NLV ((int)(sizeof(LV) / sizeof(LV[0])))

/* ── Geometry and physics (1/256 px, 60 Hz steps) ────────────────────── */
#define TS        16
#define FP        256
#define TFP       (TS * FP)
#define MAXW      128
#define MAXH      32
#define HUD_H     16

#define R_SMALL   7
#define R_BIG     14
#define GRAV      82    /* 0.32 px/step^2                 */
#define MAXFALL   (8 * FP)
#define JUMP_V    1536  /* 6.0 px/step: rises ~3.5 tiles  */
#define JUMP_BIG  1690  /* 6.6                            */
#define SPRING_V  2510  /* 9.8: ~9 tiles                  */
#define JUMP_CUT  512   /* release early: cap the rise    */
#define ACC_GND   46
#define ACC_AIR   30
#define FRICT     26
#define MAXVX     666   /* 2.6 px/step                    */
#define MAXVX_BIG 576
#define REBOUND_V 1152  /* landing faster than this bounces */
#define W_GRAV    26    /* small ball sinks slowly        */
#define W_SINK    300
#define W_JUMP    830
#define W_BUOY    77    /* big ball floats up             */
#define W_DRAG    240   /* x/256 per step                 */
#define W_MAXVX   400
#define COYOTE    6
#define JBUF      6
#define URCHIN_V  256   /* 1 px/step                      */

typedef struct {
    int32_t x, y, vx, vy; /* centre, velocity */
    uint8_t big, ground, coyote, jbuf, jumping, wet;
} ball_t;

enum { EV_DIE = 1, EV_SPRING = 2, EV_GROW = 4, EV_SHRINK = 8, EV_LAND = 16,
       EV_SPLASH = 32, EV_JUMP = 64, EV_BUMP = 128 };

static uint8_t map[MAXH][MAXW];
static uint8_t objm[MAXH][MAXW];
static int mw, mh;

#define MAX_SPARK 48
#define MAX_CP    12
#define MAX_SIGN  8
#define MAX_HAZ   16
typedef struct { int16_t tx, ty; uint8_t got; } spark_t;
typedef struct { int16_t tx, ty; uint8_t on; } cp_t;
typedef struct { int16_t tx, ty; } sign_t;
typedef struct { int32_t x, y; int16_t vx, vy; } haz_t;
static spark_t sparks[MAX_SPARK];
static cp_t cps[MAX_CP];
static sign_t signs[MAX_SIGN];
static haz_t haz[MAX_HAZ];
static int n_spark, got_spark, n_cp, n_sign, n_haz;
static int start_tx, start_ty, exit_tx, exit_ty, zone;
static int spring_tx = -1, spring_ty, spring_t;

static inline int tile(int tx, int ty)
{
    if (tx < 0 || tx >= mw || ty < 0) return '#';
    if (ty >= mh) return ' ';
    return map[ty][tx];
}
static inline int solid(int t) { return t == '#' || t == '%'; }

static int is_water_obj(int x, int y)
{
    int w = 0;
    if (y > 0 && map[y - 1][x] == '~') w++;
    if (y + 1 < mh && map[y + 1][x] == '~') w++;
    if (x > 0 && map[y][x - 1] == '~') w++;
    if (x + 1 < mw && map[y][x + 1] == '~') w++;
    return w >= 2;
}

static void load_level(int n)
{
    const level_t *L = &LV[n];
    mh = mw = 0;
    while (mh < MAXH && L->rows[mh]) {
        int len = strlen(L->rows[mh]);
        if (len > mw) mw = len;
        mh++;
    }
    if (mw > MAXW) mw = MAXW;
    n_spark = got_spark = n_cp = n_sign = n_haz = 0;
    start_tx = 1;
    start_ty = 1;
    exit_tx = -1;
    exit_ty = 0;
    zone = L->zone;
    spring_tx = -1;
    for (int y = 0; y < mh; y++) {
        const char *row = L->rows[y];
        int len = strlen(row);
        for (int x = 0; x < mw; x++) {
            int c = x < len ? row[x] : ' ', base = c, ob = 1;
            switch (c) {
            case 'P':
                start_tx = x;
                start_ty = y;
                base = ' ';
                break;
            case 'o':
                if (n_spark < MAX_SPARK) {
                    sparks[n_spark].tx = (int16_t)x;
                    sparks[n_spark].ty = (int16_t)y;
                    sparks[n_spark++].got = 0;
                }
                base = ' ';
                break;
            case 'C':
                if (n_cp < MAX_CP) {
                    cps[n_cp].tx = (int16_t)x;
                    cps[n_cp].ty = (int16_t)y;
                    cps[n_cp++].on = 0;
                }
                base = ' ';
                break;
            case '?':
                if (n_sign < MAX_SIGN) {
                    signs[n_sign].tx = (int16_t)x;
                    signs[n_sign++].ty = (int16_t)y;
                }
                base = ' ';
                break;
            case 'H':
            case 'V':
                if (n_haz < MAX_HAZ) {
                    haz_t *h = &haz[n_haz++];
                    h->x = (x * TS + 8) * FP;
                    h->y = (y * TS + 8) * FP;
                    h->vx = c == 'H' ? URCHIN_V : 0;
                    h->vy = c == 'V' ? URCHIN_V : 0;
                }
                base = ' ';
                break;
            case 'E':
                exit_tx = x;
                exit_ty = y;
                ob = 0;
                break;
            case '#': case '%': case '~': case '^': case 'v': case '<': case '>':
            case 'S': case '+': case '-': case 'x':
                ob = 0;
                break;
            default:
                base = ' ';
                ob = 0;
                break;
            }
            map[y][x] = (uint8_t)base;
            objm[y][x] = (uint8_t)ob;
        }
    }
    for (int pass = 0; pass < 2; pass++)
        for (int y = 0; y < mh; y++)
            for (int x = 0; x < mw; x++)
                if (objm[y][x] && map[y][x] == ' ' && is_water_obj(x, y)) map[y][x] = '~';
}

/* ── Physics ─────────────────────────────────────────────────────────── */
static inline int ball_r(const ball_t *b) { return b->big ? R_BIG : R_SMALL; }

static int box_solid(int32_t l, int32_t t, int32_t r, int32_t b)
{
    int tx0 = l >> 12, tx1 = (r - 1) >> 12, ty0 = t >> 12, ty1 = (b - 1) >> 12;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            if (solid(tile(tx, ty))) return 1;
    return 0;
}

/** Does the ball's box (half-size @p r px) overlap the pixel rect? */
static int ball_hits(const ball_t *b, int r, int x, int y, int w, int h)
{
    int bx = b->x >> 8, by = b->y >> 8;
    return bx - r < x + w && bx + r > x && by - r < y + h && by + r > y;
}

/** Change size keeping the bottom where it is; nudge sideways if needed. */
static int try_resize(ball_t *b, int big)
{
    static const int8_t nudge[] = {0, -4, 4, -8, 8, -12, 12, -16, 16};
    int r = big ? R_BIG : R_SMALL;
    int32_t ny = b->y + ball_r(b) * FP - r * FP;
    for (unsigned i = 0; i < sizeof(nudge); i++) {
        int32_t nx = b->x + nudge[i] * FP;
        if (!box_solid(nx - r * FP, ny - r * FP, nx + r * FP, ny + r * FP)) {
            b->x = nx;
            b->y = ny;
            b->big = (uint8_t)big;
            return 1;
        }
    }
    return 0;
}

/**
 * Advance the ball one 60 Hz step.  Reads only the map; returns EV_* bits.
 * @param dir     -1 / 0 / 1 from the D-pad
 * @param jpress  jump pressed since the last step
 * @param jheld   jump button held now
 */
static int phys_step(ball_t *b, int dir, int jpress, int jheld)
{
    int ev = 0, r = ball_r(b);
    int cx = b->x >> 8, cy = b->y >> 8;
    int wet = tile(cx >> 4, cy >> 4) == '~';
    if (wet && !b->wet) ev |= EV_SPLASH;
    b->wet = (uint8_t)wet;
    int floating = b->big && (wet || tile(cx >> 4, (cy + 6) >> 4) == '~');

    /* Rolling. */
    int maxv = (wet && !b->big) ? W_MAXVX : (b->big ? MAXVX_BIG : MAXVX);
    if (dir) {
        int a = b->ground ? ACC_GND : ACC_AIR;
        if (dir * b->vx < 0) a += FRICT;
        b->vx += dir * a;
    } else {
        int f = b->ground ? FRICT : 6;
        if (b->vx > f) b->vx -= f;
        else if (b->vx < -f) b->vx += f;
        else b->vx = 0;
    }
    if (b->vx > maxv) b->vx = imax(maxv, b->vx - 40);
    if (b->vx < -maxv) b->vx = imin(-maxv, b->vx + 40);

    /* Gravity, buoyancy. */
    if (wet) {
        b->vy += b->big ? -W_BUOY : W_GRAV;
        b->vy = b->vy * W_DRAG / 256;
        if (!b->big && b->vy > W_SINK) b->vy = W_SINK + (b->vy - W_SINK) * 3 / 4;
    } else {
        b->vy += GRAV;
        if (b->vy > MAXFALL) b->vy = MAXFALL;
    }

    /* Jump: buffered presses, coyote time, release cuts the rise. */
    if (jpress) b->jbuf = JBUF;
    else if (b->jbuf) b->jbuf--;
    if (b->ground || floating) b->coyote = COYOTE;
    else if (b->coyote) b->coyote--;
    if (b->jbuf && b->coyote) {
        b->vy = -(b->big ? JUMP_BIG : (wet ? W_JUMP : JUMP_V));
        b->jbuf = b->coyote = 0;
        b->jumping = 1;
        b->ground = 0;
        ev |= EV_JUMP;
    }
    if (b->jumping && !jheld && b->vy < -JUMP_CUT) b->vy = -JUMP_CUT;
    if (b->vy >= 0) b->jumping = 0;

    /* Move X, then Y; speeds stay under one tile per step. */
    b->x += b->vx;
    int32_t l = b->x - r * FP, rt = b->x + r * FP, t = b->y - r * FP, bt = b->y + r * FP;
    int col = -9999;
    if (b->vx > 0) col = (rt - 1) >> 12;
    else if (b->vx < 0) col = l >> 12;
    if (col != -9999) {
        int hit = 0;
        for (int ty = t >> 12; ty <= (bt - 1) >> 12; ty++)
            if (solid(tile(col, ty))) hit = 1;
        if (hit) {
            b->x = b->vx > 0 ? col * TFP - r * FP : (col + 1) * TFP + r * FP;
            if (iabs(b->vx) > 400) {
                b->vx = -b->vx / 3;
                ev |= EV_BUMP;
            } else {
                b->vx = 0;
            }
        }
    }

    int32_t vy0 = b->vy;
    b->y += b->vy;
    l = b->x - r * FP;
    rt = b->x + r * FP;
    t = b->y - r * FP;
    bt = b->y + r * FP;
    b->ground = 0;
    if (b->vy) {
        int row = b->vy > 0 ? (bt - 1) >> 12 : t >> 12, hit = 0;
        for (int tx = l >> 12; tx <= (rt - 1) >> 12; tx++)
            if (solid(tile(tx, row))) hit = 1;
        if (hit && b->vy > 0) {
            b->y = row * TFP - r * FP;
            b->ground = 1;
            b->jumping = 0;
            if (vy0 > REBOUND_V && !wet) {
                b->vy = -vy0 * 115 / 256;
                ev |= EV_LAND;
            } else {
                if (vy0 > 700) ev |= EV_LAND;
                b->vy = 0;
            }
        } else if (hit) {
            b->y = (row + 1) * TFP + r * FP;
            b->vy = 64;
            b->jumping = 0;
            ev |= EV_BUMP;
        }
    }

    /* Tiles that act on overlap. */
    cx = b->x >> 8;
    cy = b->y >> 8;
    if (cy - r >= mh * TS) return ev | EV_DIE;
    int k = r - 2; /* forgiving hitbox for spikes */
    for (int ty = (cy - r) >> 4; ty <= (cy + r - 1) >> 4; ty++) {
        for (int tx = (cx - r) >> 4; tx <= (cx + r - 1) >> 4; tx++) {
            int c = tile(tx, ty), px = tx * TS, py = ty * TS;
            switch (c) {
            case '^': if (ball_hits(b, k, px + 2, py + 9, 12, 7)) return ev | EV_DIE; break;
            case 'v': if (ball_hits(b, k, px + 2, py, 12, 7)) return ev | EV_DIE; break;
            case '>': if (ball_hits(b, k, px, py + 2, 7, 12)) return ev | EV_DIE; break;
            case '<': if (ball_hits(b, k, px + 9, py + 2, 7, 12)) return ev | EV_DIE; break;
            case 'S':
                if (b->vy >= 0 && ball_hits(b, r, px + 2, py + 8, 12, 8)) {
                    b->vy = -SPRING_V;
                    b->jumping = 0;
                    b->coyote = b->jbuf = 0;
                    spring_tx = tx;
                    spring_ty = ty;
                    spring_t = 14;
                    return ev | EV_SPRING;
                }
                break;
            case '+':
                if (!b->big && ball_hits(b, r, px + 2, py + 10, 12, 6) && try_resize(b, 1))
                    return ev | EV_GROW;
                break;
            case '-':
                if (b->big && ball_hits(b, r, px + 2, py + 10, 12, 6) && try_resize(b, 0))
                    return ev | EV_SHRINK;
                break;
            }
        }
    }
    return ev;
}

/* ── Game state ──────────────────────────────────────────────────────── */
enum { S_PLAY, S_DYING, S_CLEAR };
static ball_t ball;
static int cur_lv, state, state_t, anim, lv_steps, deaths;
static int spawn_tx, spawn_ty, spawn_big;
static int squash_t, squash_kind, look;
static int32_t cam_x, cam_y, look_ahead;
static int sign_now = -1, banner_t, pop_x, pop_y;

#define MAX_PART 40
typedef struct { int32_t x, y, vx, vy; int16_t life; uint16_t col; } part_t;
static part_t parts[MAX_PART];

static int unlocked = 1;
static int best_cs[16], best_falls[16]; /* 0 = not cleared; falls stored +1 */

static uint16_t C_BALL, C_SPARK, C_WATER;

static void burst(int x, int y, int n, uint16_t col, int speed)
{
    for (int i = 0; i < MAX_PART && n > 0; i++) {
        part_t *p = &parts[i];
        if (p->life > 0) continue;
        int dx, dy, tries = 0;
        do { /* a disc, not a square */
            dx = (int)g_rand_n(2 * speed + 1) - speed;
            dy = (int)g_rand_n(2 * speed + 1) - speed;
        } while (dx * dx + dy * dy > speed * speed && ++tries < 4);
        p->x = x * 16;
        p->y = y * 16;
        p->vx = dx;
        p->vy = dy - speed / 3;
        p->life = (int16_t)(18 + g_rand_n(14));
        p->col = col;
        n--;
    }
}

static void place_ball(int tx, int ty, int big)
{
    ball.big = (uint8_t)big;
    int r = ball_r(&ball);
    ball.x = (tx * TS + 8) * FP;
    ball.y = ((ty + 1) * TS - r) * FP;
    ball.vx = ball.vy = 0;
    ball.ground = ball.coyote = ball.jbuf = ball.jumping = ball.wet = 0;
}

static void camera_target(int32_t *tx, int32_t *ty)
{
    int x = (ball.x >> 8) - SW / 2 + (look_ahead >> 8);
    int y = (ball.y >> 8) - SH * 11 / 20;
    x = iclamp(x, 0, imax(0, mw * TS - SW));
    y = iclamp(y, -HUD_H, imax(-HUD_H, mh * TS - SH));
    *tx = x * FP;
    *ty = y * FP;
}

static void camera_step(int snap)
{
    int want = iclamp(ball.vx * 22, -40 * FP, 40 * FP);
    look_ahead += (want - look_ahead) / 24;
    int32_t tx, ty;
    camera_target(&tx, &ty);
    if (snap) {
        cam_x = tx;
        cam_y = ty;
    } else {
        cam_x += (tx - cam_x) / 7;
        cam_y += (ty - cam_y) / 6;
    }
}

static void die(void)
{
    int r = ball_r(&ball);
    pop_x = ball.x >> 8;
    pop_y = ball.y >> 8;
    burst(pop_x, pop_y, ball.big ? 28 : 20, C_BALL, r * 5);
    burst(pop_x, pop_y, 6, 0xFFFF, r * 4);
    state = S_DYING;
    state_t = 42;
    deaths++;
}

static void game_step(int dir, int jpress, int jheld)
{
    anim++;
    if (banner_t) banner_t--;
    if (spring_t) spring_t--;
    for (int i = 0; i < MAX_PART; i++) {
        part_t *p = &parts[i];
        if (p->life <= 0) continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 3;
    }
    for (int i = 0; i < n_haz; i++) {
        haz_t *h = &haz[i];
        int hx = h->x >> 8, hy = h->y >> 8;
        if (h->vx) {
            int lead = hx + (h->vx > 0 ? 8 : -9), t = tile(lead >> 4, hy >> 4);
            if (solid(t) || t == 'x' || lead < 0 || lead >= mw * TS) h->vx = (int16_t)-h->vx;
            h->x += h->vx;
        }
        if (h->vy) {
            int lead = hy + (h->vy > 0 ? 8 : -9), t = tile(hx >> 4, lead >> 4);
            if (solid(t) || t == 'x' || lead < 0 || lead >= mh * TS) h->vy = (int16_t)-h->vy;
            h->y += h->vy;
        }
    }

    if (state == S_DYING) {
        if (--state_t <= 0) {
            place_ball(spawn_tx, spawn_ty, spawn_big);
            state = S_PLAY;
            squash_t = 0;
        }
        camera_step(0);
        return;
    }
    if (state == S_CLEAR) {
        if (state_t) state_t--;
        if ((anim & 3) == 0)
            burst(exit_tx * TS + 8, exit_ty * TS - 2, 2, C_SPARK, 40);
        camera_step(0);
        return;
    }

    lv_steps++;
    int ev = phys_step(&ball, dir, jpress, jheld);
    if (dir) look = dir;
    else if (iabs(ball.vx) < 64 && (anim & 63) == 0) look = 0;
    if (ev & EV_DIE) {
        die();
        return;
    }
    if (ev & EV_LAND) {
        squash_t = 8;
        squash_kind = 1;
    }
    if (ev & (EV_GROW | EV_SHRINK))
        burst(ball.x >> 8, ball.y >> 8, 10, 0xFFFF, 40);
    if (ev & EV_SPLASH)
        burst(ball.x >> 8, ball.y >> 8, 8, C_WATER, 30);
    if (squash_t) {
        squash_t--;
    } else {
        squash_kind = (ball.vy < -3 * FP || ball.vy > 5 * FP) ? 2 : 0;
    }

    int r = ball_r(&ball), bx = ball.x >> 8, by = ball.y >> 8;
    for (int i = 0; i < n_haz; i++) {
        int dx = bx - (haz[i].x >> 8), dy = by - (haz[i].y >> 8), rr = r + 5;
        if (dx * dx + dy * dy < rr * rr) {
            die();
            return;
        }
    }
    for (int i = 0; i < n_spark; i++) {
        spark_t *s = &sparks[i];
        if (!s->got && ball_hits(&ball, r, s->tx * TS + 3, s->ty * TS + 3, 10, 10)) {
            s->got = 1;
            got_spark++;
            burst(s->tx * TS + 8, s->ty * TS + 8, 8, C_SPARK, 36);
        }
    }
    for (int i = 0; i < n_cp; i++) {
        cp_t *c = &cps[i];
        if (ball_hits(&ball, r, c->tx * TS + 3, c->ty * TS - 12, 10, 28)) {
            if (!c->on || spawn_tx != c->tx || spawn_ty != c->ty || spawn_big != ball.big) {
                if (!c->on) burst(c->tx * TS + 7, c->ty * TS - 10, 10, 0xFFE0, 30);
                for (int j = 0; j < n_cp; j++) cps[j].on = (uint8_t)(j == i);
                spawn_tx = c->tx;
                spawn_ty = c->ty;
                spawn_big = ball.big;
            }
        }
    }
    if (exit_tx >= 0 && got_spark >= n_spark &&
        ball_hits(&ball, r, exit_tx * TS + 3, exit_ty * TS - 10, 10, 26)) {
        state = S_CLEAR;
        state_t = 50;
        burst(bx, by, 14, C_SPARK, 40);
    }
    sign_now = -1;
    for (int i = 0; i < n_sign; i++)
        if (iabs(bx - (signs[i].tx * TS + 8)) < 30 && iabs(by - (signs[i].ty * TS + 8)) < 36)
            sign_now = i;
    camera_step(0);
}

/* ── Art ─────────────────────────────────────────────────────────────── */
/* Bricks / blocks / water use digit and a-c slots set per zone. */
static const char A_BRICK[] =
    "4444444444444444" "4222222222222222" "4111111111111113" "4111111111111113"
    "4111111111111113" "4111111111111113" "4111111111111113" "4333333333333333"
    "4444444444444444" "2222222342222222" "1111111341111111" "1111111341111111"
    "1111111341111111" "1111111341111111" "1111111341111111" "3333333343333333";
static const char A_BRICK_TOP[] =
    "5555555555555555" "5555555555555555" "6556556655565565" "4664664444664464"
    "4111111111111113" "4111111111111113" "4111111111111113" "4333333333333333"
    "4444444444444444" "2222222342222222" "1111111341111111" "1111111341111111"
    "1111111341111111" "1111111341111111" "1111111341111111" "3333333343333333";
static const char A_BLOCK[] =
    "8888888888888889" "8777777777777779" "8787777777777879" "8777777777777779"
    "8777777777777779" "8777777777777779" "8777777777777779" "8777777777777779"
    "8777777777777779" "8777777777777779" "8777777777777779" "8777777777777779"
    "8777777777777779" "8797777777777979" "8777777777777779" "9999999999999999";
static const char A_WATER[] =
    "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaabbaaaaaaaaaaa" "aaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaa" "aaaaaaaaaaabbaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaa" "abbaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa"
    "aaaaaaabbaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaabb";
static const char A_SURF[] =
    "ccccbbccccccbbcc" "bbbbaabbbbbbaabb" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaa" "aaaaaaaaaaabbaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaa" "abbaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa"
    "aaaaaaabbaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaabb";

static const char A_SPIKE[] =
    "................" "................" "................" "................"
    "................" "................" "................" "................"
    "...wg......wg..." "...Gg......Gg..." "..GGgg....GGgg.." "..GGgg....GGgg.."
    ".GGGggg..GGGggg." ".GGGggg..GGGggg." "GGGGggggGGGGgggg" "KKKKKKKKKKKKKKKK";
static const char A_SPRING0[] =
    "................" "................" "................" "................"
    "..wrrrrrrrrrrr.." "..rrrrrrrrrrrr.." "..RRRRRRRRRRRR.." "....GGGGGGGG...."
    "......gggg......" "....GGGGGGGG...." "......gggg......" "....GGGGGGGG...."
    "......gggg......" "....GGGGGGGG...." "..dddddddddddd.." "..KKKKKKKKKKKK..";
static const char A_SPRING1[] =
    "..wrrrrrrrrrrr.." "..rrrrrrrrrrrr.." "..RRRRRRRRRRRR.." "....GGGGGGGG...."
    "......gggg......" "....GGGGGGGG...." "......gggg......" "....GGGGGGGG...."
    "......gggg......" "....GGGGGGGG...." "......gggg......" "....GGGGGGGG...."
    "......gggg......" "....GGGGGGGG...." "..dddddddddddd.." "..KKKKKKKKKKKK..";
static const char A_PUMP[] =
    "................" "................" "................" "................"
    "................" "................" "......kkkk......" "....kkrrrrkk...."
    "...krrrQQrrrk..." "...krrrQQrrrk..." "..krQQQQQQQQrk.." "..krrrrQQrrrrk.."
    "..krrrrQQrrrrk.." "..kRRRRRRRRRRk.." ".dddddddddddddd." ".KKKKKKKKKKKKKK.";
static const char A_PIN[] =
    "................" "................" "................" "................"
    "................" "................" "......kkkk......" "....kkbbbbkk...."
    "...kbbbbbbbbk..." "...kbbbbbbbbk..." "..kbQQQQQQQQbk.." "..kbbbbbbbbbbk.."
    "..kbbbbbbbbbbk.." "..kBBBBBBBBBBk.." ".dddddddddddddd." ".KKKKKKKKKKKKKK.";
static const char A_SIGN[] =
    "................" "................" ".kkkkkkkkkkkkkk." ".knnnnnnnnnnnnk."
    ".knnnnnkknnnnnk." ".knnnnnkknnnnnk." ".knnnnnkknnnnnk." ".knnnnnnnnnnnnk."
    ".knnnnnkknnnnnk." ".kNNNNNNNNNNNNk." ".kkkkkkkkkkkkkk." "......kNNk......"
    "......kNnk......" "......kNnk......" "......kNnk......" "......kNnk......";
static const char A_SPARK0[] =
    "............" "......y....." "......y....." ".....ywy...." "....yywyy..."
    "..yyywwwyyy." "....yywyy..." ".....ywy...." "......y....." "......y....."
    "............" "............";
static const char A_SPARK1[] =
    "............" "............" "......y....." ".....ywy...." "....ywwwy..."
    "...ywwwwwy.." "....ywwwy..." ".....ywy...." "......y....." "............"
    "............" "............";
static const char A_SKULL[] =
    ".kkkkkk." "kwwwwwwk" "kwkwwkwk" "kwkwwkwk" "kwwwwwwk" ".kwkkwk." ".kwwwwk." "..kkkk..";
#define POLE "......Gg........"
static const char A_BEACON[] =
    "................" "................" "................" "......kkk......."
    ".....k333k......" ".....k333k......" "......kkk......." "......Gg2222...."
    "......Gg222222.." "......Gg2222222." "......Gg222222.." "......Gg2222...."
    POLE POLE POLE POLE POLE POLE POLE POLE POLE POLE POLE POLE POLE POLE POLE POLE
    "....kGGGgk......" "...kGGGGggk....." "..kddddddddk...." "..kKKKKKKKKk....";

static uint16_t t_brick[2][256], t_block[256], t_water[2][256];
static uint16_t s_brick[2][1024], s_block[1024], s_water[2][1024]; /* 4-tile strips */
static uint16_t c_spike[4][256], c_spring[2][256], c_pump[256], c_pin[256], c_sign[256];
static uint16_t c_spark[2][144], c_skull[64], c_beacon[2][512], c_portal[4][512], c_urchin[2][256];
#define SB 18 /* small-ball cache box */
#define BB 36 /* big-ball cache box   */
static uint16_t c_ball_s[3][3][SB * SB], c_ball_b[3][3][BB * BB];
static uint16_t bg_band[8];
static int built_zone = -1;

static void strip_of(uint16_t *dst, const uint16_t *t)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 64; x++) dst[y * 64 + x] = t[y * 16 + (x & 15)];
}

static uint16_t mix(int r0, int g0, int b0, int r1, int g1, int b1, int i, int n)
{
    return spr_col(r0 + (r1 - r0) * i / n, g0 + (g1 - g0) * i / n, b0 + (b1 - b0) * i / n);
}

typedef struct { uint8_t c[9][3]; uint8_t sky[2][3]; } zone_pal_t;
static const zone_pal_t ZP[3] = {
    {{{214, 120, 62}, {240, 168, 104}, {150, 74, 40}, {96, 48, 30}, {120, 210, 70}, {60, 150, 50},
      {150, 160, 176}, {200, 208, 220}, {90, 96, 112}},
     {{88, 160, 240}, {200, 230, 255}}},
    {{{64, 128, 136}, {110, 176, 176}, {40, 88, 96}, {20, 44, 52}, {110, 200, 140}, {50, 130, 90},
      {130, 118, 96}, {176, 164, 134}, {74, 64, 52}},
     {{8, 14, 28}, {30, 66, 84}}},
    {{{128, 84, 168}, {176, 136, 214}, {86, 54, 120}, {48, 28, 72}, {240, 200, 120}, {190, 140, 70},
      {92, 102, 134}, {140, 150, 182}, {50, 56, 82}},
     {{40, 24, 80}, {252, 146, 96}}},
};
static const uint8_t ZP_MONO[9] = {1, 1, 0, 0, 1, 0, 1, 1, 0};

static void build_zone(int z)
{
    if (built_zone == z) return;
    built_zone = z;
    for (int i = 0; i < 9; i++)
        spr_pal_set((char)('1' + i), ZP[z].c[i][0], ZP[z].c[i][1], ZP[z].c[i][2], ZP_MONO[i]);
    if (z == 1) {
        spr_pal_set('a', 26, 92, 150, 0);
        spr_pal_set('b', 60, 140, 190, 1);
    } else {
        spr_pal_set('a', 40, 110, 200, 0);
        spr_pal_set('b', 96, 166, 232, 1);
    }
    spr_pal_set('c', 214, 242, 255, 1);
    spr_build(t_brick[0], A_BRICK, 256, 0);
    spr_build(t_brick[1], A_BRICK_TOP, 256, 0);
    spr_build(t_block, A_BLOCK, 256, 0);
    spr_build(t_water[0], A_WATER, 256, 0);
    spr_build(t_water[1], A_SURF, 256, 0);
    strip_of(s_brick[0], t_brick[0]);
    strip_of(s_brick[1], t_brick[1]);
    strip_of(s_block, t_block);
    strip_of(s_water[0], t_water[0]);
    strip_of(s_water[1], t_water[1]);
    const uint8_t *a = ZP[z].sky[0], *b = ZP[z].sky[1];
    for (int i = 0; i < 8; i++) bg_band[i] = mix(a[0], a[1], a[2], b[0], b[1], b[2], i, 7);
    C_WATER = spr_mono ? 0xFFFF : SPR_RGB(150, 200, 255);
}

/* Ball: an ellipse in 1/16 px with shading and eyes, bottom-aligned in its box. */
static void gen_ball(uint16_t *dst, int bw, int r, int sq, int lk)
{
    int rw = r * 16, rh = r * 16;
    if (sq == 1) { rw = r * 20; rh = r * 12; }
    if (sq == 2) { rw = r * 14; rh = r * 18; }
    if (rw > bw * 8) rw = bw * 8;
    int cxf = bw * 8, cyf = bw * 16 - rh;
    int rmin = (rw < rh ? rw : rh) / 16;
    int thr = 65536 - 65536 * 22 / (rmin * 10); /* ~1 px outline */
    int eu = 62, ev = 78, pu = 34, pv = 44;   /* eye and pupil radii (1/256 of r) */
    for (int y = 0; y < bw; y++) {
        for (int x = 0; x < bw; x++) {
            int px = x * 16 + 8 - cxf, py = y * 16 + 8 - cyf;
            int u = px * 256 / rw, v = py * 256 / rh, d = u * u + v * v;
            char c = '.';
            if (d <= 65536) {
                if (d > thr) c = '4';
                else if ((u + 100) * (u + 100) + (v + 110) * (v + 110) < 46 * 46) c = '5';
                else if (u + v > 130 && d > 26000) c = '3';
                else c = '2';
                for (int e = -1; e <= 1; e += 2) {
                    int ux = e * 84 + lk * 50, du = u - ux, dv = v + 70;
                    if (du * du * ev * ev + dv * dv * eu * eu <= eu * eu * ev * ev) {
                        c = '6';
                        int qu = du - lk * 22, qv = dv - 14;
                        if (qu * qu * pv * pv + qv * qv * pu * pu <= pu * pu * pv * pv) c = '7';
                    }
                }
            }
            dst[y * bw + x] = c == '.' ? SPR_KEY : spr_px(c);
        }
    }
}

static void gen_portal(uint16_t *dst, int mode)
{
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 16; x++) {
            int dx = 2 * x + 1 - 16, dy = 2 * y + 1 - 16;
            int outer = y >= 8 || dx * dx + dy * dy <= 16 * 16;
            int inner = (y >= 8 && x >= 3 && x <= 12 && y <= 30) || (y < 8 && dx * dx + dy * dy <= 10 * 10);
            int edge = outer && (x == 0 || x == 15 || (y < 8 && dx * dx + dy * dy > 13 * 13) || y == 31);
            char c = '.';
            if (outer && !inner) c = edge ? 'k' : (x < 8 ? 'z' : 'Z');
            else if (inner) {
                if (mode == 0) {
                    c = (x == 5 || x == 10 || y == 18) ? 'g' : 'K';
                } else {
                    int ex = 2 * x + 1 - 16, ey = 2 * y + 1 - 40;
                    int band = ((ex * ex + ey * ey) / 40 + 12 - mode * 1) % 3;
                    c = band == 0 ? '#' : (band == 1 ? '$' : 'w');
                }
            }
            dst[y * 16 + x] = c == '.' ? SPR_KEY : spr_px(c);
        }
    }
}

static const int8_t COS16[16] = {64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24, 0, 24, 45, 59};

static void gen_urchin(uint16_t *dst, int frame)
{
    char a[256];
    for (int i = 0; i < 256; i++) a[i] = '.';
    for (int k = 0; k < 8; k++) {
        int d = (2 * k + frame) & 15, cs = COS16[d], sn = COS16[(d + 12) & 15];
        for (int t = 20; t <= 30; t++) {
            int x = (1920 + cs * t) >> 8, y = (1920 + sn * t) >> 8;
            if (x >= 0 && x < 16 && y >= 0 && y < 16) a[y * 16 + x] = t >= 27 ? 'w' : 'G';
        }
    }
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int dx = 2 * x + 1 - 16, dy = 2 * y + 1 - 16, d = dx * dx + dy * dy;
            if (d <= 121) a[y * 16 + x] = (dx + dy > 6 && d > 50) ? 'U' : 'u';
            if (d <= 121 && d > 90) a[y * 16 + x] = 'k';
        }
    a[6 * 16 + 5] = a[6 * 16 + 6] = a[6 * 16 + 9] = a[6 * 16 + 10] = 'w';
    a[7 * 16 + 5] = a[7 * 16 + 9] = 'w';
    a[7 * 16 + 6] = a[7 * 16 + 10] = 'k';
    a[5 * 16 + 5] = a[5 * 16 + 10] = 'k';
    spr_build(dst, a, 256, 0);
}

static void build_sprites(void)
{
    spr_pal_set('Q', 255, 255, 255, 0); /* pump/pin marks: white, black on the Sharp */
    spr_pal_set('#', 110, 220, 255, 1); /* gate swirl */
    spr_pal_set('$', 176, 96, 232, 0);
    spr_build(c_spike[0], A_SPIKE, 256, 0);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            uint16_t p = c_spike[0][y * 16 + x];
            c_spike[1][(15 - y) * 16 + x] = p;       /* ceiling      */
            c_spike[2][x * 16 + (15 - y)] = p;       /* points right */
            c_spike[3][x * 16 + y] = p;              /* points left  */
        }
    spr_build(c_spring[0], A_SPRING0, 256, 0);
    spr_build(c_spring[1], A_SPRING1, 256, 0);
    spr_build(c_pump, A_PUMP, 256, 0);
    spr_build(c_pin, A_PIN, 256, 0);
    spr_build(c_sign, A_SIGN, 256, 0);
    spr_build(c_spark[0], A_SPARK0, 144, 0);
    spr_build(c_spark[1], A_SPARK1, 144, 0);
    spr_build(c_skull, A_SKULL, 64, 0);
    spr_pal_set('2', 140, 136, 150, 0);
    spr_pal_set('3', 60, 56, 70, 0);
    spr_build(c_beacon[0], A_BEACON, 512, 0);
    spr_pal_set('2', 110, 230, 100, 1);
    spr_pal_set('3', 255, 230, 90, 1);
    spr_build(c_beacon[1], A_BEACON, 512, 0);
    for (int m = 0; m < 4; m++) gen_portal(c_portal[m], m);
    gen_urchin(c_urchin[0], 0);
    gen_urchin(c_urchin[1], 1);

    /* Rollo: lime body, light top, shade, dark rim, eye white, pupil. */
    spr_pal_set('2', 150, 228, 60, 1);
    spr_pal_set('5', 226, 255, 170, 1);
    spr_pal_set('3', 88, 168, 40, 1);
    spr_pal_set('4', 26, 74, 30, 0);
    spr_pal_set('6', 255, 255, 255, 1);
    spr_pal_set('7', 16, 20, 24, 0);
    for (int sq = 0; sq < 3; sq++)
        for (int lk = 0; lk < 3; lk++) {
            gen_ball(c_ball_s[sq][lk], SB, R_SMALL, sq, lk - 1);
            gen_ball(c_ball_b[sq][lk], BB, R_BIG, sq, lk - 1);
        }
    C_BALL = spr_mono ? 0xFFFF : SPR_RGB(150, 228, 60);
    C_SPARK = spr_mono ? 0xFFFF : SPR_RGB(255, 226, 90);
    built_zone = -1; /* digit slots were borrowed: rebuild the zone */
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
enum { KD_NONE, KD_BRICK, KD_TOP, KD_BLOCK, KD_WATER, KD_SURF };

static int kind_at(int tx, int ty)
{
    int t = map[ty][tx];
    if (t == '#') return solid(tile(tx, ty - 1)) ? KD_BRICK : KD_TOP;
    if (t == '%') return KD_BLOCK;
    if (t == '~') return tile(tx, ty - 1) == '~' ? KD_WATER : KD_SURF;
    return KD_NONE;
}

static void draw_run(int k, int sx, int sy, int n)
{
    const uint16_t *strip, *one;
    switch (k) {
    case KD_BRICK: strip = s_brick[0]; one = t_brick[0]; break;
    case KD_TOP:   strip = s_brick[1]; one = t_brick[1]; break;
    case KD_BLOCK: strip = s_block;    one = t_block;    break;
    case KD_WATER: strip = s_water[0]; one = t_water[0]; break;
    default:       strip = s_water[1]; one = t_water[1]; break;
    }
    for (; n >= 4; n -= 4, sx += 64) display_bitmap(sx, sy, 64, 16, strip, 2048);
    for (; n > 0; n--, sx += 16) display_bitmap(sx, sy, 16, 16, one, 512);
}

static void draw_special(int c, int tx, int ty, int sx, int sy)
{
    switch (c) {
    case '^': spr_blit16(sx, sy, c_spike[0]); break;
    case 'v': spr_blit16(sx, sy, c_spike[1]); break;
    case '>': spr_blit16(sx, sy, c_spike[2]); break;
    case '<': spr_blit16(sx, sy, c_spike[3]); break;
    case 'S': spr_blit16(sx, sy, c_spring[spring_t && spring_tx == tx && spring_ty == ty]); break;
    case '+': spr_blit16(sx, sy, c_pump); break;
    case '-': spr_blit16(sx, sy, c_pin); break;
    case 'E': {
        int open = got_spark >= n_spark;
        spr_blit(sx, sy - 16, 16, 32, c_portal[open ? 1 + (anim / 6) % 3 : 0]);
        break;
    }
    }
}

static void draw_bg(int cx, int cy)
{
    if (spr_mono) {
        display_clear(0x0000);
        if (zone == 2)
            for (int i = 0; i < 14; i++) {
                int x = (i * 97 - cx / 8) % (SW + 40);
                if (x < 0) x += SW + 40;
                display_pixel(x - 20, (i * 53) % (SH / 2) + 20, 0xFFFF);
            }
        return;
    }
    int bh = (SH + 7) / 8;
    for (int i = 0; i < 8; i++) display_rect(0, i * bh, SW, bh, bg_band[i]);
    int par_y = cy / 6;
    if (zone == 0) {
        uint16_t hill = SPR_RGB(150, 214, 140), hill2 = SPR_RGB(118, 192, 120), cl = 0xFFFF;
        for (int i = 0; i < 3; i++) {
            int span = SW + 300, x = (i * 233 - cx / 6) % span;
            if (x < 0) x += span;
            display_circle_fill(x - 150, SH + 60 - par_y, 120, i & 1 ? hill2 : hill);
        }
        for (int i = 0; i < 5; i++) {
            int span = SW + 140, x = (i * 151 - cx / 3) % span;
            if (x < 0) x += span;
            x -= 70;
            int y = 26 + (i * 37) % 70 - par_y;
            display_rounded_rect_fill(x, y, 56, 14, 7, cl);
            display_rounded_rect_fill(x + 12, y - 8, 28, 14, 7, cl);
        }
    } else if (zone == 1) {
        uint16_t rock = SPR_RGB(18, 34, 48), glow = SPR_RGB(120, 230, 210);
        for (int i = 0; i < 9; i++) {
            int span = SW + 60, x = (i * 67 - cx / 3) % span;
            if (x < 0) x += span;
            x -= 30;
            int len = 26 + (i * 29) % 50;
            display_triangle_fill(x, -par_y, x + 26, -par_y, x + 13, len - par_y, rock);
        }
        for (int i = 0; i < 7; i++) {
            int span = SW + 20, x = (i * 89 - cx / 2 + ((anim / 4 + i * 11) % 24)) % span;
            if (x < 0) x += span;
            int y = 40 + (i * 41) % (SH - 80) + ((anim / 8 + i * 5) % 12) - par_y;
            display_rect(x - 10, y, 2, 2, glow);
        }
    } else {
        uint16_t sun = SPR_RGB(255, 214, 120), tower = SPR_RGB(70, 40, 92);
        display_circle_fill(SW * 3 / 4 - cx / 40, SH / 2 + 10 - par_y, 34, sun);
        for (int i = 0; i < 6; i++) {
            int span = SW + 120, x = (i * 109 - cx / 5) % span;
            if (x < 0) x += span;
            x -= 60;
            int h = 60 + (i * 37) % 70, w = 28 + (i * 13) % 20, y = SH - h - par_y + 30;
            display_rect(x, y, w, h + 40, tower);
            display_rect(x - 3, y - 6, w + 6, 6, tower);
        }
    }
}

static void draw_hud(void)
{
    char b[32];
    display_rect(0, 0, SW, HUD_H, SPRT.panel);
    display_hline(0, HUD_H, SW, SPRT.edge);
    b[0] = 0;
    g_cat(b, "L");
    g_cat_int(b, cur_lv + 1);
    display_text(4, 3, b, SPRT.yellow);
    spr_blit(34, 2, 12, 12, c_spark[(anim / 12) & 1]);
    b[0] = 0;
    g_cat_int(b, got_spark);
    g_cat(b, "/");
    g_cat_int(b, n_spark);
    display_text(48, 3, b, got_spark >= n_spark ? SPRT.green : SPRT.text);
    g_text_c(SW / 2, 3, LV[cur_lv].name, SPRT.dim);
    b[0] = 0;
    g_cat_time(b, lv_steps / 60);
    g_text_r(SW - 4, 3, b, SPRT.text);
    b[0] = 0;
    g_cat_int(b, deaths);
    int dx = SW - 4 - g_tw("00:00") - 10 - g_tw(b);
    spr_blit(dx - 11, 4, 8, 8, c_skull);
    display_text(dx, 3, b, SPRT.text);
}

static void toast(int y, const char *s, uint16_t col)
{
    int w = g_tw(s) + 20;
    if (w > SW - 8) w = SW - 8;
    int x = (SW - w) / 2;
    if (!spr_mono) display_rounded_rect_fill(x + 2, y + 2, w, 20, 5, 0x0000);
    display_rounded_rect_fill(x, y, w, 20, 5, SPRT.panel);
    display_rounded_rect(x, y, w, 20, 5, SPRT.accent);
    g_text_c(SW / 2, y + 5, s, col);
}

static void draw_world(void)
{
    int cx = cam_x >> 8, cy = cam_y >> 8;
    draw_bg(cx, cy);
    int tx0 = imax(0, cx >> 4), tx1 = imin(mw - 1, (cx + SW - 1) >> 4);
    int ty0 = imax(0, cy >> 4), ty1 = imin(mh - 1, (cy + SH - 1) >> 4);
    for (int ty = ty0; ty <= ty1; ty++) {
        int sy = ty * TS - cy, tx = tx0;
        while (tx <= tx1) {
            int k = kind_at(tx, ty);
            if (k == KD_NONE) {
                int c = map[ty][tx];
                if (c != ' ' && c != 'x') {
                    if (tile(tx, ty - 1) == '~') display_bitmap(tx * TS - cx, sy, 16, 16, t_water[0], 512);
                    draw_special(c, tx, ty, tx * TS - cx, sy);
                }
                tx++;
                continue;
            }
            int s = tx;
            while (tx <= tx1 && kind_at(tx, ty) == k) tx++;
            draw_run(k, s * TS - cx, sy, tx - s);
        }
    }
    /* The gate's top half may sit above the first visible row. */
    if (exit_tx >= 0 && exit_ty == ty1 + 1)
        draw_special('E', exit_tx, exit_ty, exit_tx * TS - cx, exit_ty * TS - cy);
    for (int i = 0; i < n_sign; i++) {
        int sx = signs[i].tx * TS - cx, sy = signs[i].ty * TS - cy;
        if (sx > -16 && sx < SW && sy > -16 && sy < SH) spr_blit16(sx, sy, c_sign);
    }
    for (int i = 0; i < n_cp; i++) {
        int sx = cps[i].tx * TS - cx, sy = cps[i].ty * TS - 16 - cy;
        if (sx > -16 && sx < SW && sy > -32 && sy < SH) spr_blit(sx, sy, 16, 32, c_beacon[cps[i].on]);
    }
    for (int i = 0; i < n_spark; i++) {
        if (sparks[i].got) continue;
        int bob = ((anim / 8 + i * 3) & 7) < 4 ? 0 : 1;
        int sx = sparks[i].tx * TS + 2 - cx, sy = sparks[i].ty * TS + 2 + bob - cy;
        if (sx > -12 && sx < SW && sy > -12 && sy < SH)
            spr_blit(sx, sy, 12, 12, c_spark[((anim / 10) + i) & 1]);
    }
    for (int i = 0; i < n_haz; i++) {
        int sx = (haz[i].x >> 8) - 8 - cx, sy = (haz[i].y >> 8) - 8 - cy;
        if (sx > -16 && sx < SW && sy > -16 && sy < SH) spr_blit16(sx, sy, c_urchin[(anim / 8) & 1]);
    }
    if (state == S_DYING && state_t > 30) {
        int rr = (42 - state_t) * 3 + 6;
        display_circle(pop_x - cx, pop_y - cy, rr, 0xFFFF);
        display_circle(pop_x - cx, pop_y - cy, rr - 2, C_BALL);
    }
    if (state == S_PLAY) {
        int r = ball_r(&ball), bw = ball.big ? BB : SB;
        const uint16_t *img = ball.big ? c_ball_b[squash_kind][look + 1] : c_ball_s[squash_kind][look + 1];
        spr_blit((ball.x >> 8) - bw / 2 - cx, (ball.y >> 8) + r - bw - cy, bw, bw, img);
    }
    for (int i = 0; i < MAX_PART; i++) {
        part_t *p = &parts[i];
        if (p->life <= 0) continue;
        int s = p->life > 12 ? 3 : 2;
        display_rect((p->x >> 4) - cx, (p->y >> 4) - cy, s, s, p->col);
    }
}

static void draw_frame(void)
{
    draw_world();
    draw_hud();
    if (banner_t) {
        char b[32];
        b[0] = 0;
        g_cat(b, "Level ");
        g_cat_int(b, cur_lv + 1);
        g_cat(b, ": ");
        g_cat(b, LV[cur_lv].name);
        toast(28, b, SPRT.yellow);
    } else if (sign_now >= 0) {
        const char *const *h = LV[cur_lv].hints;
        int n = 0;
        while (h && h[n]) n++;
        if (sign_now < n) toast(SH - 30, h[sign_now], SPRT.text);
    }
    if (state == S_CLEAR) toast(SH / 2 - 10, "Gate reached!", SPRT.green);
}

/* ── Saves ───────────────────────────────────────────────────────────── */
static void key_of(char *b, const char *pfx, int i)
{
    b[0] = 0;
    g_cat(b, pfx);
    g_cat_int(b, i);
}

static void load_progress(void)
{
    char k[24];
    unlocked = iclamp(g_load_int("rollo/unl", 1), 1, NLV);
    for (int i = 0; i < NLV; i++) {
        key_of(k, "rollo/t", i);
        best_cs[i] = g_load_int(k, 0);
        key_of(k, "rollo/f", i);
        best_falls[i] = g_load_int(k, 0);
    }
}

static char *cat_cs(char *b, int cs)
{
    g_cat_time(b, cs / 100);
    g_cat(b, ".");
    return g_cat_int(b, (cs / 10) % 10);
}

/* ── Menus ───────────────────────────────────────────────────────────── */
static int menu(const char *title, const char *const *items, int n, int sel)
{
    int w = 210, h = 36 + n * 20 + 6, x = (SW - w) / 2, y = (SH - h) / 2;
    g_flush_input();
    for (;;) {
        spr_panel(x, y, w, h, title);
        for (int i = 0; i < n; i++) {
            int iy = y + 34 + i * 20;
            if (i == sel) spr_sel_row(x + 8, iy - 4, w - 16, 18);
            display_text(x + 22, iy, items[i], i == sel ? SPRT.seltext : SPRT.text);
        }
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_DOWN) sel = (sel + 1) % n;
        else if (k == K_A) return sel;
        else if (k == K_B || k == K_HOME) return -1;
    }
}

static void title_letters(int x0, int y0)
{
    static const char *const G_R = "####." "#...#" "####." "#.#.." "#..#." "#...#";
    static const char *const G_O = ".###." "#...#" "#...#" "#...#" "#...#" ".###.";
    static const char *const G_L = "#...." "#...." "#...." "#...." "#...." "#####";
    const char *word[5] = {G_R, G_O, G_L, G_L, G_O};
    uint16_t face = spr_col(150, 228, 60), hi = spr_col(226, 255, 170), sh = spr_col(26, 74, 30);
    for (int pass = 0; pass < 2; pass++)
        for (int l = 0; l < 5; l++)
            for (int i = 0; i < 30; i++) {
                if (word[l][i] != '#') continue;
                int x = x0 + l * 46 + (i % 5) * 8, y = y0 + (i / 5) * 8;
                if (pass == 0) {
                    if (!spr_mono) display_rect(x - 1, y - 1, 11, 11, sh);
                } else {
                    display_rect(x, y, 8, 8, face);
                    if (!spr_mono) display_rect(x, y, 7, 2, hi);
                }
            }
}

static int cleared_count(void)
{
    int n = 0;
    for (int i = 0; i < NLV; i++) n += best_cs[i] > 0;
    return n;
}

/** 0: play, -1: quit. */
static int title_screen(void)
{
    build_zone(0);
    zone = 0;
    g_flush_input();
    int32_t bx = 40 * FP, bvx = 2 * FP, by = 0, bvy = 0;
    int sq = 0, sqt = 0;
    uint32_t last = g_now();
    int acc = 0;
    for (;;) {
        akira_input_event_t ev;
        while (input_poll_event(&ev, sizeof(ev)) == 1) {
            if (!ev.pressed) continue;
            if (ev.button_id == AKIRA_BTN_ID_A || ev.button_id == AKIRA_BTN_ID_X) return 0;
            if (ev.button_id == AKIRA_BTN_ID_B || ev.button_id == 1) return -1;
        }
        uint32_t now = g_now();
        acc += (int)(now - last) * 3;
        last = now;
        if (acc > 200) acc = 200;
        int floor_y = SH - 48;
        while (acc >= 50) {
            acc -= 50;
            anim++;
            bx += bvx;
            if (bx > (SW - 12) * FP || bx < 12 * FP) bvx = -bvx;
            bvy += GRAV;
            by += bvy;
            if (by > 0) {
                by = 0;
                bvy = -1150;
                sq = 1;
                sqt = 6;
            }
            if (sqt) sqt--;
            else sq = bvy < -1200 ? 2 : 0;
        }
        draw_bg(anim / 2, 0);
        for (int x = 0; x < SW; x += 64) {
            display_bitmap(x, floor_y, 64, 16, s_brick[1], 2048);
            display_bitmap(x, floor_y + 16, 64, 16, s_brick[0], 2048);
            display_bitmap(x, floor_y + 32, 64, 16, s_brick[0], 2048);
        }
        title_letters((SW - 226) / 2, 24);
        spr_blit((bx >> 8) - SB / 2, floor_y + (by >> 8) - SB, SB, SB, c_ball_s[sq][bvx > 0 ? 2 : 0]);
        uint16_t sub = spr_mono ? 0xFFFF : SPR_RGB(40, 60, 110);
        g_text_c(SW / 2, 82, "a rubber-ball platformer", sub);
        char b[40];
        b[0] = 0;
        g_cat(b, "Levels cleared: ");
        g_cat_int(b, cleared_count());
        g_cat(b, "/");
        g_cat_int(b, NLV);
        toast(98, b, SPRT.yellow);
        if ((anim / 30) & 1) g_text_c(SW / 2, 128, "Press A to play", spr_mono ? 0xFFFF : SPR_RGB(20, 30, 60));
        if (spr_mono) display_rect(3, SH - 16, g_tw("HOME: quit") + 6, 14, 0x0000); /* readable over white bricks */
        spr_text_out(6, SH - 14, "HOME: quit", 0xFFFF);
        display_flush();
        delay(4000);
    }
}

/** Index of the chosen level, or -1 for back. */
static int level_select(int sel)
{
    g_flush_input();
    for (;;) {
        build_zone(LV[sel].zone);
        zone = LV[sel].zone;
        draw_bg(sel * 40, 0);
        int cols = 5, cw = 48, ch = 44, gw = cols * cw + (cols - 1) * 8;
        int pw = gw + 24, ph = 2 * ch + 8 + 106, px = (SW - pw) / 2, py = (SH - ph) / 2;
        spr_panel(px, py, pw, ph, "LEVELS");
        for (int i = 0; i < NLV; i++) {
            int x = px + 12 + (i % cols) * (cw + 8), y = py + 34 + (i / cols) * (ch + 8);
            int open = i < unlocked, done = best_cs[i] > 0;
            if (i == sel) spr_sel_row(x - 3, y - 3, cw + 6, ch + 6);
            uint16_t box = spr_mono ? 0x0000 : (done ? SPR_RGB(40, 90, 50) : SPR_RGB(36, 32, 54));
            display_rounded_rect_fill(x, y, cw, ch, 5, box);
            display_rounded_rect(x, y, cw, ch, 5, open ? SPRT.accent : SPRT.edge);
            char b[8];
            b[0] = 0;
            g_cat_int(b, i + 1);
            if (open) {
                g_text_large_c(x + cw / 2, y + 7, b, done ? SPRT.green : SPRT.text);
                if (done) spr_blit(x + cw / 2 - 6, y + 28, 12, 12, c_spark[0]);
            } else {
                uint16_t lc = SPRT.dim;
                display_rounded_rect(x + cw / 2 - 5, y + 10, 10, 12, 4, lc);
                display_rect(x + cw / 2 - 8, y + 18, 16, 13, lc);
                display_rect(x + cw / 2 - 1, y + 22, 2, 5, box);
            }
        }
        int iy = py + 34 + 2 * (ch + 8) + 4;
        display_hline(px + 10, iy - 4, pw - 20, SPRT.edge);
        g_text_large_c(SW / 2, iy + 2, sel < unlocked ? LV[sel].name : "Locked", SPRT.yellow);
        char b[48];
        b[0] = 0;
        if (best_cs[sel] > 0) {
            g_cat(b, "Best ");
            cat_cs(b, best_cs[sel]);
            g_cat(b, "   fewest falls ");
            g_cat_int(b, best_falls[sel] - 1);
        } else {
            g_cat(b, sel < unlocked ? "Not cleared yet" : "Clear the level before it");
        }
        g_text_c(SW / 2, iy + 26, b, SPRT.text);
        g_text_c(SW / 2, iy + 44, "A play   B back", SPRT.dim);
        display_flush();
        uint32_t k = g_wait_key();
        if (k == K_LEFT) sel = (sel + NLV - 1) % NLV;
        else if (k == K_RIGHT) sel = (sel + 1) % NLV;
        else if (k == K_UP || k == K_DOWN) sel = (sel + cols) % (2 * cols) < NLV ? (sel + cols) % (2 * cols) : sel;
        else if (k == K_A && sel < unlocked) return sel;
        else if (k == K_B || k == K_HOME) return -1;
    }
}

/* ── Playing a level ─────────────────────────────────────────────────── */
enum { R_NEXT, R_RETRY, R_LEVELS, R_QUIT };

static void reset_level(int lv)
{
    cur_lv = lv;
    load_level(lv);
    build_zone(zone);
    spawn_tx = start_tx;
    spawn_ty = start_ty;
    spawn_big = 0;
    place_ball(spawn_tx, spawn_ty, 0);
    for (int i = 0; i < MAX_PART; i++) parts[i].life = 0;
    state = S_PLAY;
    deaths = lv_steps = 0;
    squash_t = squash_kind = look = 0;
    look_ahead = 0;
    banner_t = 130;
    sign_now = -1;
    camera_step(1);
}

static int result_screen(void)
{
    int cs = lv_steps * 100 / 60, lv = cur_lv;
    int new_time = best_cs[lv] == 0 || cs < best_cs[lv];
    int new_falls = best_falls[lv] == 0 || deaths + 1 < best_falls[lv];
    char k[24], b[48];
    if (new_time) {
        best_cs[lv] = cs;
        key_of(k, "rollo/t", lv);
        g_save_int(k, cs);
    }
    if (new_falls) {
        best_falls[lv] = deaths + 1;
        key_of(k, "rollo/f", lv);
        g_save_int(k, deaths + 1);
    }
    if (lv + 1 < NLV && unlocked < lv + 2) {
        unlocked = lv + 2;
        g_save_int("rollo/unl", unlocked);
    }
    g_flush_input();
    int w = 240, h = 132, x = (SW - w) / 2, y = (SH - h) / 2;
    spr_panel(x, y, w, h, lv + 1 == NLV ? "ALL CLEAR!" : "LEVEL CLEAR");
    b[0] = 0;
    g_cat(b, "Time   ");
    cat_cs(b, cs);
    display_text(x + 20, y + 38, b, SPRT.text);
    if (new_time) display_text(x + 140, y + 38, "new best!", SPRT.green);
    else {
        b[0] = 0;
        g_cat(b, "best ");
        cat_cs(b, best_cs[lv]);
        display_text(x + 140, y + 38, b, SPRT.dim);
    }
    b[0] = 0;
    g_cat(b, "Falls  ");
    g_cat_int(b, deaths);
    display_text(x + 20, y + 56, b, SPRT.text);
    if (new_falls) display_text(x + 140, y + 56, "new best!", SPRT.green);
    else {
        b[0] = 0;
        g_cat(b, "best ");
        g_cat_int(b, best_falls[lv] - 1);
        display_text(x + 140, y + 56, b, SPRT.dim);
    }
    b[0] = 0;
    g_cat(b, "Sparks ");
    g_cat_int(b, n_spark);
    g_cat(b, "/");
    g_cat_int(b, n_spark);
    display_text(x + 20, y + 74, b, SPRT.yellow);
    display_hline(x + 10, y + 94, w - 20, SPRT.edge);
    g_text_c(SW / 2, y + 104, lv + 1 < NLV ? "A next   X retry   B levels" : "A levels   X retry", SPRT.dim);
    display_flush();
    for (;;) {
        uint32_t key = g_wait_key();
        if (key == K_A) return lv + 1 < NLV ? R_NEXT : R_LEVELS;
        if (key == K_X) return R_RETRY;
        if (key == K_B || key == K_HOME) return R_LEVELS;
    }
}

static int play_level(int lv)
{
    static const char *const PAUSE[] = {"Resume", "Back to beacon", "Restart level", "Level select", "Quit Rollo"};
    reset_level(lv);
    g_flush_input();
    uint32_t last = g_now();
    int acc = 0, jpress = 0, pause = 0;
    for (;;) {
        akira_input_event_t ev;
        while (input_poll_event(&ev, sizeof(ev)) == 1) {
            if (!ev.pressed) continue;
            if (ev.button_id == AKIRA_BTN_ID_A || ev.button_id == AKIRA_BTN_ID_UP) jpress = 1;
            else if (ev.button_id == AKIRA_BTN_ID_B || ev.button_id == 1) pause = 1;
        }
        if (pause && state != S_CLEAR) {
            pause = 0;
            draw_frame();
            int m = menu("PAUSED", PAUSE, 5, 0);
            if (m == 1 && state == S_PLAY) die();
            else if (m == 2) reset_level(lv);
            else if (m == 3) return R_LEVELS;
            else if (m == 4) return R_QUIT;
            g_flush_input();
            last = g_now();
            acc = 0;
            jpress = 0;
            continue;
        }
        pause = 0;
        uint32_t held = (uint32_t)input_get_buttons();
        int dir = ((held & AKIRA_BTN_RIGHT) ? 1 : 0) - ((held & AKIRA_BTN_LEFT) ? 1 : 0);
        int jheld = (held & (AKIRA_BTN_A | AKIRA_BTN_UP)) != 0;
        uint32_t now = g_now();
        acc += (int)(now - last) * 3;
        last = now;
        if (acc > 200) acc = 200;
        while (acc >= 50) {
            acc -= 50;
            game_step(dir, jpress, jheld);
            jpress = 0;
        }
        draw_frame();
        display_flush();
        if (state == S_CLEAR && state_t == 0) return result_screen();
        delay(1000);
    }
}

int main(void)
{
    printf("AkiraOS Rollo");
    g_init();
    SW = G_W;
    SH = G_H;
    spr_init(SW >= 400);
    spr_theme_init(150, 228, 60);
    build_sprites();
    load_progress();
    g_seed_random();
    for (;;) {
        if (title_screen() < 0) break;
        int sel = imin(unlocked - 1, NLV - 1), quit = 0;
        for (;;) {
            int lv = level_select(sel);
            if (lv < 0) break;
            for (;;) {
                int r = play_level(lv);
                if (r == R_NEXT) lv++;
                else if (r == R_RETRY) continue;
                else {
                    quit = r == R_QUIT;
                    break;
                }
            }
            sel = lv;
            if (quit) break;
        }
        if (quit) break;
    }
    app_switch("supervisor");
    return 0;
}
