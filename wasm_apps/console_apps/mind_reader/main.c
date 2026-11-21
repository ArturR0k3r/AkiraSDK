/**
 * @file main.c
 * @brief Mind Reader — rock-paper-scissors against a machine that learns you.
 *
 * People are bad at being random.  In 1953 Claude Shannon and David
 * Hagelbarger built "mind-reading machines" that beat humans at guessing
 * games by spotting their habits.  This is that idea for rock-paper-scissors.
 *
 * Six experts each model you differently: how often you play each move, what
 * you play after your last move, after your last two, after winning / losing,
 * the longest earlier stretch of the game that matches the last few rounds,
 * and whether you counter the machine's last move.  Every round each expert's
 * weight moves with how well it predicted you (an exponentially-forgetting
 * Bayesian mixture), and the machine plays the move with the best expected
 * score against the blended prediction.  It locks its move in before you
 * press, so it never sees your choice.  What it learned is kept between
 * sessions in the app's storage sandbox.
 *
 * Controls:
 *   LEFT / UP / RIGHT   rock / paper / scissors
 *   X                   show / hide the machine's reasoning
 *   B / HOME            menu
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"
#include "../../common/akira_ml.h"

enum { ROCK, PAPER, SCISSORS };
enum { YOU_WIN, TIE, YOU_LOSE }; /* outcome, from your side */
#define NE    6                  /* experts */
#define HIST  256                /* rounds kept for pattern matching */
#define DECAY 0.97f              /* count forgetting per round */
#define WFORGET 0.93f            /* expert-score forgetting per round */

static const char *const MOVE_NAME[3] = {"ROCK", "PAPER", "SCISSORS"};
static const char *const EXPERT_NAME[NE] = {"how often", "after 1 move", "after 2 moves", "win/lose",
                                            "replaying", "countering me"};

static int beats(int m) { return (m + 1) % 3; } /* the move that beats m */

static int outcome(int you, int ai)
{
    if (you == ai) return TIE;
    return beats(ai) == you ? YOU_WIN : YOU_LOSE;
}

/* ── The model (persisted) ──────────────────────────────────────────── */
#define BRAIN_MAGIC 0x4D524431u /* "MRD1" */

typedef struct {
    uint32_t magic;
    float c0[3];          /* your move frequencies           */
    float c1[3][3];       /* after your last move             */
    float c2[3][3][3];    /* after your last two moves        */
    float c3[3][3][3];    /* after (your move, outcome)       */
    float score[NE];      /* forgetting log-likelihood sums   */
    uint8_t hy[HIST];     /* your moves, ring                 */
    uint8_t ha[HIST];     /* machine's moves, ring            */
    uint32_t n;           /* rounds in history (all time)     */
    uint32_t wins, ties, losses; /* yours, all time */
} brain_t;

static brain_t B;

/* This session */
static int s_win, s_tie, s_loss;
static uint8_t recent[20]; /* last outcomes, newest last */
static int nrecent;
static int last_you = -1, last_ai = -1, last_out = -1;
static float last_pred[3];   /* what it expected last round */
static float last_w[NE];
static int show_brain = 1;

static void brain_reset(void)
{
    uint8_t *p = (uint8_t *)&B;
    for (unsigned i = 0; i < sizeof(B); i++) p[i] = 0;
    B.magic = BRAIN_MAGIC;
}

static void brain_load(void)
{
    int fd = storage_open("brain.bin", STORAGE_O_READ);
    if (fd >= 0) {
        int n = storage_read(fd, &B, sizeof(B));
        storage_close(fd);
        if (n == (int)sizeof(B) && B.magic == BRAIN_MAGIC) return;
    }
    brain_reset();
}

static void brain_save(void)
{
    int fd = storage_open("brain.bin", STORAGE_O_WRITE);
    if (fd >= 0) {
        storage_write(fd, &B, sizeof(B));
        storage_close(fd);
    }
}

static int hist_you(int back) { return B.hy[(B.n - back) % HIST]; } /* back=1: last round */
static int hist_ai(int back) { return B.ha[(B.n - back) % HIST]; }

static void from_counts(const float *c, float *p)
{
    float s = c[0] + c[1] + c[2] + 0.9f;
    for (int m = 0; m < 3; m++) p[m] = (c[m] + 0.3f) / s;
}

/* Longest earlier match of the last k rounds (k <= 6); predicts what came next. */
static void expert_replay(float *p)
{
    int avail = B.n < HIST ? (int)B.n : HIST;
    float c[3] = {0, 0, 0};
    for (int k = 6; k >= 1 && c[0] + c[1] + c[2] == 0.0f; k--) {
        if (avail < k + 1) continue;
        for (int start = 1 + 1; start + k - 1 <= avail - 1; start++) {
            /* window ends 'start' rounds back; compare with the last k rounds */
            int ok = 1;
            for (int j = 0; j < k && ok; j++) {
                ok = hist_you(start + j) == hist_you(1 + j) && hist_ai(start + j) == hist_ai(1 + j);
            }
            if (ok) c[hist_you(start - 1)] += 1.0f + (float)k;
        }
    }
    float s = c[0] + c[1] + c[2];
    if (s == 0.0f) {
        p[0] = p[1] = p[2] = 1.0f / 3.0f;
        return;
    }
    for (int m = 0; m < 3; m++) p[m] = (c[m] + 0.2f) / (s + 0.6f);
}

static void experts(float out[NE][3])
{
    from_counts(B.c0, out[0]);
    if (B.n >= 1) from_counts(B.c1[hist_you(1)], out[1]);
    else from_counts(B.c0, out[1]);
    if (B.n >= 2) from_counts(B.c2[hist_you(2)][hist_you(1)], out[2]);
    else from_counts(B.c0, out[2]);
    if (B.n >= 1) from_counts(B.c3[hist_you(1)][outcome(hist_you(1), hist_ai(1))], out[3]);
    else from_counts(B.c0, out[3]);
    expert_replay(out[4]);
    for (int m = 0; m < 3; m++) out[5][m] = 0.2f;
    if (B.n >= 1) out[5][beats(hist_ai(1))] = 0.6f;
    else out[5][0] = out[5][1] = out[5][2] = 1.0f / 3.0f;
}

static void weights(float *w)
{
    float mx = B.score[0], s = 0.0f;
    for (int i = 1; i < NE; i++) {
        if (B.score[i] > mx) mx = B.score[i];
    }
    for (int i = 0; i < NE; i++) {
        w[i] = ml_expf((B.score[i] - mx) * 1.5f);
        s += w[i];
    }
    for (int i = 0; i < NE; i++) w[i] /= s;
}

/* Predict you, then pick the machine's move.  Fills pred/ex/w. */
static int think(float pred[3], float ex[NE][3], float w[NE])
{
    experts(ex);
    weights(w);
    for (int m = 0; m < 3; m++) {
        pred[m] = 0.0f;
        for (int i = 0; i < NE; i++) pred[m] += w[i] * ex[i][m];
    }
    /* expected score of each reply: +1 if it beats you, -1 if you beat it */
    int best = (int)g_rand_n(3);
    float bv = -9.0f;
    for (int a = 0; a < 3; a++) {
        float v = pred[(a + 2) % 3] - pred[beats(a)];
        v += (float)g_rand_n(1000) * 1e-6f; /* break ties at random */
        if (v > bv) {
            bv = v;
            best = a;
        }
    }
    return best;
}

static void learn(int you, int ai, float ex[NE][3])
{
    for (int i = 0; i < NE; i++) B.score[i] = B.score[i] * WFORGET + ml_logf(ex[i][you] * 3.0f);

    for (int m = 0; m < 3; m++) B.c0[m] *= DECAY;
    B.c0[you] += 1.0f;
    if (B.n >= 1) {
        float *c = B.c1[hist_you(1)];
        for (int m = 0; m < 3; m++) c[m] *= DECAY;
        c[you] += 1.0f;
        c = B.c3[hist_you(1)][outcome(hist_you(1), hist_ai(1))];
        for (int m = 0; m < 3; m++) c[m] *= DECAY;
        c[you] += 1.0f;
    }
    if (B.n >= 2) {
        float *c = B.c2[hist_you(2)][hist_you(1)];
        for (int m = 0; m < 3; m++) c[m] *= DECAY;
        c[you] += 1.0f;
    }
    B.hy[B.n % HIST] = (uint8_t)you;
    B.ha[B.n % HIST] = (uint8_t)ai;
    B.n++;
}

/* ── Drawing ────────────────────────────────────────────────────────── */
#define C_BG     0x0841
#define C_HDR    0x18E4
#define C_TEXT   0xFFFF
#define C_DIM    0x8CB5
#define C_YOU    0x07FF
#define C_AI     0xF9A6
#define C_TIE    0xBDF7
#define C_GOLD   0xFE87
#define C_PANEL  0x10A3
#define C_ROCK   0x9CD3
#define C_ROCK_D 0x5AEB
#define C_PAPER  0xFFDE

static void icon(int cx, int cy, int s, int m, int dim)
{
    /* s = half size */
    if (m == ROCK) {
        display_circle_fill(cx, cy + s / 6, s * 4 / 5, dim ? C_ROCK_D : C_ROCK);
        display_circle_fill(cx - s / 3, cy - s / 3, s / 3, dim ? C_ROCK_D : C_ROCK);
        display_circle_fill(cx + s / 4, cy - s / 4, s / 3, dim ? C_ROCK_D : C_ROCK);
        display_circle(cx - s / 4, cy + s / 4, s / 6, C_ROCK_D);
        display_circle(cx + s / 3, cy, s / 8, C_ROCK_D);
    } else if (m == PAPER) {
        int w = s * 3 / 2, h = s * 2;
        display_rect(cx - w / 2, cy - h / 2, w, h, dim ? C_ROCK_D : C_PAPER);
        for (int k = 1; k <= 4; k++) display_hline(cx - w / 2 + 3, cy - h / 2 + k * h / 5, w - 6, C_ROCK);
        display_triangle_fill(cx + w / 2 - s / 3, cy - h / 2, cx + w / 2, cy - h / 2, cx + w / 2, cy - h / 2 + s / 3, C_BG);
    } else {
        uint16_t c = dim ? C_ROCK_D : 0xC618;
        for (int d = -1; d <= 1; d++) {
            display_line(cx - s / 3 + d, cy + s / 3, cx + s / 2 + d, cy - s, c);
            display_line(cx + s / 3 + d, cy + s / 3, cx - s / 2 + d, cy - s, c);
        }
        display_circle(cx - s / 3, cy + s * 2 / 3, s / 3, 0xF800);
        display_circle(cx - s / 3, cy + s * 2 / 3, s / 3 - 1, 0xF800);
        display_circle(cx + s / 3, cy + s * 2 / 3, s / 3, 0xF800);
        display_circle(cx + s / 3, cy + s * 2 / 3, s / 3 - 1, 0xF800);
    }
}

static void bar(int x, int y, int w, int h, float v, uint16_t col)
{
    display_rect(x, y, w, h, C_HDR);
    int f = (int)(v * (float)w + 0.5f);
    if (f > w) f = w;
    if (f > 0) display_rect(x, y, f, h, col);
}

static int pct(float v) { return (int)(v * 100.0f + 0.5f); }

static void draw(float conf)
{
    char b[48];
    int cx = G_W / 2;

    display_clear(C_BG);
    display_rect(0, 0, G_W, 14, C_HDR);
    display_text(4, 2, "MIND READER", C_GOLD);
    b[0] = '\0';
    g_cat_int(g_cat(b, "round "), s_win + s_tie + s_loss + 1);
    g_text_r(G_W - 4, 2, b, C_DIM);

    /* Last round, or the welcome line */
    int ty = 20;
    display_text(G_W / 4 - 12, ty, "YOU", C_YOU);
    display_text(3 * G_W / 4 - 8, ty, "ME", C_AI);
    if (last_you >= 0) {
        icon(G_W / 4, ty + 46, 22, last_you, last_out == YOU_LOSE);
        icon(3 * G_W / 4, ty + 46, 22, last_ai, last_out == YOU_WIN);
        const char *res = last_out == YOU_WIN ? "YOU WIN" : last_out == TIE ? "TIE" : "I WIN";
        g_text_large_c(cx, ty + 34, res, last_out == YOU_WIN ? C_YOU : last_out == TIE ? C_TIE : C_AI);
    } else {
        g_text_c(cx, ty + 30, B.n ? "Welcome back." : "Try to be random.", C_TEXT);
        b[0] = '\0';
        if (B.n) g_cat(g_cat_int(g_cat(b, "I remember "), (int)B.n), " of your throws.");
        else g_cat(b, "I'll learn how you play.");
        g_text_c(cx, ty + 44, b, C_DIM);
    }

    /* Score + last 20 outcomes */
    int sy = ty + 82;
    b[0] = '\0';
    g_cat_int(g_cat(g_cat_int(g_cat(g_cat_int(g_cat(b, "you "), s_win), "   ties "), s_tie), "   me "), s_loss);
    g_text_c(cx, sy, b, C_TEXT);
    int sq = 9, x0 = cx - (20 * (sq + 2)) / 2;
    for (int i = 0; i < 20; i++) {
        uint16_t c = C_HDR;
        if (i < nrecent) c = recent[i] == YOU_WIN ? C_YOU : recent[i] == TIE ? C_TIE : C_AI;
        display_rect(x0 + i * (sq + 2), sy + 14, sq, sq, c);
    }

    /* Reasoning panel */
    int py = sy + 30, ph = G_H - 34 - py;
    display_rounded_rect_fill(4, py, G_W - 8, ph, 4, C_PANEL);
    if (show_brain && last_you >= 0) {
        int lx = 10, rx = G_W / 2 + 4, bw = G_W / 2 - 74;
        display_text(lx, py + 4, "I expected you:", C_DIM);
        for (int m = 0; m < 3; m++) {
            int y = py + 18 + m * 12;
            display_text(lx, y, MOVE_NAME[m], m == last_you ? C_YOU : C_TEXT);
            bar(lx + 66, y + 1, bw, 8, last_pred[m], m == last_you ? C_YOU : C_DIM);
        }
        display_text(rx, py + 4, "Trusting:", C_DIM);
        /* the four heaviest experts, heaviest first */
        int order[NE], shown;
        for (int i = 0; i < NE; i++) order[i] = i;
        for (int i = 1; i < NE; i++) {
            for (int j = i; j > 0 && last_w[order[j]] > last_w[order[j - 1]]; j--) {
                int t = order[j];
                order[j] = order[j - 1];
                order[j - 1] = t;
            }
        }
        for (shown = 0; shown < 4; shown++) {
            int i = order[shown], y = py + 18 + shown * 12;
            display_text(rx, y, EXPERT_NAME[i], shown == 0 ? C_GOLD : C_TEXT);
            bar(G_W - 46, y + 1, 36, 8, last_w[i], shown == 0 ? C_GOLD : C_DIM);
        }
    } else if (!show_brain) {
        g_text_c(cx, py + ph / 2 - 5, "X: show my reasoning", C_DIM);
    } else {
        g_text_c(cx, py + ph / 2 - 10, "My move is locked in before you", C_DIM);
        g_text_c(cx, py + ph / 2 + 2, "press - I never see your choice.", C_DIM);
    }

    /* Prompt + choices */
    int by = G_H - 30;
    b[0] = '\0';
    g_cat(g_cat_int(g_cat(b, "Locked in. I read you "), pct(conf)), "%");
    g_text_c(cx, by - 2, b, conf > 0.5f ? C_AI : C_DIM);
    display_text(8, by + 14, "< ROCK", C_TEXT);
    g_text_c(cx, by + 14, "^ PAPER", C_TEXT);
    g_text_r(G_W - 8, by + 14, "SCISSORS >", C_TEXT);
    display_flush();
}

/* ── Flow ───────────────────────────────────────────────────────────── */

static void show_stats(void)
{
    char b[200] = "";
    uint32_t n = B.wins + B.ties + B.losses;
    g_cat_int(g_cat(b, "All time: "), (int)n);
    g_cat(b, " rounds\n");
    if (n) {
        g_cat(g_cat_int(g_cat(b, "  you won  "), (int)(B.wins * 100 / n)), "%\n");
        g_cat(g_cat_int(g_cat(b, "  ties     "), (int)(B.ties * 100 / n)), "%\n");
        g_cat(g_cat_int(g_cat(b, "  I won    "), (int)(B.losses * 100 / n)), "%\n");
    }
    g_cat(b, "\nA truly random player wins 33%\nand so do I. Above that, you\nhave habits I can use.");
    g_message("STATISTICS", b);
}

static const char ABOUT[] =
    "People are bad at being random.\n"
    "In 1953 Claude Shannon built a\n"
    "'mind-reading machine' that beat\n"
    "people at guessing games.\n"
    "\n"
    "Six experts watch your habits;\n"
    "each round I trust the ones that\n"
    "predicted you best, then play\n"
    "what beats my blended guess.\n"
    "My move is fixed before you press.";

enum { R_OK, R_QUIT };

static int menu(void)
{
    const char *items[] = {"Resume", "Statistics", "How it works", "Forget me", "Quit"};
    for (;;) {
        int r = g_menu("MIND READER", items, 5, 0, 0);
        switch (r) {
        case 1:
            show_stats();
            break;
        case 2:
            g_message("HOW IT WORKS", ABOUT);
            break;
        case 3:
            brain_reset();
            brain_save();
            s_win = s_tie = s_loss = nrecent = 0;
            last_you = -1;
            return R_OK;
        case 4:
            return R_QUIT;
        default:
            return R_OK;
        }
    }
}

int main(void)
{
    g_init();
    g_seed_random();
    brain_load();

    float pred[3], ex[NE][3], w[NE];
    for (;;) {
        int ai = think(pred, ex, w); /* committed before reading your move */
        float conf = pred[0];
        if (pred[1] > conf) conf = pred[1];
        if (pred[2] > conf) conf = pred[2];
        draw(conf);

        int you = -1;
        while (you < 0) {
            uint32_t k = g_wait_key();
            if (k == K_LEFT) you = ROCK;
            else if (k == K_UP) you = PAPER;
            else if (k == K_RIGHT) you = SCISSORS;
            else if (k == K_X) {
                show_brain = !show_brain;
                draw(conf);
            } else if (k == K_B || k == K_HOME) {
                if (menu() == R_QUIT) {
                    brain_save();
                    display_clear(0);
                    display_flush();
                    return 0;
                }
                break;
            }
        }
        if (you < 0) continue; /* back from the menu: think again */

        int out = outcome(you, ai);
        for (int m = 0; m < 3; m++) last_pred[m] = pred[m];
        for (int i = 0; i < NE; i++) last_w[i] = w[i];
        learn(you, ai, ex);
        last_you = you;
        last_ai = ai;
        last_out = out;
        if (out == YOU_WIN) {
            s_win++;
            B.wins++;
        } else if (out == TIE) {
            s_tie++;
            B.ties++;
        } else {
            s_loss++;
            B.losses++;
        }
        if (nrecent == 20) {
            for (int i = 1; i < 20; i++) recent[i - 1] = recent[i];
            nrecent--;
        }
        recent[nrecent++] = (uint8_t)out;
        if ((s_win + s_tie + s_loss) % 10 == 0) brain_save();
    }
}
