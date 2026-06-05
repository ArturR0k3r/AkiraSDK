/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tetris for AkiraConsole
 * Controls: LEFT/RIGHT=move  UP/A/B=rotate  DOWN=soft-drop  SETTINGS=pause
 */

#include "akira_api.h"

/* ── GPIO pins (active-HIGH, pull-down — same as space_invaders) ──────── */
#define PIN_UP       4
#define PIN_DOWN     5
#define PIN_LEFT     7   /* GPIO7 = physical LEFT  */
#define PIN_RIGHT    6   /* GPIO6 = physical RIGHT */
#define PIN_A        15
#define PIN_B        16
#define PIN_SETTINGS 0   /* active-LOW, pull-up */

/* ── Board geometry ───────────────────────────────────────────────────── */
#define COLS        10
#define ROWS        20
#define CELL        11
#define BOARD_X     10
#define BOARD_Y     20
#define SIDEBAR_X   (BOARD_X + COLS * CELL + 8)

/* ── Colours ─────────────────────────────────────────────────────────── */
#define C_BG     0x0000u
#define C_BORDER 0x4A69u
#define C_GRID   0x18C3u
#define C_WHITE  0xFFFFu
#define C_DIM    0x8410u
#define C_CYAN   0x07FFu

static const uint16_t PIECE_COLOR[7] = {
    0x07FFu, /* I cyan    */
    0xFFE0u, /* O yellow  */
    0xA81Fu, /* T purple  */
    0x07E0u, /* S green   */
    0xF800u, /* Z red     */
    0x211Fu, /* J blue    */
    0xFD20u, /* L orange  */
};

/*
 * Piece shapes: 4 rotations, each rotation is a 16-bit mask for a 4x4 grid.
 * Bit 15 = top-left, bit 0 = bottom-right.
 */
static const uint16_t SHAPES[7][4] = {
    {0x0F00u, 0x2222u, 0x00F0u, 0x4444u}, /* I */
    {0x6600u, 0x6600u, 0x6600u, 0x6600u}, /* O */
    {0x0E40u, 0x4C40u, 0x4E00u, 0x4640u}, /* T */
    {0x06C0u, 0x4620u, 0x06C0u, 0x4620u}, /* S */
    {0x0C60u, 0x2640u, 0x0C60u, 0x2640u}, /* Z */
    {0x44C0u, 0x8E00u, 0x6440u, 0x0E20u}, /* J */
    {0x4460u, 0x0E80u, 0xC440u, 0x2E00u}, /* L */
};

static int shape_bit(int piece, int rot, int row, int col) {
    return (SHAPES[piece][rot] >> (15 - row * 4 - col)) & 1;
}

/* ── Board ────────────────────────────────────────────────────────────── */
static uint8_t board[ROWS][COLS]; /* 0=empty, 1..7=piece+1 */

/* ── Game state ───────────────────────────────────────────────────────── */
static int cur_p, cur_r, cur_x, cur_y;
static int next_p;
static int score, lines, level;
static int game_over;
static uint32_t rng;

static int rng7(void) {
    rng = rng * 1664525u + 1013904223u;
    return (int)((rng >> 16) & 0x7FFFu) % 7;
}

/* ── Collision ────────────────────────────────────────────────────────── */
static int collides(int p, int r, int x, int y) {
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            if (!shape_bit(p, r, row, col)) continue;
            int bx = x + col, by = y + row;
            if (bx < 0 || bx >= COLS || by >= ROWS) return 1;
            if (by >= 0 && board[by][bx]) return 1;
        }
    return 0;
}

/* ── Draw ─────────────────────────────────────────────────────────────── */
static void draw_cell(int col, int row, uint16_t color) {
    int px = BOARD_X + col * CELL;
    int py = BOARD_Y + row * CELL;
    if (color) {
        display_rect(px, py, CELL-1, CELL-1, color);
        display_rect(px, py, CELL-2, 1, 0xFFFFu);
        display_rect(px, py, 1, CELL-2, 0xFFFFu);
    } else {
        display_rect(px, py, CELL-1, CELL-1, C_BG);
        display_rect(px, py, CELL-1, 1, C_GRID);
        display_rect(px, py, 1, CELL-1, C_GRID);
    }
}

static void draw_piece(int p, int r, int x, int y, int erase) {
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            if (!shape_bit(p, r, row, col)) continue;
            int by = y + row;
            if (by < 0 || by >= ROWS) continue;
            draw_cell(x + col, by, erase ? 0 : PIECE_COLOR[p]);
        }
}

static void redraw_board(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            draw_cell(c, r, board[r][c] ? PIECE_COLOR[board[r][c]-1] : 0);
}

static char tmp_buf[16];
static char *itoa_simple(int v) {
    int i = 0;
    if (v == 0) { tmp_buf[i++]='0'; }
    else { int n=v; while(n){tmp_buf[i++]='0'+n%10; n/=10;} }
    for (int a=0,b=i-1; a<b; a++,b--){ char t=tmp_buf[a]; tmp_buf[a]=tmp_buf[b]; tmp_buf[b]=t; }
    tmp_buf[i]='\0'; return tmp_buf;
}

static void draw_sidebar(void) {
    int x = SIDEBAR_X;
    display_rect(x, 20, 80, 220, C_BG);
    display_text(x, 28,  "NEXT",  C_DIM);
    display_text(x, 106, "SCORE", C_DIM);
    display_text(x, 140, "LINES", C_DIM);
    display_text(x, 174, "LEVEL", C_DIM);
    display_text(x, 118, itoa_simple(score), C_WHITE);
    display_text(x, 152, itoa_simple(lines), C_WHITE);
    display_text(x, 186, itoa_simple(level), C_WHITE);
}

static void draw_next_piece(void) {
    int x = SIDEBAR_X;
    display_rect(x, 40, 48, 48, C_BG);
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            if (!shape_bit(next_p, 0, row, col)) continue;
            display_rect(x + col*11 + 2, 42 + row*11, 10, 10, PIECE_COLOR[next_p]);
        }
}

/* ── Lock + clear lines ───────────────────────────────────────────────── */
static void lock_current(void) {
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            if (!shape_bit(cur_p, cur_r, row, col)) continue;
            int by = cur_y + row;
            if (by >= 0) board[by][cur_x + col] = (uint8_t)(cur_p + 1);
        }
}

static int clear_lines(void) {
    static const int pts[5] = {0, 100, 300, 500, 800};
    int n = 0;
    for (int r = ROWS-1; r >= 0; r--) {
        int full = 1;
        for (int c = 0; c < COLS; c++) if (!board[r][c]) { full=0; break; }
        if (!full) continue;
        for (int rr = r; rr > 0; rr--)
            for (int c = 0; c < COLS; c++) board[rr][c] = board[rr-1][c];
        for (int c = 0; c < COLS; c++) board[0][c] = 0;
        n++; r++;
    }
    if (n) {
        score += pts[n] * level;
        lines += n;
        level  = lines / 10 + 1;
        if (level > 15) level = 15;
    }
    return n;
}

static void spawn_piece(void) {
    cur_p = next_p; next_p = rng7();
    cur_r = 0;
    cur_x = COLS/2 - 2;
    cur_y = -1;
    if (collides(cur_p, cur_r, cur_x, cur_y)) game_over = 1;
}

/* ── Main ─────────────────────────────────────────────────────────────── */
int main(void) {
    gpio_configure(PIN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* Title screen */
    display_clear(C_BG);
    display_text_large(98, 80, "TETRIS", C_CYAN);
    display_text(105, 130, "Press A to play", C_WHITE);
    display_flush();

    uint32_t seed = 1;
    while (!gpio_read(PIN_A) && !gpio_read(PIN_B))
        { seed++; delay(20000); }
    rng = seed ^ 0xDEADBEEFu;
    delay(120000); /* debounce */

restart:
    for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++) board[r][c]=0;
    score=0; lines=0; level=1; game_over=0;

    next_p = rng7();
    spawn_piece();

    /* Static frame */
    display_clear(C_BG);
    display_rect(BOARD_X-2, BOARD_Y-2, 2, ROWS*CELL+4, C_BORDER);
    display_rect(BOARD_X+COLS*CELL, BOARD_Y-2, 2, ROWS*CELL+4, C_BORDER);
    display_rect(BOARD_X-2, BOARD_Y+ROWS*CELL, COLS*CELL+4, 2, C_BORDER);
    redraw_board();
    draw_next_piece();
    draw_sidebar();
    display_flush();

    /* Drop timing: µs per row, level 1 = 600ms */
    uint32_t drop_us   = 600000u;
    uint32_t drop_acc  = 0;

    /* Previous button states — exactly like space_invaders */
    int pup=0, pdown=0, pleft=0, pright=0, pa=0, pb=0, ps=0;

    /* DAS state */
    int das_frames = 0;   /* frames held in current L/R direction */
    int das_dir    = 0;   /* -1=left  +1=right */
#define DAS_START 12      /* 12×20ms = 240ms before repeat */
#define DAS_RATE   4      /*  4×20ms =  80ms repeat rate  */

    int paused = 0;

    while (!game_over) {
        /* Read all buttons — same as space_invaders */
        int up    = gpio_read(PIN_UP);
        int down  = gpio_read(PIN_DOWN);
        int left  = gpio_read(PIN_LEFT);
        int right = gpio_read(PIN_RIGHT);
        int a     = gpio_read(PIN_A);
        int b     = gpio_read(PIN_B);
        int s     = gpio_read(PIN_SETTINGS);
        /* SETTINGS is active-low: physical LOW = pressed = gpio_read returns 0 */
        int settings_pressed = (!s && ps); /* falling edge = button pressed */
        ps = s;

        /* Pause toggle */
        if (settings_pressed) paused = !paused;
        if (paused) { delay(20000); pup=up;pdown=down;pleft=left;pright=right;pa=a;pb=b; continue; }

        /* Rotate — one press = one rotation */
        if ((up && !pup) || (a && !pa) || (b && !pb)) {
            int nr = (cur_r+1)%4;
            draw_piece(cur_p, cur_r, cur_x, cur_y, 1);
            if      (!collides(cur_p, nr, cur_x,   cur_y)) cur_r=nr;
            else if (!collides(cur_p, nr, cur_x-1, cur_y)) { cur_r=nr; cur_x--; }
            else if (!collides(cur_p, nr, cur_x+1, cur_y)) { cur_r=nr; cur_x++; }
            draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
        }
        pup=up; pa=a; pb=b;

        /* Left / Right with DAS */
        int dx = 0;
        if (left && !right) {
            if (!pleft) { dx=-1; das_frames=0; das_dir=-1; }
            else if (das_dir==-1) {
                das_frames++;
                if (das_frames==DAS_START) dx=-1;
                else if (das_frames>DAS_START && (das_frames-DAS_START)%DAS_RATE==0) dx=-1;
            }
        } else if (right && !left) {
            if (!pright) { dx=+1; das_frames=0; das_dir=+1; }
            else if (das_dir==+1) {
                das_frames++;
                if (das_frames==DAS_START) dx=+1;
                else if (das_frames>DAS_START && (das_frames-DAS_START)%DAS_RATE==0) dx=+1;
            }
        } else { das_frames=0; das_dir=0; }
        pleft=left; pright=right;

        if (dx && !collides(cur_p, cur_r, cur_x+dx, cur_y)) {
            draw_piece(cur_p, cur_r, cur_x, cur_y, 1);
            cur_x += dx;
            draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
        }

        /* Soft drop (DOWN held — same as space_invaders: act every tick) */
        uint32_t effective_drop = down ? 80000u : drop_us;
        pdown = down;

        /* Auto drop */
        drop_acc += 20000u;
        if (drop_acc >= effective_drop) {
            drop_acc = 0;
            draw_piece(cur_p, cur_r, cur_x, cur_y, 1);
            if (!collides(cur_p, cur_r, cur_x, cur_y+1)) {
                cur_y++;
                if (down) score++; /* soft drop point */
            } else {
                /* Lock */
                draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
                lock_current();
                if (clear_lines()) {
                    drop_us = 600000u / (uint32_t)level;
                    if (drop_us < 80000u) drop_us = 80000u;
                    redraw_board();
                }
                draw_sidebar();
                draw_next_piece();
                spawn_piece();
                if (!game_over) {
                    draw_next_piece();
                    draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
                }
            }
        } else {
            draw_piece(cur_p, cur_r, cur_x, cur_y, 0);
        }

        display_flush();
        delay(20000); /* 20ms = 50fps */
    }

    /* Game over screen */
    display_rect(BOARD_X+5, BOARD_Y+80, COLS*CELL-10, 50, 0x0000u);
    display_rect(BOARD_X+5, BOARD_Y+80, COLS*CELL-10, 2, 0xF800u);
    display_rect(BOARD_X+5, BOARD_Y+128, COLS*CELL-10, 2, 0xF800u);
    display_text(BOARD_X+12, BOARD_Y+90,  "GAME OVER", 0xF800u);
    display_text(BOARD_X+12, BOARD_Y+108, itoa_simple(score), C_WHITE);
    display_flush();
    delay(2000000u);

    /* Wait for A then restart */
    while (gpio_read(PIN_A) || gpio_read(PIN_B)) delay(20000);
    while (!gpio_read(PIN_A) && !gpio_read(PIN_B)) delay(20000);
    delay(100000u);
    goto restart;
    return 0;
}
