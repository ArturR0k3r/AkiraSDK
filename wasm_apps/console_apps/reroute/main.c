/**
 * @file main.c
 * @brief Reroute — rotate cable tiles until the whole network is back online.
 *
 * Someone scrambled the wiring.  Power flows out of the orange core; rotate
 * tiles so every cable joins up and every terminal lights green, with no
 * loose ends.  Grids grow level by level, and from level 16 the board wraps
 * around its edges.  The daily puzzle is seeded by the date and keeps a
 * streak.
 *
 * Every puzzle is a random spanning tree with each tile turned at random, so
 * it is always solvable.  Any arrangement that powers every tile counts: the
 * tiles carry exactly 2(N-1) connectors, so "all N tiles powered" already
 * implies a tree with no loose ends.
 *
 * Controls:
 *   D-pad      move the cursor
 *   A / B      rotate clockwise / anticlockwise
 *   X          lock a tile (locked tiles can't be rotated by accident)
 *   Y / HOME   menu
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"

/* ── Board ──────────────────────────────────────────────────────────── */
#define MAX_GW    12
#define MAX_GH    8
#define MAX_CELLS (MAX_GW * MAX_GH)

#define DIR_N 1
#define DIR_E 2
#define DIR_S 4
#define DIR_W 8

static uint8_t cell[MAX_CELLS];    /* current connector mask            */
static uint8_t sol[MAX_CELLS];     /* the generator's arrangement       */
static uint8_t start[MAX_CELLS];   /* scrambled state, for restart      */
static uint8_t locked[MAX_CELLS];
static uint8_t powered[MAX_CELLS];
static uint8_t dist[MAX_CELLS];    /* BFS distance from the core        */
static int gw, gh, wrap, core;

/* ── Layout ─────────────────────────────────────────────────────────── */
#define TOP 20 /* header bar */
#define BOT 16 /* footer     */
static int cs, ox, oy;

/* ── Colours (RGB565) ───────────────────────────────────────────────── */
#define C_BG       0x0842
#define C_HDR      0x18E4
#define C_CELL     0x10A3
#define C_CELL_LK  0x2947
#define C_LOCKMARK 0x8CB5
#define C_OFF      0x52AA
#define C_ON       0x07FF
#define C_WAVE     0xFFFF
#define C_CORE     0xFD20
#define C_TERM_OFF 0x6B6D
#define C_TERM_ON  0x07E0
#define C_SCREEN   0x0200
#define C_CURSOR   0xFFE0
#define C_TEXT     0xFFFF
#define C_DIM      0x8CB5
#define C_GOLD     0xFE87

/* ── Game state ─────────────────────────────────────────────────────── */
static int cur;            /* cursor cell              */
static int moves, par;
static int level;          /* next level to play       */
static int daily;          /* playing the daily puzzle */
static int today;          /* days since epoch, or -1  */
static int stars_total, dday, dstreak;
static uint32_t start_ms;

static int popcount4(uint8_t m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1); }
static uint8_t rot_cw(uint8_t m) { return (uint8_t)(((m << 1) | (m >> 3)) & 15); }
static uint8_t rot_ccw(uint8_t m) { return (uint8_t)(((m >> 1) | (m << 3)) & 15); }
static uint8_t opposite(uint8_t d) { return (uint8_t)(((d << 2) | (d >> 2)) & 15); }

static int neighbor(int c, uint8_t d)
{
    int x = c % gw, y = c / gw;

    if (d == DIR_N) y--;
    else if (d == DIR_E) x++;
    else if (d == DIR_S) y++;
    else x--;

    if (wrap) {
        x = (x + gw) % gw;
        y = (y + gh) % gh;
    } else if (x < 0 || y < 0 || x >= gw || y >= gh) {
        return -1;
    }
    return y * gw + x;
}

/* ── Generation ─────────────────────────────────────────────────────── */

/* Random spanning tree grown from the core (randomised Prim): lots of
 * branches and terminals, few long corridors.  Result goes in cell[]. */
static void generate_tree(void)
{
    static uint8_t in_tree[MAX_CELLS];
    static uint16_t frontier[MAX_CELLS * 4];
    int n = gw * gh, nf = 0;

    for (int i = 0; i < n; i++) {
        cell[i] = 0;
        in_tree[i] = 0;
    }
    core = (gh / 2) * gw + gw / 2;
    in_tree[core] = 1;
    for (int b = 0; b < 4; b++) {
        frontier[nf++] = (uint16_t)(core << 2 | b);
    }
    while (nf) {
        int i = (int)g_rand_n((uint32_t)nf);
        uint16_t e = frontier[i];
        frontier[i] = frontier[--nf];

        int c = e >> 2;
        uint8_t d = (uint8_t)(1 << (e & 3));
        int nb = neighbor(c, d);
        if (nb < 0 || in_tree[nb]) {
            continue;
        }
        /* Four-way crossings never need turning; keep them rare. */
        if (popcount4(cell[c]) == 3 && g_rand_n(3)) {
            continue;
        }
        cell[c] |= d;
        cell[nb] |= opposite(d);
        in_tree[nb] = 1;
        for (int b = 0; b < 4; b++) {
            int nn = neighbor(nb, (uint8_t)(1 << b));
            if (nn >= 0 && !in_tree[nn]) {
                frontier[nf++] = (uint16_t)(nb << 2 | b);
            }
        }
    }
}

static int compute_power(void)
{
    static uint8_t q[MAX_CELLS];
    int n = gw * gh, head = 0, tail = 0;

    for (int i = 0; i < n; i++) {
        powered[i] = 0;
        dist[i] = 255;
    }
    powered[core] = 1;
    dist[core] = 0;
    q[tail++] = (uint8_t)core;
    while (head < tail) {
        int c = q[head++];
        for (int b = 0; b < 4; b++) {
            uint8_t d = (uint8_t)(1 << b);
            if (!(cell[c] & d)) {
                continue;
            }
            int nb = neighbor(c, d);
            if (nb >= 0 && !powered[nb] && (cell[nb] & opposite(d))) {
                powered[nb] = 1;
                dist[nb] = (uint8_t)(dist[c] + 1);
                q[tail++] = (uint8_t)nb;
            }
        }
    }
    return tail;
}

/* Presses needed to turn @p from into @p to, either direction. */
static int turns_needed(uint8_t from, uint8_t to)
{
    uint8_t m = from;
    for (int k = 0; k < 4; k++) {
        if (m == to) {
            return k <= 2 ? k : 4 - k;
        }
        m = rot_cw(m);
    }
    return 0;
}

static void new_puzzle(int w, int h, int wrap_edges)
{
    int n;

    gw = w;
    gh = h;
    wrap = wrap_edges;
    n = gw * gh;
    for (;;) {
        generate_tree();
        /* Prim skips can strand a cell; retry until the tree spans. */
        int spans = 1;
        for (int i = 0; i < n; i++) {
            if (!cell[i]) {
                spans = 0;
            }
        }
        if (spans) {
            break;
        }
    }
    for (int i = 0; i < n; i++) {
        sol[i] = cell[i];
    }
    do {
        par = 0;
        for (int i = 0; i < n; i++) {
            uint8_t m = sol[i];
            for (int r = (int)g_rand_n(4); r > 0; r--) {
                m = rot_cw(m);
            }
            cell[i] = m;
            par += turns_needed(m, sol[i]);
        }
    } while (compute_power() == n || par < n / 3);

    for (int i = 0; i < n; i++) {
        start[i] = cell[i];
        locked[i] = 0;
    }
    cur = core;
    moves = 0;
    start_ms = g_now();
}

static void level_size(int lv, int *w, int *h, int *wr)
{
    static const uint8_t LW[] = {4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10, 10};
    static const uint8_t LH[] = {4, 4, 5, 5, 6, 5, 6, 6, 7, 6, 7, 7, 7, 7, 7};
    static const uint8_t WW[] = {6, 7, 7, 8, 8, 9};
    static const uint8_t WH[] = {5, 5, 6, 6, 7, 7};
    if (lv < 16) {
        *w = LW[lv - 1];
        *h = LH[lv - 1];
        *wr = 0;
    } else {
        int i = lv - 16 < 5 ? lv - 16 : 5;
        *w = WW[i];
        *h = WH[i];
        *wr = 1;
    }
}

/* ── Drawing ────────────────────────────────────────────────────────── */

static void layout(void)
{
    int aw = (G_W - 8) / gw, ah = (G_H - TOP - BOT - 4) / gh;
    cs = aw < ah ? aw : ah;
    if (cs > 40) cs = 40;
    ox = (G_W - cs * gw) / 2;
    oy = TOP + (G_H - TOP - BOT - cs * gh) / 2;
}

static void draw_cell(int c, uint16_t on_col)
{
    int x = ox + (c % gw) * cs, y = oy + (c / gw) * cs;
    uint8_t m = cell[c];
    uint16_t col = powered[c] ? on_col : C_OFF;
    int t = cs >= 34 ? 7 : cs >= 26 ? 5 : cs >= 18 ? 4 : 3;
    int h0 = t / 2;
    int cx = x + cs / 2, cy = y + cs / 2;

    display_rect(x + 1, y + 1, cs - 1, cs - 1, locked[c] ? C_CELL_LK : C_CELL);
    if (locked[c]) {
        /* corner ticks */
        display_rect(x + 2, y + 2, 3, 1, C_LOCKMARK);
        display_rect(x + 2, y + 2, 1, 3, C_LOCKMARK);
        display_rect(x + cs - 4, y + cs - 2, 3, 1, C_LOCKMARK);
        display_rect(x + cs - 2, y + cs - 4, 1, 3, C_LOCKMARK);
    }

    /* Cables run over the 1px grid line so neighbours join seamlessly. */
    if (m & DIR_N) display_rect(cx - h0, y, t, cy - y + 1, col);
    if (m & DIR_S) display_rect(cx - h0, cy, t, y + cs - cy, col);
    if (m & DIR_W) display_rect(x, cy - h0, cx - x + 1, t, col);
    if (m & DIR_E) display_rect(cx, cy - h0, x + cs - cx, t, col);
    display_rect(cx - h0, cy - h0, t, t, col);

    if (c == core) {
        int s = cs * 3 / 5;
        display_rounded_rect_fill(cx - s / 2, cy - s / 2, s, s, 3, C_CORE);
        display_circle_fill(cx, cy, s / 4, C_BG);
        display_circle_fill(cx, cy, s / 8 > 1 ? s / 8 : 1, C_CORE);
    } else if (popcount4(m) == 1) {
        int s = cs / 2 > 8 ? cs / 2 : 8;
        int lit = powered[c];
        display_rounded_rect_fill(cx - s / 2, cy - s / 2, s, s, 2, lit ? C_TERM_ON : C_TERM_OFF);
        display_rect(cx - s / 2 + 2, cy - s / 2 + 2, s - 4, s / 2 - 1, lit ? C_SCREEN : C_CELL);
    }
}

static void draw_cursor(void)
{
    int x = ox + (cur % gw) * cs, y = oy + (cur / gw) * cs;
    display_rect_outline(x, y, cs + 1, cs + 1, C_CURSOR);
    display_rect_outline(x + 1, y + 1, cs - 1, cs - 1, C_CURSOR);
}

static void draw_board(uint16_t on_col)
{
    for (int i = 0; i < gw * gh; i++) {
        draw_cell(i, on_col);
    }
    if (wrap) {
        /* Dashed frame: the edges continue on the far side. */
        for (int x = ox; x < ox + gw * cs; x += 6) {
            display_hline(x, oy - 2, 3, C_DIM);
            display_hline(x, oy + gh * cs + 2, 3, C_DIM);
        }
        for (int y = oy; y < oy + gh * cs; y += 6) {
            display_vline(ox - 2, y, 3, C_DIM);
            display_vline(ox + gw * cs + 2, y, 3, C_DIM);
        }
    }
}

static void draw_screen(int online)
{
    char b[48];

    display_clear(C_BG);
    display_rect(0, 0, G_W, TOP - 4, C_HDR);
    display_text(6, 3, "REROUTE", C_ON);
    b[0] = '\0';
    if (daily) {
        char d[16] = "";
        g_cat_date(d, today);
        g_cat(g_cat(b, "DAILY "), d + 5); /* MM-DD: room for the move count */
    } else {
        g_cat_int(g_cat(b, "LEVEL "), level);
        if (wrap) g_cat(b, " WRAP");
    }
    display_text(70, 3, b, C_GOLD);
    b[0] = '\0';
    g_cat_int(g_cat(g_cat_int(g_cat(b, "MOVES "), moves), " PAR "), par);
    g_text_r(G_W - 6, 3, b, C_TEXT);

    draw_board(C_ON);
    draw_cursor();

    b[0] = '\0';
    g_cat(g_cat_int(g_cat(g_cat_int(b, online), "/"), gw * gh), " online");
    display_text(6, G_H - BOT + 3, b, online == gw * gh ? C_TERM_ON : C_DIM);
    g_text_r(G_W - 6, G_H - BOT + 3, "A/B turn X lock Y menu", C_DIM);
    display_flush();
}

static const int8_t STAR_O[5][2] = {{0, -9}, {9, -3}, {5, 7}, {-5, 7}, {-9, -3}};
static const int8_t STAR_I[5][2] = {{2, -3}, {4, 1}, {0, 4}, {-4, 1}, {-2, -3}};

static void draw_star(int cx, int cy, int s, uint16_t col)
{
#define SX(v) (cx + (v) * s / 9)
#define SY(v) (cy + (v) * s / 9)
    for (int k = 0; k < 5; k++) {
        int p = (k + 4) % 5;
        display_triangle_fill(SX(STAR_O[k][0]), SY(STAR_O[k][1]), SX(STAR_I[p][0]), SY(STAR_I[p][1]),
                              SX(STAR_I[k][0]), SY(STAR_I[k][1]), col);
    }
    for (int k = 1; k < 4; k++) {
        display_triangle_fill(SX(STAR_I[0][0]), SY(STAR_I[0][1]), SX(STAR_I[k][0]), SY(STAR_I[k][1]),
                              SX(STAR_I[k + 1][0]), SY(STAR_I[k + 1][1]), col);
    }
#undef SX
#undef SY
}

/* Power ripples out from the core once the last tile clicks in. */
static void win_wave(void)
{
    int maxd = 0;
    for (int i = 0; i < gw * gh; i++) {
        if (dist[i] > maxd) maxd = dist[i];
    }
    int step = maxd > 12 ? 2 : 1;
    for (int s = 0; s <= maxd + 1; s += step) {
        for (int i = 0; i < gw * gh; i++) {
            int d = dist[i];
            draw_cell(i, (d <= s && d > s - 2 * step) ? C_WAVE : C_ON);
        }
        display_flush();
        delay(30000);
    }
    draw_board(C_ON);
    display_flush();
}

static const char HELP[] =
    "Power flows out of the orange core.\n"
    "Turn the tiles so every cable joins\n"
    "up and every terminal lights green.\n"
    "No loose ends allowed.\n"
    "\n"
    "A  turn clockwise\n"
    "B  turn anticlockwise\n"
    "X  lock a tile you're sure about\n"
    "Y  menu\n"
    "\n"
    "From level 16 the edges wrap: a\n"
    "cable leaving the right side comes\n"
    "back in on the left.";

/* ── Results ────────────────────────────────────────────────────────── */

/* Returns 1 for "next", 0 for "menu". */
static int show_result(int secs)
{
    int stars = moves <= par ? 3 : moves <= par + par / 2 ? 2 : 1;
    int w = 220, h = 150, x = (G_W - w) / 2, y = (G_H - h) / 2;
    char b[48];

    g_panel(x, y, w, h);
    g_text_large_c(G_W / 2, y + 10, "NETWORK ONLINE", C_GOLD);
    for (int i = 0; i < 3; i++) {
        draw_star(G_W / 2 - 34 + i * 34, y + 52, 13, i < stars ? C_GOLD : C_HDR);
    }
    b[0] = '\0';
    g_cat_int(g_cat(g_cat_int(g_cat(b, "Moves "), moves), "   par "), par);
    g_text_c(G_W / 2, y + 74, b, C_TEXT);
    b[0] = '\0';
    g_cat_time(g_cat(b, "Time "), secs);
    g_text_c(G_W / 2, y + 88, b, C_TEXT);
    b[0] = '\0';
    if (daily) {
        g_cat(g_cat_int(g_cat(b, "Daily streak "), dstreak), dstreak == 1 ? " day" : " days");
    } else {
        g_cat_int(g_cat(b, "Stars collected "), stars_total + stars);
    }
    g_text_c(G_W / 2, y + 104, b, C_ON);
    g_text_c(G_W / 2, y + h - 22, daily ? "A: done" : "A: next level   Y: menu", C_DIM);
    display_flush();

    if (!daily) {
        stars_total += stars;
        g_save_int("stars", stars_total);
    }
    for (;;) {
        uint32_t k = g_wait_key();
        if (k == K_A) return 1;
        if (k == K_Y || k == K_HOME || k == K_B) return 0;
    }
}

/* ── Play ───────────────────────────────────────────────────────────── */
enum { PLAY_SOLVED, PLAY_MENU, PLAY_QUIT };

static int pause_menu(void)
{
    static const char *const items[] = {"Resume", "Restart puzzle", "New puzzle", "How to play", "Main menu",
                                        "Quit"};
    for (;;) {
        int r = g_menu("PAUSED", items, daily ? 5 : 6, 0, 0);
        if (daily && r >= 2) {
            r++; /* the daily has no "new puzzle" */
        }
        switch (r) {
        case 1:
            for (int i = 0; i < gw * gh; i++) {
                cell[i] = start[i];
                locked[i] = 0;
            }
            moves = 0;
            cur = core;
            return -1;
        case 2: {
            int w, h, wr;
            level_size(level, &w, &h, &wr);
            new_puzzle(w, h, wr);
            return -1;
        }
        case 3:
            g_message("HOW TO PLAY", HELP);
            continue;
        case 4:
            return PLAY_MENU;
        case 5:
            return PLAY_QUIT;
        default:
            return -1;
        }
    }
}

static int play(void)
{
    layout();
    g_flush_input();
    draw_screen(compute_power());

    for (;;) {
        uint32_t k = g_key();
        if (!k) {
            delay(15000);
            continue;
        }
        int dirty = 0, solved = 0;
        /* Apply every queued press before paying for one redraw. */
        while (k && !solved) {
            int x = cur % gw, y = cur / gw;
            switch (k) {
            case K_UP:    cur = ((y + gh - 1) % gh) * gw + x; break;
            case K_DOWN:  cur = ((y + 1) % gh) * gw + x; break;
            case K_LEFT:  cur = y * gw + (x + gw - 1) % gw; break;
            case K_RIGHT: cur = y * gw + (x + 1) % gw; break;
            case K_A:
            case K_B:
                if (!locked[cur]) {
                    cell[cur] = k == K_A ? rot_cw(cell[cur]) : rot_ccw(cell[cur]);
                    moves++;
                    solved = compute_power() == gw * gh;
                }
                break;
            case K_X:
                locked[cur] ^= 1;
                break;
            case K_Y:
            case K_HOME: {
                int r = pause_menu();
                if (r >= 0) return r;
                layout();
                break;
            }
            default:
                break;
            }
            dirty = 1;
            k = solved ? 0 : g_key();
        }
        if (solved) {
            draw_screen(gw * gh);
            win_wave();
            return PLAY_SOLVED;
        }
        if (dirty) {
            draw_screen(compute_power());
        }
    }
}

static int play_levels(void)
{
    for (;;) {
        int w, h, wr;
        daily = 0;
        level_size(level, &w, &h, &wr);
        new_puzzle(w, h, wr);
        int r = play();
        if (r != PLAY_SOLVED) return r;
        int secs = (int)((g_now() - start_ms) / 1000);
        int next = show_result(secs);
        level++;
        g_save_int("lvl", level);
        if (!next) return PLAY_MENU;
    }
}

static int play_daily(void)
{
    if (today < 0) {
        g_message("NO CLOCK", "The daily puzzle needs the date.\nConnect to Wi-Fi once so the\nconsole can set its clock.");
        return PLAY_MENU;
    }
    daily = 1;
    g_seed_from((uint32_t)today);
    new_puzzle(9, 7, 0);
    g_seed_random();
    int r = play();
    if (r != PLAY_SOLVED) return r;
    int secs = (int)((g_now() - start_ms) / 1000);
    if (dday != today) {
        dstreak = (dday == today - 1) ? dstreak + 1 : 1;
        dday = today;
        g_save_int("dday", dday);
        g_save_int("dstrk", dstreak);
    }
    show_result(secs);
    return PLAY_MENU;
}

/* ── Title ──────────────────────────────────────────────────────────── */

static int title_screen(void)
{
    static char item0[32], item1[40];
    const char *items[4] = {item0, item1, "How to play", "Quit"};
    static int sel = 0;

    /* A small solved network as cover art. */
    gw = G_W >= 400 ? 13 : 10;
    gh = 3;
    wrap = 0;
    generate_tree();
    for (int i = 0; i < gw * gh; i++) locked[i] = 0;
    compute_power();
    cs = 20;
    ox = (G_W - gw * cs) / 2;
    oy = 58;

    item0[0] = '\0';
    g_cat_int(g_cat(item0, level > 1 ? "Continue - level " : "Start - level "), level);
    item1[0] = '\0';
    if (today < 0) {
        g_cat(item1, "Daily puzzle (needs clock)");
    } else if (dday == today) {
        g_cat_int(g_cat(item1, "Daily puzzle - done, streak "), dstreak);
    } else {
        g_cat(item1, "Daily puzzle");
        if (dstreak && dday == today - 1) g_cat_int(g_cat(item1, " - streak "), dstreak);
    }

    for (;;) {
        display_clear(C_BG);
        g_text_large_c(G_W / 2, 14, "R E R O U T E", C_ON);
        g_text_c(G_W / 2, 38, "bring the network back online", C_DIM);
        draw_board(C_ON);
        for (int i = 0; i < 4; i++) {
            int y = 132 + i * 22;
            if (i == sel) {
                display_rounded_rect_fill(G_W / 2 - 120, y - 4, 240, 18, 4, g_theme.sel_bg);
                display_text(G_W / 2 - 112, y, ">", C_GOLD);
            }
            g_text_c(G_W / 2, y, items[i], i == sel ? C_TEXT : C_DIM);
        }
        if (stars_total) {
            char b[32] = "";
            g_cat_int(g_cat(b, "stars "), stars_total);
            g_text_r(G_W - 6, G_H - 14, b, C_GOLD);
        }
        display_flush();

        uint32_t k = g_wait_key();
        if (k == K_UP) sel = (sel + 3) % 4;
        else if (k == K_DOWN) sel = (sel + 1) % 4;
        else if (k == K_A) return sel;
        else if (k == K_B || k == K_HOME) return 3;
    }
}

int main(void)
{
    g_init();
    g_seed_random();
    today = g_today();
    level = g_load_int("lvl", 1);
    if (level < 1) level = 1;
    stars_total = g_load_int("stars", 0);
    dday = g_load_int("dday", -9);
    dstreak = g_load_int("dstrk", 0);

    for (;;) {
        int choice = title_screen();
        int r = PLAY_MENU;
        if (choice == 0) {
            r = play_levels();
        } else if (choice == 1) {
            r = play_daily();
        } else if (choice == 2) {
            g_message("HOW TO PLAY", HELP);
        } else {
            break;
        }
        if (r == PLAY_QUIT) break;
    }
    display_clear(0);
    display_flush();
    return 0;
}
