/**
 * @file main.c
 * @brief Long Nardy (длинные нарды) — the long backgammon of Russia and the
 *        Caucasus, against the CPU.
 *
 * Rules played:
 *  - 15 checkers each, all starting on the player's head; heads sit in
 *    diagonally opposite corners and both sides move the same way round,
 *    into the quarter just before their own head.
 *  - No hitting: a point holding even one enemy checker is closed.
 *  - One checker may leave the head per turn — two on a player's first turn
 *    if it is 6-6, 4-4 or 3-3.
 *  - Doubles play four times.  As many dice as possible must be used; if only
 *    one die can be played, it must be the larger one when that is possible.
 *  - No wall of six points in a row unless an enemy checker is already past it.
 *  - With all 15 at home a player bears off: exact number, or a higher die
 *    from the rearmost checker.  First off wins; 2 points (mars) if the loser
 *    bore off none.
 *
 * Each side walks its own path 0..23 from its head (black's is white's
 * shifted by 12), so the head, home, bear-off and wall rules are all simple
 * path-index checks.  The CPU enumerates every legal way to play the roll and
 * scores the result (racing, head, walls in your path, your free dice).
 *
 * Controls:
 *   D-pad    choose a checker / a destination (only legal ones are offered)
 *   A        pick the checker / move it there
 *   B        back; with nothing picked: menu
 *   Y        undo a move this turn
 *   X        hint
 *   HOME     menu
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"

#define WHITE 0  /* you     */
#define BLACK 1  /* the CPU */
#define OFF   24 /* pseudo-point: borne off */
#define HOME0 18 /* first home point on a player's path */

typedef struct {
    int8_t pt[24];  /* >0 white checkers, <0 black checkers */
    uint8_t off[2]; /* borne off */
} pos_t;

typedef struct {
    uint8_t head_used, head_limit;
} turn_t;

typedef struct {
    uint8_t v[4]; /* remaining dice, largest first */
    uint8_t n;
} dice_t;

/* ── Board geometry in path terms ───────────────────────────────────── */
static int owner(const pos_t *p, int a)
{
    int v = p->pt[a];
    return v > 0 ? WHITE : v < 0 ? BLACK : -1;
}

static int count_at(const pos_t *p, int a)
{
    int v = p->pt[a];
    return v < 0 ? -v : v;
}

static int to_abs(int c, int f) { return c == WHITE ? f : (f + 12) % 24; }
static int to_path(int c, int a) { return c == WHITE ? a : (a + 12) % 24; }
static void put(pos_t *p, int a, int c, int n) { p->pt[a] = (int8_t)(p->pt[a] + (c == WHITE ? n : -n)); }

static int all_home(const pos_t *p, int c)
{
    for (int f = 0; f < HOME0; f++) {
        if (owner(p, to_abs(c, f)) == c) return 0;
    }
    return 1;
}

/* Does c have six points in a row with no enemy checker beyond them?
 * Walls are measured along the enemy's path, where they block. */
static int illegal_wall(const pos_t *p, int c)
{
    int o = !c;
    uint32_t mine = 0, theirs = 0;
    for (int q = 0; q < 24; q++) {
        int w = owner(p, to_abs(o, q));
        if (w == c) mine |= 1u << q;
        else if (w == o) theirs |= 1u << q;
    }
    uint32_t run = mine & (mine >> 1) & (mine >> 2) & (mine >> 3) & (mine >> 4) & (mine >> 5);
    if (!run) return 0;
    int k = 31 - __builtin_clz(run); /* the furthest wall is the hardest to excuse */
    return (theirs >> (k + 6)) == 0;
}

/* Where c's checker on point a lands with die d (OFF = borne off), or -1. */
static int step_dest(const pos_t *p, int c, int a, int d, const turn_t *t)
{
    if (owner(p, a) != c) return -1;
    int f = to_path(c, a);
    if (f == 0 && t->head_used >= t->head_limit) return -1;
    int tf = f + d;
    if (tf < 24) {
        int b = to_abs(c, tf);
        return owner(p, b) == !c ? -1 : b;
    }
    if (!all_home(p, c)) return -1;
    if (tf > 24) {
        /* A higher die only bears off the rearmost checker. */
        for (int q = HOME0; q < f; q++) {
            if (owner(p, to_abs(c, q)) == c) return -1;
        }
    }
    return OFF;
}

/* Make the step if it is legal (wall rule included); returns dest or -1. */
static int do_step(pos_t *p, int c, int a, int d, turn_t *t)
{
    int b = step_dest(p, c, a, d, t);
    if (b < 0) return -1;
    put(p, a, c, -1);
    if (b == OFF) {
        p->off[c]++;
    } else {
        put(p, b, c, 1);
        if (illegal_wall(p, c)) {
            put(p, b, c, -1);
            put(p, a, c, 1);
            return -1;
        }
    }
    if (to_path(c, a) == 0) t->head_used++;
    return b;
}

static void dice_without(const dice_t *d, int i, dice_t *r)
{
    r->n = 0;
    for (int k = 0; k < d->n; k++) {
        if (k != i) r->v[r->n++] = d->v[k];
    }
}

static int is_double(const dice_t *d) { return d->n > 1 && d->v[0] == d->v[d->n - 1]; }

/* Most dice c can still use.  For doubles, steps are tried in path order
 * (each from at or past the previous source) so equal orderings are walked
 * once. */
static int max_dice(const pos_t *p, int c, const dice_t *d, turn_t t, int min_f)
{
    if (!d->n) return 0;
    int best = 0, dbl = is_double(d);
    for (int i = 0; i < d->n; i++) {
        if (i && d->v[i] == d->v[i - 1]) continue;
        dice_t r;
        dice_without(d, i, &r);
        for (int f = dbl ? min_f : 0; f < 24; f++) {
            int a = to_abs(c, f);
            if (owner(p, a) != c) continue;
            pos_t q = *p;
            turn_t tt = t;
            if (do_step(&q, c, a, d->v[i], &tt) < 0) continue;
            int n = 1 + max_dice(&q, c, &r, tt, f);
            if (n > best) {
                best = n;
                if (best == d->n) return best;
            }
        }
    }
    return best;
}

static int any_step(const pos_t *p, int c, int die, turn_t t)
{
    for (int a = 0; a < 24; a++) {
        pos_t q = *p;
        turn_t tt = t;
        if (owner(p, a) == c && do_step(&q, c, a, die, &tt) >= 0) return 1;
    }
    return 0;
}

/* May c play die d->v[i] from point a now?  It must start a sequence using
 * @p m dice (= max_dice for this state), and the larger die when only one
 * of two different dice can be played. */
static int step_legal(const pos_t *p, int c, const dice_t *d, turn_t t, int m, int a, int i)
{
    if (!m) return 0;
    pos_t q = *p;
    turn_t tt = t;
    if (do_step(&q, c, a, d->v[i], &tt) < 0) return 0;
    dice_t r;
    dice_without(d, i, &r);
    if (1 + max_dice(&q, c, &r, tt, 0) != m) return 0;
    if (m == 1 && d->n == 2 && d->v[0] != d->v[1] && i == 1 && any_step(p, c, d->v[0], t)) return 0;
    return 1;
}

/* ── Evaluation (from c's side, higher is better) ───────────────────── */

/* How many die faces give o at least one step (ignoring head/wall limits). */
static int free_faces(const pos_t *p, int o)
{
    int n = 0, home = all_home(p, o);
    for (int d = 1; d <= 6; d++) {
        for (int f = 0; f < 24; f++) {
            if (owner(p, to_abs(o, f)) != o) continue;
            int tf = f + d;
            if (tf >= 24 ? home : owner(p, to_abs(o, tf)) != !o) {
                n++;
                break;
            }
        }
    }
    return n;
}

static const int8_t WALL_STEP[7] = {0, 0, 4, 7, 11, 15, 19};

static int evaluate(const pos_t *p, int c)
{
    int o = !c, s = 0, pip = 0, head = 0, pts = 0, race = 1;

    for (int f = 0; f < 24; f++) {
        int a = to_abs(c, f), n = count_at(p, a);
        if (!n) continue;
        if (owner(p, a) == c) {
            pip += n * (24 - f);
            if (f < 12) race = 0;
            if (f == 0) {
                head = n;
            } else {
                pts++;
                if (n > 4) s -= (n - 4) * 4;
            }
        } else if (to_path(o, a) < 12) {
            race = 0;
        }
    }
    if (race) {
        /* No more contact: pure race and bear-off. */
        return -pip * 6 + p->off[c] * 10;
    }
    s -= pip * 2 + head * 6 - pts * 3;

    /* Walls in the enemy's path, ahead of its rearmost checker. */
    int rear = 24, run = 0;
    for (int q = 0; q < 24; q++) {
        if (owner(p, to_abs(o, q)) == o) {
            rear = q;
            break;
        }
    }
    for (int q = rear + 1; q < 24; q++) {
        if (owner(p, to_abs(o, q)) == c) {
            if (run < 6) run++;
            s += WALL_STEP[run];
        } else {
            run = 0;
        }
    }
    s += (6 - free_faces(p, o)) * 6;
    return s;
}

/* ── CPU move search ────────────────────────────────────────────────── */
typedef struct {
    int8_t a, b, die;
} step_t;

static step_t ai_seq[4], cur_seq[4];
static int ai_len, ai_best, ai_target, ai_noise, ai_larger_only, ai_canonical;
static uint32_t ai_leaves;

static void ai_walk(const pos_t *p, int c, const dice_t *d, turn_t t, int min_f, int used)
{
    if (used == ai_target) {
        int s = evaluate(p, c) + (ai_noise ? (int)g_rand_n((uint32_t)ai_noise) : 0);
        ai_leaves++;
        if (s > ai_best || ai_len == 0) {
            ai_best = s;
            ai_len = used;
            for (int k = 0; k < used; k++) ai_seq[k] = cur_seq[k];
        }
        if ((ai_leaves & 63) == 0) delay(0);
        return;
    }
    int dbl = ai_canonical && is_double(d);
    for (int i = 0; i < d->n; i++) {
        if (i && d->v[i] == d->v[i - 1]) continue;
        if (ai_larger_only && i == 1) continue;
        dice_t r;
        dice_without(d, i, &r);
        for (int f = dbl ? min_f : 0; f < 24; f++) {
            int a = to_abs(c, f);
            if (owner(p, a) != c) continue;
            pos_t q = *p;
            turn_t tt = t;
            int b = do_step(&q, c, a, d->v[i], &tt);
            if (b < 0) continue;
            cur_seq[used].a = (int8_t)a;
            cur_seq[used].b = (int8_t)b;
            cur_seq[used].die = (int8_t)d->v[i];
            ai_walk(&q, c, &r, tt, f, used + 1);
        }
    }
}

/* Best way for c to play the dice; result in ai_seq[0..ai_len). */
static void ai_choose(const pos_t *p, int c, const dice_t *d, turn_t t, int noise)
{
    ai_len = 0;
    ai_leaves = 0;
    ai_target = max_dice(p, c, d, t, 0);
    if (!ai_target) return;
    ai_larger_only = ai_target == 1 && d->n == 2 && d->v[0] != d->v[1] && any_step(p, c, d->v[0], t);
    ai_noise = noise;
    ai_best = -0x7FFFFFFF;
    ai_canonical = 1;
    ai_walk(p, c, d, t, 0, 0);
    if (!ai_len) {
        /* Only reachable in an odd order: walk them all. */
        ai_canonical = 0;
        ai_walk(p, c, d, t, 0, 0);
    }
}

/* ── Game state ─────────────────────────────────────────────────────── */
static pos_t P;
static dice_t D;     /* dice still to play */
static turn_t T;
static uint8_t roll[4];
static int nroll;
static int side;
static int first_turn[2];
static int level = 1; /* 0 easy, 1 normal */
static int wins, losses, mars_won, mars_lost;
static int last_cpu_dest = -1;

typedef struct {
    pos_t p;
    dice_t d;
    turn_t t;
} snap_t;
static snap_t undo_stack[4];
static int nundo;

/* Human selection */
enum { M_NONE, M_SRC, M_DST };
static int ui_mode = M_NONE;
static int8_t srcs[24];
static int nsrc, src_i;
typedef struct {
    int8_t dest;
    uint8_t n;
    uint8_t dice[4];
} dmove_t;
static dmove_t dests[16];
static int ndst, dst_i, sel_a = -1;

/* ── Layout & colours ───────────────────────────────────────────────── */
static int pw, cr, barw, bx0, by0, by1, th, tray_x, tray_w;

#define C_BG      0x0841
#define C_STATUS  0x18C3
#define C_FRAME   0x59A3
#define C_BAR     0x4162
#define C_TRAY    0x8A65
#define C_FIELD   0xCD2D
#define C_PT_DARK 0x7943
#define C_PT_LITE 0xEF39
#define C_W_FILL  0xFFDE
#define C_W_RIM   0x8C51
#define C_W_RING  0xD69A
#define C_B_FILL  0x2124
#define C_B_RIM   0x0000
#define C_B_RING  0x632C
#define C_CURSOR  0xFFE0
#define C_DEST    0x07E0
#define C_LAST    0xFD20
#define C_TEXT    0xFFFF
#define C_DIM     0x8CB5
#define C_GOLD    0xFE87

static void layout(void)
{
    barw = 8;
    pw = (G_W - 44 - barw) / 12;
    if (pw > 28) pw = 28;
    cr = pw / 2 - 1;
    bx0 = 6;
    by0 = 17;
    by1 = G_H - 3;
    th = (by1 - by0) * 2 / 5;
    tray_x = bx0 + 12 * pw + barw + 6;
    tray_w = G_W - tray_x - 3;
}

/* White's head (point 0) is top-right; points run right-to-left along the
 * top, then left-to-right along the bottom.  Black's head is bottom-left. */
static int col_of(int a) { return a < 12 ? 11 - a : a - 12; }
static int top_row(int a) { return a < 12; }
static int pt_x(int a)
{
    int c = col_of(a);
    return bx0 + c * pw + (c >= 6 ? barw : 0);
}

static int stack_sp(int n)
{
    int sp = n > 1 ? (th - 2 * cr) / (n - 1) : 2 * cr;
    return sp > 2 * cr ? 2 * cr : sp;
}

static int slot_y(int a, int i, int n)
{
    int sp = stack_sp(n);
    return top_row(a) ? by0 + 1 + cr + i * sp : by1 - 2 - cr - i * sp;
}

static void draw_checker(int x, int y, int c)
{
    if (c == WHITE) {
        display_circle_fill(x, y, cr, C_W_FILL);
        display_circle(x, y, cr, C_W_RIM);
        display_circle(x, y, cr - 3, C_W_RING);
    } else {
        display_circle_fill(x, y, cr, C_B_FILL);
        display_circle(x, y, cr, C_B_RIM);
        display_circle(x, y, cr - 3, C_B_RING);
    }
}

static void ring(int x, int y, uint16_t col, int thick)
{
    display_circle(x, y, cr + 1, col);
    if (thick) display_circle(x, y, cr + 2, col);
}

static void draw_die(int x, int y, int s, int v, int used)
{
    static const int8_t PIPS[7][7] = {
        {0}, {1, 4}, {2, 0, 8}, {3, 0, 4, 8}, {4, 0, 2, 6, 8}, {5, 0, 2, 4, 6, 8}, {6, 0, 2, 3, 5, 6, 8},
    };
    display_rounded_rect_fill(x, y, s, s, 3, used ? 0x6B4D : 0xFFFF);
    display_rounded_rect(x, y, s, s, 3, 0x0000);
    int r = s >= 18 ? 2 : 1;
    for (int k = 1; k <= PIPS[v][0]; k++) {
        int g = PIPS[v][k];
        display_circle_fill(x + s / 4 + (g % 3) * s / 4, y + s / 4 + (g / 3) * s / 4, r, used ? 0x39E7 : 0x0000);
    }
}

static void draw_dice(void)
{
    int counts[7] = {0};
    for (int k = 0; k < D.n; k++) counts[D.v[k]]++;
    int s = nroll == 4 ? 16 : 20, gap = 5;
    int w = nroll * s + (nroll - 1) * gap;
    /* in the middle of the half the mover's dice belong to */
    int cx = side == WHITE ? bx0 + 9 * pw + barw : bx0 + 3 * pw;
    int x = cx - w / 2, y = (by0 + by1) / 2 - s / 2;
    for (int k = 0; k < nroll; k++) {
        int used = !counts[roll[k]];
        if (!used) counts[roll[k]]--;
        draw_die(x + k * (s + gap), y, s, roll[k], used);
    }
}

static int pips(int c)
{
    int s = 0;
    for (int a = 0; a < 24; a++) {
        if (owner(&P, a) == c) s += count_at(&P, a) * (24 - to_path(c, a));
    }
    return s;
}

static void draw_status(const char *msg)
{
    char b[24];
    display_rect(0, 0, G_W, 13, C_STATUS);
    b[0] = '\0';
    g_cat_int(g_cat(b, "YOU "), pips(WHITE));
    display_text(4, 2, b, side == WHITE ? C_GOLD : C_DIM);
    b[0] = '\0';
    g_cat_int(g_cat(b, "CPU "), pips(BLACK));
    g_text_r(G_W - 4, 2, b, side == BLACK ? C_GOLD : C_DIM);
    if (msg) g_text_c(G_W / 2, 2, msg, C_TEXT);
}

static void draw_tray(void)
{
    display_rect(tray_x, by0 - 3, tray_w, by1 - by0 + 6, C_FRAME);
    display_rect(tray_x + 2, by0, tray_w - 4, by1 - by0, C_TRAY);
    for (int i = 0; i < P.off[BLACK]; i++) display_rect(tray_x + 4, by0 + 2 + i * 5, tray_w - 8, 4, C_B_FILL);
    for (int i = 0; i < P.off[WHITE]; i++) display_rect(tray_x + 4, by1 - 6 - i * 5, tray_w - 8, 4, C_W_FILL);
}

static void draw_board(void)
{
    display_rect(bx0 - 4, by0 - 3, 12 * pw + barw + 8, by1 - by0 + 6, C_FRAME);
    display_rect(bx0, by0, 6 * pw, by1 - by0, C_FIELD);
    display_rect(bx0 + 6 * pw + barw, by0, 6 * pw, by1 - by0, C_FIELD);
    for (int a = 0; a < 24; a++) {
        int x = pt_x(a), col = col_of(a);
        uint16_t c = ((col + top_row(a)) & 1) ? C_PT_LITE : C_PT_DARK;
        if (top_row(a)) {
            display_triangle_fill(x, by0, x + pw - 1, by0, x + pw / 2, by0 + th, c);
        } else {
            display_triangle_fill(x, by1 - 1, x + pw - 1, by1 - 1, x + pw / 2, by1 - 1 - th, c);
        }
    }
    for (int a = 0; a < 24; a++) {
        int n = count_at(&P, a), c = owner(&P, a), x = pt_x(a) + pw / 2;
        for (int i = 0; i < n; i++) draw_checker(x, slot_y(a, i, n), c);
        if (n > 5) {
            char b[4];
            itoa(n, b);
            int y = slot_y(a, n - 1, n);
            display_text(x - g_tw(b) / 2, y - 4, b, c == WHITE ? 0x0000 : 0xFFFF);
        }
    }
    draw_tray();
}

static void top_checker_xy(int a, int *x, int *y)
{
    int n = count_at(&P, a);
    *x = pt_x(a) + pw / 2;
    *y = slot_y(a, n ? n - 1 : 0, n ? n : 1);
}

static void draw_selection(void)
{
    int x, y;
    if (last_cpu_dest >= 0 && last_cpu_dest < 24 && ui_mode == M_NONE) {
        top_checker_xy(last_cpu_dest, &x, &y);
        ring(x, y, C_LAST, 1);
    }
    if (ui_mode == M_SRC && nsrc) {
        top_checker_xy(srcs[src_i], &x, &y);
        ring(x, y, C_CURSOR, 1);
    }
    if (ui_mode == M_DST) {
        top_checker_xy(sel_a, &x, &y);
        ring(x, y, C_CURSOR, 0);
        for (int k = 0; k < ndst; k++) {
            int b = dests[k].dest, focus = k == dst_i;
            if (b == OFF) {
                int y0 = (by0 + by1) / 2;
                display_rect_outline(tray_x - 1, y0, tray_w + 2, by1 - y0 + 3, focus ? C_CURSOR : C_DEST);
                if (focus) display_rect_outline(tray_x, y0 + 1, tray_w, by1 - y0 + 1, C_CURSOR);
                continue;
            }
            int n = count_at(&P, b);
            x = pt_x(b) + pw / 2;
            y = slot_y(b, n, n + 1);
            ring(x, y, focus ? C_CURSOR : C_DEST, focus);
            if (focus) display_circle(x, y, cr - 2, C_CURSOR);
        }
    }
}

static void render(const char *msg)
{
    display_clear(C_BG);
    draw_status(msg);
    draw_board();
    if (nroll) draw_dice();
    draw_selection();
}

/* ── Human turn ─────────────────────────────────────────────────────── */

static int screen_key(int a)
{
    if (a == OFF) return 1000;
    return (top_row(a) ? 0 : 100) + col_of(a) * 2 + (col_of(a) >= 6);
}

static void sort_by_screen(int8_t *v, int n)
{
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && screen_key(v[j]) < screen_key(v[j - 1]); j--) {
            int8_t t = v[j];
            v[j] = v[j - 1];
            v[j - 1] = t;
        }
    }
}

static void build_sources(int m)
{
    nsrc = 0;
    for (int a = 0; a < 24; a++) {
        if (owner(&P, a) != WHITE) continue;
        for (int i = 0; i < D.n; i++) {
            if (i && D.v[i] == D.v[i - 1]) continue;
            if (step_legal(&P, WHITE, &D, T, m, a, i)) {
                srcs[nsrc++] = (int8_t)a;
                break;
            }
        }
    }
    sort_by_screen(srcs, nsrc);
}

/* Every point the checker on @p a can reach this turn, one die at a time,
 * each step legal when it is taken. */
static void collect_dests(const pos_t *p, const dice_t *d, turn_t t, int a, uint8_t *seq, int depth)
{
    int m = max_dice(p, WHITE, d, t, 0);
    for (int i = 0; i < d->n; i++) {
        if (i && d->v[i] == d->v[i - 1]) continue;
        if (!step_legal(p, WHITE, d, t, m, a, i)) continue;
        pos_t q = *p;
        turn_t tt = t;
        int b = do_step(&q, WHITE, a, d->v[i], &tt);
        seq[depth] = d->v[i];
        int k;
        for (k = 0; k < ndst && dests[k].dest != b; k++) {
        }
        if (k == ndst && ndst < 16) {
            dests[ndst].dest = (int8_t)b;
            dests[ndst].n = (uint8_t)(depth + 1);
            for (int j = 0; j <= depth; j++) dests[ndst].dice[j] = seq[j];
            ndst++;
        }
        if (b != OFF) {
            dice_t r;
            dice_without(d, i, &r);
            collect_dests(&q, &r, tt, b, seq, depth + 1);
        }
    }
}

static void build_dests(int a)
{
    uint8_t seq[4];
    ndst = 0;
    collect_dests(&P, &D, T, a, seq, 0);
    /* screen order */
    for (int i = 1; i < ndst; i++) {
        for (int j = i; j > 0 && screen_key(dests[j].dest) < screen_key(dests[j - 1].dest); j--) {
            dmove_t t = dests[j];
            dests[j] = dests[j - 1];
            dests[j - 1] = t;
        }
    }
    /* Start on the one-die move with the larger die, wherever it is. */
    dst_i = 0;
    for (int k = 0; k < ndst; k++) {
        if (dests[k].n == 1 && (dests[dst_i].n != 1 || dests[k].dice[0] > dests[dst_i].dice[0])) dst_i = k;
    }
}

static void apply_dmove(int a, const dmove_t *mv)
{
    undo_stack[nundo].p = P;
    undo_stack[nundo].d = D;
    undo_stack[nundo].t = T;
    nundo++; /* each move uses a die: at most 4 a turn */
    for (int k = 0; k < mv->n; k++) {
        int i;
        for (i = 0; i < D.n && D.v[i] != mv->dice[k]; i++) {
        }
        a = do_step(&P, WHITE, a, D.v[i], &T);
        dice_t r;
        dice_without(&D, i, &r);
        D = r;
    }
}

/* Index in @p v (n entries) nearest to @p a in the other row. */
static int other_row(const int8_t *v, int n, int a)
{
    int best = -1, bd = 99;
    for (int k = 0; k < n; k++) {
        if (v[k] == OFF || a == OFF || top_row(v[k]) == top_row(a)) continue;
        int dd = col_of(v[k]) - col_of(a);
        if (dd < 0) dd = -dd;
        if (dd < bd) {
            bd = dd;
            best = k;
        }
    }
    return best;
}

static void hint(void)
{
    ai_choose(&P, WHITE, &D, T, 0);
    if (!ai_len) return;
    for (int k = 0; k < nsrc; k++) {
        if (srcs[k] == ai_seq[0].a) src_i = k;
    }
    sel_a = srcs[src_i];
    build_dests(sel_a);
    for (int k = 0; k < ndst; k++) {
        if (dests[k].dest == ai_seq[0].b) dst_i = k;
    }
    ui_mode = M_DST;
}

enum { R_OK, R_NEW, R_QUIT };
static int game_menu(void);

static int human_turn(void)
{
    int prev = -1;
    nundo = 0;
    for (;;) {
        int m = max_dice(&P, WHITE, &D, T, 0);
        if (!m) {
            ui_mode = M_NONE;
            if (!nundo) {
                render("No moves");
                display_flush();
                g_sleep_or_key(1300);
            }
            return R_OK;
        }
        build_sources(m);
        src_i = 0;
        for (int k = 0; k < nsrc; k++) {
            if (srcs[k] == prev) src_i = k;
        }
        ui_mode = M_SRC;

        int acted = 0;
        while (!acted) {
            render(ui_mode == M_DST ? "Where to?" : "Your move");
            display_flush();
            uint32_t k = g_wait_key();
            int8_t *list = srcs;
            int n = nsrc, *idx = &src_i;
            int8_t dlist[16];
            if (ui_mode == M_DST) {
                for (int j = 0; j < ndst; j++) dlist[j] = dests[j].dest;
                list = dlist;
                n = ndst;
                idx = &dst_i;
            }
            switch (k) {
            case K_RIGHT:
                *idx = (*idx + 1) % n;
                break;
            case K_LEFT:
                *idx = (*idx + n - 1) % n;
                break;
            case K_UP:
            case K_DOWN: {
                int j = other_row(list, n, list[*idx]);
                if (j >= 0) *idx = j;
                else *idx = (*idx + (k == K_DOWN ? 1 : n - 1)) % n;
                break;
            }
            case K_A:
                if (ui_mode == M_SRC) {
                    sel_a = srcs[src_i];
                    build_dests(sel_a);
                    ui_mode = M_DST;
                } else {
                    prev = dests[dst_i].dest;
                    apply_dmove(sel_a, &dests[dst_i]);
                    acted = 1;
                }
                break;
            case K_X:
                hint();
                break;
            case K_Y:
                if (ui_mode == M_DST) {
                    ui_mode = M_SRC;
                } else if (nundo) {
                    nundo--;
                    P = undo_stack[nundo].p;
                    D = undo_stack[nundo].d;
                    T = undo_stack[nundo].t;
                    acted = 1;
                }
                break;
            case K_B:
            case K_HOME:
                if (k == K_B && ui_mode == M_DST) {
                    ui_mode = M_SRC; /* B steps back; with nothing picked it is the menu */
                } else {
                    ui_mode = M_NONE;
                    int r = game_menu();
                    if (r != R_OK) return r;
                    ui_mode = M_SRC;
                }
                break;
            default:
                break;
            }
        }
    }
}

/* ── CPU turn ───────────────────────────────────────────────────────── */

static void cpu_turn(void)
{
    uint32_t t0 = g_now();
    render("CPU thinking");
    display_flush();
    ai_choose(&P, BLACK, &D, T, level == 0 ? 90 : 2);
    int spent = (int)(g_now() - t0);
    if (spent < 400) delay((uint32_t)(400 - spent) * 1000);
    if (!ai_len) {
        render("CPU can't move");
        display_flush();
        g_sleep_or_key(1200);
        return;
    }
    for (int k = 0; k < ai_len; k++) {
        int i;
        for (i = 0; i < D.n && D.v[i] != ai_seq[k].die; i++) {
        }
        do_step(&P, BLACK, ai_seq[k].a, ai_seq[k].die, &T);
        dice_t r;
        dice_without(&D, i, &r);
        D = r;
        last_cpu_dest = ai_seq[k].b;
        render("CPU moves");
        display_flush();
        delay(330000);
    }
    g_flush_input();
}

/* ── Game flow ──────────────────────────────────────────────────────── */

static void stats_load(void)
{
    wins = g_load_int("w", 0);
    losses = g_load_int("l", 0);
    mars_won = g_load_int("mw", 0);
    mars_lost = g_load_int("ml", 0);
    level = g_load_int("lvl", 1) ? 1 : 0;
}

static void roll_dice(void)
{
    for (int f = 0; f < 4; f++) {
        D.v[0] = roll[0] = (uint8_t)(1 + g_rand_n(6));
        D.v[1] = roll[1] = (uint8_t)(1 + g_rand_n(6));
        nroll = D.n = 2;
        render(side == WHITE ? "Rolling" : "CPU rolls");
        display_flush();
        delay(50000);
    }
    int d1 = (int)(1 + g_rand_n(6)), d2 = (int)(1 + g_rand_n(6));
    if (d2 > d1) {
        int t = d1;
        d1 = d2;
        d2 = t;
    }
    nroll = d1 == d2 ? 4 : 2;
    D.n = (uint8_t)nroll;
    for (int k = 0; k < nroll; k++) roll[k] = D.v[k] = (uint8_t)(k ? (d1 == d2 ? d1 : d2) : d1);
    T.head_used = 0;
    T.head_limit = (first_turn[side] && d1 == d2 && (d1 == 6 || d1 == 4 || d1 == 3)) ? 2 : 1;
    first_turn[side] = 0;
}

static void new_game(void)
{
    for (int a = 0; a < 24; a++) P.pt[a] = 0;
    P.pt[0] = 15;   /* white head */
    P.pt[12] = -15; /* black head */
    P.off[0] = P.off[1] = 0;
    first_turn[0] = first_turn[1] = 1;
    last_cpu_dest = -1;
    ui_mode = M_NONE;
    nroll = 0;
    D.n = 0;

    /* Opening roll: one die each, higher starts. */
    int w, b;
    do {
        w = (int)(1 + g_rand_n(6));
        b = (int)(1 + g_rand_n(6));
    } while (w == b);
    side = w > b ? WHITE : BLACK;
    roll[0] = (uint8_t)w;
    roll[1] = (uint8_t)b;
    nroll = 2;
    D.n = 2;
    D.v[0] = (uint8_t)w;
    D.v[1] = (uint8_t)b;
    char msg[32] = "";
    g_cat(g_cat_int(g_cat(g_cat_int(g_cat(msg, "You "), w), " CPU "), b), w > b ? ": you start" : ": CPU starts");
    int s = side;
    side = WHITE; /* draw the opening dice on your side */
    render(msg);
    side = s;
    display_flush();
    g_sleep_or_key(1600);
    nroll = 0;
}

static const char HELP[] =
    "You are white: head top-right, home\n"
    "bottom-right. Both sides go the same\n"
    "way round; the CPU mirrors you.\n"
    "No hitting: you can't land where the\n"
    "CPU has even one checker.\n"
    "One checker leaves the head a turn\n"
    "(two on a first 6-6, 4-4 or 3-3).\n"
    "No wall of 6 unless a CPU checker is\n"
    "already past it. Use all the dice you\n"
    "can. All 15 home: bear off. First off\n"
    "wins - double (mars) if the loser\n"
    "bore off none.";

static void show_stats(void)
{
    char b[120] = "";
    int n = wins + losses;
    g_cat_int(g_cat(g_cat_int(g_cat(b, "Won "), wins), "   lost "), losses);
    if (n) g_cat(g_cat_int(g_cat(b, "  ("), wins * 100 / n), "%)");
    g_cat_int(g_cat(g_cat_int(g_cat(b, "\nMars won "), mars_won), "   mars lost "), mars_lost);
    g_message("STATISTICS", b);
}

static int game_menu(void)
{
    static char lv_item[24];
    const char *items[] = {"Resume", "New game", lv_item, "Statistics", "How to play", "Quit"};
    int sel = 0;
    for (;;) {
        lv_item[0] = '\0';
        g_cat(g_cat(lv_item, "CPU: "), level ? "Normal  < >" : "Easy  < >");
        render("Paused");
        int r = g_menu("LONG NARDY", items, 6, sel, 1);
        if (r < 0) return R_OK;
        int idx = r & 0xFF;
        sel = idx;
        if (idx == 2) {
            level = !level;
            g_save_int("lvl", level);
            continue;
        }
        if (r & (G_MENU_LEFT | G_MENU_RIGHT)) continue;
        switch (idx) {
        case 1:
            return R_NEW;
        case 3:
            render("Paused");
            show_stats();
            continue;
        case 4:
            render("Paused");
            g_message("HOW TO PLAY", HELP);
            continue;
        case 5:
            return R_QUIT;
        default:
            return R_OK;
        }
    }
}

/* Returns R_NEW or R_QUIT. */
static int game_over(int winner)
{
    int mars = P.off[!winner] == 0;
    char b[48] = "";
    if (winner == WHITE) {
        wins++;
        g_save_int("w", wins);
        if (mars) g_save_int("mw", ++mars_won);
    } else {
        losses++;
        g_save_int("l", losses);
        if (mars) g_save_int("ml", ++mars_lost);
    }
    nroll = 0;
    ui_mode = M_NONE;
    render("Game over");
    int w = 200, h = 104, x = (G_W - w) / 2, y = (G_H - h) / 2;
    g_panel(x, y, w, h);
    g_text_large_c(G_W / 2, y + 10, winner == WHITE ? "YOU WIN" : "CPU WINS", winner == WHITE ? C_GOLD : C_TEXT);
    g_text_c(G_W / 2, y + 34, mars ? "MARS - 2 points" : "1 point", mars ? C_GOLD : C_DIM);
    g_cat_int(g_cat(g_cat_int(g_cat(b, "Won "), wins), "  lost "), losses);
    g_text_c(G_W / 2, y + 52, b, C_TEXT);
    g_text_c(G_W / 2, y + h - 30, "A: play again", C_DIM);
    g_text_c(G_W / 2, y + h - 16, "B: menu", C_DIM);
    display_flush();
    for (;;) {
        uint32_t k = g_wait_key();
        if (k == K_A) return R_NEW;
        if (k == K_B || k == K_HOME) {
            int r = game_menu();
            if (r == R_QUIT) return R_QUIT;
            return R_NEW;
        }
    }
}

int main(void)
{
    g_init();
    g_seed_random();
    g_theme.bg = 0x2124;
    g_theme.border = 0xCD2D;
    g_theme.sel_bg = 0x7943;
    layout();
    stats_load();

    for (;;) {
        new_game();
        int r = R_OK;
        for (;;) {
            roll_dice();
            if (side == WHITE) {
                r = human_turn();
                if (r != R_OK) break;
            } else {
                cpu_turn();
            }
            if (P.off[side] == 15) {
                r = game_over(side);
                break;
            }
            side = !side;
        }
        if (r == R_QUIT) break;
    }
    display_clear(0);
    display_flush();
    return 0;
}
