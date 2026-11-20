/**
 * @file main.c
 * @brief Reversi — outflank and flip against the CPU.
 *
 * Place a disc so that it traps a straight line of your opponent's discs
 * between it and another of yours; the trapped discs flip to your colour.
 * If you can't move you pass.  Most discs when the board fills wins.
 *
 * The CPU is a bitboard alpha-beta search with iterative deepening against
 * a wall-clock budget, so it answers in about a second whatever the
 * interpreter speed.  Easy blunders on purpose; Hard reads the last dozen
 * moves of the game out exactly.
 *
 * Controls:
 *   D-pad     move the cursor
 *   A         place a disc
 *   Y         undo your last move
 *   X         show / hide legal-move hints
 *   B / HOME  menu (new game, level, colour, statistics, quit)
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"

typedef uint64_t bb_t;

#define BLACK 0
#define WHITE 1
#define INF   1000000

/* ── Bitboards: square = y*8 + x, bit 0 = top-left ──────────────────── */
#define INNER    0x7E7E7E7E7E7E7E7EULL /* files B..G */
#define CORNERS  0x8100000000000081ULL
#define XSQ      0x0042000000004200ULL
#define CSQ      0x4281000000008142ULL

static int popcnt(bb_t b) { return __builtin_popcountll(b); }
static int lowbit(bb_t b) { return __builtin_ctzll(b); }

/* Squares where the side owning @p P can move. */
static bb_t legal_moves(bb_t P, bb_t O)
{
    bb_t empty = ~(P | O), m = 0, t, mO = O & INNER;

#define RUN(SH, MASK)                \
    t = MASK & SH(P);                \
    t |= MASK & SH(t);               \
    t |= MASK & SH(t);               \
    t |= MASK & SH(t);               \
    t |= MASK & SH(t);               \
    t |= MASK & SH(t);               \
    m |= empty & SH(t);
#define L1(b) ((b) << 1)
#define R1(b) ((b) >> 1)
#define L8(b) ((b) << 8)
#define R8(b) ((b) >> 8)
#define L7(b) ((b) << 7)
#define R7(b) ((b) >> 7)
#define L9(b) ((b) << 9)
#define R9(b) ((b) >> 9)
    RUN(L1, mO)
    RUN(R1, mO)
    RUN(L8, O)
    RUN(R8, O)
    RUN(L7, mO)
    RUN(R7, mO)
    RUN(L9, mO)
    RUN(R9, mO)
#undef RUN
    return m;
}

/* Discs flipped when the side owning @p P plays @p sq. */
static bb_t flips_for(bb_t P, bb_t O, int sq)
{
    bb_t flips = 0, m = 1ULL << sq, f, t, mO = O & INNER;

#define FLIP(SH, MASK)                 \
    f = 0;                             \
    t = SH(m) & MASK;                  \
    while (t) {                        \
        f |= t;                        \
        t = SH(t);                     \
        if (t & P) {                   \
            flips |= f;                \
            break;                     \
        }                              \
        t &= MASK;                     \
    }
    FLIP(L1, mO)
    FLIP(R1, mO)
    FLIP(L8, O)
    FLIP(R8, O)
    FLIP(L7, mO)
    FLIP(R7, mO)
    FLIP(L9, mO)
    FLIP(R9, mO)
#undef FLIP
    return flips;
}

/* ── Evaluation ─────────────────────────────────────────────────────── */
static const int8_t WEIGHT[64] = {
    100, -20, 10, 5, 5, 10, -20, 100,
    -20, -50, -2, -2, -2, -2, -50, -20,
    10,  -2,  1,  1,  1,  1,  -2,  10,
    5,   -2,  1,  0,  0,  1,  -2,  5,
    5,   -2,  1,  0,  0,  1,  -2,  5,
    10,  -2,  1,  1,  1,  1,  -2,  10,
    -20, -50, -2, -2, -2, -2, -50, -20,
    100, -20, 10, 5, 5, 10, -20, 100,
};
static bb_t wmask[8];
static int wval[8], nw;
static bb_t corner_adj[4]; /* X + C squares next to each corner */
static const int CORNER_SQ[4] = {0, 7, 56, 63};

static void eval_init(void)
{
    for (int i = 0; i < 64; i++) {
        int k;
        for (k = 0; k < nw && wval[k] != WEIGHT[i]; k++) {
        }
        if (k == nw) {
            wval[nw] = WEIGHT[i];
            wmask[nw++] = 0;
        }
        wmask[k] |= 1ULL << i;
    }
    for (int c = 0; c < 4; c++) {
        int cx = CORNER_SQ[c] % 8, cy = CORNER_SQ[c] / 8;
        int dx = cx ? -1 : 1, dy = cy ? -1 : 1;
        corner_adj[c] = (1ULL << (cy * 8 + cx + dx)) | (1ULL << ((cy + dy) * 8 + cx)) |
                        (1ULL << ((cy + dy) * 8 + cx + dx));
    }
}

static int final_score(bb_t P, bb_t O)
{
    int d = popcnt(P) - popcnt(O);
    return d > 0 ? 20000 + d : d < 0 ? -20000 + d : 0;
}

/* Static score for the side owning @p P; @p mp = its legal moves. */
static int evaluate(bb_t P, bb_t O, bb_t mp)
{
    int s = 0;
    bb_t occ = P | O, neutral = 0;

    /* X/C squares stop being dangerous once their corner is taken. */
    for (int c = 0; c < 4; c++) {
        if (occ & (1ULL << CORNER_SQ[c])) neutral |= corner_adj[c];
    }
    for (int k = 0; k < nw; k++) {
        bb_t m = wmask[k];
        if (wval[k] < 0) m &= ~neutral;
        s += wval[k] * (popcnt(P & m) - popcnt(O & m));
    }
    s += 8 * (popcnt(mp) - popcnt(legal_moves(O, P)));
    int empties = 64 - popcnt(occ);
    if (empties < 14) s += (14 - empties) * 3 * (popcnt(P) - popcnt(O));
    return s;
}

/* ── Search ─────────────────────────────────────────────────────────── */
static uint32_t deadline;
static int aborted;
static uint32_t nodes;

static int search(bb_t P, bb_t O, int depth, int alpha, int beta, int passed)
{
    if ((++nodes & 511) == 0) {
        if ((int32_t)(g_now() - deadline) > 0) aborted = 1;
        delay(0); /* let the shell breathe */
    }
    if (aborted) return 0;

    bb_t moves = legal_moves(P, O);
    if (!moves) {
        if (passed) return final_score(P, O);
        return -search(O, P, depth, -beta, -alpha, 1);
    }
    if (depth <= 0) return evaluate(P, O, moves);

    int best = -INF;
    const bb_t groups[3] = {moves & CORNERS, moves & ~(CORNERS | XSQ | CSQ), moves & (XSQ | CSQ)};
    for (int g = 0; g < 3; g++) {
        bb_t mm = groups[g];
        while (mm) {
            int sq = lowbit(mm);
            mm &= mm - 1;
            bb_t f = flips_for(P, O, sq);
            int v = -search(O & ~f, P | f | (1ULL << sq), depth - 1, -beta, -alpha, 0);
            if (aborted) return 0;
            if (v > best) best = v;
            if (v > alpha) alpha = v;
            if (alpha >= beta) return best;
        }
    }
    return best;
}

enum { LV_EASY, LV_NORMAL, LV_HARD };
static const char *const LEVEL_NAME[3] = {"Easy", "Normal", "Hard"};

static int ai_choose(bb_t P, bb_t O, int level)
{
    bb_t moves = legal_moves(P, O);
    int n = popcnt(moves);
    int sqs[32];
    int k = 0;

    for (bb_t mm = moves; mm; mm &= mm - 1) sqs[k++] = lowbit(mm);
    if (n == 1) return sqs[0];

    if (level == LV_EASY) {
        /* Greedy one-ply with a careless streak. */
        if (g_rand_n(100) < 35) return sqs[g_rand_n((uint32_t)n)];
        int best = sqs[0], bv = -INF;
        for (int i = 0; i < n; i++) {
            bb_t f = flips_for(P, O, sqs[i]);
            bb_t nP = P | f | (1ULL << sqs[i]), nO = O & ~f;
            int v = -evaluate(nO, nP, legal_moves(nO, nP)) + (int)g_rand_n(40);
            if (v > bv) {
                bv = v;
                best = sqs[i];
            }
        }
        return best;
    }

    int empties = 64 - popcnt(P | O);
    int maxd = level == LV_HARD ? (empties <= 12 ? empties : 8) : 3;
    uint32_t budget = level == LV_HARD ? 2000 : 900;
    int best_sq = sqs[0];

    deadline = g_now() + budget;
    aborted = 0;
    nodes = 0;
    for (int d = 1; d <= maxd; d++) {
        int alpha = -INF, cur = -1, ties = 0;
        for (int i = 0; i < n; i++) {
            bb_t f = flips_for(P, O, sqs[i]);
            int v = -search(O & ~f, P | f | (1ULL << sqs[i]), d - 1, -INF, -alpha + 1, 0);
            if (aborted) break;
            if (v > alpha) {
                alpha = v;
                cur = i;
                ties = 1;
            } else if (v == alpha && g_rand_n((uint32_t)++ties) == 0) {
                cur = i; /* vary play between equal moves */
            }
        }
        if (aborted && d > 1) break;
        if (cur >= 0) {
            best_sq = sqs[cur];
            /* Search the best move first next time round. */
            int t = sqs[cur];
            sqs[cur] = sqs[0];
            sqs[0] = t;
        }
        if (aborted || alpha >= 20000) break;
    }
    return best_sq;
}

/* ── Game state ─────────────────────────────────────────────────────── */
static bb_t disc[2];
static int side;              /* to move                         */
static int human = BLACK;        /* this game          */
static int pref_human = BLACK;   /* from the next game */
static int level = LV_NORMAL;    /* from the next game */
static int game_level;           /* this game          */
static int snap_pending = 1;     /* move the cursor to a legal square */
static int hints = 1;
static int last_sq = -1;
static int cur_sq = 19;
static int game_over;

typedef struct { bb_t b[2]; int8_t side, last; } snap_t;
static snap_t hist[64];
static int nhist;

static int wins[3], losses[3], draws[3];

/* ── Layout & colours ───────────────────────────────────────────────── */
static int cs, bx, by, px;

#define C_BG      0x0841
#define C_BOARD   0x0327
#define C_GRID    0x0204
#define C_BLACKD  0x18C3
#define C_BLACKR  0x4A69
#define C_WHITED  0xEF5D
#define C_WHITER  0x9CF3
#define C_EDGE    0x8410
#define C_HINT    0x5FEB
#define C_LAST    0xF800
#define C_CURSOR  0xFFE0
#define C_TEXT    0xFFFF
#define C_DIM     0x8CB5
#define C_ACCENT  0x07FF
#define C_GOLD    0xFE87

static void layout(void)
{
    int a = (G_H - 16) / 8, b = (G_W - 104) / 8;
    cs = a < b ? a : b;
    by = (G_H - 8 * cs) / 2;
    bx = by;
    px = bx + 8 * cs + 10;
}

static void draw_disc(int cx, int cy, int r, int color)
{
    if (color == BLACK) {
        display_circle_fill(cx, cy, r, C_BLACKD);
        display_circle(cx, cy, r, C_BLACKR);
        display_rect(cx - r / 2, cy - r / 2, 2, 2, C_BLACKR);
    } else {
        display_circle_fill(cx, cy, r, C_WHITED);
        display_circle(cx, cy, r, C_WHITER);
        display_rect(cx - r / 2, cy - r / 2, 2, 2, 0xFFFF);
    }
}

/* @p edge: discs drawn mid-flip (seen edge-on). */
static void draw_board(bb_t edge)
{
    int r = cs / 2 - 3;
    bb_t moves = (!game_over && side == human && hints) ? legal_moves(disc[human], disc[!human]) : 0;

    display_rounded_rect_fill(bx - 4, by - 4, 8 * cs + 9, 8 * cs + 9, 5, C_GRID);
    display_rect(bx, by, 8 * cs + 1, 8 * cs + 1, C_BOARD);
    for (int i = 0; i <= 8; i++) {
        display_hline(bx, by + i * cs, 8 * cs + 1, C_GRID);
        display_vline(bx + i * cs, by, 8 * cs + 1, C_GRID);
    }
    for (int i = 0; i < 4; i++) {
        display_rect(bx + (i & 1 ? 6 : 2) * cs - 1, by + (i & 2 ? 6 : 2) * cs - 1, 3, 3, C_GRID);
    }
    for (int sq = 0; sq < 64; sq++) {
        int cx = bx + (sq % 8) * cs + cs / 2, cy = by + (sq / 8) * cs + cs / 2;
        bb_t bit = 1ULL << sq;
        if (edge & bit) {
            display_rect(cx - r, cy - 2, 2 * r + 1, 5, C_EDGE);
        } else if (disc[BLACK] & bit) {
            draw_disc(cx, cy, r, BLACK);
        } else if (disc[WHITE] & bit) {
            draw_disc(cx, cy, r, WHITE);
        } else if (moves & bit) {
            display_circle_fill(cx, cy, 2, C_HINT);
        }
        if (sq == last_sq && !(edge & bit)) display_circle_fill(cx, cy, 2, C_LAST);
    }
    if (!game_over && side == human) {
        int x = bx + (cur_sq % 8) * cs, y = by + (cur_sq / 8) * cs;
        display_rect_outline(x, y, cs + 1, cs + 1, C_CURSOR);
        display_rect_outline(x + 1, y + 1, cs - 1, cs - 1, C_CURSOR);
    }
}

static void draw_panel(const char *status, uint16_t status_col)
{
    char b[24];
    int w = G_W - px - 4;
    int r = 7;

    display_text(px, by, "REVERSI", C_ACCENT);

    for (int i = 0; i < 2; i++) {
        int who = i == 0 ? human : !human;
        int y = by + 24 + i * 30;
        draw_disc(px + r, y + 9, r, who);
        display_text(px + 2 * r + 6, y + 4, i == 0 ? "YOU" : "CPU", side == who && !game_over ? C_GOLD : C_DIM);
        b[0] = '\0';
        g_cat_int(b, popcnt(disc[who]));
        display_text_large(px + w - g_tw_large(b), y, b, C_TEXT);
    }

    display_hline(px, by + 88, w, C_GRID);
    display_text(px, by + 96, status, status_col);

    display_hline(px, by + 116, w, C_GRID);
    display_text(px, by + 124, LEVEL_NAME[game_level], C_ACCENT);
    b[0] = '\0';
    g_cat_int(g_cat(g_cat_int(g_cat(g_cat_int(g_cat(b, "W"), wins[game_level]), " L"),
                              losses[game_level]), " D"), draws[game_level]);
    display_text(px, by + 138, b, C_DIM);

    int hy = by + 8 * cs - 40;
    display_text(px, hy, "A place", C_DIM);
    display_text(px, hy + 12, "Y undo", C_DIM);
    display_text(px, hy + 24, "X hints", C_DIM);
    display_text(px, hy + 36, "B menu", C_DIM);
}

static void render(const char *status, uint16_t col, bb_t edge)
{
    display_clear(C_BG);
    draw_board(edge);
    draw_panel(status, col);
}

/* ── Rules & flow ───────────────────────────────────────────────────── */

static void stats_load(void)
{
    char k[4] = "w0";
    for (int i = 0; i < 3; i++) {
        k[1] = (char)('0' + i);
        k[0] = 'w'; wins[i] = g_load_int(k, 0);
        k[0] = 'l'; losses[i] = g_load_int(k, 0);
        k[0] = 'd'; draws[i] = g_load_int(k, 0);
    }
    level = g_load_int("lvl", LV_NORMAL);
    if (level < 0 || level > 2) level = LV_NORMAL;
    pref_human = g_load_int("col", BLACK) ? WHITE : BLACK;
    hints = g_load_int("hint", 1) ? 1 : 0;
}

static void new_game(void)
{
    disc[BLACK] = (1ULL << 28) | (1ULL << 35);
    disc[WHITE] = (1ULL << 27) | (1ULL << 36);
    side = BLACK;
    last_sq = -1;
    nhist = 0;
    game_over = 0;
    game_level = level;
    human = pref_human;
    cur_sq = 19;
    snap_pending = 1;
}

/* Put a disc for the side to move, with a little flip animation. */
static void play_move(int sq, int animate)
{
    bb_t f = flips_for(disc[side], disc[!side], sq);
    last_sq = sq;
    if (animate) {
        disc[side] |= 1ULL << sq;
        render(side == human ? "" : "CPU moves", C_DIM, f);
        display_flush();
        delay(90000);
    }
    disc[side] |= f | (1ULL << sq);
    disc[!side] &= ~f;
    side = !side;
}

static void show_result(void)
{
    char b[40];
    int me = popcnt(disc[human]), cpu = popcnt(disc[!human]);
    const char *title = me > cpu ? "YOU WIN" : me < cpu ? "CPU WINS" : "DRAW";
    uint16_t col = me > cpu ? C_GOLD : C_TEXT;

    render("Game over", C_GOLD, 0);
    int w = 180, h = 92, x = bx + 4 * cs - w / 2, y = by + 4 * cs - h / 2;
    g_panel(x, y, w, h);
    g_text_large_c(x + w / 2, y + 10, title, col);
    b[0] = '\0';
    g_cat_int(g_cat(g_cat_int(b, me), " - "), cpu);
    g_text_c(x + w / 2, y + 36, b, C_TEXT);
    g_text_c(x + w / 2, y + 56, "A: play again", C_DIM);
    g_text_c(x + w / 2, y + 70, "B: menu", C_DIM);
    display_flush();
}

static void finish_game(void)
{
    char k[4] = "w0";
    int me = popcnt(disc[human]), cpu = popcnt(disc[!human]);
    int lv = game_level;

    game_over = 1;
    k[1] = (char)('0' + lv);
    if (me > cpu) {
        k[0] = 'w';
        g_save_int(k, ++wins[lv]);
    } else if (me < cpu) {
        k[0] = 'l';
        g_save_int(k, ++losses[lv]);
    } else {
        k[0] = 'd';
        g_save_int(k, ++draws[lv]);
    }
    show_result();
}

static const char HELP[] =
    "Place a disc so it traps a line of\n"
    "the CPU's discs between it and one\n"
    "of yours - straight or diagonal.\n"
    "Trapped discs flip to your colour.\n"
    "No legal move? You pass.\n"
    "Most discs at the end wins.\n"
    "\n"
    "Corners can never be flipped back:\n"
    "fight for them, and be careful on\n"
    "the squares right next to them.";

enum { M_RESUME, M_NEW, M_QUIT };

static int menu(void)
{
    static char lv_item[32], col_item[32], hint_item[24];
    const char *items[] = {"Resume", "New game", lv_item, col_item, hint_item, "Statistics", "How to play",
                           "Quit"};
    int sel = 0;

    for (;;) {
        lv_item[0] = col_item[0] = hint_item[0] = '\0';
        g_cat(g_cat(g_cat(lv_item, "Level: "), LEVEL_NAME[level]), "  < >");
        g_cat(g_cat(col_item, "Play as: "), pref_human == BLACK ? "Black (1st)" : "White (2nd)");
        g_cat(hint_item, hints ? "Hints: on" : "Hints: off");
        render(game_over ? "Game over" : "Paused", C_DIM, 0);
        int r = g_menu("REVERSI", items, 8, sel, 1);
        int idx = r & 0xFF;
        if (r < 0) return M_RESUME;
        sel = idx;
        if (idx == 2) {
            level = (r & G_MENU_LEFT) ? (level + 2) % 3 : (level + 1) % 3;
            g_save_int("lvl", level);
            continue;
        }
        if (idx == 3) {
            pref_human = !pref_human;
            g_save_int("col", pref_human);
            continue;
        }
        if (idx == 4) {
            hints = !hints;
            g_save_int("hint", hints);
            continue;
        }
        if (r & (G_MENU_LEFT | G_MENU_RIGHT)) continue;
        switch (idx) {
        case 1:
            return M_NEW;
        case 5: {
            char b[160];
            b[0] = '\0';
            for (int i = 0; i < 3; i++) {
                int n = wins[i] + losses[i] + draws[i];
                g_cat(g_cat(b, LEVEL_NAME[i]), "\n");
                g_cat_int(g_cat(g_cat_int(g_cat(g_cat_int(g_cat(b, "  won "), wins[i]), "  lost "), losses[i]),
                                "  drawn "), draws[i]);
                if (n) g_cat(g_cat_int(g_cat(b, "  ("), wins[i] * 100 / n), "%)");
                g_cat(b, "\n");
            }
            render("Paused", C_DIM, 0);
            g_message("STATISTICS", b);
            continue;
        }
        case 6:
            render("Paused", C_DIM, 0);
            g_message("HOW TO PLAY", HELP);
            continue;
        case 7:
            return M_QUIT;
        default:
            return M_RESUME;
        }
    }
}

/* If the side to move has no move, pass (or end the game). */
static int resolve_passes(void)
{
    if (legal_moves(disc[side], disc[!side])) return 0;
    if (!legal_moves(disc[!side], disc[side])) {
        finish_game();
        return 1;
    }
    render(side == human ? "You pass" : "CPU pass", C_GOLD, 0);
    display_flush();
    g_sleep_or_key(1200);
    side = !side;
    return 0;
}

static void snap_cursor_to_move(void)
{
    bb_t moves = legal_moves(disc[human], disc[!human]);
    if (moves && !(moves & (1ULL << cur_sq))) {
        /* Nearest legal square, so the first press does something. */
        int best = lowbit(moves), bd = 99;
        for (bb_t m = moves; m; m &= m - 1) {
            int sq = lowbit(m);
            int d = (sq % 8 - cur_sq % 8) * (sq % 8 - cur_sq % 8) + (sq / 8 - cur_sq / 8) * (sq / 8 - cur_sq / 8);
            if (d < bd) {
                bd = d;
                best = sq;
            }
        }
        cur_sq = best;
    }
}

int main(void)
{
    g_init();
    g_seed_random();
    g_theme.bg = 0x0882;
    g_theme.border = 0x3D6B;
    g_theme.sel_bg = 0x0327;
    layout();
    eval_init();
    stats_load();
    new_game();

    int dirty = 1;
    const char *note = 0; /* replaces "Your move" until the next key */
    for (;;) {
        if (game_over) {
            uint32_t k = g_wait_key();
            if (k == K_A) {
                new_game();
                dirty = 1;
            } else if (k == K_B || k == K_HOME) {
                int r = menu();
                if (r == M_QUIT) break;
                if (r == M_NEW) {
                    new_game();
                    dirty = 1;
                } else {
                    show_result();
                }
            }
            continue;
        }
        if (resolve_passes()) continue;

        if (side != human) {
            uint32_t t0 = g_now();
            render("Thinking", C_ACCENT, 0);
            display_flush();
            int sq = ai_choose(disc[side], disc[!side], game_level);
            int spent = (int)(g_now() - t0);
            if (spent < 450) delay((uint32_t)(450 - spent) * 1000);
            play_move(sq, 1);
            g_flush_input(); /* presses made while it thought don't count */
            snap_pending = 1;
            dirty = 1;
            continue;
        }

        if (dirty) {
            if (snap_pending) snap_cursor_to_move();
            snap_pending = 0;
            render(note ? note : "Your move", note ? C_LAST : C_GOLD, 0);
            display_flush();
            dirty = 0;
        }
        uint32_t k = g_key();
        if (!k) {
            delay(15000);
            continue;
        }
        int x = cur_sq % 8, y = cur_sq / 8;
        note = 0;
        switch (k) {
        case K_UP:    cur_sq = ((y + 7) % 8) * 8 + x; break;
        case K_DOWN:  cur_sq = ((y + 1) % 8) * 8 + x; break;
        case K_LEFT:  cur_sq = y * 8 + (x + 7) % 8; break;
        case K_RIGHT: cur_sq = y * 8 + (x + 1) % 8; break;
        case K_A:
            if (legal_moves(disc[human], disc[!human]) & (1ULL << cur_sq)) {
                if (nhist < 64) {
                    hist[nhist].b[0] = disc[0];
                    hist[nhist].b[1] = disc[1];
                    hist[nhist].side = (int8_t)side;
                    hist[nhist].last = (int8_t)last_sq;
                    nhist++;
                }
                play_move(cur_sq, 1);
            } else {
                note = "Not legal";
            }
            break;
        case K_Y:
            if (nhist) {
                nhist--;
                disc[0] = hist[nhist].b[0];
                disc[1] = hist[nhist].b[1];
                side = hist[nhist].side;
                last_sq = hist[nhist].last;
            }
            break;
        case K_X:
            hints = !hints;
            g_save_int("hint", hints);
            break;
        case K_B:
        case K_HOME: {
            int r = menu();
            if (r == M_QUIT) goto out;
            /* A new level or colour takes effect with the next game. */
            if (r == M_NEW) new_game();
            break;
        }
        default:
            break;
        }
        dirty = 1;
    }
out:
    display_clear(0);
    display_flush();
    return 0;
}
