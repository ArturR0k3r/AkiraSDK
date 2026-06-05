/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tetris for AkiraConsole — clean rewrite.
 * Controls: LEFT/RIGHT=move  UP/A=rotate  DOWN=soft-drop  SETTINGS=pause
 */

#include "akira_api.h"

/* ── Pins ──────────────────────────────────────────────────────────────── */
#define PIN_UP       4
#define PIN_DOWN     5
#define PIN_LEFT     7
#define PIN_RIGHT    6
#define PIN_A        15
#define PIN_B        16
#define PIN_SETTINGS 0

/* ── Board ─────────────────────────────────────────────────────────────── */
#define COLS     10
#define ROWS     20
#define CELL     11          /* px per cell                                  */
#define BX       10          /* board left edge on screen                    */
#define BY       20          /* board top  edge on screen                    */

static uint8_t board[ROWS][COLS];   /* 0 = empty, 1-7 = piece colour index  */

/* ── Colours (RGB565) ─────────────────────────────────────────────────── */
#define C_BG     0x0000u
#define C_GRID   0x18C3u   /* very dark grey grid lines                      */
#define C_BORDER 0x4A69u
#define C_TEXT   0xFFFFu
#define C_DIM    0x8410u
#define C_TITLE  0x07FFu

static const uint16_t PC[8] = {
    0x0000u,    /* 0 = empty            */
    0x07FFu,    /* 1 I – cyan           */
    0xFFE0u,    /* 2 O – yellow         */
    0xA81Fu,    /* 3 T – purple         */
    0x07E0u,    /* 4 S – green          */
    0xF800u,    /* 5 Z – red            */
    0x211Fu,    /* 6 J – blue           */
    0xFD20u,    /* 7 L – orange         */
};

/* ── Pieces: 4 rotations × 4 rows, each row is a 4-bit mask ─────────── */
static const uint8_t SHAPES[7][4][4] = {
    /* I */ {{ 0,0xF,0,0  }, { 2,2,2,2   }, { 0,0xF,0,0 }, { 2,2,2,2   }},
    /* O */ {{ 0,6,6,0    }, { 0,6,6,0   }, { 0,6,6,0   }, { 0,6,6,0   }},
    /* T */ {{ 0,7,0,0    }, { 2,6,2,0   }, { 0,3,0,0   }, { 2,3,0,0   }},
    /* S */ {{ 0,6,3,0    }, { 2,6,4,0   }, { 0,6,3,0   }, { 2,6,4,0   }},
    /* Z */ {{ 0,3,6,0    }, { 4,6,2,0   }, { 0,3,6,0   }, { 4,6,2,0   }},
    /* J */ {{ 2,7,0,0    }, { 6,2,2,0   }, { 0,7,2,0   }, { 2,2,3,0   }},
    /* L */ {{ 2,7,0,0    }, { 2,2,6,0   }, { 0,7,8,0   }, { 3,2,2,0   }},
};

/* Bit helpers */
static int cell(int p, int r, int row, int col) {
    return (SHAPES[p][r][row] >> (3 - col)) & 1;
}

/* ── Game state ─────────────────────────────────────────────────────────*/
static int cur_p, cur_r, cur_x, cur_y;   /* current piece                  */
static int next_p;
static int score, lines, level;
static int game_over;
static uint32_t rng_state;

static int rng7(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (int)((rng_state >> 16) & 0xFFFF) % 7;
}

/* ── Collision ─────────────────────────────────────────────────────────── */
static int collides(int p, int r, int x, int y) {
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            if (!cell(p, r, row, col)) continue;
            int bx = x + col, by = y + row;
            if (bx < 0 || bx >= COLS || by >= ROWS) return 1;
            if (by >= 0 && board[by][bx]) return 1;
        }
    return 0;
}

/* ── Draw one board cell ──────────────────────────────────────────────── */
static void draw_cell(int col, int row, uint16_t color) {
    int px = BX + col * CELL;
    int py = BY + row * CELL;
    if (color) {
        display_rect(px, py, CELL - 1, CELL - 1, color);
        display_rect(px,     py,     CELL - 2, 1, 0xFFFFu); /* highlight */
        display_rect(px,     py,     1, CELL - 2, 0xFFFFu);
    } else {
        display_rect(px, py, CELL - 1, CELL - 1, C_BG);
        display_rect(px, py, CELL - 1, 1, C_GRID);
        display_rect(px, py, 1, CELL - 1, C_GRID);
    }
}

/* ── Draw current piece (erase=1 draws background) ───────────────────── */
static void draw_piece(int p, int r, int x, int y, int erase) {
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            if (!cell(p, r, row, col)) continue;
            int by = y + row;
            if (by < 0) continue;
            draw_cell(x + col, by, erase ? 0 : PC[p + 1]);
        }
}

/* ── Number formatter ─────────────────────────────────────────────────── */
static void draw_num(int x, int y, int v, uint16_t c) {
    char buf[12]; int i = 0;
    if (v == 0) { buf[i++] = '0'; }
    else { int n = v; while (n) { buf[i++] = '0' + n % 10; n /= 10; } }
    /* reverse */
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = buf[a]; buf[a] = buf[b]; buf[b] = t;
    }
    buf[i] = '\0';
    display_text(x, y, buf, c);
}

/* ── Sidebar ──────────────────────────────────────────────────────────── */
#define SX  (BX + COLS * CELL + 8)

static void draw_sidebar(void) {
    int sx = SX;
    display_text(sx, 28,  "NEXT",  C_DIM);
    display_text(sx, 110, "SCORE", C_DIM);
    display_text(sx, 148, "LINES", C_DIM);
    display_text(sx, 186, "LEVEL", C_DIM);
    draw_num(sx, 122, score, C_TEXT);
    draw_num(sx, 160, lines, C_TEXT);
    draw_num(sx, 198, level, C_TEXT);
}

static void draw_next(int erase) {
    int sx = SX;
    display_rect(sx, 40, 46, 46, C_BG);
    if (erase) return;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!cell(next_p, 0, r, c)) continue;
            display_rect(sx + c * 11 + 2, 42 + r * 11,
                         10, 10, PC[next_p + 1]);
        }
}

/* ── Lock, clear lines, spawn ─────────────────────────────────────────── */
static void lock_piece(void) {
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            if (!cell(cur_p, cur_r, row, col)) continue;
            int by = cur_y + row;
            if (by >= 0) board[by][cur_x + col] = (uint8_t)(cur_p + 1);
        }
}

static int clear_lines(void) {
    int n = 0;
    for (int r = ROWS - 1; r >= 0; r--) {
        int full = 1;
        for (int c = 0; c < COLS; c++) if (!board[r][c]) { full = 0; break; }
        if (!full) continue;
        n++;
        for (int rr = r; rr > 0; rr--)
            for (int c = 0; c < COLS; c++) board[rr][c] = board[rr - 1][c];
        for (int c = 0; c < COLS; c++) board[0][c] = 0;
        r++; /* re-check same row */
    }
    return n;
}

static void redraw_board(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            draw_cell(c, r, board[r][c] ? PC[board[r][c]] : 0);
}

static void spawn(void) {
    cur_p = next_p;
    next_p = rng7();
    cur_r = 0;
    cur_x = COLS / 2 - 2;
    cur_y = -1;
    if (collides(cur_p, cur_r, cur_x, cur_y)) game_over = 1;
}

/* ── Input ─────────────────────────────────────────────────────────────── */
static int prev_up, prev_a, prev_b, prev_s;
static int prev_left, prev_right;
static int das_frames = 0;   /* frames held in one direction */
#define DAS_START  12        /* frames before first repeat (~240ms) */
#define DAS_RATE    4        /* frames between repeats      (~80ms)  */

static int paused = 0;

static void handle_input(uint32_t *drop_acc, uint32_t drop_delay) {
    int up    = gpio_read(PIN_UP);
    int down  = gpio_read(PIN_DOWN);
    int left  = gpio_read(PIN_LEFT);
    int right = gpio_read(PIN_RIGHT);
    int a     = gpio_read(PIN_A);
    int b     = gpio_read(PIN_B);
    int s     = gpio_read(PIN_SETTINGS);

    /* Pause */
    if (s && !prev_s) { paused = !paused; }
    prev_s = s;
    if (paused) return;

    /* Rotate — one press, one rotation */
    if ((up && !prev_up) || (a && !prev_a) || (b && !prev_b)) {
        int nr = (cur_r + 1) % 4;
        draw_piece(cur_p, cur_r, cur_x, cur_y, 1);
        if      (!collides(cur_p, nr, cur_x,     cur_y)) cur_r = nr;
        else if (!collides(cur_p, nr, cur_x - 1, cur_y)) { cur_r = nr; cur_x--; }
        else if (!collides(cur_p, nr, cur_x + 1, cur_y)) { cur_r = nr; cur_x++; }
        draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
    }
    prev_up = up; prev_a = a; prev_b = b;

    /* Soft drop */
    if (down) {
        draw_piece(cur_p, cur_r, cur_x, cur_y, 1);
        if (!collides(cur_p, cur_r, cur_x, cur_y + 1)) cur_y++;
        draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
        *drop_acc = 0;
        display_flush();
        delay(80000);
        prev_left = left; prev_right = right;
        return;
    }

    /* Left / Right with DAS */
    int dx = 0;
    if (left  && !right) {
        if (!prev_left) { dx = -1; das_frames = 0; }          /* first press  */
        else { das_frames++; if (das_frames >= DAS_START && (das_frames - DAS_START) % DAS_RATE == 0) dx = -1; }
    } else if (right && !left) {
        if (!prev_right) { dx = 1; das_frames = 0; }
        else { das_frames++; if (das_frames >= DAS_START && (das_frames - DAS_START) % DAS_RATE == 0) dx = 1; }
    } else {
        das_frames = 0;
    }

    if (dx && !collides(cur_p, cur_r, cur_x + dx, cur_y)) {
        draw_piece(cur_p, cur_r, cur_x, cur_y, 1);
        cur_x += dx;
        draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
    }

    prev_left = left; prev_right = right;
}

/* ── Main ──────────────────────────────────────────────────────────────── */
int main(void) {
    static const int32_t pts[5] = {0, 100, 300, 500, 800};

    gpio_configure(PIN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* Title */
    display_clear(C_BG);
    display_text_large(100, 80,  "TETRIS",  C_TITLE);
    display_text(110, 120, "AkiraOS", C_DIM);
    display_text(90,  160, "Press any button", C_TEXT);
    display_flush();

    uint32_t seed = 1;
    while (!gpio_read(PIN_A) && !gpio_read(PIN_B) &&
           !gpio_read(PIN_UP) && !gpio_read(PIN_LEFT) &&
           !gpio_read(PIN_RIGHT)) { seed++; delay(20000); }
    rng_state = seed ^ 0xDEADBEEFu;
    delay(100000);

restart:
    /* Init */
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) board[r][c] = 0;
    score = 0; lines = 0; level = 1; game_over = 0; paused = 0;
    prev_up = prev_a = prev_b = prev_s = prev_left = prev_right = 0;
    das_frames = 0;

    /* Seed first two pieces */
    next_p = rng7();
    spawn();

    /* Draw static UI */
    display_clear(C_BG);
    display_rect(BX - 2,            BY - 2, 2,                  ROWS * CELL + 4, C_BORDER);
    display_rect(BX + COLS * CELL,  BY - 2, 2,                  ROWS * CELL + 4, C_BORDER);
    display_rect(BX - 2,            BY + ROWS * CELL, COLS * CELL + 4, 2,        C_BORDER);
    redraw_board();
    draw_next(0);
    draw_sidebar();
    display_flush();

    uint32_t drop_acc = 0;
    uint32_t drop_delay = 600000u; /* µs — level 1 */

    while (!game_over) {
        handle_input(&drop_acc, drop_delay);
        if (paused) { delay(20000); continue; }

        drop_acc += 20000; /* 20ms per frame */
        if (drop_acc >= drop_delay) {
            drop_acc = 0;
            draw_piece(cur_p, cur_r, cur_x, cur_y, 1);
            if (!collides(cur_p, cur_r, cur_x, cur_y + 1)) {
                cur_y++;
            } else {
                /* Lock */
                draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
                lock_piece();
                int n = clear_lines();
                if (n) {
                    score += pts[n] * level;
                    lines += n;
                    level  = lines / 10 + 1;
                    if (level > 15) level = 15;
                    drop_delay = 600000u / (uint32_t)level;
                    if (drop_delay < 80000u) drop_delay = 80000u;
                    redraw_board();
                }
                draw_sidebar();
                draw_next(1);
                spawn();
                draw_next(0);
                if (game_over) break;
                draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
            }
        } else {
            draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
        }

        display_flush();
        delay(20000);
    }

    /* Game over */
    display_text(BX + 8, BY + ROWS * CELL / 2 - 10, "GAME OVER", 0xF800u);
    draw_num(BX + 8, BY + ROWS * CELL / 2 + 8, score, C_TEXT);
    display_flush();
    delay(2000000);

    /* Wait restart */
    while (gpio_read(PIN_A) || gpio_read(PIN_B)) delay(20000); /* drain */
    while (!gpio_read(PIN_A) && !gpio_read(PIN_B)) delay(20000);
    delay(100000);
    goto restart;

    return 0;
}
