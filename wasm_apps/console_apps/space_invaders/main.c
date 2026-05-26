/**
 * @file main.c
 * @brief Space Invaders game for AkiraOS
 *
 * Controls (akiraconsole DPAD):
 *   LEFT/RIGHT = Move ship   A = Shoot   SETTINGS = Pause
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
* @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
#define SCR_W          320
#define SCR_H          240

/* ── Button pins ─────────────────────────────────────────────────────── */
#define BTN_UP         4
#define BTN_DOWN       5
#define BTN_LEFT       6
#define BTN_RIGHT      7
#define BTN_A          15
#define BTN_B          16
#define BTN_SETTINGS   0   /* BTN.OK = GPIO0, active-low pull-up */

/* ── Colours ─────────────────────────────────────────────────────────── */
#define COL_BG         0x0000   /* black   */
#define COL_SHIP       0x07FF   /* cyan    */
#define COL_BULLET     0xFFFF   /* white   */
#define COL_ALIEN_1    0x07E0   /* green   */
#define COL_ALIEN_2    0xFFE0   /* yellow  */
#define COL_ALIEN_3    0xF800   /* red     */
#define COL_SHIELD     0x2589   /* teal    */
#define COL_ABOMB      0xFBE0   /* orange  */
#define COL_SCORE      0xFFFF   /* white   */
#define COL_LABEL      0x8410   /* dim grey */
#define COL_TITLE      0x07FF   /* cyan     */
#define COL_GAMEOVER   0xF800   /* red      */
#define COL_EXPLOSION  0xFD20   /* bright orange */

/* ── Game tuning ─────────────────────────────────────────────────────── */
#define FRAME_US       20000    /* ~50 fps */

#define SHIP_Y         222
#define SHIP_W         16
#define SHIP_H         8
#define SHIP_SPEED     3

#define BULLET_SPEED   4
#define BULLET_W       2
#define BULLET_H       6
#define MAX_BULLETS    4

#define ALIEN_COLS     11
#define ALIEN_ROWS     5
#define ALIEN_W        12
#define ALIEN_H        8
#define ALIEN_PAD_X    4
#define ALIEN_PAD_Y    4
#define ALIEN_TOTAL    (ALIEN_COLS * ALIEN_ROWS)
#define ALIEN_AREA_W   (ALIEN_COLS * (ALIEN_W + ALIEN_PAD_X))

#define BOMB_SPEED     2
#define BOMB_W         2
#define BOMB_H         6
#define MAX_BOMBS      4
#define BOMB_CHANCE    60       /* 1-in-N chance per frame per alive col */

#define SHIELD_COUNT   4
#define SHIELD_W       24
#define SHIELD_H       12
#define SHIELD_Y       200

#define MYSTERY_W      16
#define MYSTERY_H      6
#define MYSTERY_Y      18
#define MYSTERY_SPEED  1
#define MYSTERY_CHANCE 800      /* 1-in-N per frame */

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng = 12345;
static int rng_next(int mod) {
    rng = rng * 1103515245 + 12345;
    return (int)((rng >> 16) & 0x7FFF) % mod;
}

/* ── Bullet ──────────────────────────────────────────────────────────── */
typedef struct { int x, y, active; } bullet_t;
static bullet_t bullets[MAX_BULLETS];

/* ── Bomb (alien projectile) ─────────────────────────────────────────── */
typedef struct { int x, y, active; } bomb_t;
static bomb_t bombs[MAX_BOMBS];

/* ── Aliens ──────────────────────────────────────────────────────────── */
static int alien_alive[ALIEN_ROWS][ALIEN_COLS];
static int alien_base_x, alien_base_y;
static int alien_dir;           /* 1 = right, -1 = left */
static int alien_move_timer;
static int alien_move_delay;    /* frames between alien steps */
static int alien_step_x;        /* pixels per horizontal step */
static int aliens_remaining;

/* ── Mystery ship ────────────────────────────────────────────────────── */
static int mystery_x, mystery_active, mystery_dir;

/* ── Shields ─────────────────────────────────────────────────────────── */
/* Each shield is a pixel grid; we track health per 4×4 block */
#define SH_COLS  (SHIELD_W / 4)
#define SH_ROWS  (SHIELD_H / 4)
static uint8_t shield_hp[SHIELD_COUNT][SH_ROWS][SH_COLS]; /* 0=destroyed */

/* ── Ship ────────────────────────────────────────────────────────────── */
static int ship_x;

/* ── Game state ──────────────────────────────────────────────────────── */
static int score, lives, wave, game_over;
static int exit_to_supervisor, restart_game;

/* ── Explosion effect ────────────────────────────────────────────────── */
#define MAX_EXPLOSIONS 8
typedef struct { int x, y, timer; } explosion_t;
static explosion_t explosions[MAX_EXPLOSIONS];

static void add_explosion(int x, int y) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].timer <= 0) {
            explosions[i].x = x;
            explosions[i].y = y;
            explosions[i].timer = 6;
            return;
        }
    }
}

/* ── Shield helpers ──────────────────────────────────────────────────── */
static int shield_base_x(int idx) {
    /* Evenly distribute shields across screen width */
    int spacing = SCR_W / (SHIELD_COUNT + 1);
    return spacing * (idx + 1) - SHIELD_W / 2;
}

static void init_shields(void) {
    for (int s = 0; s < SHIELD_COUNT; s++)
        for (int r = 0; r < SH_ROWS; r++)
            for (int c = 0; c < SH_COLS; c++)
                shield_hp[s][r][c] = 3;
}

static void draw_shields(void) {
    for (int s = 0; s < SHIELD_COUNT; s++) {
        int bx = shield_base_x(s);
        for (int r = 0; r < SH_ROWS; r++) {
            for (int c = 0; c < SH_COLS; c++) {
                uint16_t col;
                if (shield_hp[s][r][c] == 0)
                    col = COL_BG;
                else
                    col = COL_SHIELD;
                display_rect(bx + c * 4, SHIELD_Y + r * 4, 4, 4, col);
            }
        }
    }
}

/* Damage a shield block at pixel position; returns 1 if hit shield */
static int damage_shield(int px, int py) {
    for (int s = 0; s < SHIELD_COUNT; s++) {
        int bx = shield_base_x(s);
        if (px >= bx && px < bx + SHIELD_W &&
            py >= SHIELD_Y && py < SHIELD_Y + SHIELD_H) {
            int c = (px - bx) / 4;
            int r = (py - SHIELD_Y) / 4;
            if (c >= 0 && c < SH_COLS && r >= 0 && r < SH_ROWS &&
                shield_hp[s][r][c] > 0) {
                shield_hp[s][r][c]--;
                return 1;
            }
        }
    }
    return 0;
}

/* ── Alien helpers ───────────────────────────────────────────────────── */
static uint16_t alien_color(int row) {
    if (row == 0) return COL_ALIEN_3;       /* top row = red (most points) */
    if (row <= 2) return COL_ALIEN_2;       /* middle = yellow */
    return COL_ALIEN_1;                     /* bottom = green */
}

static int alien_points(int row) {
    if (row == 0) return 30;
    if (row <= 2) return 20;
    return 10;
}

static void get_alien_pos(int row, int col, int *ax, int *ay) {
    *ax = alien_base_x + col * (ALIEN_W + ALIEN_PAD_X);
    *ay = alien_base_y + row * (ALIEN_H + ALIEN_PAD_Y);
}

/* Find leftmost and rightmost alive alien columns */
static void alien_bounds(int *left_col, int *right_col) {
    *left_col = ALIEN_COLS;
    *right_col = -1;
    for (int c = 0; c < ALIEN_COLS; c++)
        for (int r = 0; r < ALIEN_ROWS; r++)
            if (alien_alive[r][c]) {
                if (c < *left_col) *left_col = c;
                if (c > *right_col) *right_col = c;
            }
}

/* ── Init game ───────────────────────────────────────────────────────── */
static void init_game(void) {
    ship_x = SCR_W / 2 - SHIP_W / 2;
    score = 0;
    lives = 3;
    wave = 1;
    game_over = 0;

    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = 0;
    for (int i = 0; i < MAX_BOMBS; i++) bombs[i].active = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) explosions[i].timer = 0;
    mystery_active = 0;

    /* Init aliens */
    aliens_remaining = ALIEN_TOTAL;
    alien_base_x = (SCR_W - ALIEN_AREA_W) / 2;
    alien_base_y = 30;
    alien_dir = 1;
    alien_move_timer = 0;
    alien_move_delay = 30;
    alien_step_x = 2;
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            alien_alive[r][c] = 1;

    init_shields();
}

static void next_wave(void) {
    wave++;
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = 0;
    for (int i = 0; i < MAX_BOMBS; i++) bombs[i].active = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) explosions[i].timer = 0;
    mystery_active = 0;

    aliens_remaining = ALIEN_TOTAL;
    alien_base_x = (SCR_W - ALIEN_AREA_W) / 2;
    alien_base_y = 30;
    alien_dir = 1;
    alien_move_timer = 0;
    /* Speed up each wave */
    alien_move_delay = 30 - wave * 3;
    if (alien_move_delay < 6) alien_move_delay = 6;
    alien_step_x = 2;
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            alien_alive[r][c] = 1;

    /* Restore shields */
    init_shields();
}

/* ── Shoot ───────────────────────────────────────────────────────────── */
static void fire_bullet(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].x = ship_x + SHIP_W / 2 - BULLET_W / 2;
            bullets[i].y = SHIP_Y - BULLET_H;
            bullets[i].active = 1;
            return;
        }
    }
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int prev_a, prev_s;

static int handle_input(void) {
    int left  = gpio_read(BTN_LEFT);
    int right = gpio_read(BTN_RIGHT);
    int a     = gpio_read(BTN_A);
    int b     = gpio_read(BTN_B);
    int s     = gpio_read(BTN_SETTINGS);

    if (left && ship_x > 0) ship_x -= SHIP_SPEED;
    if (right && ship_x < SCR_W - SHIP_W) ship_x += SHIP_SPEED;
    if ((a && !prev_a) || (b && !prev_a)) fire_bullet();
    if (s && !prev_s) { prev_s = s; return 1; } /* pause */

    prev_a = a;
    prev_s = s;
    return 0;
}

/* ── Update ──────────────────────────────────────────────────────────── */
static void update_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        /* Erase old position */
        display_rect(bullets[i].x, bullets[i].y, BULLET_W, BULLET_H, COL_BG);
        bullets[i].y -= BULLET_SPEED;
        if (bullets[i].y < 0) { bullets[i].active = 0; continue; }

        /* Check shield hit */
        if (damage_shield(bullets[i].x + BULLET_W/2, bullets[i].y)) {
            bullets[i].active = 0;
            continue;
        }

        /* Check mystery ship hit */
        if (mystery_active &&
            bullets[i].x + BULLET_W > mystery_x &&
            bullets[i].x < mystery_x + MYSTERY_W &&
            bullets[i].y < MYSTERY_Y + MYSTERY_H &&
            bullets[i].y + BULLET_H > MYSTERY_Y) {
            score += 100 + rng_next(150);
            add_explosion(mystery_x + MYSTERY_W/2, MYSTERY_Y + MYSTERY_H/2);
            display_rect(mystery_x, MYSTERY_Y, MYSTERY_W, MYSTERY_H, COL_BG);
            mystery_active = 0;
            bullets[i].active = 0;
            continue;
        }

        /* Check alien hit */
        int hit = 0;
        for (int r = 0; r < ALIEN_ROWS && !hit; r++) {
            for (int c = 0; c < ALIEN_COLS && !hit; c++) {
                if (!alien_alive[r][c]) continue;
                int ax, ay;
                get_alien_pos(r, c, &ax, &ay);
                if (bullets[i].x + BULLET_W > ax &&
                    bullets[i].x < ax + ALIEN_W &&
                    bullets[i].y < ay + ALIEN_H &&
                    bullets[i].y + BULLET_H > ay) {
                    alien_alive[r][c] = 0;
                    aliens_remaining--;
                    score += alien_points(r);
                    add_explosion(ax + ALIEN_W/2, ay + ALIEN_H/2);
                    display_rect(ax, ay, ALIEN_W, ALIEN_H, COL_BG);
                    bullets[i].active = 0;
                    hit = 1;
                }
            }
        }
    }
}

static void update_bombs(void) {
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!bombs[i].active) continue;
        display_rect(bombs[i].x, bombs[i].y, BOMB_W, BOMB_H, COL_BG);
        bombs[i].y += BOMB_SPEED;

        if (bombs[i].y > SCR_H) { bombs[i].active = 0; continue; }

        /* Hit shield? */
        if (damage_shield(bombs[i].x + BOMB_W/2, bombs[i].y + BOMB_H)) {
            bombs[i].active = 0;
            continue;
        }

        /* Hit ship? */
        if (bombs[i].x + BOMB_W > ship_x &&
            bombs[i].x < ship_x + SHIP_W &&
            bombs[i].y + BOMB_H > SHIP_Y &&
            bombs[i].y < SHIP_Y + SHIP_H) {
            bombs[i].active = 0;
            lives--;
            add_explosion(ship_x + SHIP_W/2, SHIP_Y + SHIP_H/2);
            if (lives <= 0) game_over = 1;
        }
    }
}

static void update_aliens(void) {
    alien_move_timer++;
    if (alien_move_timer < alien_move_delay) return;
    alien_move_timer = 0;

    /* Erase all aliens at old positions */
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            if (alien_alive[r][c]) {
                int ax, ay;
                get_alien_pos(r, c, &ax, &ay);
                display_rect(ax, ay, ALIEN_W, ALIEN_H, COL_BG);
            }

    /* Find bounds */
    int lc, rc;
    alien_bounds(&lc, &rc);
    if (lc > rc) return; /* all dead */

    int left_px = alien_base_x + lc * (ALIEN_W + ALIEN_PAD_X);
    int right_px = alien_base_x + rc * (ALIEN_W + ALIEN_PAD_X) + ALIEN_W;

    int need_drop = 0;
    if (alien_dir > 0 && right_px + alien_step_x >= SCR_W - 4) need_drop = 1;
    if (alien_dir < 0 && left_px - alien_step_x <= 4) need_drop = 1;

    if (need_drop) {
        alien_base_y += ALIEN_H;
        alien_dir = -alien_dir;
        /* Speed up slightly when they drop */
        if (alien_move_delay > 6) alien_move_delay--;
    } else {
        alien_base_x += alien_dir * alien_step_x;
    }

    /* Check if aliens reached ship level */
    for (int r = ALIEN_ROWS - 1; r >= 0; r--) {
        for (int c = 0; c < ALIEN_COLS; c++) {
            if (alien_alive[r][c]) {
                int ax, ay;
                get_alien_pos(r, c, &ax, &ay);
                if (ay + ALIEN_H >= SHIP_Y) game_over = 1;
            }
        }
    }

    /* Drop bombs from random alive aliens */
    for (int c = 0; c < ALIEN_COLS; c++) {
        /* Find bottom-most alive alien in this column */
        for (int r = ALIEN_ROWS - 1; r >= 0; r--) {
            if (alien_alive[r][c]) {
                if (rng_next(BOMB_CHANCE) == 0) {
                    for (int b = 0; b < MAX_BOMBS; b++) {
                        if (!bombs[b].active) {
                            int ax, ay;
                            get_alien_pos(r, c, &ax, &ay);
                            bombs[b].x = ax + ALIEN_W / 2;
                            bombs[b].y = ay + ALIEN_H;
                            bombs[b].active = 1;
                            break;
                        }
                    }
                }
                break;
            }
        }
    }

    /* Speed up as aliens are destroyed */
    if (aliens_remaining > 0 && aliens_remaining <= 5) {
        alien_move_delay = 2;
    } else if (aliens_remaining <= 15) {
        alien_move_delay = 4;
    }
}

static void update_mystery(void) {
    if (!mystery_active) {
        if (rng_next(MYSTERY_CHANCE) == 0) {
            mystery_active = 1;
            mystery_dir = (rng_next(2) == 0) ? 1 : -1;
            mystery_x = (mystery_dir > 0) ? -MYSTERY_W : SCR_W;
        }
        return;
    }
    display_rect(mystery_x, MYSTERY_Y, MYSTERY_W, MYSTERY_H, COL_BG);
    mystery_x += mystery_dir * MYSTERY_SPEED;
    if (mystery_x < -MYSTERY_W || mystery_x > SCR_W) {
        mystery_active = 0;
    }
}

static void update_explosions(void) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].timer > 0) {
            explosions[i].timer--;
            if (explosions[i].timer == 0) {
                /* Erase explosion */
                display_rect(explosions[i].x - 6, explosions[i].y - 6,
                             12, 12, COL_BG);
            }
        }
    }
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void draw_hud(void) {
    /* Score */
    display_rect(0, 0, 160, 14, COL_BG);
    display_text(4, 2, "SCORE", COL_LABEL);
    display_number(50, 2, score, COL_SCORE);
    /* Lives */
    display_rect(220, 0, 100, 14, COL_BG);
    display_text(220, 2, "LIVES", COL_LABEL);
    for (int i = 0; i < lives; i++) {
        display_rect(270 + i * 14, 3, 10, 8, COL_SHIP);
    }
    /* Wave */
    display_text(140, 2, "W", COL_LABEL);
    display_number(152, 2, wave, COL_SCORE);
}

static void draw_ship(void) {
    /* Simple ship shape: a rectangle with a turret */
    display_rect(ship_x, SHIP_Y + 3, SHIP_W, SHIP_H - 3, COL_SHIP);
    display_rect(ship_x + SHIP_W/2 - 1, SHIP_Y, 2, 4, COL_SHIP);
}

static void draw_aliens(void) {
    for (int r = 0; r < ALIEN_ROWS; r++) {
        uint16_t col = alien_color(r);
        for (int c = 0; c < ALIEN_COLS; c++) {
            if (!alien_alive[r][c]) continue;
            int ax, ay;
            get_alien_pos(r, c, &ax, &ay);
            /* Simple alien: body rectangle + two "eyes" */
            display_rect(ax + 1, ay, ALIEN_W - 2, ALIEN_H, col);
            display_rect(ax, ay + 2, ALIEN_W, ALIEN_H - 4, col);
            /* Eyes */
            display_rect(ax + 3, ay + 2, 2, 2, COL_BG);
            display_rect(ax + 7, ay + 2, 2, 2, COL_BG);
        }
    }
}

static void draw_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active)
            display_rect(bullets[i].x, bullets[i].y, BULLET_W, BULLET_H, COL_BULLET);
    }
}

static void draw_bombs(void) {
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (bombs[i].active)
            display_rect(bombs[i].x, bombs[i].y, BOMB_W, BOMB_H, COL_ABOMB);
    }
}

static void draw_mystery(void) {
    if (!mystery_active) return;
    /* Mystery ship: a wider rectangle with "?" feel */
    display_rect(mystery_x, MYSTERY_Y, MYSTERY_W, MYSTERY_H, COL_ALIEN_3);
    display_rect(mystery_x + 2, MYSTERY_Y + 1, MYSTERY_W - 4, 1, COL_GAMEOVER);
}

static void draw_explosions(void) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].timer > 0) {
            int sz = 4 + (6 - explosions[i].timer);
            int x = explosions[i].x - sz/2;
            int y = explosions[i].y - sz/2;
            display_rect(x, y, sz, sz, COL_EXPLOSION);
        }
    }
}

static void draw_ground_line(void) {
    display_hline(0, SHIP_Y + SHIP_H + 2, SCR_W, COL_SCORE);
}

/* ── Pause menu ──────────────────────────────────────────────────────── */
enum { MENU_RESUME = 0, MENU_RESTART, MENU_EXIT, MENU_ITEMS };
static const char *menu_labels[] = { "Resume", "Restart", "Exit" };

static int show_pause_menu(void) {
    int cur = 0;
    int prev_u = 1, prev_d = 1, prev_a = 1, prev_s = 1;

    while (1) {
        /* Draw overlay */
        display_rect(80, 60, 160, 120, 0x0000);
        display_rect_outline(80, 60, 160, 120, COL_TITLE);
        display_text(130, 70, "PAUSED", COL_TITLE);
        for (int i = 0; i < MENU_ITEMS; i++) {
            uint16_t c = (i == cur) ? COL_TITLE : COL_LABEL;
            display_text(120, 95 + i * 18, menu_labels[i], c);
            if (i == cur) display_text(108, 95 + i * 18, ">", c);
        }
        display_flush();

        int u = gpio_read(BTN_UP);
        int d = gpio_read(BTN_DOWN);
        int a = gpio_read(BTN_A);
        int b = gpio_read(BTN_B);
        int s = gpio_read(BTN_SETTINGS);

        if (s && !prev_s) return MENU_RESUME;
        if (u && !prev_u) cur = (cur > 0) ? cur - 1 : MENU_ITEMS - 1;
        if (d && !prev_d) cur = (cur < MENU_ITEMS - 1) ? cur + 1 : 0;
        if ((a && !prev_a) || (b && !prev_a)) return cur;

        prev_u = u; prev_d = d; prev_a = a; prev_s = s;
        delay(20000);
    }
}

/* ── Full redraw ─────────────────────────────────────────────────────── */
static void full_redraw(void) {
    display_clear(COL_BG);
    draw_hud();
    draw_ground_line();
    draw_shields();
    draw_aliens();
    draw_ship();
    draw_bullets();
    draw_bombs();
    draw_mystery();
    draw_explosions();
    display_flush();
}

/* ── Game loop ───────────────────────────────────────────────────────── */
static void game_loop(void) {
    int prev_ship_x = ship_x;

    full_redraw();

    while (!game_over) {
        /* Input */
        if (handle_input()) {
            int choice = show_pause_menu();
            if (choice == MENU_EXIT) { exit_to_supervisor = 1; return; }
            if (choice == MENU_RESTART) { restart_game = 1; return; }
            full_redraw();
        }

        /* Erase ship at old position */
        if (prev_ship_x != ship_x) {
            display_rect(prev_ship_x, SHIP_Y, SHIP_W, SHIP_H, COL_BG);
        }

        /* Update */
        update_bullets();
        update_bombs();
        update_aliens();
        update_mystery();
        update_explosions();

        /* Check wave clear */
        if (aliens_remaining <= 0) {
            next_wave();
            full_redraw();
            delay(500000);
            prev_ship_x = ship_x;
            continue;
        }

        /* Draw */
        draw_hud();
        draw_ship();
        draw_bullets();
        draw_bombs();
        draw_aliens();
        draw_mystery();
        draw_explosions();
        draw_shields();
        display_flush();

        prev_ship_x = ship_x;
        delay(FRAME_US);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS Space Invaders v1.0");

    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* Title screen */
    display_clear(COL_BG);
    display_text_large(68, 60, "SPACE", COL_TITLE);
    display_text_large(44, 90, "INVADERS", COL_TITLE);
    display_text(80, 150, "Press A to start", COL_SCORE);
    display_text(72, 180, "AkiraOS Edition", COL_LABEL);
    display_flush();

    /* Wait for button */
    uint32_t seed = 1;
    while (!gpio_read(BTN_A) && !gpio_read(BTN_B)) {
        seed++;
        delay(20000);
    }
    rng = seed ^ 0xDEADBEEF;
    /* Warm up RNG */
    rng_next(7); rng_next(7); rng_next(7);
    delay(100000);

    /* Main game loop with restart support */
    while (1) {
        restart_game = 0;
        exit_to_supervisor = 0;
        init_game();
        game_loop();

        if (exit_to_supervisor) {
            app_switch("supervisor");
            return 0;
        }
        if (restart_game) {
            seed++;
            rng = seed ^ 0xDEADBEEF;
            rng_next(7); rng_next(7);
            continue;
        }
        break;
    }

    /* Game over screen */
    display_clear(COL_BG);
    display_text_large(68, 60, "GAME", COL_GAMEOVER);
    display_text_large(68, 95, "OVER", COL_GAMEOVER);
    display_text(100, 145, "SCORE:", COL_LABEL);
    display_number(155, 145, score, COL_SCORE);
    display_text(100, 165, "WAVE:", COL_LABEL);
    display_number(150, 165, wave, COL_SCORE);
    display_flush();
    delay(5000000);

    printf("Game over!");
    return 0;
}
