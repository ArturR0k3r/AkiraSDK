/**
 * @file main.c
 * @brief Pixel Farmer — farming sim for AkiraOS
 *
 * Controls:
 *   DPAD = Move cursor   A = Action (plant/water/harvest)
 *   B = Cycle seed type  SETTINGS = Pause
 *
 * Gameplay:
 *   Walk around a 10×8 plot grid. Plant seeds, water them, wait for
 *   growth, then harvest for gold. Buy better seeds with earnings.
 *   Crops grow in real-time (frame-counted). 4 crop types with
 *   different grow times and sell values.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
* @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320, SCR_H = 240;

/* ── Buttons ─────────────────────────────────────────────────────────── */
#define BTN_UP       4
#define BTN_DOWN     5
#define BTN_LEFT     6
#define BTN_RIGHT    7
#define BTN_A        15
#define BTN_B        16
#define BTN_SETTINGS 0    /* BTN.OK = GPIO0, active-low pull-up */

/* ── Colours ─────────────────────────────────────────────────────────── */
#define COL_BG        0x2945   /* dark earth brown  */
#define COL_SOIL      0x4A29   /* tilled soil       */
#define COL_GRASS     0x2C84   /* untilled grass    */
#define COL_WATER     0x34BF   /* watered soil      */
#define COL_CURSOR    0xFFFF   /* white cursor      */
#define COL_HUD_BG    0x0000   /* black HUD bar     */
#define COL_HUD_TXT   0xFFFF   /* white text        */
#define COL_GOLD      0xFE60   /* gold/coin colour  */
#define COL_LABEL     0x8410   /* grey label        */
#define COL_TITLE     0x07E0   /* green title       */
#define COL_GAMEOVER  0xF800   /* red               */

/* ── Farm grid ───────────────────────────────────────────────────────── */
#define GRID_COLS  10
#define GRID_ROWS   7
#define CELL_W     28
#define CELL_H     26
#define GRID_X     14
#define GRID_Y     24
#define FRAME_US   33333   /* ~30 fps (farming doesn't need 50) */

/* ── Crop types ──────────────────────────────────────────────────────── */
enum {
    CROP_WHEAT = 0,
    CROP_CARROT,
    CROP_TOMATO,
    CROP_PUMPKIN,
    CROP_COUNT
};

static const char *crop_names[] = { "Wheat", "Carrot", "Tomato", "Pumpkin" };
static const uint16_t crop_colors[] = {
    0xFE60,  /* wheat — golden yellow  */
    0xFCA0,  /* carrot — orange        */
    0xF800,  /* tomato — red           */
    0xFF20,  /* pumpkin — dark orange  */
};
static const uint16_t crop_seed_colors[] = {
    0x4A29,  /* wheat seed — brown     */
    0x4A29,  /* carrot seed            */
    0x4A29,  /* tomato seed            */
    0x4A29,  /* pumpkin seed           */
};
static const uint16_t crop_sprout_colors[] = {
    0x2C84,  /* wheat sprout — light green  */
    0x34A4,  /* carrot sprout               */
    0x2C84,  /* tomato sprout               */
    0x34A4,  /* pumpkin sprout              */
};
/* Grow time in frames, sell value */
static const int crop_grow_time[]  = { 150, 250, 400, 600 };
static const int crop_sell_value[] = {   5,  12,  25,  50 };
static const int crop_seed_cost[]  = {   0,   5,  10,  25 };

/* ── Cell state ──────────────────────────────────────────────────────── */
enum {
    STATE_GRASS = 0,    /* untilled */
    STATE_TILLED,       /* tilled, empty */
    STATE_PLANTED,      /* seed planted, needs water */
    STATE_WATERED,      /* watered, growing */
    STATE_GROWING,      /* sprouted */
    STATE_READY         /* ready to harvest */
};

typedef struct {
    uint8_t state;
    uint8_t crop;       /* which crop type */
    int     grow_timer; /* frames remaining */
} cell_t;

static cell_t farm[GRID_ROWS][GRID_COLS];

/* ── Game state ──────────────────────────────────────────────────────── */
static int cursor_x, cursor_y;
static int selected_seed;
static int gold;
static int day;
static int day_timer;
static int harvested_total;
static int game_over;
static int exit_to_supervisor, restart_game;

#define DAY_LENGTH  900  /* frames per day (~30 seconds) */
#define MAX_DAYS     30

/* ── PRNG ────────────────────────────────────────────────────────────── */
static uint32_t rng = 54321;
static int rng_next(int mod) {
    rng = rng * 1103515245 + 12345;
    return (int)((rng >> 16) & 0x7FFF) % mod;
}

/* ── Init ────────────────────────────────────────────────────────────── */
static void init_game(void) {
    cursor_x = 0;
    cursor_y = 0;
    selected_seed = CROP_WHEAT;
    gold = 10;
    day = 1;
    day_timer = 0;
    harvested_total = 0;
    game_over = 0;

    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++) {
            farm[r][c].state = STATE_GRASS;
            farm[r][c].crop = 0;
            farm[r][c].grow_timer = 0;
        }
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static void cell_screen_pos(int col, int row, int *sx, int *sy) {
    *sx = GRID_X + col * CELL_W;
    *sy = GRID_Y + row * CELL_H;
}

static void draw_cell(int col, int row) {
    int sx, sy;
    cell_screen_pos(col, row, &sx, &sy);
    uint16_t bg;

    switch (farm[row][col].state) {
    case STATE_GRASS:   bg = COL_GRASS; break;
    case STATE_TILLED:  bg = COL_SOIL;  break;
    case STATE_PLANTED: bg = COL_SOIL;  break;
    case STATE_WATERED: bg = COL_WATER; break;
    case STATE_GROWING: bg = COL_WATER; break;
    case STATE_READY:   bg = COL_WATER; break;
    default:            bg = COL_GRASS; break;
    }

    /* Cell background */
    display_rect(sx + 1, sy + 1, CELL_W - 2, CELL_H - 2, bg);

    /* Crop indicators */
    int cx = sx + CELL_W / 2;
    int cy = sy + CELL_H / 2;
    int crop = farm[row][col].crop;

    switch (farm[row][col].state) {
    case STATE_PLANTED:
        /* Small seed dot */
        display_rect(cx - 1, cy, 3, 3, crop_seed_colors[crop]);
        break;
    case STATE_WATERED:
        /* Seed + water shine */
        display_rect(cx - 1, cy, 3, 3, crop_seed_colors[crop]);
        display_rect(cx + 3, cy - 3, 2, 2, 0x653F);
        break;
    case STATE_GROWING:
        /* Small sprout */
        display_rect(cx, cy - 4, 2, 8, crop_sprout_colors[crop]);
        display_rect(cx - 2, cy - 2, 2, 3, crop_sprout_colors[crop]);
        display_rect(cx + 2, cy - 1, 2, 3, crop_sprout_colors[crop]);
        break;
    case STATE_READY:
        /* Full plant — big colored block */
        display_rect(cx - 4, cy - 5, 8, 6, crop_colors[crop]);
        display_rect(cx - 1, cy + 1, 2, 6, crop_sprout_colors[crop]);
        break;
    default:
        break;
    }

    /* Cursor */
    if (col == cursor_x && row == cursor_y) {
        display_rect_outline(sx, sy, CELL_W, CELL_H, COL_CURSOR);
    }
}

static void draw_grid(void) {
    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++)
            draw_cell(c, r);
}

static void draw_hud(void) {
    /* Top bar */
    display_rect(0, 0, SCR_W, 20, COL_HUD_BG);
    display_text(4, 4, "GOLD:", COL_LABEL);
    display_number(42, 4, gold, COL_GOLD);
    display_text(110, 4, "DAY:", COL_LABEL);
    display_number(142, 4, day, COL_HUD_TXT);

    /* Day progress bar */
    int bar_w = 60;
    int filled = (day_timer * bar_w) / DAY_LENGTH;
    display_rect(170, 6, bar_w, 8, 0x2104);
    display_rect(170, 6, filled, 8, COL_GOLD);

    /* Seed selector */
    display_text(240, 4, crop_names[selected_seed], crop_colors[selected_seed]);

    /* Bottom bar */
    display_rect(0, SCR_H - 18, SCR_W, 18, COL_HUD_BG);
    /* Show contextual action hint */
    int st = farm[cursor_y][cursor_x].state;
    if (st == STATE_GRASS)
        display_text(4, SCR_H - 14, "A:Till  B:Seed", COL_LABEL);
    else if (st == STATE_TILLED)
        display_text(4, SCR_H - 14, "A:Plant  B:Seed", COL_LABEL);
    else if (st == STATE_PLANTED)
        display_text(4, SCR_H - 14, "A:Water  B:Seed", COL_LABEL);
    else if (st == STATE_READY)
        display_text(4, SCR_H - 14, "A:Harvest  B:Seed", COL_LABEL);
    else
        display_text(4, SCR_H - 14, "Growing...  B:Seed", COL_LABEL);

    display_text(200, SCR_H - 14, "Harvested:", COL_LABEL);
    display_number(280, SCR_H - 14, harvested_total, COL_HUD_TXT);
}

/* ── Actions ─────────────────────────────────────────────────────────── */
static void do_action(void) {
    cell_t *c = &farm[cursor_y][cursor_x];
    switch (c->state) {
    case STATE_GRASS:
        /* Till the soil */
        c->state = STATE_TILLED;
        break;
    case STATE_TILLED:
        /* Plant selected seed */
        if (gold >= crop_seed_cost[selected_seed]) {
            gold -= crop_seed_cost[selected_seed];
            c->state = STATE_PLANTED;
            c->crop = selected_seed;
            c->grow_timer = crop_grow_time[selected_seed];
        }
        break;
    case STATE_PLANTED:
        /* Water the crop */
        c->state = STATE_WATERED;
        break;
    case STATE_READY:
        /* Harvest */
        gold += crop_sell_value[c->crop];
        harvested_total++;
        c->state = STATE_TILLED;
        c->crop = 0;
        c->grow_timer = 0;
        break;
    default:
        break;
    }
}

/* ── Update crops ────────────────────────────────────────────────────── */
static void update_crops(void) {
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            cell_t *cell = &farm[r][c];
            if (cell->state == STATE_WATERED || cell->state == STATE_GROWING) {
                cell->grow_timer--;
                if (cell->grow_timer <= crop_grow_time[cell->crop] / 2 &&
                    cell->state == STATE_WATERED) {
                    cell->state = STATE_GROWING;
                }
                if (cell->grow_timer <= 0) {
                    cell->state = STATE_READY;
                }
            }
        }
    }
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
        display_text(130, 70, "PAUSED", COL_TITLE);
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

/* ── Input ───────────────────────────────────────────────────────────── */
static int prev_u, prev_d, prev_l, prev_r, prev_a, prev_b, prev_s;

static int handle_input(void) {
    int u = gpio_read(BTN_UP);
    int d = gpio_read(BTN_DOWN);
    int l = gpio_read(BTN_LEFT);
    int r = gpio_read(BTN_RIGHT);
    int a = gpio_read(BTN_A);
    int b = gpio_read(BTN_B);
    int s = gpio_read(BTN_SETTINGS);

    if (u && !prev_u && cursor_y > 0) cursor_y--;
    if (d && !prev_d && cursor_y < GRID_ROWS - 1) cursor_y++;
    if (l && !prev_l && cursor_x > 0) cursor_x--;
    if (r && !prev_r && cursor_x < GRID_COLS - 1) cursor_x++;
    if (a && !prev_a) do_action();
    if (b && !prev_b) selected_seed = (selected_seed + 1) % CROP_COUNT;
    if (s && !prev_s) { prev_s = s; return 1; }

    prev_u = u; prev_d = d; prev_l = l; prev_r = r;
    prev_a = a; prev_b = b; prev_s = s;
    return 0;
}

/* ── Full redraw ─────────────────────────────────────────────────────── */
static void full_redraw(void) {
    display_clear(COL_BG);
    draw_hud();
    draw_grid();
    display_flush();
}

/* ── Game loop ───────────────────────────────────────────────────────── */
static void game_loop(void) {
    full_redraw();

    while (!game_over) {
        if (handle_input()) {
            int choice = show_pause_menu();
            if (choice == MENU_EXIT) { exit_to_supervisor = 1; return; }
            if (choice == MENU_RESTART) { restart_game = 1; return; }
            full_redraw();
        }

        update_crops();

        /* Advance day */
        day_timer++;
        if (day_timer >= DAY_LENGTH) {
            day_timer = 0;
            day++;
            if (day > MAX_DAYS) {
                game_over = 1;
                break;
            }
        }

        draw_hud();
        draw_grid();
        display_flush();
        delay(FRAME_US);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS Pixel Farmer v1.0");
    display_get_size(&SCR_W, &SCR_H);
    printf("[pixel_farmer] display: %dx%d\n", (int)SCR_W, (int)SCR_H);

    gpio_configure(BTN_UP,       GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,        GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* Title screen */
    display_clear(0x0000);
    display_text_large(52, 50, "PIXEL", COL_TITLE);
    display_text_large(44, 85, "FARMER", COL_GOLD);
    display_text(76, 140, "Plant  Grow  Harvest", COL_HUD_TXT);
    display_text(84, 170, "Press A to start", COL_HUD_TXT);
    display_text(80, 200, "AkiraOS Edition", COL_LABEL);
    display_flush();

    uint32_t seed = 1;
    while (!gpio_read(BTN_A) && !gpio_read(BTN_B)) {
        seed++;
        delay(20000);
    }
    rng = seed ^ 0xDEADBEEF;
    rng_next(7); rng_next(7); rng_next(7);
    delay(100000);

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
            continue;
        }
        break;
    }

    /* End screen */
    display_clear(0x0000);
    display_text_large(44, 40, "SEASON", COL_TITLE);
    display_text_large(68, 75, "OVER", COL_TITLE);
    display_text(100, 120, "GOLD:", COL_LABEL);
    display_number(145, 120, gold, COL_GOLD);
    display_text(100, 145, "HARVESTED:", COL_LABEL);
    display_number(190, 145, harvested_total, COL_HUD_TXT);
    display_text(100, 170, "DAYS:", COL_LABEL);
    display_number(145, 170, day, COL_HUD_TXT);
    display_flush();
    delay(5000000);

    printf("Season over!");
    return 0;
}
