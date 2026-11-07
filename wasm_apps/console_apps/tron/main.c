/**
 * @file main.c
 * @brief TRON — Light Cycle trail game vs AI for AkiraOS
 *
 * Controls:
 *   LEFT  = turn your cycle left (relative to heading)
 *   RIGHT = turn your cycle right
 *   SETTINGS = pause
 *
 * Leave a permanent white trail. Force the AI to crash into a wall or trail
 * before you do. First to 3 round wins takes the match.
 *
 * Monochrome: player trail = solid white tiles. AI trail = white outline tiles.
 * Both heads blink to show current position.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Screen ──────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;
#define CX      (SCR_W / 2)
#define CY      (SCR_H / 2)

/* ── Buttons ─────────────────────────────────────────────────────────── */
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_SETTINGS 0

/* ── Monochrome ──────────────────────────────────────────────────────── */
#define WHITE  0xFFFF
#define BLACK  0x0000

/* ── Grid ────────────────────────────────────────────────────────────── */
#define CELL      4     /* pixels per grid cell */
#define GRID_W   80     /* cells wide (320 / 4) */
#define GRID_H   60     /* cells tall (240 / 4) */

/* Cell values */
#define EMPTY    0
#define P_TRAIL  1
#define AI_TRAIL 2
#define WALL     3

/* Tick rate: 1 move per N display frames (~30fps / 3 = 10 moves/sec) */
#define TICKS_PER_MOVE 3
#define FRAME_MS       33

/* ── Directions: 0=R 1=D 2=L 3=U ────────────────────────────────────── */
static const int8_t DX[4] = { 1,  0, -1,  0 };
static const int8_t DY[4] = { 0,  1,  0, -1 };

/* Turn left / right relative to current heading */
#define TURN_L(d)  (((d) + 3) & 3)
#define TURN_R(d)  (((d) + 1) & 3)
#define REVERSE(d) (((d) + 2) & 3)

/* ── State ───────────────────────────────────────────────────────────── */
static uint8_t grid[GRID_H][GRID_W];

static int8_t  px, py, pdir;   /* player position + heading */
static int8_t  ax, ay, adir;   /* AI position + heading */

static uint8_t p_wins, ai_wins;
static uint8_t round_over, match_over, paused;
static uint8_t frame_tick, move_timer;

/* input edge-detection */
static int pl_prev, pr_prev, ps_prev;

/* ── Grid helpers ────────────────────────────────────────────────────── */
static int cell_blocked(int x, int y)
{
    if (x <= 0 || x >= GRID_W - 1 || y <= 0 || y >= GRID_H - 1) return 1;
    return grid[y][x] != EMPTY;
}

/* ── Cell drawing ────────────────────────────────────────────────────── */
static void draw_cell_player(int x, int y)
{
    int sx = x * CELL, sy = y * CELL;
    display_rect(sx, sy, CELL, CELL, WHITE);
}

static void draw_cell_ai(int x, int y)
{
    int sx = x * CELL, sy = y * CELL;
    /* Outline only: distinguishable from solid player trail */
    display_rect(sx, sy, CELL, CELL, WHITE);
    display_rect(sx + 1, sy + 1, CELL - 2, CELL - 2, BLACK);
}

static void draw_head_player(int x, int y, int blink)
{
    int sx = x * CELL, sy = y * CELL;
    if (!blink) {
        display_rect(sx, sy, CELL, CELL, WHITE);
    } else {
        /* Invert: black square with white border to flash */
        display_rect(sx, sy, CELL, CELL, BLACK);
        display_rect_outline(sx, sy, CELL, CELL, WHITE);
    }
}

static void draw_head_ai(int x, int y, int blink)
{
    int sx = x * CELL, sy = y * CELL;
    if (!blink) {
        display_rect(sx, sy, CELL, CELL, WHITE);
        display_pixel(sx + 1, sy + 1, BLACK);
        display_pixel(sx + CELL - 2, sy + 1, BLACK);
    } else {
        display_rect(sx, sy, CELL, CELL, BLACK);
        display_rect_outline(sx, sy, CELL, CELL, WHITE);
    }
}

/* ── AI logic ────────────────────────────────────────────────────────── */

/* Count open cells looking straight in direction d from (x, y) */
static int ai_lookahead(int x, int y, int d)
{
    int cx = x + DX[d], cy = y + DY[d];
    int count = 0;
    while (!cell_blocked(cx, cy) && count < 32) {
        count++;
        cx += DX[d];
        cy += DY[d];
    }
    return count;
}

/* Simple open-space flood score: lookahead straight + partial sides */
static int ai_score(int x, int y, int d)
{
    int straight = ai_lookahead(x, y, d);
    int left_v   = ai_lookahead(x, y, TURN_L(d));
    int right_v  = ai_lookahead(x, y, TURN_R(d));
    /* Weight: ahead is most important, sides add some bonus */
    return straight * 3 + left_v + right_v;
}

static int8_t ai_choose(void)
{
    int rev = REVERSE(adir);
    int8_t best_dir = adir;
    int    best_score = -1;

    for (int d = 0; d < 4; d++) {
        if (d == rev) continue;                  /* no reversing */
        int nx = ax + DX[d], ny = ay + DY[d];
        if (cell_blocked(nx, ny)) continue;      /* blocked */

        int s = ai_score(ax, ay, d);
        /* Add small random factor so AI is beatable */
        s += (int)(((uint32_t)d * 17 + frame_tick * 7) & 7) - 3;
        if (s > best_score) { best_score = s; best_dir = (int8_t)d; }
    }

    /* If current direction is fine, prefer not turning */
    if (best_dir == adir) return adir;
    return best_dir;
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_round(void)
{
    /* Clear grid */
    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            grid[y][x] = EMPTY;

    /* Borders */
    for (int x = 0; x < GRID_W; x++) { grid[0][x] = WALL; grid[GRID_H-1][x] = WALL; }
    for (int y = 0; y < GRID_H; y++) { grid[y][0] = WALL; grid[y][GRID_W-1] = WALL; }

    /* Players start on opposite sides, heading toward each other */
    px = 12; py = GRID_H / 2; pdir = 0; /* right */
    ax = GRID_W - 13; ay = GRID_H / 2; adir = 2; /* left */

    grid[py][px] = P_TRAIL;
    grid[ay][ax] = AI_TRAIL;

    round_over = 0;
    move_timer = 0;
    frame_tick = 0;

    /* Draw initial state */
    display_clear(BLACK);

    /* Border */
    display_rect_outline(0, 0, SCR_W, SCR_H, WHITE);

    draw_cell_player(px, py);
    draw_cell_ai(ax, ay);

    /* Score bar at bottom */
    display_rect(0, SCR_H - CELL, SCR_W, CELL, BLACK);
    display_text(SCR_W * 1 / 100, SCR_H - CELL + 1, "You:", WHITE);
    display_number(SCR_W * 11 / 100, SCR_H - CELL + 1, p_wins, WHITE);
    display_text(CX, SCR_H - CELL + 1, "AI:", WHITE);
    display_number(CX + SCR_W * 7 / 100, SCR_H - CELL + 1, ai_wins, WHITE);

    display_flush();
}

static void init_match(void)
{
    p_wins = 0; ai_wins = 0;
    match_over = 0; paused = 0;
    pl_prev = 0; pr_prev = 0; ps_prev = 0;
    init_round();
}

/* ── Game tick ───────────────────────────────────────────────────────── */
static void game_tick(void)
{
    /* AI decides direction */
    adir = ai_choose();

    /* Compute new positions */
    int npx = px + DX[pdir], npy = py + DY[pdir];
    int nax = ax + DX[adir], nay = ay + DY[adir];

    int p_crash  = cell_blocked(npx, npy);
    int ai_crash = cell_blocked(nax, nay);

    /* Head-on collision: both crash */
    if (!p_crash && !ai_crash && npx == nax && npy == nay)
        p_crash = ai_crash = 1;

    if (!p_crash) {
        /* Draw old head as trail */
        draw_cell_player(px, py);
        grid[py][px] = P_TRAIL;
        px = (int8_t)npx; py = (int8_t)npy;
        grid[py][px] = P_TRAIL;
    }

    if (!ai_crash) {
        draw_cell_ai(ax, ay);
        grid[ay][ax] = AI_TRAIL;
        ax = (int8_t)nax; ay = (int8_t)nay;
        grid[ay][ax] = AI_TRAIL;
    }

    if (p_crash || ai_crash) {
        round_over = 1;
        if (p_crash && !ai_crash)       ai_wins++;
        else if (!p_crash && ai_crash)  p_wins++;
        /* Both crash: no point awarded */

        if (p_wins >= 3 || ai_wins >= 3) match_over = 1;
    }
}

/* ── Input ───────────────────────────────────────────────────────────── */
static void handle_input(void)
{
    int l = gpio_read(BTN_LEFT);
    int r = gpio_read(BTN_RIGHT);
    int s = gpio_read(BTN_SETTINGS);

    if (l && !pl_prev) pdir = TURN_L(pdir);
    if (r && !pr_prev) pdir = TURN_R(pdir);
    if (s && !ps_prev) paused = !paused;

    pl_prev = l; pr_prev = r; ps_prev = s;
}

/* ── Per-frame draw (just heads) ─────────────────────────────────────── */
static void draw_heads(void)
{
    int blink = (frame_tick & 4) != 0;
    draw_head_player(px, py, blink);
    draw_head_ai(ax, ay, blink);

    /* Score strip refresh */
    display_rect(0, SCR_H - CELL, SCR_W, CELL, BLACK);
    display_text(SCR_W * 1 / 100, SCR_H - CELL + 1, "You:", WHITE);
    display_number(SCR_W * 11 / 100, SCR_H - CELL + 1, p_wins, WHITE);
    display_text(CX, SCR_H - CELL + 1, "AI:", WHITE);
    display_number(CX + SCR_W * 7 / 100, SCR_H - CELL + 1, ai_wins, WHITE);

    if (paused) {
        display_rect(CX - SCR_W * 9 / 100, CY - SCR_H * 3 / 100, SCR_W * 18 / 100, SCR_H * 7 / 100, BLACK);
        display_text(CX - SCR_W * 8 / 100, CY - SCR_H * 3 / 100 + 2, "PAUSE", WHITE);
    }

    display_flush();
    frame_tick++;
}

/* ── Result screens ──────────────────────────────────────────────────── */
static void show_round_result(void)
{
    /* Flash the loser's position */
    for (int i = 0; i < 6; i++) {
        display_rect(0, CY - SCR_H * 4 / 100, SCR_W, SCR_H * 8 / 100, BLACK);
        if (i & 1) {
            if (p_wins > ai_wins)
                display_text(CX - SCR_W * 10 / 100, CY - SCR_H * 2 / 100, "YOU WIN!", WHITE);
            else if (ai_wins > p_wins)
                display_text(CX - SCR_W * 6 / 100, CY - SCR_H * 2 / 100, "AI WINS", WHITE);
            else
                display_text(CX - SCR_W * 6 / 100, CY - SCR_H * 2 / 100, "DRAW", WHITE);
        }
        display_flush();
        delay(150000);
    }
}

static void show_match_result(void)
{
    display_clear(BLACK);
    display_rect_outline(SCR_W * 3 / 100, SCR_H * 4 / 100, SCR_W * 94 / 100, SCR_H * 92 / 100, WHITE);

    if (p_wins > ai_wins) {
        display_text_large(SCR_W * 18 / 100, SCR_H * 21 / 100, "YOU WIN!", WHITE);
        display_text(SCR_W * 24 / 100, SCR_H * 42 / 100, "You beat the AI!", WHITE);
    } else {
        display_text_large(SCR_W * 14 / 100, SCR_H * 21 / 100, "AI WINS", WHITE);
        display_text(SCR_W * 22 / 100, SCR_H * 42 / 100, "Better luck next time!", WHITE);
    }

    display_text(SCR_W * 13 / 100, SCR_H * 58 / 100, "Final score:", WHITE);
    display_text(SCR_W * 16 / 100, SCR_H * 66 / 100, "You:", WHITE);
    display_number(SCR_W * 28 / 100, SCR_H * 66 / 100, p_wins, WHITE);
    display_text(SCR_W * 16 / 100, SCR_H * 73 / 100, "AI: ", WHITE);
    display_number(SCR_W * 28 / 100, SCR_H * 73 / 100, ai_wins, WHITE);

    display_text(SCR_W * 18 / 100, SCR_H * 88 / 100, "Press A for a rematch", WHITE);
    display_flush();
}

/* ── Title ───────────────────────────────────────────────────────────── */
static void draw_title(void)
{
    display_clear(BLACK);

    /* Simulate a Tron grid with a few trail lines */
    display_hline(SCR_W * 6 / 100, SCR_H * 21 / 100, SCR_W * 56 / 100, WHITE);
    display_vline(SCR_W * 63 / 100, SCR_H * 21 / 100, SCR_H * 33 / 100, WHITE);
    display_hline(SCR_W * 12 / 100, SCR_H * 54 / 100, SCR_W * 50 / 100, WHITE);
    display_vline(SCR_W * 12 / 100, SCR_H * 21 / 100, SCR_H * 33 / 100, WHITE);
    display_hline(SCR_W * 12 / 100, SCR_H * 21 / 100, SCR_W * 25 / 100, WHITE);  /* inner */
    display_vline(SCR_W * 38 / 100, SCR_H * 21 / 100, SCR_H * 17 / 100, WHITE);

    display_text_large(SCR_W * 18 / 100, SCR_H * 8 / 100, "TRON", WHITE);

    display_text(SCR_W * 9 / 100,  SCR_H * 69 / 100, "LEFT / RIGHT : turn your cycle", WHITE);
    display_text(SCR_W * 9 / 100,  SCR_H * 76 / 100, "Leave a trail. Trap the AI.", WHITE);
    display_text(SCR_W * 9 / 100,  SCR_H * 83 / 100, "First to 3 round wins!", WHITE);

    display_text(SCR_W * 19 / 100, SCR_H * 92 / 100, "Press A to race!", WHITE);
    display_flush();
}

/* ── Main ────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS TRON v1.0");

    display_get_size(&SCR_W, &SCR_H);

    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    draw_title();
    while (!gpio_read(BTN_A)) delay(20000);
    delay(80000);

    int tmr = timer_create();

    while (1) {
        init_match();

        while (!match_over) {
            while (!round_over) {
                timer_start(tmr);

                handle_input();

                if (!paused) {
                    move_timer++;
                    if (move_timer >= TICKS_PER_MOVE) {
                        move_timer = 0;
                        game_tick();
                    }
                }

                if (!round_over) draw_heads();

                int left = FRAME_MS - timer_elapsed(tmr);
                if (left > 1) delay((uint32_t)left * 1000);
            }

            show_round_result();
            delay(400000);

            if (!match_over) {
                init_round();
            }
        }

        show_match_result();
        delay(500000);
        while (!gpio_read(BTN_A)) delay(20000);
        delay(80000);
    }

    return 0;
}
