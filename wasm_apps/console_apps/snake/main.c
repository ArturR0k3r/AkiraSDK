/**
 * @file snake.c
 * @brief Snake game WASM application for AkiraOS
 *
 * Classic snake: eat apples, grow longer, don't bite yourself or hit a wall.
 * Speed ramps up with every apple. Best score is saved to NVS.
 *
 * Controls (AkiraConsole DPAD):
 *   UP/DOWN/LEFT/RIGHT = steer   (can't reverse into yourself)
 *   A (hold)           = dash (move faster while held)
 *   SETTINGS/OK        = pause menu
 *
 * Size-aware: the grid is derived from display_get_size() at runtime so it
 * fills both the 320-wide console and the 400-wide Sharp LCD.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Grid geometry (computed at runtime from screen size) ───────────────── */
#define CELL 10       /* pixels per grid cell                     */
#define HEADER_H 20   /* top status bar height                    */
#define MAX_COLS 40   /* 400px / CELL — covers the widest panel   */
#define MAX_ROWS 24   /* (260-ish usable) / CELL                  */
#define MAX_CELLS (MAX_COLS * MAX_ROWS)

static int COLS, ROWS; /* active grid, clamped to the MAX_* bounds */
static int OX, OY;     /* play-area origin in pixels (centred)     */

/* ── Button pins (AkiraConsole) — mirrors sibling game apps ─────────────── */
#define BTN_UP 4
#define BTN_DOWN 5
#define BTN_LEFT 6
#define BTN_RIGHT 7
#define BTN_A 15       /* A — dash (hold)          */
#define BTN_B 16       /* B — confirm in menus     */
#define BTN_SETTINGS 0 /* BTN.OK = GPIO0, pause     */
#define BTN_X 17       /* unused                   */
#define BTN_Y 41       /* unused                   */

/* ── Colour palette (RGB565) ────────────────────────────────────────────── */
#define COL_BG 0x0000         /* black playfield            */
#define COL_GRID 0x0841       /* barely-there grid dot      */
#define COL_BORDER 0x4A69     /* mid-grey play border       */
#define COL_HEADER_BG 0x0140  /* deep green status bar      */
#define COL_SNAKE_HEAD 0x07E0 /* bright green head          */
#define COL_SNAKE_BODY 0x0480 /* darker green body          */
#define COL_FOOD 0xF800       /* red apple                  */
#define COL_FOOD_LEAF 0x05E0  /* apple leaf                 */
#define COL_TITLE 0x07FF      /* cyan title                 */
#define COL_LABEL 0x8410      /* dim grey labels            */
#define COL_VALUE 0xFFFF      /* white numbers              */
#define COL_HINT 0x4208       /* dark grey hint text        */
#define COL_GAMEOVER_T 0xF800 /* red GAME OVER              */
#define COL_GAMEOVER_S 0xFFE0 /* yellow final score         */
#define COL_NEWBEST 0x07FF    /* cyan "NEW BEST!"           */

/* ── Timing ─────────────────────────────────────────────────────────────── */
#define POLL_US 20000         /* input poll / frame tick, µs      */
#define START_STEP_US 180000  /* snake advances every 180 ms      */
#define MIN_STEP_US 70000     /* fastest step after many apples   */
#define STEP_RAMP_US 4000     /* shave off per apple eaten        */
#define DASH_DIV 2            /* A held → step delay / DASH_DIV   */

/* ── Directions ─────────────────────────────────────────────────────────── */
#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_RIGHT 3

/* ── Game state ─────────────────────────────────────────────────────────── */
typedef struct
{
    /* Snake body as a circular buffer of grid cells. */
    uint8_t sx[MAX_CELLS];
    uint8_t sy[MAX_CELLS];
    int head;   /* index of the head cell          */
    int len;    /* current body length             */
    int dir;    /* current heading                 */
    int next_dir; /* queued heading from input     */

    uint8_t occ[MAX_ROWS][MAX_COLS]; /* cell occupied by body */

    int food_x, food_y;
    uint32_t score;
    uint32_t step_us;
    int game_over;
} game_t;

static game_t g;
static uint32_t best_score = 0;
static int new_best = 0;

/* Pause-menu / lifecycle flags (mirrors tetris) */
static volatile int pause_requested = 0;
static int g_exit_to_supervisor = 0;
static int g_restart_game = 0;

/* ── PRNG (LCG, seeded from HW RNG) ─────────────────────────────────────── */
static uint32_t rng = 12345;
static uint32_t rand_u32(void)
{
    rng = rng * 1103515245 + 12345;
    return (rng >> 8);
}
static int rand_range(int n) { return (int)(rand_u32() % (uint32_t)n); }

static void rng_seed(uint32_t seed)
{
    rng = seed ^ 0xDEADBEEF;
    rand_u32();
    rand_u32();
    rand_u32();
}

/**
 * @brief Produce a fresh, unpredictable seed.
 * Primary source is the hardware RNG; falls back to uptime mixed with @p mix
 * (title-screen frame count) so the seed still varies if "crypto" is denied.
 */
static uint32_t random_seed(uint32_t mix)
{
    uint32_t s = 0;
    if (crypto_random(&s, sizeof(s)) == 0 && s != 0)
        return s;
    uint32_t up = (uint32_t)rtc_get_uptime_ms();
    return (up * 2654435761u) ^ (mix * 40503u) ^ 0xA5A5A5A5u;
}

/* ── Small string helpers (no libc) ─────────────────────────────────────── */
static int str_to_int(const char *s)
{
    int v = 0;
    for (int i = 0; s[i] >= '0' && s[i] <= '9'; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}
static void int_to_str(uint32_t v, char *b)
{
    char t[12];
    int n = 0, p = 0;
    if (v == 0)
        t[n++] = '0';
    while (v)
    {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        b[p++] = t[--n];
    b[p] = '\0';
}

static void load_best(void)
{
    char b[16];
    best_score = (settings_get("snake/best", b, sizeof(b)) == 0)
                     ? (uint32_t)str_to_int(b)
                     : 0;
}
static void save_best(void)
{
    if (g.score > best_score)
    {
        best_score = g.score;
        new_best = 1;
        char b[16];
        int_to_str(best_score, b);
        settings_set("snake/best", b);
    }
}

/* ── Grid → pixel helpers ───────────────────────────────────────────────── */
static inline int px_x(int cx) { return OX + cx * CELL; }
static inline int px_y(int cy) { return OY + cy * CELL; }

static void draw_cell(int cx, int cy, uint32_t color)
{
    /* 1px inset so neighbouring segments stay visually separated */
    display_rect(px_x(cx) + 1, px_y(cy) + 1, CELL - 2, CELL - 2, color);
}
static void clear_cell(int cx, int cy)
{
    display_rect(px_x(cx), px_y(cy), CELL, CELL, COL_BG);
}
static void draw_food(void)
{
    int cxp = px_x(g.food_x) + CELL / 2;
    int cyp = px_y(g.food_y) + CELL / 2;
    display_circle_fill(cxp, cyp, CELL / 2 - 1, COL_FOOD);
    display_pixel(cxp, cyp - CELL / 2 + 1, COL_FOOD_LEAF); /* tiny stem */
}

/* ── Body circular-buffer accessors ─────────────────────────────────────── */
static inline int tail_index(void)
{
    return (g.head - g.len + 1 + MAX_CELLS) % MAX_CELLS;
}

/* ── Food placement — pick a random unoccupied cell ─────────────────────── */
static void place_food(void)
{
    int free_cells = COLS * ROWS - g.len;
    if (free_cells <= 0)
    {
        g.food_x = -1; /* board full — effectively a win */
        return;
    }
    /* Pick the Nth free cell so placement stays O(grid) even when crowded. */
    int target = rand_range(free_cells);
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if (!g.occ[y][x])
            {
                if (target-- == 0)
                {
                    g.food_x = x;
                    g.food_y = y;
                    return;
                }
            }
}

/* ── Header bar ─────────────────────────────────────────────────────────── */
static uint32_t prev_score_drawn = 0xFFFFFFFF;
static void draw_header(int force)
{
    if (!force && prev_score_drawn == g.score)
        return;
    prev_score_drawn = g.score;

    display_rect(0, 0, SCR_W, HEADER_H, COL_HEADER_BG);
    display_text(4, 6, "SNAKE", COL_TITLE);

    char b[16];
    display_text(SCR_W / 2 - 30, 6, "SCORE", COL_LABEL);
    int_to_str(g.score, b);
    display_text(SCR_W / 2 + 18, 6, b, COL_VALUE);

    display_text(SCR_W - 90, 6, "BEST", COL_LABEL);
    int_to_str(best_score, b);
    display_text(SCR_W - 50, 6, b, COL_VALUE);
}

/* ── Full-frame redraw (title->play transition and after pause) ─────────── */
static void draw_full(void)
{
    display_clear(COL_BG);
    draw_header(1);

    /* Play-area border */
    display_rect_outline(OX - 1, OY - 1, COLS * CELL + 2, ROWS * CELL + 2,
                         COL_BORDER);

    /* Snake */
    for (int i = 0; i < g.len; i++)
    {
        int idx = (g.head - i + MAX_CELLS) % MAX_CELLS;
        draw_cell(g.sx[idx], g.sy[idx],
                  (i == 0) ? COL_SNAKE_HEAD : COL_SNAKE_BODY);
    }
    if (g.food_x >= 0)
        draw_food();
    display_flush();
}

/* ── Init a fresh game ──────────────────────────────────────────────────── */
static void init_game(void)
{
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            g.occ[y][x] = 0;

    g.len = 3;
    g.dir = DIR_RIGHT;
    g.next_dir = DIR_RIGHT;
    g.score = 0;
    g.step_us = START_STEP_US;
    g.game_over = 0;

    int cy = ROWS / 2;
    int cx = COLS / 3;
    /* Lay the 3-cell snake out head-first (head at highest buffer index). */
    for (int i = 0; i < g.len; i++)
    {
        int bx = cx - (g.len - 1 - i); /* tail ... head left→right */
        g.sx[i] = (uint8_t)bx;
        g.sy[i] = (uint8_t)cy;
        g.occ[cy][bx] = 1;
    }
    g.head = g.len - 1;

    place_food();
    prev_score_drawn = 0xFFFFFFFF;
}

/* ── Input ──────────────────────────────────────────────────────────────── */
static int opposite(int a, int b)
{
    return (a == DIR_UP && b == DIR_DOWN) || (a == DIR_DOWN && b == DIR_UP) ||
           (a == DIR_LEFT && b == DIR_RIGHT) || (a == DIR_RIGHT && b == DIR_LEFT);
}

static int prev_settings = 0;
static int dash_held = 0;
static void handle_buttons(void)
{
    int u = gpio_read(BTN_UP);
    int d = gpio_read(BTN_DOWN);
    int l = gpio_read(BTN_LEFT);
    int r = gpio_read(BTN_RIGHT);
    int s = gpio_read(BTN_SETTINGS);

    /* Steer: queue the new heading unless it reverses the current one.
     * Compare against g.dir (committed) so a double-tap can't fold back. */
    int want = -1;
    if (u) want = DIR_UP;
    else if (d) want = DIR_DOWN;
    else if (l) want = DIR_LEFT;
    else if (r) want = DIR_RIGHT;

    if (want >= 0 && !opposite(g.dir, want))
        g.next_dir = want;

    dash_held = gpio_read(BTN_A);

    if (s && !prev_settings)
        pause_requested = 1;
    prev_settings = s;
}

/* ── One simulation step ────────────────────────────────────────────────── */
static void step(void)
{
    g.dir = g.next_dir;

    int hx = g.sx[g.head];
    int hy = g.sy[g.head];
    int nx = hx, ny = hy;
    switch (g.dir)
    {
    case DIR_UP: ny--; break;
    case DIR_DOWN: ny++; break;
    case DIR_LEFT: nx--; break;
    case DIR_RIGHT: nx++; break;
    }

    /* Wall collision */
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS)
    {
        g.game_over = 1;
        return;
    }

    int grow = (nx == g.food_x && ny == g.food_y);

    /* Self collision. The tail cell will vacate this tick unless we grow,
     * so running into the current tail is legal when not growing. */
    int t = tail_index();
    if (g.occ[ny][nx] && !(!grow && nx == g.sx[t] && ny == g.sy[t]))
    {
        g.game_over = 1;
        return;
    }

    /* Advance: drop tail first (unless growing) so occ stays correct. */
    if (!grow)
    {
        int tx = g.sx[t], ty = g.sy[t];
        g.occ[ty][tx] = 0;
        clear_cell(tx, ty);
        g.len--;
    }

    /* New head */
    g.head = (g.head + 1) % MAX_CELLS;
    g.sx[g.head] = (uint8_t)nx;
    g.sy[g.head] = (uint8_t)ny;
    g.occ[ny][nx] = 1;
    g.len++;

    /* Repaint: recolour the old head as body, paint the new head. */
    int prev_head = (g.head - 1 + MAX_CELLS) % MAX_CELLS;
    if (g.len > 1)
        draw_cell(g.sx[prev_head], g.sy[prev_head], COL_SNAKE_BODY);
    draw_cell(nx, ny, COL_SNAKE_HEAD);

    if (grow)
    {
        g.score++;
        if (g.step_us > MIN_STEP_US + STEP_RAMP_US)
            g.step_us -= STEP_RAMP_US;
        place_food();
        if (g.food_x < 0) /* board filled — treat as game over (win) */
            g.game_over = 1;
        else
            draw_food();
        draw_header(0);
    }
}

/* ── Pause menu (mirrors tetris) ────────────────────────────────────────── */
#define MENU_BACK 0
#define MENU_EXIT 1
#define MENU_RESTART 2
#define MENU_ITEMS 3

static const char *MENU_LABELS[MENU_ITEMS] = {
    "[   Resume    ]",
    "[Exit to Menu ]",
    "[Restart Game ]",
};

static int show_pause_menu(void)
{
    const int ITEM_H = 22;
    const int PAD = 6;
    const int mw = 150;
    const int mh = PAD + 16 + MENU_ITEMS * ITEM_H + PAD;
    const int mx = (SCR_W - mw) / 2;
    const int my = (SCR_H - mh) / 2;

    int cur = MENU_BACK;

    while (gpio_read(BTN_SETTINGS))
        delay(10000);
    delay(50000);

    int prev_s = 0, prev_u = 0, prev_d = 0, prev_a = 0, prev_b = 0;
    int redraw = 1;

    while (1)
    {
        if (redraw)
        {
            display_rect(mx - 2, my - 2, mw + 4, mh + 4, COL_BORDER);
            display_rect(mx, my, mw, mh, 0x0821);
            display_text(mx + PAD, my + PAD, "PAUSED", COL_TITLE);
            for (int i = 0; i < MENU_ITEMS; i++)
            {
                int iy = my + PAD + 18 + i * ITEM_H;
                uint32_t bg = (i == cur) ? 0x001F : 0x0821;
                uint32_t fg = (i == cur) ? 0xFFFF : 0xC618;
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

        if (s && !prev_s)
            return MENU_BACK;
        if (u && !prev_u)
        {
            cur = (cur > 0) ? cur - 1 : MENU_ITEMS - 1;
            redraw = 1;
        }
        if (d && !prev_d)
        {
            cur = (cur < MENU_ITEMS - 1) ? cur + 1 : 0;
            redraw = 1;
        }
        if ((a && !prev_a) || (b && !prev_b))
            return cur;

        prev_s = s;
        prev_u = u;
        prev_d = d;
        prev_a = a;
        prev_b = b;
        delay(20000);
    }
}

/* ── Game loop ──────────────────────────────────────────────────────────── */
static void game_loop(void)
{
    uint32_t acc = 0;

    while (!g.game_over)
    {
        handle_buttons();

        if (pause_requested)
        {
            pause_requested = 0;
            int choice = show_pause_menu();
            if (choice == MENU_EXIT)
            {
                g_exit_to_supervisor = 1;
                return;
            }
            if (choice == MENU_RESTART)
            {
                g_restart_game = 1;
                return;
            }
            draw_full(); /* wipe the overlay */
            acc = 0;
            /* swallow any button still held from the menu */
            prev_settings = gpio_read(BTN_SETTINGS);
        }

        uint32_t step_us = dash_held ? (g.step_us / DASH_DIV) : g.step_us;
        acc += POLL_US;
        if (acc >= step_us)
        {
            acc = 0;
            step();
            display_flush();
        }
        delay(POLL_US);
    }
}

/* ── Entry point ────────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS Snake v1.0");

    display_get_size(&SCR_W, &SCR_H);

    /* Derive the grid from the panel, clamped to the static buffers. */
    COLS = SCR_W / CELL;
    ROWS = (SCR_H - HEADER_H) / CELL;
    if (COLS > MAX_COLS) COLS = MAX_COLS;
    if (ROWS > MAX_ROWS) ROWS = MAX_ROWS;
    OX = (SCR_W - COLS * CELL) / 2;
    OY = HEADER_H + (SCR_H - HEADER_H - ROWS * CELL) / 2;

    gpio_configure(BTN_UP, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_DOWN, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_LEFT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_RIGHT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_A, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_B, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_X, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_Y, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    load_best();

    /* Title screen */
    display_clear(COL_BG);
    display_text_large(SCR_W * 36 / 100, SCR_H * 30 / 100, "SNAKE", COL_TITLE);
    display_text_large(SCR_W * 37 / 100, SCR_H * 45 / 100, "AkiraOS", COL_LABEL);
    display_text(SCR_W * 28 / 100, SCR_H * 66 / 100, "DPAD steer  A dash", COL_HINT);
    display_text(SCR_W * 30 / 100, SCR_H * 78 / 100, "Press any button", COL_VALUE);
    display_flush();

    uint32_t frames = 0;
    while (!gpio_read(BTN_UP) && !gpio_read(BTN_DOWN) &&
           !gpio_read(BTN_LEFT) && !gpio_read(BTN_RIGHT) &&
           !gpio_read(BTN_A) && !gpio_read(BTN_B))
    {
        frames++;
        delay(20000);
    }
    rng_seed(random_seed(frames));
    delay(100000); /* debounce */

    printf("Starting game...");

    while (1)
    {
        g_restart_game = 0;
        g_exit_to_supervisor = 0;
        new_best = 0;
        init_game();
        draw_full();
        game_loop();

        if (g_exit_to_supervisor)
        {
            app_switch("supervisor");
            return 0;
        }
        if (g_restart_game)
        {
            rng_seed(random_seed(++frames));
            continue;
        }
        break; /* normal game over */
    }

    save_best();

    /* Game-over screen */
    display_clear(COL_BG);
    display_text_large(SCR_W * 32 / 100, SCR_H * 24 / 100, "GAME", COL_GAMEOVER_T);
    display_text_large(SCR_W * 32 / 100, SCR_H * 39 / 100, "OVER", COL_GAMEOVER_T);

    char buf[16];
    display_text(SCR_W * 32 / 100, SCR_H * 58 / 100, "SCORE:", COL_LABEL);
    int_to_str(g.score, buf);
    display_text(SCR_W * 55 / 100, SCR_H * 58 / 100, buf, COL_GAMEOVER_S);

    display_text(SCR_W * 32 / 100, SCR_H * 68 / 100, "BEST:", COL_LABEL);
    int_to_str(best_score, buf);
    display_text(SCR_W * 55 / 100, SCR_H * 68 / 100, buf, COL_VALUE);

    if (new_best)
        display_text(SCR_W * 32 / 100, SCR_H * 80 / 100, "NEW BEST!", COL_NEWBEST);

    display_flush();
    delay(4000000);

    app_switch("supervisor");
    printf("Game over!");
    return 0;
}
