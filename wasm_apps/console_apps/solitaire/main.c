/**
 * @file main.c
 * @brief Solitaire — Klondike patience for AkiraConsole.
 *
 * Build the four foundations up by suit from Ace to King.  On the seven
 * tableau columns, cards go down in alternating colours; only a King may
 * fill an empty column.  Deal from the stock one card at a time (or three,
 * from the menu).  Once every card is face up the game finishes itself.
 *
 * Controls:
 *   D-pad   move between piles; UP/DOWN on a column grows / shrinks the run
 *           of face-up cards you'll pick up
 *   A       pick up / put down (on the stock: deal)
 *   X       quick move: send the selection to the best legal spot
 *   B       put back what you're holding, otherwise deal from the stock
 *   Y       undo
 *   HOME    menu (new game, draw 1 / 3, statistics, quit)
 *
 * Statistics (played, won, best time, streaks) are kept per draw mode in NVS.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"

/* ── Cards ──────────────────────────────────────────────────────────── */
typedef uint8_t card_t; /* bits 0-3 rank 1..13, bits 4-5 suit, bit 7 face up */
#define RANK(c)   ((c) & 15)
#define SUIT(c)   (((c) >> 4) & 3)
#define FACEUP    0x80
#define IS_UP(c)  ((c) & FACEUP)
#define IS_RED(c) (SUIT(c) == S_HEART || SUIT(c) == S_DIAMOND)
#define MAKE(r, s) ((card_t)((r) | ((s) << 4)))

enum { S_SPADE, S_HEART, S_DIAMOND, S_CLUB };

/* Pile ids: one space for the cursor, the hand's origin and drop targets */
#define P_STOCK  0
#define P_WASTE  1
#define P_FOUND  2 /* 2..5, suit = id - 2  */
#define P_TAB    6 /* 6..12, col = id - 6  */
#define P_COUNT  13

typedef struct {
    card_t stock[24];
    card_t waste[24];
    card_t tab[7][20];
    uint8_t nstock, nwaste;
    uint8_t found[4]; /* top rank on each suit's foundation */
    uint8_t ntab[7];
    uint16_t moves;
} deal_t;

static deal_t D, deal0;

#define UNDO_MAX 32
static deal_t undo_buf[UNDO_MAX];
static int undo_n, undo_head;

static card_t hand[20];
static int nhand, hand_src;

static int cur = P_TAB;   /* pile under the cursor              */
static int depth = 1;     /* tableau: cards selected from the top */
static int draw_n = 1;    /* 1 or 3                              */
static int won;
static int started;       /* the clock runs from the first move  */
static int counted;       /* this deal was added to "played"     */
static uint32_t start_ms;
static uint32_t won_secs;

static char msg[32];
static uint32_t msg_until;

/* ── Statistics ─────────────────────────────────────────────────────── */
typedef struct { int played, won, best_time, streak, best_streak; } stats_t;
static stats_t st[2]; /* [0] draw 1, [1] draw 3 */

static void stat_key(char *k, char what, int mode)
{
    k[0] = what;
    k[1] = mode ? '3' : '1';
    k[2] = '\0';
}

static void stats_load(void)
{
    char k[4];
    for (int m = 0; m < 2; m++) {
        stat_key(k, 'p', m); st[m].played = g_load_int(k, 0);
        stat_key(k, 'w', m); st[m].won = g_load_int(k, 0);
        stat_key(k, 't', m); st[m].best_time = g_load_int(k, 0);
        stat_key(k, 's', m); st[m].streak = g_load_int(k, 0);
        stat_key(k, 'b', m); st[m].best_streak = g_load_int(k, 0);
    }
    draw_n = g_load_int("draw", 1) == 3 ? 3 : 1;
}

static void stats_save(int m)
{
    char k[4];
    stat_key(k, 'p', m); g_save_int(k, st[m].played);
    stat_key(k, 'w', m); g_save_int(k, st[m].won);
    stat_key(k, 't', m); g_save_int(k, st[m].best_time);
    stat_key(k, 's', m); g_save_int(k, st[m].streak);
    stat_key(k, 'b', m); g_save_int(k, st[m].best_streak);
}

static int mode(void) { return draw_n == 3; }

/* Leaving a deal you've played without winning ends the streak. */
static void abandon_deal(void)
{
    if (counted && !won && st[mode()].streak) {
        char k[4];
        st[mode()].streak = 0;
        stat_key(k, 's', mode());
        g_save_int(k, 0);
    }
}

/* ── Layout & colours ───────────────────────────────────────────────── */
#define HDR_H 13
static int cw, ch, gap, top_y, tab_y;

#define C_TABLE   0x0A46
#define C_HDR     0x0124
#define C_CARD    0xFFFF
#define C_EDGE    0x9CD3
#define C_RED     0xE0E4
#define C_BLACK   0x0000
#define C_BACK    0x1A95
#define C_BACK_HI 0x7D5C
#define C_SLOT    0x3BCC
#define C_CURSOR  0xFFE0
#define C_OK      0x07E0
#define C_BAD     0xFA49
#define C_TEXT    0xFFFF
#define C_DIM     0x8CB5
#define KEY       0xF81F /* transparent colour in glyph bitmaps */

static int col_x(int col) { return gap + col * (cw + gap); }

static void layout(void)
{
    cw = (G_W - 8 * 4) / 7;
    if (cw > 52) cw = 52;
    ch = cw * 4 / 3;
    if (ch > 64) ch = 64;
    gap = (G_W - 7 * cw) / 8;
    top_y = HDR_H + 3;
    tab_y = top_y + ch + 6;
}

/* ── Suit glyphs ────────────────────────────────────────────────────── */
static const uint8_t SUIT_BITS[4][7] = {
    {0x08, 0x1C, 0x3E, 0x7F, 0x6B, 0x08, 0x1C}, /* spade   */
    {0x36, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x08}, /* heart   */
    {0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x1C, 0x08}, /* diamond */
    {0x1C, 0x1C, 0x6B, 0x7F, 0x6B, 0x08, 0x1C}, /* club    */
};
static uint16_t glyph_s[4][7 * 7];     /* 7x7, suit colour   */
static uint16_t glyph_b[4][14 * 14];   /* 14x14, suit colour */
static uint16_t glyph_dim[4][14 * 14]; /* 14x14, slot colour */

static void build_glyphs(void)
{
    for (int s = 0; s < 4; s++) {
        uint16_t col = (s == S_HEART || s == S_DIAMOND) ? C_RED : C_BLACK;
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 7; x++) {
                int on = (SUIT_BITS[s][y] >> (6 - x)) & 1;
                glyph_s[s][y * 7 + x] = on ? col : KEY;
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int i = (y * 2 + dy) * 14 + x * 2 + dx;
                        glyph_b[s][i] = on ? col : KEY;
                        glyph_dim[s][i] = on ? C_SLOT : KEY;
                    }
                }
            }
        }
    }
}

/* ── Drawing ────────────────────────────────────────────────────────── */
static const char *const RANK_STR[14] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

static void draw_back(int x, int y)
{
    display_rounded_rect_fill(x, y, cw, ch, 3, C_BACK);
    display_rounded_rect(x + 2, y + 2, cw - 4, ch - 4, 2, C_BACK_HI);
    display_rounded_rect(x, y, cw, ch, 3, C_EDGE);
}

/* @p full: the whole card is visible (draw its centre). */
static void draw_card(int x, int y, card_t c, int full)
{
    if (!IS_UP(c)) {
        draw_back(x, y);
        return;
    }
    int s = SUIT(c), r = RANK(c);
    uint16_t col = IS_RED(c) ? C_RED : C_BLACK;

    display_rounded_rect_fill(x, y, cw, ch, 3, C_CARD);
    display_rounded_rect(x, y, cw, ch, 3, C_EDGE);
    /* Rank ink sits in rows 1-8, so a 9px overlap still reads cleanly. */
    display_text(x + 3, y + 1, RANK_STR[r], col);
    display_bitmap_transparent(x + cw - 10, y + 2, 7, 7, glyph_s[s], sizeof(glyph_s[s]), KEY);
    if (full) {
        int cx = x + cw / 2, cy = y + ch / 2 + 4;
        if (r >= 11) {
            display_text_large(cx - 5, cy - 9, RANK_STR[r], col);
            display_bitmap_transparent(cx - 3, cy + 11, 7, 7, glyph_s[s], sizeof(glyph_s[s]), KEY);
        } else {
            display_bitmap_transparent(cx - 7, cy - 7, 14, 14, glyph_b[s], sizeof(glyph_b[s]), KEY);
        }
    }
}

static void draw_slot(int x, int y) { display_rounded_rect(x, y, cw, ch, 3, C_SLOT); }

/* Vertical offsets for a tableau column so it fits above the bottom edge. */
static void col_steps(int col, int extra, int *fd, int *fu)
{
    int n = D.ntab[col] + extra, nfd = 0;
    for (int i = 0; i < D.ntab[col] && !IS_UP(D.tab[col][i]); i++) nfd++;
    int nfu = n - nfd;
    int avail = G_H - 2 - tab_y - ch;

    *fd = 5;
    *fu = 13;
    if (nfd * *fd + (nfu - 1) * *fu > avail) {
        *fd = 2;
        if (nfu > 1 && nfd * *fd + (nfu - 1) * *fu > avail) {
            *fu = (avail - nfd * *fd) / (nfu - 1);
            if (*fu < 6) *fu = 6;
        }
    }
}

static int card_y(int col, int idx, int fd, int fu)
{
    int y = tab_y;
    for (int i = 0; i < idx; i++) y += IS_UP(D.tab[col][i]) ? fu : fd;
    return y;
}

static int faceup_run(int col)
{
    int n = 0;
    for (int i = D.ntab[col] - 1; i >= 0 && IS_UP(D.tab[col][i]); i--) n++;
    return n;
}

static int pile_x(int p)
{
    if (p == P_STOCK) return col_x(0);
    if (p == P_WASTE) return col_x(1);
    if (p < P_TAB) return col_x(3 + p - P_FOUND);
    return col_x(p - P_TAB);
}

static int can_drop(int p);

static void draw_header(void)
{
    char b[24];
    display_rect(0, 0, G_W, HDR_H, C_HDR);
    b[0] = '\0';
    g_cat_int(g_cat(b, "Moves "), D.moves);
    display_text(4, 2, b, C_TEXT);
    if (msg[0] && (int32_t)(g_now() - msg_until) < 0) {
        g_text_c(G_W / 2, 2, msg, C_CURSOR);
    } else {
        g_text_c(G_W / 2, 2, draw_n == 3 ? "Draw 3" : "Draw 1", C_DIM);
    }
    b[0] = '\0';
    int secs = won ? (int)won_secs : started ? (int)((g_now() - start_ms) / 1000) : 0;
    g_cat_time(b, secs);
    g_text_r(G_W - 4, 2, b, C_TEXT);
}

static void draw_table(void)
{
    display_clear(C_TABLE);
    draw_header();

    /* Stock */
    int x = col_x(0);
    if (D.nstock) {
        draw_back(x, top_y);
        if (D.nstock > 1) display_rounded_rect(x + 1, top_y - 1, cw, ch, 3, C_EDGE);
    } else {
        draw_slot(x, top_y);
        if (D.nwaste) {
            display_circle(x + cw / 2, top_y + ch / 2, cw / 4, C_SLOT);
            display_circle(x + cw / 2, top_y + ch / 2, cw / 4 - 1, C_SLOT);
        }
    }

    /* Waste: the top one (draw 1) or three (draw 3) cards, fanned */
    x = col_x(1);
    if (!D.nwaste) {
        draw_slot(x, top_y);
    } else {
        int show = draw_n == 3 ? (D.nwaste < 3 ? D.nwaste : 3) : 1;
        for (int i = 0; i < show; i++) {
            draw_card(x + i * 9, top_y, D.waste[D.nwaste - show + i], i == show - 1);
        }
    }

    /* Foundations */
    for (int s = 0; s < 4; s++) {
        x = col_x(3 + s);
        if (D.found[s]) {
            draw_card(x, top_y, MAKE(D.found[s], s) | FACEUP, 1);
        } else {
            draw_slot(x, top_y);
            display_bitmap_transparent(x + cw / 2 - 7, top_y + ch / 2 - 7, 14, 14, glyph_dim[s],
                                       sizeof(glyph_dim[s]), KEY);
        }
    }

    /* Tableau */
    for (int c = 0; c < 7; c++) {
        int fd, fu;
        x = col_x(c);
        col_steps(c, 0, &fd, &fu);
        if (!D.ntab[c]) draw_slot(x, tab_y);
        for (int i = 0; i < D.ntab[c]; i++) {
            draw_card(x, card_y(c, i, fd, fu), D.tab[c][i], i == D.ntab[c] - 1);
        }
    }

    /* Cursor: the selection, or the hand floating over the target */
    x = pile_x(cur);
    if (nhand) {
        int y, step = 14;
        if (cur >= P_TAB) {
            int c = cur - P_TAB, fd, fu;
            col_steps(c, nhand, &fd, &fu);
            step = fu;
            y = D.ntab[c] ? card_y(c, D.ntab[c] - 1, fd, fu) + fu : tab_y;
        } else {
            y = top_y + 8;
        }
        y += 4;
        int hx = x + 4, hh = ch + (nhand - 1) * step;
        uint16_t ring = can_drop(cur) ? C_OK : C_CURSOR;
        for (int i = 0; i < nhand; i++) draw_card(hx, y + i * step, hand[i], i == nhand - 1);
        display_rounded_rect(hx - 1, y - 1, cw + 2, hh + 2, 4, ring);
        display_rounded_rect(hx - 2, y - 2, cw + 4, hh + 4, 5, ring);
    } else {
        int y = top_y, h = ch;
        if (cur >= P_TAB) {
            int c = cur - P_TAB, fd, fu;
            col_steps(c, 0, &fd, &fu);
            if (D.ntab[c]) {
                int first = D.ntab[c] - depth;
                y = card_y(c, first, fd, fu);
                h = card_y(c, D.ntab[c] - 1, fd, fu) + ch - y;
            } else {
                y = tab_y;
            }
        } else if (cur == P_WASTE && draw_n == 3 && D.nwaste) {
            int show = D.nwaste < 3 ? D.nwaste : 3;
            x += (show - 1) * 9;
        }
        display_rounded_rect(x - 1, y - 1, cw + 2, h + 2, 4, C_CURSOR);
        display_rounded_rect(x - 2, y - 2, cw + 4, h + 4, 5, C_CURSOR);
    }
}

static void say(const char *s)
{
    msg[0] = '\0';
    g_cat(msg, s);
    msg_until = g_now() + 1500;
}

/* ── Rules & moves ──────────────────────────────────────────────────── */

static void push_undo(void)
{
    undo_buf[undo_head] = D;
    undo_head = (undo_head + 1) % UNDO_MAX;
    if (undo_n < UNDO_MAX) undo_n++;
}

static int pop_undo(void)
{
    if (!undo_n) return 0;
    undo_head = (undo_head + UNDO_MAX - 1) % UNDO_MAX;
    D = undo_buf[undo_head];
    undo_n--;
    return 1;
}

static int fits_tab(card_t bottom, int col)
{
    if (!D.ntab[col]) return RANK(bottom) == 13;
    card_t top = D.tab[col][D.ntab[col] - 1];
    return IS_UP(top) && RANK(top) == RANK(bottom) + 1 && IS_RED(top) != IS_RED(bottom);
}

static int fits_found(card_t c, int s) { return SUIT(c) == s && D.found[s] == RANK(c) - 1; }

/* Would the hand go on pile @p? */
static int can_drop(int p)
{
    if (!nhand || p == hand_src) return 0;
    if (p >= P_TAB) return fits_tab(hand[0], p - P_TAB);
    if (p >= P_FOUND) return nhand == 1 && fits_found(hand[0], p - P_FOUND);
    return 0;
}

static void clamp_depth(void)
{
    if (cur >= P_TAB) {
        int run = faceup_run(cur - P_TAB);
        if (depth > run) depth = run;
    }
    if (depth < 1) depth = 1;
}

static void first_move(void)
{
    if (!started) {
        started = 1;
        start_ms = g_now();
    }
    if (!counted) {
        counted = 1;
        st[mode()].played++;
        char k[4];
        stat_key(k, 'p', mode());
        g_save_int(k, st[mode()].played);
    }
}

/* Lift cards from @p into the hand.  Returns 0 if there was nothing. */
static int pick_up(int p, int n)
{
    push_undo();
    nhand = 0;
    if (p == P_WASTE && D.nwaste) {
        hand[nhand++] = D.waste[--D.nwaste];
    } else if (p >= P_FOUND && p < P_TAB && D.found[p - P_FOUND]) {
        int s = p - P_FOUND;
        hand[nhand++] = MAKE(D.found[s], s) | FACEUP;
        D.found[s]--;
    } else if (p >= P_TAB) {
        int c = p - P_TAB, run = faceup_run(c);
        if (n > run) n = run;
        for (int i = D.ntab[c] - n; i < D.ntab[c]; i++) hand[nhand++] = D.tab[c][i];
        D.ntab[c] -= (uint8_t)nhand;
    }
    if (!nhand) {
        pop_undo();
        return 0;
    }
    hand_src = p;
    return 1;
}

static void cancel_hand(void)
{
    if (nhand) {
        pop_undo();
        nhand = 0;
    }
}

static void flip_tops(void)
{
    for (int c = 0; c < 7; c++) {
        if (D.ntab[c] && !IS_UP(D.tab[c][D.ntab[c] - 1])) D.tab[c][D.ntab[c] - 1] |= FACEUP;
    }
}

/* Put the hand on @p.  Returns 1 if the move was made. */
static int put_down(int p)
{
    if (!can_drop(p)) return 0;
    if (p >= P_TAB) {
        int c = p - P_TAB;
        for (int i = 0; i < nhand; i++) D.tab[c][D.ntab[c]++] = hand[i];
    } else {
        D.found[p - P_FOUND]++;
    }
    nhand = 0;
    D.moves++;
    flip_tops();
    first_move();
    return 1;
}

static void deal_stock(void)
{
    if (!D.nstock && !D.nwaste) {
        say("Stock is empty");
        return;
    }
    push_undo();
    if (!D.nstock) {
        while (D.nwaste) D.stock[D.nstock++] = D.waste[--D.nwaste] & (card_t)~FACEUP;
    } else {
        for (int i = 0; i < draw_n && D.nstock; i++) D.waste[D.nwaste++] = D.stock[--D.nstock] | FACEUP;
    }
    D.moves++;
    first_move();
}

/* Best destination for the hand: a foundation, then a non-empty column,
 * then an empty one (but never shuffle a whole column to another gap). */
static int best_target(void)
{
    if (nhand == 1 && (hand_src < P_FOUND || hand_src >= P_TAB)) {
        if (can_drop(P_FOUND + SUIT(hand[0]))) return P_FOUND + SUIT(hand[0]);
    }
    int from = hand_src >= P_TAB ? hand_src - P_TAB : -1;
    int whole_col = from >= 0 && D.ntab[from] == 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 1; i <= 7; i++) {
            int c = (from + i + 7) % 7;
            if (from < 0) c = i - 1;
            if ((D.ntab[c] == 0) != pass) continue;
            if (pass && whole_col) continue;
            if (can_drop(P_TAB + c)) return P_TAB + c;
        }
    }
    return -1;
}

static void quick_move(void)
{
    if (cur == P_STOCK && !nhand) {
        deal_stock();
        return;
    }
    if (!nhand && !pick_up(cur, cur >= P_TAB ? depth : 1)) return;
    int t = best_target();
    if (t < 0) {
        cancel_hand();
        say("No move for that");
        return;
    }
    put_down(t);
}

static int all_home(void) { return D.found[0] + D.found[1] + D.found[2] + D.found[3] == 52; }

static int can_autofinish(void)
{
    if (D.nstock || D.nwaste || nhand) return 0;
    for (int c = 0; c < 7; c++) {
        for (int i = 0; i < D.ntab[c]; i++) {
            if (!IS_UP(D.tab[c][i])) return 0;
        }
    }
    return !all_home();
}

/* With every card face up, the lowest card left is always on top of its
 * column and its foundation is waiting for it, so this always finishes. */
static void autofinish(void)
{
    while (!all_home()) {
        int best = -1, best_r = 99;
        for (int c = 0; c < 7; c++) {
            if (!D.ntab[c]) continue;
            card_t t = D.tab[c][D.ntab[c] - 1];
            if (fits_found(t, SUIT(t)) && RANK(t) < best_r) {
                best = c;
                best_r = RANK(t);
            }
        }
        if (best < 0) return;
        card_t t = D.tab[best][--D.ntab[best]];
        D.found[SUIT(t)]++;
        D.moves++;
        cur = P_FOUND + SUIT(t);
        draw_table();
        display_flush();
        delay(40000);
    }
}

/* ── Game flow ──────────────────────────────────────────────────────── */

static void new_deal(int replay)
{
    if (!replay) {
        card_t deck[52];
        int k = 0;
        for (int s = 0; s < 4; s++)
            for (int r = 1; r <= 13; r++) deck[k++] = MAKE(r, s);
        for (int i = 51; i > 0; i--) {
            int j = (int)g_rand_n((uint32_t)i + 1);
            card_t t = deck[i];
            deck[i] = deck[j];
            deck[j] = t;
        }
        k = 0;
        for (int c = 0; c < 7; c++) {
            D.ntab[c] = (uint8_t)(c + 1);
            for (int i = 0; i <= c; i++) D.tab[c][i] = deck[k++];
            D.tab[c][c] |= FACEUP;
        }
        D.nstock = 0;
        while (k < 52) D.stock[D.nstock++] = deck[k++];
        D.nwaste = 0;
        for (int s = 0; s < 4; s++) D.found[s] = 0;
        D.moves = 0;
        deal0 = D;
    } else {
        D = deal0;
    }
    undo_n = 0;
    nhand = 0;
    won = 0;
    started = 0;
    counted = 0;
    cur = P_TAB;
    depth = 1;
    msg[0] = '\0';
}

static void celebrate(void)
{
    static const uint16_t CONFETTI[] = {0xFFE0, 0x07FF, 0xF81F, 0x07E0, 0xFD20, 0xFFFF, 0xE0E4};
    char b[40];
    stats_t *s = &st[mode()];

    won = 1;
    won_secs = (g_now() - start_ms) / 1000;
    s->won++;
    s->streak++;
    if (s->streak > s->best_streak) s->best_streak = s->streak;
    int record = !s->best_time || (int)won_secs < s->best_time;
    if (record) s->best_time = (int)won_secs;
    stats_save(mode());

    draw_table();
    for (int f = 0; f < 10; f++) {
        for (int i = 0; i < 24; i++) {
            display_rect((int)g_rand_n((uint32_t)G_W), (int)g_rand_n((uint32_t)G_H), 3 + (int)g_rand_n(3),
                         2 + (int)g_rand_n(3), CONFETTI[g_rand_n(7)]);
        }
        display_flush();
        delay(60000);
    }

    int w = 210, h = 120, x = (G_W - w) / 2, y = (G_H - h) / 2;
    g_panel(x, y, w, h);
    g_text_large_c(G_W / 2, y + 10, "SOLVED!", 0xFE87);
    b[0] = '\0';
    g_cat_int(g_cat(g_cat_time(g_cat(b, "Time "), (int)won_secs), "   Moves "), D.moves);
    g_text_c(G_W / 2, y + 40, b, C_TEXT);
    b[0] = '\0';
    if (record) {
        g_cat(b, "New best time!");
    } else {
        g_cat_time(g_cat(b, "Best "), s->best_time);
    }
    g_text_c(G_W / 2, y + 56, b, record ? C_OK : C_DIM);
    b[0] = '\0';
    g_cat_int(g_cat(g_cat_int(g_cat(g_cat_int(g_cat(b, "Won "), s->won), " of "), s->played), "  streak "),
              s->streak);
    g_text_c(G_W / 2, y + 72, b, C_TEXT);
    g_text_c(G_W / 2, y + h - 18, "A: new game", C_DIM);
    display_flush();
    g_flush_input();
    while (g_wait_key() != K_A) {
    }
}

static const char HELP[] =
    "Build the foundations up by suit,\n"
    "Ace to King. On the columns, go down\n"
    "in alternating colours. Only a King\n"
    "fills an empty column.\n"
    "\n"
    "A  pick up / put down / deal\n"
    "X  quick move to the best spot\n"
    "B  put back, or deal\n"
    "Y  undo\n"
    "UP/DOWN on a column: pick more or\n"
    "fewer face-up cards.";

static void show_stats(void)
{
    char b[200];
    b[0] = '\0';
    for (int m = 0; m < 2; m++) {
        stats_t *s = &st[m];
        g_cat(b, m ? "\nDraw 3\n" : "Draw 1\n");
        g_cat_int(g_cat(g_cat_int(g_cat(b, "  won "), s->won), " of "), s->played);
        if (s->played) g_cat(g_cat_int(g_cat(b, " ("), s->won * 100 / s->played), "%)");
        g_cat(b, "\n  best time ");
        if (s->best_time) g_cat_time(b, s->best_time); else g_cat(b, "-");
        g_cat_int(g_cat(g_cat_int(g_cat(b, "\n  streak "), s->streak), ", best "), s->best_streak);
        g_cat(b, "\n");
    }
    g_message("STATISTICS", b);
}

enum { R_NONE, R_QUIT };

static int menu(void)
{
    static char draw_item[24];
    const char *items[] = {"Resume", "New game", draw_item, "Restart this deal", "Statistics", "How to play",
                           "Quit"};
    int sel = 0;
    uint32_t t0 = g_now();

    for (;;) {
        draw_item[0] = '\0';
        g_cat(draw_item, draw_n == 3 ? "Deal: 3 cards  < >" : "Deal: 1 card  < >");
        draw_table();
        int r = g_menu("SOLITAIRE", items, 7, sel, 1);
        int idx = r & 0xFF;
        if (r >= 0 && idx == 2) {
            /* takes effect on the next deal */
            draw_n = draw_n == 3 ? 1 : 3;
            g_save_int("draw", draw_n);
            sel = 2;
            continue;
        }
        if (r >= 0 && (r & (G_MENU_LEFT | G_MENU_RIGHT))) {
            sel = idx;
            continue;
        }
        if (started && !won) start_ms += g_now() - t0;
        switch (r) {
        case 1:
            abandon_deal();
            new_deal(0);
            return R_NONE;
        case 3:
            abandon_deal();
            new_deal(1);
            return R_NONE;
        case 4:
            draw_table();
            show_stats();
            sel = 4;
            t0 = g_now();
            continue;
        case 5:
            draw_table();
            g_message("HOW TO PLAY", HELP);
            sel = 5;
            t0 = g_now();
            continue;
        case 6:
            abandon_deal();
            return R_QUIT;
        default:
            return R_NONE;
        }
    }
}

/* Top-row order for LEFT/RIGHT, and the column each pile sits over. */
static const int8_t TOP_ORDER[6] = {P_STOCK, P_WASTE, P_FOUND, P_FOUND + 1, P_FOUND + 2, P_FOUND + 3};

static int screen_col(int p)
{
    if (p == P_STOCK) return 0;
    if (p == P_WASTE) return 1;
    if (p < P_TAB) return 3 + p - P_FOUND;
    return p - P_TAB;
}

static void move_cursor(uint32_t k)
{
    if (cur < P_TAB) {
        int i = 0;
        while (TOP_ORDER[i] != cur) i++;
        if (k == K_LEFT) cur = TOP_ORDER[(i + 5) % 6];
        else if (k == K_RIGHT) cur = TOP_ORDER[(i + 1) % 6];
        else if (k == K_DOWN) {
            cur = P_TAB + screen_col(cur);
            depth = 1;
        }
        return;
    }
    int c = cur - P_TAB;
    if (k == K_LEFT) {
        cur = P_TAB + (c + 6) % 7;
        depth = 1;
    } else if (k == K_RIGHT) {
        cur = P_TAB + (c + 1) % 7;
        depth = 1;
    } else if (k == K_UP) {
        if (!nhand && depth < faceup_run(c)) {
            depth++;
        } else {
            cur = c == 0 ? P_STOCK : c <= 2 ? P_WASTE : P_FOUND + c - 3;
        }
    } else if (k == K_DOWN) {
        if (depth > 1) depth--;
    }
}

static void press_a(void)
{
    if (nhand) {
        if (cur == hand_src) {
            cancel_hand();
        } else if (!put_down(cur)) {
            say("Can't go there");
        }
        return;
    }
    if (cur == P_STOCK) {
        deal_stock();
        return;
    }
    if (!pick_up(cur, depth)) say("Nothing to pick up");
}

static int play(void)
{
    uint32_t last_sec = 0xFFFFFFFF;
    int dirty = 1;

    g_flush_input();
    for (;;) {
        if (dirty) {
            clamp_depth();
            draw_table();
            display_flush();
            dirty = 0;
        }
        if (all_home() && !won) {
            celebrate();
            new_deal(0);
            dirty = 1;
            continue;
        }
        if (can_autofinish()) {
            autofinish();
            dirty = 1;
            continue;
        }

        uint32_t k = g_key();
        if (!k) {
            /* Tick the clock and expire messages once a second. */
            uint32_t sec = g_now() / 1000;
            if (sec != last_sec && (started || msg[0])) {
                last_sec = sec;
                dirty = 1;
                if (msg[0] && (int32_t)(g_now() - msg_until) >= 0) msg[0] = '\0';
            }
            delay(15000);
            continue;
        }
        switch (k) {
        case K_UP:
        case K_DOWN:
        case K_LEFT:
        case K_RIGHT:
            move_cursor(k);
            break;
        case K_A:
            press_a();
            break;
        case K_X:
            quick_move();
            break;
        case K_B:
            if (nhand) cancel_hand();
            else deal_stock();
            break;
        case K_Y:
            if (nhand) cancel_hand();
            else if (!pop_undo()) say("Nothing to undo");
            break;
        case K_HOME:
            cancel_hand();
            if (menu() == R_QUIT) return R_QUIT;
            break;
        default:
            break;
        }
        dirty = 1;
    }
}

int main(void)
{
    g_init();
    g_seed_random();
    g_theme.bg = 0x0904;
    g_theme.border = 0x4E19;
    g_theme.sel_bg = 0x1B8A;
    layout();
    build_glyphs();
    stats_load();
    new_deal(0);
    play();
    display_clear(0);
    display_flush();
    return 0;
}
