/**
 * gr4v v2 — monochrome gravity-flip arcade for AkiraOS
 *
 * Controls:
 *   A / B / UP  = flip gravity    SETTINGS = pause
 *
 * Dodge barriers scrolling from the right.
 * Collect gems to build a score combo.
 *
 * Visual design: strict black-and-white only.
 * Contrast is created through fills vs. outlines, hatch patterns,
 * animation, and motion — no gray shades.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ──────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Buttons ──────────────────────────────────────────────────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_B        16
#define BTN_SETTINGS 0

/* ── Layout (computed from SCR_W/SCR_H in main) ───────────────────────── */
static int32_t HUD_H;      /* top status bar height */
static int32_t BORDER_H;   /* top / bottom play-area border */
static int32_t PLAY_Y;
static int32_t PLAY_BOT;
static int32_t PLAY_H;

/* ── Player ───────────────────────────────────────────────────────────── */
static int32_t P_X;
#define P_R    9   /* radius */

/* ── Physics ──────────────────────────────────────────────────────────── */
#define GRAVITY   1
#define FLIP_VEL  5
#define MAX_VEL   7

/* ── Mono palette (RGB565) ─────────────────────────────────────────────── */
#define BLK  0x0000
#define WHT  0xFFFF

/* ── Capacities ───────────────────────────────────────────────────────── */
#define MAX_OBS    14
#define MAX_GEMS    6
#define MAX_PARTS  40
#define TRAIL_LEN  12
#define MAX_FLOATS  4
#define NUM_DOTS   32

#define FRAME_US   16666   /* ~60 fps */

/* ── Obstacle types ───────────────────────────────────────────────────── */
enum {
    OBS_SPIKE_BOT = 0,  /* solid spike rising from the bottom */
    OBS_SPIKE_TOP,      /* solid spike hanging from the top   */
    OBS_WALL,           /* checkerboard wall with a gap       */
    OBS_DOUBLE,         /* mirrored spikes top + bottom       */
    OBS_COUNT
};

/* ── Structs ──────────────────────────────────────────────────────────── */
typedef struct { int x, y, w, h, type, active; } obstacle_t;
typedef struct { int x, y, bob, active;         } gem_t;
typedef struct { int x, y, vx, vy, life, size, active; } particle_t;
typedef struct { int x, y;                      } pt2_t;
typedef struct { int x, y, life, active; char txt[12]; } fltxt_t;
typedef struct { int x, y, spd;                 } dot_t;

/* ── Globals ──────────────────────────────────────────────────────────── */
static obstacle_t obstacles[MAX_OBS];
static gem_t      gems[MAX_GEMS];
static particle_t particles[MAX_PARTS];
static pt2_t      trail[TRAIL_LEN];
static int        trail_head;
static fltxt_t    floats[MAX_FLOATS];
static dot_t      dots[NUM_DOTS];

static int py, vy;             /* player top-Y and vertical velocity */
static int grav_dir;           /* +1 = down, -1 = up */
static int score, best_score, combo;
static int scroll_speed;
static int frame_count;
static int spawn_timer;
static int game_over;
static int exit_flag, restart_flag;
static int pulse;              /* global animation tick */
static int flip_flash;         /* remaining scan-line flash frames on flip */
static int shake_x, shake_y, shake_life;

/* ── PRNG ─────────────────────────────────────────────────────────────── */
static uint32_t rng_state = 0xDEADBEEF;

static int rng_next(int mod) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (int)((rng_state >> 16) & 0x7FFF) % mod;
}

/* ── Helpers ──────────────────────────────────────────────────────────── */
static int iclamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* ── Scrolling dot background ─────────────────────────────────────────── */
static void init_dots(void) {
    for (int i = 0; i < NUM_DOTS; i++) {
        dots[i].x   = rng_next(SCR_W);
        dots[i].y   = PLAY_Y + rng_next(PLAY_H);
        dots[i].spd = 1 + rng_next(3);
    }
}

static void update_dots(void) {
    for (int i = 0; i < NUM_DOTS; i++) {
        dots[i].x -= dots[i].spd;
        if (dots[i].x < 0) {
            dots[i].x = SCR_W;
            dots[i].y = PLAY_Y + rng_next(PLAY_H);
        }
    }
}

static void draw_dots(void) {
    for (int i = 0; i < NUM_DOTS; i++) {
        int dx = dots[i].x + shake_x;
        int dy = dots[i].y + shake_y;
        if (dx < 0 || dx >= SCR_W || dy < PLAY_Y || dy >= PLAY_BOT) continue;
        display_pixel(dx, dy, WHT);
        if (dots[i].spd == 3 && dx > 0)  /* fast dots streak one extra pixel */
            display_pixel(dx - 1, dy, WHT);
    }
}

/* ── Particle system ──────────────────────────────────────────────────── */
static void spawn_particle(int x, int y, int vx, int vy, int life, int size) {
    for (int i = 0; i < MAX_PARTS; i++) {
        if (!particles[i].active) {
            particles[i].x      = x;
            particles[i].y      = y;
            particles[i].vx     = vx;
            particles[i].vy     = vy;
            particles[i].life   = life;
            particles[i].size   = size;
            particles[i].active = 1;
            return;
        }
    }
}

static void burst(int x, int y, int n) {
    for (int i = 0; i < n; i++) {
        int vx  = rng_next(9) - 4;
        int vy2 = rng_next(9) - 4;
        spawn_particle(x, y, vx, vy2, 16 + rng_next(20), 1 + rng_next(3));
    }
}

static void update_particles(void) {
    for (int i = 0; i < MAX_PARTS; i++) {
        if (!particles[i].active) continue;
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        if (--particles[i].life <= 0) particles[i].active = 0;
    }
}

static void draw_particles(void) {
    for (int i = 0; i < MAX_PARTS; i++) {
        if (!particles[i].active) continue;
        int px = particles[i].x + shake_x;
        int py2 = particles[i].y + shake_y;
        if (px < 0 || px >= SCR_W || py2 < 0 || py2 >= SCR_H) continue;
        int s = particles[i].size;
        display_rect(px - s / 2, py2 - s / 2, s, s, WHT);
    }
}

/* ── Motion trail ─────────────────────────────────────────────────────── */
static void trail_push(int x, int y) {
    trail[trail_head].x = x;
    trail[trail_head].y = y;
    trail_head = (trail_head + 1) % TRAIL_LEN;
}

static void draw_trail(void) {
    for (int i = 0; i < TRAIL_LEN; i++) {
        int idx = (trail_head + i) % TRAIL_LEN;
        int tx = trail[idx].x + shake_x;
        int ty = trail[idx].y + shake_y;
        if (tx < 0 || tx >= SCR_W || ty < 0 || ty >= SCR_H) continue;
        int r = i / 4;
        if (r > 0)
            display_circle(tx, ty, r, WHT);
        else
            display_pixel(tx, ty, WHT);
    }
}

/* ── Floating score text ──────────────────────────────────────────────── */
static void spawn_float(int x, int y, const char *txt) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (!floats[i].active) {
            floats[i].x      = x;
            floats[i].y      = y;
            floats[i].life   = 50;
            floats[i].active = 1;
            int j = 0;
            while (txt[j] && j < 11) { floats[i].txt[j] = txt[j]; j++; }
            floats[i].txt[j] = 0;
            return;
        }
    }
}

static void update_floats(void) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (!floats[i].active) continue;
        if (frame_count % 3 == 0) floats[i].y--;
        if (--floats[i].life <= 0) floats[i].active = 0;
    }
}

static void draw_floats(void) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (!floats[i].active) continue;
        display_text(floats[i].x, floats[i].y, floats[i].txt, WHT);
    }
}

/* ── Player ───────────────────────────────────────────────────────────── */
static void draw_player(void) {
    int cx = P_X + shake_x;
    int cy = py + P_R + shake_y;

    /* Pulsing outer ring: expands and contracts on an 8-frame cycle */
    int ring_r = P_R + 3 + (pulse % 16 < 8 ? pulse % 8 : 7 - pulse % 8);
    display_circle(cx, cy, ring_r, WHT);

    /* Filled circle body */
    display_circle_fill(cx, cy, P_R, WHT);

    /* Black hollow core so the body reads clearly */
    display_circle_fill(cx, cy, P_R / 3, BLK);

    /* Large filled arrow showing current gravity direction */
    if (grav_dir > 0) {
        /* Gravity pulls down: arrow below the player */
        int ay = cy + P_R + 4;
        display_triangle_fill(cx - 7, ay, cx + 7, ay, cx, ay + 13, WHT);
    } else {
        /* Gravity pulls up: arrow above the player */
        int ay = cy - P_R - 4;
        display_triangle_fill(cx - 7, ay, cx + 7, ay, cx, ay - 13, WHT);
    }
}

/* ── Game init ────────────────────────────────────────────────────────── */
static void init_game(void) {
    py          = PLAY_Y + PLAY_H / 2 - P_R;
    vy          = 0;
    grav_dir    = 1;
    score       = 0;
    combo       = 1;
    scroll_speed = 3;
    frame_count = 0;
    spawn_timer = 0;
    game_over   = 0;
    pulse       = 0;
    flip_flash  = 0;
    shake_x = shake_y = shake_life = 0;

    for (int i = 0; i < MAX_OBS;    i++) obstacles[i].active  = 0;
    for (int i = 0; i < MAX_GEMS;   i++) { gems[i].active = 0; gems[i].bob = 0; }
    for (int i = 0; i < MAX_PARTS;  i++) particles[i].active = 0;
    for (int i = 0; i < MAX_FLOATS; i++) floats[i].active    = 0;
    for (int i = 0; i < TRAIL_LEN;  i++) { trail[i].x = P_X; trail[i].y = py + P_R; }
    trail_head = 0;
    init_dots();
}

/* ── Spawn obstacle ───────────────────────────────────────────────────── */
static void spawn_obstacle(void) {
    int slot = -1;
    for (int i = 0; i < MAX_OBS; i++) if (!obstacles[i].active) { slot = i; break; }
    if (slot < 0) return;

    int type = rng_next(OBS_COUNT);
    obstacles[slot].active = 1;
    obstacles[slot].x      = SCR_W + 10;
    obstacles[slot].type   = type;

    switch (type) {
    case OBS_SPIKE_BOT:
        obstacles[slot].w = 20;
        obstacles[slot].h = 30 + rng_next(48);
        obstacles[slot].y = PLAY_BOT - obstacles[slot].h;
        break;
    case OBS_SPIKE_TOP:
        obstacles[slot].w = 20;
        obstacles[slot].h = 30 + rng_next(48);
        obstacles[slot].y = PLAY_Y;
        break;
    case OBS_WALL:
        obstacles[slot].w = 14;
        obstacles[slot].h = PLAY_H;
        obstacles[slot].y = PLAY_Y;
        break;
    case OBS_DOUBLE:
        obstacles[slot].w = 20;
        obstacles[slot].h = 22 + rng_next(30);
        obstacles[slot].y = PLAY_Y;
        break;
    }

    /* Maybe also spawn a gem after this obstacle */
    if (rng_next(3) == 0) {
        for (int i = 0; i < MAX_GEMS; i++) {
            if (!gems[i].active) {
                gems[i].x      = SCR_W + 40 + rng_next(40);
                gems[i].y      = PLAY_Y + 30 + rng_next(PLAY_H - 60);
                gems[i].bob    = rng_next(64);
                gems[i].active = 1;
                break;
            }
        }
    }
}

/* ── Collision (circle vs rect) ───────────────────────────────────────── */
static int circle_hits_rect(int cx, int cy, int r,
                             int rx, int ry, int rw, int rh) {
    int nx = iclamp(cx, rx, rx + rw);
    int ny = iclamp(cy, ry, ry + rh);
    int dx = cx - nx, dy2 = cy - ny;
    return dx * dx + dy2 * dy2 <= r * r;
}

/* ── Input ────────────────────────────────────────────────────────────── */
static int prev_a, prev_b, prev_u, prev_s;

static int handle_input(void) {
    int a = gpio_read(BTN_A);
    int b = gpio_read(BTN_B);
    int u = gpio_read(BTN_UP);
    int s = gpio_read(BTN_SETTINGS);

    if ((a && !prev_a) || (b && !prev_b) || (u && !prev_u)) {
        grav_dir   = -grav_dir;
        vy         = -grav_dir * FLIP_VEL;
        flip_flash = 6;
        burst(P_X, py + P_R, 10);
    }

    if (s && !prev_s) { prev_s = s; return 1; }
    prev_a = a; prev_b = b; prev_u = u; prev_s = s;
    return 0;
}

/* ── Update ───────────────────────────────────────────────────────────── */
static void update(void) {
    frame_count++;
    pulse++;

    /* Screen shake decay */
    if (shake_life > 0) {
        shake_life--;
        shake_x = rng_next(5) - 2;
        shake_y = rng_next(5) - 2;
    } else {
        shake_x = shake_y = 0;
    }

    /* Score ticks up with combo multiplier */
    if (frame_count % 3 == 0) score += combo;

    /* Speed ramps up over time */
    if (frame_count % 600 == 0 && scroll_speed < 9) scroll_speed++;

    /* Physics */
    vy += grav_dir * GRAVITY;
    vy  = iclamp(vy, -MAX_VEL, MAX_VEL);
    py += vy;

    /* Clamp player inside play area (allow for border) */
    int ceil_limit  = PLAY_Y + BORDER_H;
    int floor_limit = PLAY_BOT - BORDER_H - P_R * 2;
    if (py <= ceil_limit)  { py = ceil_limit;  vy = 0; }
    if (py >= floor_limit) { py = floor_limit; vy = 0; }

    trail_push(P_X, py + P_R);
    update_particles();
    update_floats();
    update_dots();

    int cx = P_X, cy = py + P_R;

    /* Scroll and collide obstacles */
    for (int i = 0; i < MAX_OBS; i++) {
        if (!obstacles[i].active) continue;
        obstacles[i].x -= scroll_speed;
        if (obstacles[i].x + obstacles[i].w < 0) { obstacles[i].active = 0; continue; }

        int ox = obstacles[i].x, ow = obstacles[i].w;
        int oh = obstacles[i].h;

        if (obstacles[i].type == OBS_WALL) {
            int gap_y = PLAY_Y + PLAY_H / 2 - PLAY_H * 15 / 100;
            int gap_h = PLAY_H * 30 / 100;
            if (circle_hits_rect(cx, cy, P_R, ox, PLAY_Y, ow, gap_y - PLAY_Y))
                game_over = 1;
            if (circle_hits_rect(cx, cy, P_R, ox, gap_y + gap_h,
                                 ow, PLAY_BOT - gap_y - gap_h))
                game_over = 1;
        } else if (obstacles[i].type == OBS_DOUBLE) {
            if (circle_hits_rect(cx, cy, P_R, ox, PLAY_Y,          ow, oh)) game_over = 1;
            if (circle_hits_rect(cx, cy, P_R, ox, PLAY_BOT - oh,   ow, oh)) game_over = 1;
        } else {
            if (circle_hits_rect(cx, cy, P_R, ox, obstacles[i].y, ow, oh))
                game_over = 1;
        }
    }

    /* Scroll and collect gems */
    for (int i = 0; i < MAX_GEMS; i++) {
        if (!gems[i].active) continue;
        gems[i].x  -= scroll_speed;
        gems[i].bob = (gems[i].bob + 2) % 128;
        if (gems[i].x < -20) { gems[i].active = 0; combo = 1; continue; }

        int bob = gems[i].bob < 64 ? gems[i].bob / 10 : (128 - gems[i].bob) / 10;
        int gy  = gems[i].y + bob;

        if (circle_hits_rect(cx, cy, P_R + 5, gems[i].x - 6, gy - 6, 12, 12)) {
            gems[i].active = 0;
            combo = iclamp(combo + 1, 1, 8);
            int pts = 50 * combo;
            score += pts;
            burst(gems[i].x, gy, 14);

            /* Build "+NNN" string */
            char buf[8];
            int p = 0;
            buf[p++] = '+';
            int v = pts;
            if (v >= 100) buf[p++] = '0' + v / 100;
            if (v >=  10) buf[p++] = '0' + (v / 10) % 10;
            buf[p++] = '0' + v % 10;
            buf[p]   = 0;
            spawn_float(gems[i].x - 12, gy - 14, buf);
        }
    }

    /* Spawn new obstacle */
    spawn_timer++;
    int rate = 55 - scroll_speed * 3;
    if (rate < 22) rate = 22;
    if (spawn_timer >= rate) { spawn_timer = 0; spawn_obstacle(); }

    /* On death: big burst + shake */
    if (game_over) {
        burst(cx, cy, 20);
        burst(cx, cy, 15);
        shake_life = 24;
    }
}

/* ── Draw background ──────────────────────────────────────────────────── */
static void draw_bg(void) {
    display_rect(0, PLAY_Y, SCR_W, PLAY_H, BLK);
    draw_dots();
}

/* ── Draw borders ─────────────────────────────────────────────────────── */
static void draw_borders(void) {
    /* HUD bar */
    display_rect(0, 0, SCR_W, HUD_H, BLK);

    /*
     * The border on the side the player is attracted toward pulses:
     * it briefly becomes 2px thicker every 8 frames, drawing attention.
     */
    int top_extra = (grav_dir < 0 && (pulse % 8) < 4) ? 2 : 0;
    int bot_extra = (grav_dir > 0 && (pulse % 8) < 4) ? 2 : 0;

    int top_h = BORDER_H + top_extra;
    int bot_h = BORDER_H + bot_extra;

    /* Solid white strips */
    display_rect(shake_x, HUD_H            + shake_y, SCR_W, top_h, WHT);
    display_rect(shake_x, PLAY_BOT         + shake_y, SCR_W, bot_h, WHT);

    /* Horizontal hatch lines inside borders to add texture */
    for (int y = 2; y < top_h - 2; y += 3)
        display_hline(shake_x, HUD_H + y + shake_y, SCR_W, BLK);
    for (int y = 2; y < bot_h - 2; y += 3)
        display_hline(shake_x, PLAY_BOT + y + shake_y, SCR_W, BLK);

    /* Separator line between HUD and top border */
    display_hline(0, HUD_H - 1, SCR_W, WHT);
}

/* ── Draw spike obstacle ──────────────────────────────────────────────── */
/*
 * from_top == 1: body at top, spike tip points downward.
 * from_top == 0: body at bottom, spike tip points upward.
 */
static void draw_spike(int x, int y, int w, int h, int from_top) {
    int sx = x + shake_x;
    int sy = y + shake_y;

    /* Solid body */
    display_rect(sx, sy, w, h, WHT);

    /* Horizontal hatch lines every 4px: gives "dangerous machinery" look */
    for (int row = 4; row < h - 2; row += 4)
        display_hline(sx, sy + row, w, BLK);

    /* Crisp outline */
    display_rect_outline(sx, sy, w, h, WHT);

    /* Triangular spike tip */
    if (from_top) {
        /* Tip hangs DOWN below the body */
        display_triangle_fill(sx, sy + h, sx + w, sy + h,
                              sx + w / 2, sy + h + 14, WHT);
        display_line(sx,       sy + h, sx + w / 2, sy + h + 14, WHT);
        display_line(sx + w,   sy + h, sx + w / 2, sy + h + 14, WHT);
    } else {
        /* Tip points UP above the body */
        display_triangle_fill(sx, sy, sx + w, sy,
                              sx + w / 2, sy - 14, WHT);
        display_line(sx,       sy, sx + w / 2, sy - 14, WHT);
        display_line(sx + w,   sy, sx + w / 2, sy - 14, WHT);
    }
}

/* ── Draw wall obstacle (checkerboard fill + gap) ─────────────────────── */
static void draw_wall_section(int sx, int sy, int w, int h) {
    /* Solid fill */
    display_rect(sx, sy, w, h, WHT);
    /* Checkerboard cutout: 4×4 black squares in alternating offset rows */
    for (int row = 0; row < h; row += 4) {
        int col_off = ((row / 4) % 2) * 4;
        for (int col = col_off; col < w; col += 8)
            display_rect(sx + col, sy + row, 4, 4, BLK);
    }
    display_rect_outline(sx, sy, w, h, WHT);
}

static void draw_wall(int x, int w) {
    int gap_y = PLAY_Y + PLAY_H / 2 - PLAY_H * 15 / 100;
    int gap_h = PLAY_H * 30 / 100;
    int sx    = x + shake_x;

    draw_wall_section(sx, PLAY_Y + shake_y,     w, gap_y - PLAY_Y);
    draw_wall_section(sx, gap_y + gap_h + shake_y, w, PLAY_BOT - gap_y - gap_h);

    /* Gap boundary lines to make the passage obvious */
    display_hline(sx, gap_y          + shake_y, w, WHT);
    display_hline(sx, gap_y + gap_h  + shake_y, w, WHT);
}

/* ── Draw all obstacles ───────────────────────────────────────────────── */
static void draw_obstacles(void) {
    for (int i = 0; i < MAX_OBS; i++) {
        if (!obstacles[i].active) continue;
        int x = obstacles[i].x;
        int y = obstacles[i].y;
        int w = obstacles[i].w;
        int h = obstacles[i].h;

        switch (obstacles[i].type) {
        case OBS_SPIKE_BOT: draw_spike(x, y, w, h, 0); break;
        case OBS_SPIKE_TOP: draw_spike(x, y, w, h, 1); break;
        case OBS_WALL:      draw_wall(x, w);            break;
        case OBS_DOUBLE:
            draw_spike(x, PLAY_Y,      w, h, 1);
            draw_spike(x, PLAY_BOT - h, w, h, 0);
            break;
        }
    }
}

/* ── Draw gems ────────────────────────────────────────────────────────── */
static void draw_gems(void) {
    for (int i = 0; i < MAX_GEMS; i++) {
        if (!gems[i].active) continue;
        int bob = gems[i].bob < 64 ? gems[i].bob / 10 : (128 - gems[i].bob) / 10;
        int gx  = gems[i].x  + shake_x;
        int gy  = gems[i].y  + bob + shake_y;

        /* Blink: solid fill on even pulse cycles */
        if ((pulse / 6) % 2 == 0) {
            display_triangle_fill(gx, gy - 8, gx - 7, gy, gx + 7, gy, WHT);
            display_triangle_fill(gx - 7, gy, gx + 7, gy, gx, gy + 8, WHT);
            /* Black cross inside for sparkle */
            display_pixel(gx,     gy - 2, BLK);
            display_pixel(gx,     gy + 2, BLK);
            display_pixel(gx - 2, gy,     BLK);
            display_pixel(gx + 2, gy,     BLK);
        }
        /* Always draw outline (visible even when blink is "off") */
        display_triangle(gx, gy - 8, gx - 7, gy, gx + 7, gy, WHT);
        display_triangle(gx - 7, gy, gx + 7, gy, gx, gy + 8, WHT);
    }
}

/* ── Draw HUD ─────────────────────────────────────────────────────────── */
static void draw_hud(void) {
    /* Score */
    display_text(4, 4, "SC", WHT);
    display_number(22, 4, score, WHT);

    /* Best */
    display_text(SCR_W - SCR_W * 29 / 100, 4, "HI", WHT);
    display_number(SCR_W - SCR_W * 24 / 100, 4, best_score, WHT);

    /* Speed bar: empty rect filled proportionally */
    int bx = SCR_W * 44 / 100, by = 7, bw = SCR_W * 9 / 100, bh = 6;
    display_rect_outline(bx, by, bw, bh, WHT);
    int fill_w = (scroll_speed - 3) * (bw - 2) / 6;
    if (fill_w > 0)
        display_rect(bx + 1, by + 1, fill_w, bh - 2, WHT);

    /* Combo badge: shown only when active */
    if (combo > 1) {
        display_text(SCR_W * 57 / 100, 4, "x", WHT);
        display_number(SCR_W * 60 / 100, 4, combo, WHT);
    }

    /* Compact gravity direction icon in HUD */
    int ix = SCR_W * 375 / 1000, iy = 5;
    if (grav_dir > 0) {
        display_triangle_fill(ix, iy + 2, ix + 8, iy + 2, ix + 4, iy + 11, WHT);
    } else {
        display_triangle_fill(ix, iy + 9, ix + 8, iy + 9, ix + 4, iy, WHT);
    }
}

/* ── Pause menu ───────────────────────────────────────────────────────── */
enum { MENU_RESUME = 0, MENU_RESTART, MENU_EXIT, MENU_N };
static const char *menu_labels[] = { "RESUME", "RESTART", "EXIT" };

static int show_pause_menu(void) {
    int cur = 0;
    int pu = 1, pd = 1, pa = 1, ps = 1;

    int box_w = SCR_W * 625 / 1000, box_h = SCR_H * 617 / 1000;
    int box_x = (SCR_W - box_w) / 2, box_y = (SCR_H - box_h) / 2;

    while (1) {
        display_rect(box_x, box_y, box_w, box_h, BLK);
        display_rect_outline(box_x, box_y, box_w, box_h, WHT);
        display_rect_outline(box_x + 2, box_y + 2, box_w - 4, box_h - 4, WHT);

        display_text_large(box_x + 28, box_y + 12, "PAUSED", WHT);
        display_hline(box_x + 2, box_y + 34, box_w - 4, WHT);

        for (int i = 0; i < MENU_N; i++) {
            int row_y = box_y + 47 + i * 28;
            if (i == cur) {
                /* Selected item: inverted (black text on white bar) */
                display_rect(box_x + 4, row_y, box_w - 8, 22, WHT);
                display_text(box_x + 20, row_y + 6, menu_labels[i], BLK);
                display_text(box_x + 8, row_y + 6, ">", BLK);
            } else {
                display_text(box_x + 20, row_y + 6, menu_labels[i], WHT);
            }
        }

        display_flush();

        int u = gpio_read(BTN_UP);
        int d = gpio_read(BTN_DOWN);
        int a = gpio_read(BTN_A);
        int b = gpio_read(BTN_B);
        int s = gpio_read(BTN_SETTINGS);

        if (s && !ps) return MENU_RESUME;
        if (u && !pu) cur = cur > 0 ? cur - 1 : MENU_N - 1;
        if (d && !pd) cur = cur < MENU_N - 1 ? cur + 1 : 0;
        if ((a && !pa) || (b && !pa)) return cur;

        pu = u; pd = d; pa = a; ps = s;
        delay(20000);
    }
}

/* ── Title screen ─────────────────────────────────────────────────────── */
static void show_title(void) {
    int t    = 0;
    int prev = 1;  /* debounce start button */

    while (1) {
        int a = gpio_read(BTN_A);
        int b = gpio_read(BTN_B);
        if ((a || b) && !prev) break;
        prev = (a || b);

        display_rect(0, 0, SCR_W, SCR_H, BLK);
        draw_dots();

        /* Animated double-border that pulses in and out */
        int bw = 2 + (t % 16 < 8 ? t % 8 : 7 - t % 8);
        display_rect_outline(bw,     bw,     SCR_W - bw * 2,     SCR_H - bw * 2,     WHT);
        display_rect_outline(bw + 3, bw + 3, SCR_W - bw * 2 - 6, SCR_H - bw * 2 - 6, WHT);

        /* Big title */
        display_text_large(SCR_W * 26 / 100, SCR_H * 22 / 100, "gr4v", WHT);
        display_hline(SCR_W * 24 / 100, SCR_H * 375 / 1000, SCR_W * 525 / 1000, WHT);

        display_text(SCR_W * 21 / 100, SCR_H * 425 / 1000, "GRAVITY ARCADE", WHT);

        /* Controls */
        display_text(SCR_W * 125 / 1000, SCR_H * 525 / 1000, "A / UP   =   FLIP GRAVITY", WHT);
        display_text(SCR_W * 125 / 1000, SCR_H * 591 / 1000, "Dodge barriers, collect gems", WHT);

        /* Animated demo player bouncing on the right side */
        int dc_x = SCR_W * 775 / 1000;
        int phase = t % 40;
        int dc_y_base = SCR_H * 54 / 100;
        int dc_y  = dc_y_base + (phase < 20 ? phase * 3 : (40 - phase) * 3);
        int rring = 12 + (t % 16 < 8 ? t % 8 : 7 - t % 8);
        display_circle(dc_x, dc_y, rring, WHT);
        display_circle_fill(dc_x, dc_y, 10, WHT);
        display_circle_fill(dc_x, dc_y,  3, BLK);
        /* Arrow indicating current demo gravity direction */
        if (dc_y <= dc_y_base) {
            display_triangle_fill(dc_x - 6, dc_y + 14, dc_x + 6, dc_y + 14,
                                  dc_x, dc_y + 22, WHT);
        } else {
            display_triangle_fill(dc_x - 6, dc_y - 14, dc_x + 6, dc_y - 14,
                                  dc_x, dc_y - 22, WHT);
        }

        /* Blinking "PRESS A TO START" */
        if ((t / 20) % 2 == 0)
            display_text(SCR_W * 28 / 100, SCR_H * 825 / 1000, "PRESS A TO START", WHT);

        display_flush();
        update_dots();

        rng_state++;  /* accumulate entropy while user waits */
        t++;
        delay(20000);
    }
}

/* ── Entry point ──────────────────────────────────────────────────────── */
int main(void) {
    printf("gr4v v2 starting");

    display_get_size(&SCR_W, &SCR_H);
    HUD_H    = SCR_H * 9 / 100;
    BORDER_H = SCR_H * 4 / 100;
    PLAY_Y   = HUD_H + BORDER_H;
    PLAY_BOT = SCR_H - BORDER_H;
    PLAY_H   = PLAY_BOT - PLAY_Y;
    P_X      = SCR_W * 15 / 100;

    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    best_score = 0;
    init_dots();
    show_title();
    rng_state ^= 0x9E3779B9u;  /* fold title-time entropy into PRNG */

    while (1) {
        exit_flag = restart_flag = 0;
        init_game();

        /* ── Main game loop ── */
        while (!game_over) {
            if (handle_input()) {
                int choice = show_pause_menu();
                if (choice == MENU_EXIT)    { exit_flag    = 1; break; }
                if (choice == MENU_RESTART) { restart_flag = 1; break; }
            }

            update();

            draw_bg();
            draw_borders();
            draw_trail();
            draw_obstacles();
            draw_gems();
            draw_particles();
            draw_player();
            draw_floats();

            /* Scan-line flash on gravity flip: every-4th-row white overlay */
            if (flip_flash > 0) {
                flip_flash--;
                for (int fy = PLAY_Y; fy < PLAY_BOT; fy += 4)
                    display_hline(0, fy, SCR_W, WHT);
            }

            draw_hud();
            display_flush();
            delay(FRAME_US);
        }

        if (score > best_score) best_score = score;
        if (exit_flag)    { app_switch("supervisor"); return 0; }
        if (restart_flag) { rng_state ^= 0xBEEFu; continue; }

        /* ── Post-death: particle frames before game-over screen ── */
        for (int f = 0; f < 60; f++) {
            update_particles();
            if (shake_life > 0) {
                shake_life--;
                shake_x = rng_next(5) - 2;
                shake_y = rng_next(5) - 2;
            } else {
                shake_x = shake_y = 0;
            }
            draw_bg();
            draw_borders();
            draw_particles();
            display_flush();
            delay(FRAME_US);
        }

        /* ── Game-over screen ── */
        for (int f = 0; f < 300; f++) {
            display_rect(0, 0, SCR_W, SCR_H, BLK);
            draw_dots();

            /* Pulsing border */
            int bw = (f % 20 < 10) ? 2 : 3;
            display_rect_outline(bw, bw, SCR_W - bw * 2, SCR_H - bw * 2, WHT);

            display_text_large(SCR_W * 23 / 100, SCR_H * 158 / 1000, "GAME OVER", WHT);
            display_hline(SCR_W * 21 / 100, SCR_H * 308 / 1000, SCR_W * 575 / 1000, WHT);

            /* Score panel */
            display_rect_outline(SCR_W * 21 / 100, SCR_H * 35 / 100, SCR_W * 575 / 1000, SCR_H * 267 / 1000, WHT);
            display_text(SCR_W * 244 / 1000, SCR_H * 383 / 1000,  "SCORE", WHT);
            display_number(SCR_W * 425 / 1000, SCR_H * 383 / 1000, score,      WHT);
            display_text(SCR_W * 244 / 1000, SCR_H * 458 / 1000, "BEST",  WHT);
            display_number(SCR_W * 406 / 1000, SCR_H * 458 / 1000, best_score, WHT);

            if (score >= best_score && score > 0) {
                if ((f / 14) % 2 == 0) {
                    display_text(SCR_W * 256 / 1000, SCR_H * 542 / 1000, "** NEW BEST! **", WHT);
                }
            }

            display_hline(SCR_W * 21 / 100, SCR_H * 642 / 1000, SCR_W * 575 / 1000, WHT);

            if ((f / 18) % 2 == 0)
                display_text(SCR_W * 13 / 100, SCR_H * 675 / 1000, "A: RETRY   SETTINGS: EXIT", WHT);

            display_flush();
            update_dots();

            int a = gpio_read(BTN_A);
            int b = gpio_read(BTN_B);
            int s = gpio_read(BTN_SETTINGS);
            if (a || b) { restart_flag = 1; break; }
            if (s)      { exit_flag    = 1; break; }
            delay(FRAME_US);
        }

        /* Auto-retry on timeout */
        if (!restart_flag && !exit_flag) restart_flag = 1;

        if (exit_flag) { app_switch("supervisor"); return 0; }
        rng_state ^= 0xCAFEu;
        delay(80000);
    }

    return 0;
}
