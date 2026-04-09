/**
 * @file tetris.c
 * @brief Tetris game WASM application for AkiraOS
 *
 * Controls (akiraconsole DPAD):
 *   UP=Rotate  DOWN=Soft-drop  LEFT=Move left  RIGHT=Move right
 *
 * @copyright Copyright (c) 2026 AkiraOS Contributors
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display layout ────────────────────────────────────────────────────────
 * Landscape 320×240.
 *   y:0-18   Header bar (title + level)
 *   y:20-219 Game board (10×20 blocks @ 10px = 100×200) — centred at x=110
 *   x:110-209 Board area  (centred: (320-100)/2 = 110)
 *   x:218+   Sidebar     (next piece, score, lines, level)
 * ─────────────────────────────────────────────────────────────────────── */
#define BLOCK_SIZE     10
#define BOARD_WIDTH    10
#define BOARD_HEIGHT   20
#define BOARD_X        110
#define BOARD_Y        20
#define SIDEBAR_X      218
#define BOARD_PX_W     (BOARD_WIDTH  * BLOCK_SIZE)
#define BOARD_PX_H     (BOARD_HEIGHT * BLOCK_SIZE)

/* ── Game configuration ───────────────────────────────────────────────── */
#define INITIAL_DROP_DELAY  300000   /* 0.3 s at level 1 — noticeably brisker */
#define MIN_DROP_DELAY       50000
#define MAX_LEVEL               15
#define SOFT_DROP_DELAY       8000   /* µs per cell while DOWN held */

/* ── Button pins (akiraconsole, active-HIGH, pull-down) ──────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15   /* A button — also rotates */
#define BTN_B        16   /* B button — also rotates */
#define BTN_SETTINGS 2    /* Settings — pause/resume */
#define BTN_X        17   /* X button — unused        */
#define BTN_Y        41   /* Y button — unused        */

/* ── UI colour palette — logical & consistent ──────────────────────────
 *  Only tetromino cells use per-piece colours.
 *  All UI chrome uses this fixed set so it looks intentional.        */
#define COL_BG          0x0000   /* black background                */
#define COL_CELL_EMPTY  0x0641   /* very dark blue-grey — grid dot  */
#define COL_BORDER      0x4A69   /* mid-grey board border           */
#define COL_HEADER_BG   0x0045   /* deep green header bar            */
#define COL_TITLE       0x07FF   /* cyan — "TETRIS" title           */
#define COL_LABEL       0x8410   /* dim grey — NEXT/SCORE/LINES lbl */
#define COL_VALUE       0xFFFF   /* white — stat numbers            */
#define COL_HINT        0x4208   /* dark grey — controls hint text  */
#define COL_GAMEOVER_T  0xF800   /* red — GAME OVER                 */
#define COL_GAMEOVER_S  0xFFE0   /* yellow — final score            */

/* Classic Tetris piece colours — vivid, matches standard Tetris palette */
static const uint16_t PIECE_COLOR[7] = {
    0x07FF,  /* I — cyan        */
    0xFFE0,  /* O — yellow      */
    0xA81F,  /* T — vivid purple */
    0x0760,  /* S — bright green */
    0xF800,  /* Z — red         */
    0x211F,  /* J — bright blue */
    0xFBA0,  /* L — orange      */
};

/* ── Tetromino shapes (4×4 bit-packed, 4 rotations) ─────────────────── */
static const uint16_t SHAPES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},  /* I */
    {0x6600, 0x6600, 0x6600, 0x6600},  /* O */
    {0x0E40, 0x4C40, 0x4E00, 0x4640},  /* T */
    {0x06C0, 0x4620, 0x06C0, 0x4620},  /* S */
    {0x0C60, 0x2640, 0x0C60, 0x2640},  /* Z */
    {0x44C0, 0x8E00, 0x6440, 0x0E20},  /* J */
    {0x4460, 0x0E80, 0xC440, 0x2E00},  /* L */
};

/* ── Game state ──────────────────────────────────────────────────────── */
typedef struct {
    uint8_t  board[BOARD_HEIGHT][BOARD_WIDTH];
    int      cur_piece, cur_rot, cur_x, cur_y;
    int      next_piece;
    uint32_t score, lines, level, drop_delay;
    int      game_over;
    /* prev-state for incremental redraws */
    int      prev_piece, prev_rot, prev_x, prev_y;
    int      prev_next;
    uint32_t prev_score, prev_lines, prev_level;
    uint8_t  piece_moved, board_changed;
} game_t;

static game_t g;

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng = 12345;
static int rand7(void) {
    rng = rng * 1103515245 + 12345;
    return (int)((rng >> 16) & 0x7FFF) % 7;
}
/* Seed from an external value (e.g. frame counter from title screen wait) */
static void rng_seed(uint32_t seed) {
    rng = seed ^ 0xDEADBEEF;
    /* Warm up — discard first few outputs so low seeds aren't correlated */
    rand7(); rand7(); rand7();
}

/* ── Shape helpers ───────────────────────────────────────────────────── */
static int shape_cell(uint16_t s, int x, int y) {
    return (s >> ((3 - y) * 4 + (3 - x))) & 1;
}

static int collides(int piece, int rot, int x, int y) {
    uint16_t s = SHAPES[piece][rot];
    for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++) {
            if (!shape_cell(s, px, py)) continue;
            int bx = x + px, by = y + py;
            if (bx < 0 || bx >= BOARD_WIDTH || by >= BOARD_HEIGHT) return 1;
            if (by >= 0 && g.board[by][bx]) return 1;
        }
    return 0;
}

/* ── Draw primitives ─────────────────────────────────────────────────── */

/* Solid block with bevel highlight/shadow */
static void draw_block(int bx, int by, uint16_t col) {
    int px = BOARD_X + bx * BLOCK_SIZE;
    int py = BOARD_Y + by * BLOCK_SIZE;
    int sz = BLOCK_SIZE - 1;  /* 1px gap between blocks */
    display_rect(px, py, sz, sz, col);
    /* bright top-left bevel */
    display_rect(px, py, sz - 1, 1, 0xFFFF);
    display_rect(px, py, 1, sz - 1, 0xFFFF);
    /* dark bottom-right shadow */
    display_rect(px + sz - 1, py + 1,      1, sz - 1, 0x0000);
    display_rect(px + 1,      py + sz - 1, sz - 1, 1, 0x0000);
}

/* Empty cell */
static void draw_empty_cell(int bx, int by) {
    int px = BOARD_X + bx * BLOCK_SIZE;
    int py = BOARD_Y + by * BLOCK_SIZE;
    display_rect(px, py, BLOCK_SIZE - 1, BLOCK_SIZE - 1, COL_CELL_EMPTY);
}

/* ── Number to string helper (no libc printf in WASM) ───────────────── */
static void itoa_pad(uint32_t v, char *buf, int width) {
    buf[width] = '\0';
    for (int i = width - 1; i >= 0; i--) {
        buf[i] = '0' + (v % 10);
        v /= 10;
        if (!v && i > 0) {
            for (int j = i - 1; j >= 0; j--) buf[j] = ' ';
            break;
        }
    }
}

/* ── Board rendering ─────────────────────────────────────────────────── */
static void redraw_board(void) {
    for (int y = 0; y < BOARD_HEIGHT; y++)
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (g.board[y][x])
                draw_block(x, y, PIECE_COLOR[g.board[y][x] - 1]);
            else
                draw_empty_cell(x, y);
        }
}

/* ── Sidebar ─────────────────────────────────────────────────────────── */
static void draw_sidebar_static(void) {
    int x = SIDEBAR_X;
    display_text(x, 22,  "NEXT",  COL_LABEL);
    display_text(x, 90,  "SCORE", COL_LABEL);
    display_text(x, 130, "LINES", COL_LABEL);
    display_text(x, 170, "LEVEL", COL_LABEL);
    display_rect(x, 84,  100, 1, COL_BORDER);
    display_rect(x, 124, 100, 1, COL_BORDER);
    display_rect(x, 164, 100, 1, COL_BORDER);
}

static void draw_next(int force) {
    if (!force && g.next_piece == g.prev_next) return;
    int ox = SIDEBAR_X, oy = 36;
    display_rect(ox, oy, 44, 40, COL_BG);
    uint16_t s = SHAPES[g.next_piece][0];
    uint16_t c = PIECE_COLOR[g.next_piece];
    for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++)
            if (shape_cell(s, px, py)) {
                int bx2 = ox + px * 10, by2 = oy + py * 10;
                display_rect(bx2, by2, 9, 9, c);
                display_rect(bx2, by2, 8, 1, 0xFFFF);
                display_rect(bx2, by2, 1, 8, 0xFFFF);
            }
    g.prev_next = g.next_piece;
}

static void draw_stats(int force) {
    int x = SIDEBAR_X;
    char buf[8];
    if (force || g.score != g.prev_score) {
        display_rect(x, 104, 100, 14, COL_BG);
        itoa_pad(g.score, buf, 6);
        display_text(x, 104, buf, COL_VALUE);
        g.prev_score = g.score;
    }
    if (force || g.lines != g.prev_lines) {
        display_rect(x, 144, 100, 14, COL_BG);
        itoa_pad(g.lines, buf, 4);
        display_text(x, 144, buf, COL_VALUE);
        g.prev_lines = g.lines;
    }
    if (force || g.level != g.prev_level) {
        display_rect(x, 184, 100, 14, COL_BG);
        itoa_pad(g.level, buf, 2);
        display_text(x, 184, buf, COL_VALUE);
        g.prev_level = g.level;
    }
}

/* ── Header bar ──────────────────────────────────────────────────────── */
static void draw_header(void) {
    display_rect(0, 0, 320, 18, COL_HEADER_BG);
    display_text_large(6, 1, "TETRIS", COL_TITLE);
    display_text(275, 3, "LV:", COL_LABEL);
    char buf[3];
    itoa_pad(g.level, buf, 2);
    display_text(298, 3, buf, COL_VALUE);
    g.prev_level = g.level;
}

/* ── Current piece rendering ─────────────────────────────────────────── */
static int is_cur_piece_at(int x, int y) {
    uint16_t s = SHAPES[g.cur_piece][g.cur_rot];
    for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++)
            if (shape_cell(s, px, py) &&
                g.cur_x + px == x && g.cur_y + py == y) return 1;
    return 0;
}

static void erase_piece_at(int piece, int rot, int ox, int oy) {
    uint16_t s = SHAPES[piece][rot];
    for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++) {
            if (!shape_cell(s, px, py)) continue;
            int bx = ox + px, by = oy + py;
            if (by < 0 || by >= BOARD_HEIGHT || bx < 0 || bx >= BOARD_WIDTH) continue;
            if (is_cur_piece_at(bx, by)) continue;
            if (g.board[by][bx])
                draw_block(bx, by, PIECE_COLOR[g.board[by][bx] - 1]);
            else
                draw_empty_cell(bx, by);
        }
}

static void draw_cur_piece(void) {
    uint16_t s = SHAPES[g.cur_piece][g.cur_rot];
    uint16_t c = PIECE_COLOR[g.cur_piece];
    for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++)
            if (shape_cell(s, px, py)) {
                int by = g.cur_y + py;
                if (by >= 0) draw_block(g.cur_x + px, by, c);
            }
}

/* ── Incremental frame update ────────────────────────────────────────── */
static void update_display(void) {
    if (!g.piece_moved && !g.board_changed) return;

    if (g.board_changed) {
        redraw_board();
        g.board_changed = 0;
        g.piece_moved = 1;
    }

    if (g.piece_moved) {
        draw_cur_piece();
        if (g.prev_piece >= 0)
            erase_piece_at(g.prev_piece, g.prev_rot, g.prev_x, g.prev_y);
        g.piece_moved = 0;
    }

    /* Update header level badge */
    if (g.level != g.prev_level) {
        display_rect(298, 3, 20, 12, COL_HEADER_BG);
        char buf[3];
        itoa_pad(g.level, buf, 2);
        display_text(298, 3, buf, COL_VALUE);
    }

    draw_next(0);
    draw_stats(0);

    g.prev_piece = g.cur_piece;
    g.prev_rot   = g.cur_rot;
    g.prev_x     = g.cur_x;
    g.prev_y     = g.cur_y;
}

/* ── Piece logic ─────────────────────────────────────────────────────── */
static void lock_piece(void) {
    uint16_t s = SHAPES[g.cur_piece][g.cur_rot];
    for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++)
            if (shape_cell(s, px, py)) {
                int bx = g.cur_x + px, by = g.cur_y + py;
                if (by >= 0 && by < BOARD_HEIGHT && bx >= 0 && bx < BOARD_WIDTH)
                    g.board[by][bx] = g.cur_piece + 1;
            }
}

static int clear_lines(void) {
    int n = 0;
    for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < BOARD_WIDTH; x++) if (!g.board[y][x]) { full = 0; break; }
        if (full) {
            n++;
            for (int yy = y; yy > 0; yy--)
                for (int x = 0; x < BOARD_WIDTH; x++)
                    g.board[yy][x] = g.board[yy-1][x];
            for (int x = 0; x < BOARD_WIDTH; x++) g.board[0][x] = 0;
            y++;
        }
    }
    return n;
}

static void spawn_piece(void) {
    g.cur_piece = g.next_piece;
    g.next_piece = rand7();
    g.cur_rot = 0;
    g.cur_x = BOARD_WIDTH / 2 - 2;
    g.cur_y = -1;
    g.piece_moved = 1;
    if (collides(g.cur_piece, g.cur_rot, g.cur_x, g.cur_y))
        g.game_over = 1;
}

static void auto_drop(void) {
    if (!collides(g.cur_piece, g.cur_rot, g.cur_x, g.cur_y + 1)) {
        g.cur_y++;
        g.piece_moved = 1;
        return;
    }
    lock_piece();
    g.board_changed = 1;
    int n = clear_lines();
    if (n > 0) {
        static const uint32_t pts[5] = {0, 100, 300, 500, 800};
        g.score += (n <= 4 ? pts[n] : 800) * g.level;
        g.lines += n;
        g.level = g.lines / 10 + 1;
        if (g.level > MAX_LEVEL) g.level = MAX_LEVEL;
        g.drop_delay = INITIAL_DROP_DELAY / g.level;
        if (g.drop_delay < MIN_DROP_DELAY) g.drop_delay = MIN_DROP_DELAY;
    }
    spawn_piece();
}

/* ── Buttons ─────────────────────────────────────────────────────────── */
static int btn_prev[4]      = {0, 0, 0, 0};
static int btn_prev_a       = 0;
static int btn_prev_b       = 0;
static int btn_prev_settings = 0;
static int lr_repeat        = 0;
#define LR_INITIAL 8
#define LR_HELD    3
static int soft_drop_active = 0;
static int pause_requested  = 0;

/* App-switch / restart flags — set by pause menu, acted on by main() */
static int g_exit_to_supervisor = 0;
static int g_restart_game       = 0;

static void handle_buttons(void) {
    int up       = gpio_read(BTN_UP);
    int down     = gpio_read(BTN_DOWN);
    int left     = gpio_read(BTN_LEFT);
    int right    = gpio_read(BTN_RIGHT);
    int a        = gpio_read(BTN_A);
    int b        = gpio_read(BTN_B);
    int settings = gpio_read(BTN_SETTINGS);

    /* SETTINGS — pause on press edge */
    if (settings && !btn_prev_settings) pause_requested = 1;
    btn_prev_settings = settings;

    /* UP / A / B — rotate on press edge, with simple wall-kick */
    if ((up && !btn_prev[0]) || (a && !btn_prev_a) || (b && !btn_prev_b)) {
        int nr = (g.cur_rot + 1) % 4;
        if      (!collides(g.cur_piece, nr, g.cur_x,     g.cur_y)) { g.cur_rot = nr;             g.piece_moved = 1; }
        else if (!collides(g.cur_piece, nr, g.cur_x - 1, g.cur_y)) { g.cur_rot = nr; g.cur_x--;  g.piece_moved = 1; }
        else if (!collides(g.cur_piece, nr, g.cur_x + 1, g.cur_y)) { g.cur_rot = nr; g.cur_x++;  g.piece_moved = 1; }
    }
    btn_prev[0] = up;
    btn_prev_a  = a;
    btn_prev_b  = b;

    /* DOWN — soft drop (handled in game loop) */
    soft_drop_active = down;
    btn_prev[1] = down;

    /* LEFT */
    if (left) {
        int move = 0;
        if (!btn_prev[2])       { move = 1; lr_repeat = LR_INITIAL; }
        else if (--lr_repeat <= 0) { move = 1; lr_repeat = LR_HELD;    }
        if (move && !collides(g.cur_piece, g.cur_rot, g.cur_x - 1, g.cur_y)) {
            g.cur_x--; g.piece_moved = 1;
        }
    } else { lr_repeat = 0; }
    btn_prev[2] = left;

    /* RIGHT */
    if (right) {
        int move = 0;
        if (!btn_prev[3])       { move = 1; lr_repeat = LR_INITIAL; }
        else if (--lr_repeat <= 0) { move = 1; lr_repeat = LR_HELD;    }
        if (move && !collides(g.cur_piece, g.cur_rot, g.cur_x + 1, g.cur_y)) {
            g.cur_x++; g.piece_moved = 1;
        }
    } else { lr_repeat = 0; }
    btn_prev[3] = right;
}

/* ── Display / game init ─────────────────────────────────────────────── */
static void init_display(void) {
    display_clear(COL_BG);
    draw_header();

    /* Board border (2px) */
    int bx = BOARD_X - 2, by = BOARD_Y - 2;
    int bw = BOARD_PX_W + 4, bh = BOARD_PX_H + 4;
    display_rect(bx,      by,      bw, 2,  COL_BORDER);
    display_rect(bx,      by+bh-2, bw, 2,  COL_BORDER);
    display_rect(bx,      by,      2,  bh, COL_BORDER);
    display_rect(bx+bw-2, by,      2,  bh, COL_BORDER);

    for (int y = 0; y < BOARD_HEIGHT; y++)
        for (int x = 0; x < BOARD_WIDTH; x++)
            draw_empty_cell(x, y);

    draw_sidebar_static();

    g.prev_piece = g.prev_rot = g.prev_x = g.prev_y = -1;
    g.prev_next  = -1;
    g.prev_score = g.prev_lines = g.prev_level = 0xFFFFFFFF;
}

static void init_game(void) {
    for (int y = 0; y < BOARD_HEIGHT; y++)
        for (int x = 0; x < BOARD_WIDTH; x++)
            g.board[y][x] = 0;
    g.score = 0; g.lines = 0; g.level = 1;
    g.drop_delay = INITIAL_DROP_DELAY;
    g.game_over  = 0;
    g.piece_moved = 1; g.board_changed = 0;
    g.next_piece = rand7();
    spawn_piece();
    init_display();
    draw_next(1);
    draw_stats(1);
}
/* ── In-game pause menu ─────────────────────────────────────────────────── */
#define MENU_BACK    0
#define MENU_EXIT    1
#define MENU_RESTART 2
#define MENU_ITEMS   3

static const char *MENU_LABELS[MENU_ITEMS] = {
    "[   Go Back   ]",
    "[Exit to Menu ]",
    "[Restart Game ]",
};

/**
 * @brief Show the pause overlay and wait for a menu selection.
 *
 * Uses UP/DOWN for navigation and A or B to confirm.  Settings button
 * acts as a quick "Go Back" shortcut.
 *
 * @return MENU_BACK, MENU_EXIT, or MENU_RESTART
 */
static int show_pause_menu(void)
{
    /* Overlay dimensions — centred on the board column */
    const int ITEM_H = 22;
    const int PAD    = 6;
    const int mw     = BOARD_PX_W - 8;
    const int mh     = PAD + 16 + MENU_ITEMS * ITEM_H + PAD;
    const int mx     = BOARD_X + 4;
    const int my     = BOARD_Y + (BOARD_PX_H - mh) / 2;

    int cur = MENU_BACK;

    /* Debounce: wait for the settings button to be released before reading
     * any new presses. */
    while (gpio_read(BTN_SETTINGS)) delay(10000);
    delay(50000);

    int prev_s = 0, prev_u = 0, prev_d = 0, prev_a = 0, prev_b = 0;
    int redraw = 1;

    while (1) {
        if (redraw) {
            /* Outer border */
            display_rect(mx - 2, my - 2, mw + 4, mh + 4, COL_BORDER);
            /* Background */
            display_rect(mx, my, mw, mh, 0x0821);
            /* Title */
            display_text(mx + PAD, my + PAD, "PAUSED", COL_TITLE);
            /* Items */
            for (int i = 0; i < MENU_ITEMS; i++) {
                int iy     = my + PAD + 18 + i * ITEM_H;
                uint32_t bg = (i == cur) ? 0x001F : 0x0821; /* blue / dark */
                uint32_t fg = (i == cur) ? 0xFFFF : 0xC618; /* white / light-gray */
                display_rect(mx, iy - 2, mw, ITEM_H - 2, bg);
                display_text(mx + PAD, iy, MENU_LABELS[i], fg);
            }
            display_flush();
            redraw = 0;
        }

        int s = gpio_read(BTN_SETTINGS);
        int u = gpio_read(BTN_UP);
        int d = gpio_read(BTN_DOWN);
        int a = gpio_read(BTN_A);
        int b = gpio_read(BTN_B);

        if (s && !prev_s)  { return MENU_BACK; }  /* quick-exit shortcut */
        if (u && !prev_u)  { cur = (cur > 0) ? cur - 1 : MENU_ITEMS - 1; redraw = 1; }
        if (d && !prev_d)  { cur = (cur < MENU_ITEMS - 1) ? cur + 1 : 0; redraw = 1; }
        if ((a && !prev_a) || (b && !prev_b)) { return cur; }

        prev_s = s;  prev_u = u;  prev_d = d;  prev_a = a;  prev_b = b;
        delay(20000);
    }
}
/* ── Game loop ───────────────────────────────────────────────────────── */
static void game_loop(void) {
    uint32_t drop_acc = 0;

    while (!g.game_over) {
        handle_buttons();

        /* SETTINGS pressed — open pause menu */
        if (pause_requested) {
            pause_requested = 0;
            int choice = show_pause_menu();
            if (choice == MENU_EXIT) {
                g_exit_to_supervisor = 1;
                return; /* game_loop returns; main() handles the switch */
            }
            if (choice == MENU_RESTART) {
                g_restart_game = 1;
                return; /* game_loop returns; main() re-inits and loops */
            }
            /* MENU_BACK — force full redraw to wipe the overlay */
            g.board_changed = 1;
            g.piece_moved   = 1;
        }

        if (soft_drop_active) {
            /* Soft-drop: move 1 cell per SOFT_DROP_DELAY µs, score 1 pt/cell */
            if (!collides(g.cur_piece, g.cur_rot, g.cur_x, g.cur_y + 1)) {
                g.cur_y++;
                g.piece_moved = 1;
                g.score++;
            } else {
                /* Hit bottom while soft-dropping — lock immediately */
                auto_drop();
            }
            drop_acc = 0;
            update_display();
            display_flush();
            delay(SOFT_DROP_DELAY);
        } else {
            drop_acc += 20000;
            if (drop_acc >= g.drop_delay) {
                auto_drop();
                drop_acc = 0;
            }
            update_display();
            display_flush();
            delay(20000);
        }
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS Tetris v2.0");

    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_X,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_Y,        GPIO_INPUT | GPIO_PULL_DOWN);

    /* Title screen — centred on 320×240 */
    display_clear(COL_BG);
    display_text_large(124,  80, "TETRIS",         COL_TITLE);
    display_text_large(118, 115, "AkiraOS",        COL_LABEL);
    display_text(112,  170, "Press any button",   COL_VALUE);
    display_flush();

    /* Wait for a button press — count frames for RNG seed (human reaction
     * time is unpredictable, giving a different seed on every run)       */
    uint32_t seed = 1;
    while (!gpio_read(BTN_UP)   && !gpio_read(BTN_DOWN) &&
           !gpio_read(BTN_LEFT) && !gpio_read(BTN_RIGHT) &&
           !gpio_read(BTN_A)    && !gpio_read(BTN_B)) {
        seed++;
        delay(20000);
    }
    rng_seed(seed);
    delay(100000);  /* debounce */

    printf("Starting game...");

    /* Game loop with restart and exit-to-supervisor support */
    while (1) {
        g_restart_game       = 0;
        g_exit_to_supervisor = 0;
        init_game();
        game_loop();

        if (g_exit_to_supervisor) {
            /* Start (or resume) supervisor then exit cleanly.
             * Supervisor detects the lifecycle events for both this app
             * stopping and itself resuming, then redraws its launcher. */
            app_switch("supervisor");
            return 0;
        }
        if (g_restart_game) {
            seed++; /* Different RNG seed on each restart */
            rng_seed(seed);
            continue;
        }
        break; /* Normal game over */
    }

    /* Game over screen */
    display_clear(COL_BG);
    display_text_large(110,  60, "GAME",  COL_GAMEOVER_T);
    display_text_large(110,  95, "OVER",  COL_GAMEOVER_T);
    display_text(110,  145, "SCORE:",    COL_LABEL);
    char buf[8];
    itoa_pad(g.score, buf, 7);
    display_text(110,  160, buf,         COL_GAMEOVER_S);
    display_text(110,  185, "LINES:",    COL_LABEL);
    itoa_pad(g.lines, buf, 4);
    display_text(175,  185, buf,         COL_VALUE);
    display_flush();
    delay(5000000);

    printf("Game over!");
    return 0;
}
