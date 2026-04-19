/**
 * @file main.c
 * @brief Gravity Dash — endless runner with gravity flipping for AkiraOS
 *
 * Controls:
 *   A/B/UP = Flip gravity   SETTINGS = Pause
 *
 * Gameplay:
 *   Player runs automatically. Press A to flip gravity between
 *   floor and ceiling. Dodge obstacles (spikes, walls) that scroll
 *   from right to left. Speed increases over time. Collect gems
 *   for bonus points. Survive as long as possible.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
* @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
#define SCR_W   320
#define SCR_H   240

/* ── Buttons ─────────────────────────────────────────────────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_B        16
#define BTN_SETTINGS 2

/* ── Colours ─────────────────────────────────────────────────────────── */
#define COL_BG        0x0000
#define COL_PLAYER    0x07FF   /* cyan    */
#define COL_FLOOR     0x2104   /* dark    */
#define COL_CEIL      0x2104
#define COL_SPIKE_T   0xF800   /* red     */
#define COL_SPIKE_B   0xF800
#define COL_WALL      0x8410   /* grey    */
#define COL_GEM       0xFFE0   /* yellow  */
#define COL_TRAIL     0x0208   /* dim cyan trail */
#define COL_SCORE     0xFFFF
#define COL_LABEL     0x8410
#define COL_TITLE     0x07FF
#define COL_GAMEOVER  0xF800
#define COL_BEST      0xFFE0

/* ── Game area (inside floor/ceiling borders) ────────────────────────── */
#define BORDER_H   16
#define PLAY_Y     BORDER_H
#define PLAY_H     (SCR_H - BORDER_H * 2)

/* ── Player ──────────────────────────────────────────────────────────── */
#define P_X        40
#define P_W        12
#define P_H        12

/* ── Physics ─────────────────────────────────────────────────────────── */
#define GRAVITY       1
#define FLIP_BOOST    4
#define MAX_VEL       6

/* ── Obstacles ───────────────────────────────────────────────────────── */
#define MAX_OBS    12
#define MAX_GEMS    6
#define FRAME_US   16666   /* ~60 fps for smooth scrolling */

enum {
    OBS_SPIKE_BOTTOM = 0,
    OBS_SPIKE_TOP,
    OBS_WALL_GAP,
    OBS_TYPE_COUNT
};

typedef struct {
    int x, y, w, h, type, active;
} obstacle_t;

typedef struct {
    int x, y, active, collected;
} gem_t;

static obstacle_t obs[MAX_OBS];
static gem_t gems[MAX_GEMS];

/* ── Game state ──────────────────────────────────────────────────────── */
static int py, vy;          /* player Y position and velocity */
static int grav_dir;        /* 1 = down, -1 = up */
static int score, best_score;
static int scroll_speed;
static int frame_count;
static int spawn_timer;
static int game_over;
static int exit_to_supervisor, restart_game;

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng = 77777;
static int rng_next(int mod) {
    rng = rng * 1103515245 + 12345;
    return (int)((rng >> 16) & 0x7FFF) % mod;
}

/* ── Trail effect ────────────────────────────────────────────────────── */
#define TRAIL_LEN  6
static int trail_y[TRAIL_LEN];
static int trail_idx;

static void trail_push(int y) {
    trail_y[trail_idx] = y;
    trail_idx = (trail_idx + 1) % TRAIL_LEN;
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_game(void) {
    py = PLAY_Y + PLAY_H / 2 - P_H / 2;
    vy = 0;
    grav_dir = 1;
    score = 0;
    scroll_speed = 3;
    frame_count = 0;
    spawn_timer = 0;
    game_over = 0;

    for (int i = 0; i < MAX_OBS; i++) obs[i].active = 0;
    for (int i = 0; i < MAX_GEMS; i++) gems[i].active = 0;
    for (int i = 0; i < TRAIL_LEN; i++) trail_y[i] = py;
    trail_idx = 0;
}

/* ── Spawn obstacles ─────────────────────────────────────────────────── */
static void spawn_obstacle(void) {
    int slot = -1;
    for (int i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    int type = rng_next(OBS_TYPE_COUNT);
    obs[slot].active = 1;
    obs[slot].x = SCR_W;
    obs[slot].type = type;

    switch (type) {
    case OBS_SPIKE_BOTTOM:
        obs[slot].w = 16;
        obs[slot].h = 20 + rng_next(30);
        obs[slot].y = PLAY_Y + PLAY_H - obs[slot].h;
        break;
    case OBS_SPIKE_TOP:
        obs[slot].w = 16;
        obs[slot].h = 20 + rng_next(30);
        obs[slot].y = PLAY_Y;
        break;
    case OBS_WALL_GAP: {
        obs[slot].w = 12;
        obs[slot].h = PLAY_H;
        obs[slot].y = PLAY_Y;
        /* We'll draw it as a full wall with a gap — gap info encoded in h */
        break;
    }
    }

    /* Maybe also spawn a gem */
    if (rng_next(3) == 0) {
        for (int i = 0; i < MAX_GEMS; i++) {
            if (!gems[i].active) {
                gems[i].x = SCR_W + 30 + rng_next(40);
                gems[i].y = PLAY_Y + 20 + rng_next(PLAY_H - 40);
                gems[i].active = 1;
                gems[i].collected = 0;
                break;
            }
        }
    }
}

/* ── Collision check ─────────────────────────────────────────────────── */
static int rect_overlap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int prev_a, prev_b, prev_u, prev_s;

static int handle_input(void) {
    int a = gpio_read(BTN_A);
    int b = gpio_read(BTN_B);
    int u = gpio_read(BTN_UP);
    int s = gpio_read(BTN_SETTINGS);

    /* Flip gravity on press */
    if ((a && !prev_a) || (b && !prev_b) || (u && !prev_u)) {
        grav_dir = -grav_dir;
        vy = -grav_dir * FLIP_BOOST;
    }

    if (s && !prev_s) { prev_s = s; return 1; }

    prev_a = a; prev_b = b; prev_u = u; prev_s = s;
    return 0;
}

/* ── Update ──────────────────────────────────────────────────────────── */
static void update(void) {
    frame_count++;

    /* Score grows with time */
    if (frame_count % 3 == 0) score++;

    /* Increase speed over time */
    if (frame_count % 600 == 0 && scroll_speed < 8) scroll_speed++;

    /* Physics */
    vy += grav_dir * GRAVITY;
    if (vy > MAX_VEL) vy = MAX_VEL;
    if (vy < -MAX_VEL) vy = -MAX_VEL;
    py += vy;

    /* Clamp to play area */
    if (py < PLAY_Y) { py = PLAY_Y; vy = 0; }
    if (py + P_H > PLAY_Y + PLAY_H) { py = PLAY_Y + PLAY_H - P_H; vy = 0; }

    trail_push(py);

    /* Scroll obstacles */
    for (int i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) continue;
        obs[i].x -= scroll_speed;
        if (obs[i].x + obs[i].w < 0) { obs[i].active = 0; continue; }

        /* Collision */
        if (obs[i].type == OBS_WALL_GAP) {
            /* Wall with gap in middle */
            int gap_y = PLAY_Y + PLAY_H / 2 - 25;
            int gap_h = 50;
            /* Top part */
            if (rect_overlap(P_X, py, P_W, P_H,
                             obs[i].x, PLAY_Y, obs[i].w, gap_y - PLAY_Y))
                game_over = 1;
            /* Bottom part */
            if (rect_overlap(P_X, py, P_W, P_H,
                             obs[i].x, gap_y + gap_h, obs[i].w,
                             PLAY_Y + PLAY_H - gap_y - gap_h))
                game_over = 1;
        } else {
            if (rect_overlap(P_X, py, P_W, P_H,
                             obs[i].x, obs[i].y, obs[i].w, obs[i].h))
                game_over = 1;
        }
    }

    /* Scroll gems */
    for (int i = 0; i < MAX_GEMS; i++) {
        if (!gems[i].active) continue;
        gems[i].x -= scroll_speed;
        if (gems[i].x < -10) { gems[i].active = 0; continue; }

        /* Collect */
        if (!gems[i].collected &&
            rect_overlap(P_X, py, P_W, P_H,
                         gems[i].x, gems[i].y, 8, 8)) {
            gems[i].collected = 1;
            gems[i].active = 0;
            score += 25;
        }
    }

    /* Spawn new obstacles */
    spawn_timer++;
    int spawn_rate = 50 - scroll_speed * 3;
    if (spawn_rate < 20) spawn_rate = 20;
    if (spawn_timer >= spawn_rate) {
        spawn_timer = 0;
        spawn_obstacle();
    }
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_bg(void) {
    /* Floor and ceiling borders */
    display_rect(0, 0, SCR_W, BORDER_H, COL_FLOOR);
    display_rect(0, SCR_H - BORDER_H, SCR_W, BORDER_H, COL_CEIL);
    /* Play area background */
    display_rect(0, PLAY_Y, SCR_W, PLAY_H, COL_BG);

    /* Grid lines for depth effect */
    for (int x = 0; x < SCR_W; x += 40) {
        display_vline(x, PLAY_Y, PLAY_H, 0x0841);
    }
}

static void draw_trail(void) {
    for (int i = 0; i < TRAIL_LEN; i++) {
        int idx = (trail_idx + i) % TRAIL_LEN;
        int tx = P_X - (TRAIL_LEN - i) * 5;
        int sz = 2 + i;
        if (tx > 0)
            display_rect(tx, trail_y[idx] + P_H/2 - sz/2, sz, sz, COL_TRAIL);
    }
}

static void draw_player(void) {
    /* Player body */
    display_rect(P_X, py, P_W, P_H, COL_PLAYER);
    /* Direction indicator */
    if (grav_dir > 0) {
        /* Gravity down — triangle pointing down */
        display_rect(P_X + 3, py + P_H - 3, 6, 2, 0xFFFF);
    } else {
        /* Gravity up — triangle pointing up */
        display_rect(P_X + 3, py + 1, 6, 2, 0xFFFF);
    }
}

static void draw_obstacles(void) {
    for (int i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) continue;

        if (obs[i].type == OBS_WALL_GAP) {
            int gap_y = PLAY_Y + PLAY_H / 2 - 25;
            int gap_h = 50;
            /* Top wall */
            display_rect(obs[i].x, PLAY_Y, obs[i].w, gap_y - PLAY_Y, COL_WALL);
            /* Bottom wall */
            display_rect(obs[i].x, gap_y + gap_h, obs[i].w,
                         PLAY_Y + PLAY_H - gap_y - gap_h, COL_WALL);
        } else {
            uint16_t c = (obs[i].type == OBS_SPIKE_BOTTOM) ?
                          COL_SPIKE_B : COL_SPIKE_T;
            display_rect(obs[i].x, obs[i].y, obs[i].w, obs[i].h, c);
            /* Spike tip highlights */
            if (obs[i].type == OBS_SPIKE_BOTTOM) {
                display_rect(obs[i].x + obs[i].w/2 - 1, obs[i].y,
                             2, 4, COL_GEM);
            } else {
                display_rect(obs[i].x + obs[i].w/2 - 1,
                             obs[i].y + obs[i].h - 4, 2, 4, COL_GEM);
            }
        }
    }
}

static void draw_gems(void) {
    for (int i = 0; i < MAX_GEMS; i++) {
        if (!gems[i].active || gems[i].collected) continue;
        /* Diamond shape */
        display_rect(gems[i].x + 2, gems[i].y, 4, 8, COL_GEM);
        display_rect(gems[i].x, gems[i].y + 2, 8, 4, COL_GEM);
    }
}

static void draw_hud(void) {
    display_rect(4, 2, 120, 12, COL_FLOOR);
    display_text(4, 2, "SCORE", COL_LABEL);
    display_number(48, 2, score, COL_SCORE);

    display_rect(SCR_W - 100, 2, 96, 12, COL_FLOOR);
    display_text(SCR_W - 100, 2, "BEST", COL_LABEL);
    display_number(SCR_W - 62, 2, best_score, COL_BEST);
}

/* ── Pause menu ──────────────────────────────────────────────────────── */
enum { MENU_RESUME = 0, MENU_RESTART, MENU_EXIT, MENU_ITEMS };
static const char *menu_labels[] = { "Resume", "Restart", "Exit" };

static int show_pause_menu(void) {
    int cur = 0;
    int pu = 1, pd = 1, pa = 1, ps = 1;

    while (1) {
        display_rect(80, 60, 160, 120, 0x0000);
        display_rect_outline(80, 60, 160, 120, COL_TITLE);
        display_text(118, 70, "PAUSED", COL_TITLE);
        for (int i = 0; i < MENU_ITEMS; i++) {
            uint16_t cl = (i == cur) ? COL_TITLE : COL_LABEL;
            display_text(120, 95 + i * 18, menu_labels[i], cl);
            if (i == cur) display_text(108, 95 + i * 18, ">", cl);
        }
        display_flush();

        int u = gpio_read(BTN_UP);
        int d = gpio_read(BTN_DOWN);
        int a = gpio_read(BTN_A);
        int b = gpio_read(BTN_B);
        int s = gpio_read(BTN_SETTINGS);

        if (s && !ps) return MENU_RESUME;
        if (u && !pu) cur = (cur > 0) ? cur - 1 : MENU_ITEMS - 1;
        if (d && !pd) cur = (cur < MENU_ITEMS - 1) ? cur + 1 : 0;
        if ((a && !pa) || (b && !pa)) return cur;

        pu = u; pd = d; pa = a; ps = s;
        delay(20000);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS Gravity Dash v1.0");

    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_DOWN);

    best_score = 0;

    /* Title screen */
    display_clear(0x0000);
    display_text_large(36, 50, "GRAVITY", COL_TITLE);
    display_text_large(68, 85, "DASH", COL_GAMEOVER);
    display_text(60, 140, "Flip gravity to survive", COL_SCORE);
    display_text(84, 170, "Press A to start", COL_SCORE);
    display_text(80, 200, "AkiraOS Edition", COL_LABEL);
    display_flush();

    uint32_t seed = 1;
    while (!gpio_read(BTN_A) && !gpio_read(BTN_B)) {
        seed++;
        delay(20000);
    }
    rng = seed ^ 0xBEEFCAFE;
    rng_next(7); rng_next(7); rng_next(7);
    delay(100000);

    while (1) {
        restart_game = 0;
        exit_to_supervisor = 0;
        init_game();

        /* Game loop */
        while (!game_over) {
            if (handle_input()) {
                int choice = show_pause_menu();
                if (choice == MENU_EXIT) { exit_to_supervisor = 1; break; }
                if (choice == MENU_RESTART) { restart_game = 1; break; }
            }

            update();

            draw_bg();
            draw_trail();
            draw_obstacles();
            draw_gems();
            draw_player();
            draw_hud();
            display_flush();
            delay(FRAME_US);
        }

        if (score > best_score) best_score = score;

        if (exit_to_supervisor) {
            app_switch("supervisor");
            return 0;
        }
        if (restart_game) {
            seed++;
            rng = seed ^ 0xBEEFCAFE;
            continue;
        }

        /* Death screen */
        display_clear(0x0000);
        display_text_large(68, 50, "GAME", COL_GAMEOVER);
        display_text_large(68, 85, "OVER", COL_GAMEOVER);
        display_text(100, 130, "SCORE:", COL_LABEL);
        display_number(155, 130, score, COL_SCORE);
        display_text(100, 155, "BEST:", COL_LABEL);
        display_number(150, 155, best_score, COL_BEST);
        display_text(72, 195, "A:Retry  SETTINGS:Exit", COL_LABEL);
        display_flush();

        /* Wait for retry or exit */
        int pa2 = 1, ps2 = 1;
        while (1) {
            int a = gpio_read(BTN_A);
            int b = gpio_read(BTN_B);
            int s = gpio_read(BTN_SETTINGS);
            if ((a && !pa2) || (b && !pa2)) { restart_game = 1; break; }
            if (s && !ps2) { exit_to_supervisor = 1; break; }
            pa2 = a; ps2 = s;
            delay(20000);
        }

        if (exit_to_supervisor) {
            app_switch("supervisor");
            return 0;
        }
        if (restart_game) {
            seed++;
            rng = seed ^ 0xBEEFCAFE;
            delay(100000);
            continue;
        }
        break;
    }

    printf("Gravity Dash done!");
    return 0;
}
