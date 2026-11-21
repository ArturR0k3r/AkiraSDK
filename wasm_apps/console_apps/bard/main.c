/**
 * @file main.c
 * @brief Pocket Bard — a neural language model writing Shakespeare, live,
 *        on the console.
 *
 * A character-level GRU network (one layer, see bard_model.h for its size)
 * trained on Shakespeare's plays (the public-domain "tinyshakespeare"
 * corpus) runs here, inside the WASM sandbox, one character at a time.
 * Nothing leaves the device and nothing was pre-written: every character is
 * sampled from the network's prediction of what comes next.
 *
 * Inference: the GRU's input side only ever sees one character, so its input
 * products are a per-character table.  Weights are int8 with a float scale
 * per row; the hidden state is quantized to int8 for the matrix products and
 * accumulated in int32, which keeps the interpreter to a few instructions
 * per multiply-accumulate.  bard/model/ has the training and export scripts.
 *
 * Controls:
 *   A            pause / continue
 *   LEFT/RIGHT   choose who speaks next     X  new scene with them
 *   UP/DOWN      temperature (how adventurous the sampling is)
 *   Y            show what the network is considering next
 *   B / HOME     menu
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"
#include "../../common/akira_ml.h"
#include "bard_model.h"

#define H BARD_H
#define V BARD_V

/* ── Inference ──────────────────────────────────────────────────────── */
static float h[H];
static int32_t hq[H];
static float gh[3 * H];
static float logit[V];

static void quantize_h(void)
{
    for (int k = 0; k < H; k++) {
        float v = h[k] * 127.0f;
        int q = (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
        hq[k] = q > 127 ? 127 : q < -127 ? -127 : q;
    }
}

/* int8 row . int8 vector, int32 accumulate, unrolled for the interpreter */
static int32_t dot(const int8_t *w)
{
    int32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    const int32_t *x = hq;
    for (int k = 0; k < H; k += 8) {
        a0 += w[k] * x[k] + w[k + 4] * x[k + 4];
        a1 += w[k + 1] * x[k + 1] + w[k + 5] * x[k + 5];
        a2 += w[k + 2] * x[k + 2] + w[k + 6] * x[k + 6];
        a3 += w[k + 3] * x[k + 3] + w[k + 7] * x[k + 7];
    }
    return a0 + a1 + a2 + a3;
}

/* Feed character index @p c; afterwards logit[] predicts the next one. */
static void step(int c)
{
    quantize_h();
    for (int j = 0; j < 3 * H; j++) gh[j] = (float)dot(&BARD_WHH[j * H]) * BARD_WHH_S[j] * (1.0f / 127.0f) + BARD_BHH[j];
    const int8_t *t = &BARD_TBL[c * 3 * H];
    float ts = BARD_TBL_S[c];
    for (int k = 0; k < H; k++) {
        float r = ml_sigmoidf((float)t[k] * ts + gh[k]);
        float z = ml_sigmoidf((float)t[H + k] * ts + gh[H + k]);
        float n = ml_tanhf((float)t[2 * H + k] * ts + r * gh[2 * H + k]);
        h[k] = (1.0f - z) * n + z * h[k];
    }
    quantize_h();
    for (int v = 0; v < V; v++) logit[v] = (float)dot(&BARD_WO[v * H]) * BARD_WO_S[v] * (1.0f / 127.0f) + BARD_BO[v];
}

static int char_index(char ch)
{
    for (int i = 0; i < V; i++) {
        if (BARD_VOCAB[i] == ch) return i;
    }
    return 0;
}

static void reset_state(void)
{
    for (int k = 0; k < H; k++) h[k] = 0.0f;
}

/* ── Text page ──────────────────────────────────────────────────────── */
#define COLS_MAX 50
#define LINES    48
static char page[LINES][COLS_MAX + 1];
static int top_line, cur_line, cur_col, cols;

static void page_clear(void)
{
    for (int i = 0; i < LINES; i++) page[i][0] = '\0';
    top_line = cur_line = cur_col = 0;
}

static void new_line(void)
{
    cur_line++;
    page[cur_line % LINES][0] = '\0';
    cur_col = 0;
    if (cur_line - top_line >= LINES) top_line = cur_line - LINES + 1;
}

static void put_char(char ch)
{
    if (ch == '\n') {
        new_line();
        return;
    }
    char *ln = page[cur_line % LINES];
    if (cur_col >= cols) {
        /* wrap at the last space, carrying the partial word over */
        int sp = cur_col - 1;
        while (sp > 0 && ln[sp] != ' ') sp--;
        char carry[COLS_MAX + 1];
        int nc = 0;
        if (sp > 0 && cur_col - sp - 1 < cols / 2) {
            for (int i = sp + 1; i < cur_col; i++) carry[nc++] = ln[i];
            ln[sp] = '\0';
        }
        new_line();
        ln = page[cur_line % LINES];
        for (int i = 0; i < nc; i++) ln[i] = carry[i];
        cur_col = nc;
        if (ch == ' ' && !cur_col) return;
    }
    ln[cur_col++] = ch;
    ln[cur_col] = '\0';
}

/* "NAME:" lines are speakers */
static int is_speaker(const char *s)
{
    int n = strlen(s);
    if (n < 3 || s[n - 1] != ':' || s[0] < 'A' || s[0] > 'Z') return 0;
    for (int i = 0; i < n - 1; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ')) return 0;
    }
    return 1;
}

/* ── App ────────────────────────────────────────────────────────────── */
static const char *const SPEAKERS[] = {"ROMEO", "JULIET", "KING RICHARD III", "GLOUCESTER", "QUEEN ELIZABETH",
                                       "MENENIUS", "CORIOLANUS", "First Citizen", "PETRUCHIO", "KATHARINA",
                                       "DUKE VINCENTIO", "LEONTES"};
#define NSPEAKERS 12

#define C_BG    0x0841
#define C_HDR   0x18E4
#define C_TEXT  0xEF7D
#define C_NAME  0xFE87
#define C_DIM   0x8CB5
#define C_PANEL 0x10A3
#define C_ACC   0x07FF

static int speaker;
static int temp10 = 8; /* temperature x10 */
static int paused, show_think;
static uint32_t total_chars;
static float rate; /* chars per second, smoothed */

static void draw(void)
{
    char b[40];
    display_clear(C_BG);
    display_rect(0, 0, G_W, 14, C_HDR);
    display_text(4, 2, "POCKET BARD", C_NAME);
    b[0] = '\0';
    g_cat_int(g_cat(b, "temp "), temp10 / 10);
    g_cat_int(g_cat(b, "."), temp10 % 10);
    if (paused) g_cat(b, "  paused");
    else g_cat(g_cat_int(g_cat(b, "  "), (int)(rate + 0.5f)), " ch/s");
    g_text_r(G_W - 4, 2, b, C_DIM);

    int panel_h = show_think ? 46 : 0;
    int text_y = 18, foot_y = G_H - 13;
    int nlines = (foot_y - panel_h - text_y - 2) / 12;
    int first = cur_line - nlines + 1;
    if (first < top_line) first = top_line;
    for (int i = 0; first + i <= cur_line && i < nlines; i++) {
        const char *s = page[(first + i) % LINES];
        display_text(4, text_y + i * 12, s, is_speaker(s) ? C_NAME : C_TEXT);
    }
    /* caret */
    int cy = text_y + (cur_line - first) * 12;
    if (!paused) display_rect(4 + cur_col * G_FONT_ADV, cy + 8, 6, 2, C_ACC);

    if (show_think) {
        /* the network's top candidates for the next character */
        int py = foot_y - panel_h - 2;
        display_rounded_rect_fill(2, py, G_W - 4, panel_h, 4, C_PANEL);
        display_text(8, py + 4, "next character:", C_DIM);
        float p[V];
        for (int v = 0; v < V; v++) p[v] = logit[v];
        ml_softmax(p, V, (float)temp10 / 10.0f);
        int used[V];
        for (int v = 0; v < V; v++) used[v] = 0;
        int cw = (G_W - 16) / 3, bw = cw - 70;
        for (int r = 0; r < 6; r++) {
            int best = -1;
            for (int v = 0; v < V; v++) {
                if (!used[v] && (best < 0 || p[v] > p[best])) best = v;
            }
            used[best] = 1;
            int x = 8 + (r % 3) * cw, y = py + 17 + (r / 3) * 13;
            char ch = BARD_VOCAB[best];
            const char *label = ch == ' ' ? "spc" : ch == '\n' ? "ret" : 0;
            char one[2] = {ch, '\0'};
            display_text(x, y, label ? label : one, r == 0 ? C_NAME : C_TEXT);
            display_rect(x + 28, y + 1, bw, 8, C_HDR);
            int f = (int)(p[best] * (float)bw + 0.5f);
            if (f > 0) display_rect(x + 28, y + 1, f, 8, r == 0 ? C_NAME : C_ACC);
            b[0] = '\0';
            g_cat(g_cat_int(b, (int)(p[best] * 100.0f + 0.5f)), "%");
            display_text(x + 32 + bw, y, b, C_DIM);
        }
    }

    b[0] = '\0';
    g_cat(g_cat(g_cat(b, "X: scene for < "), SPEAKERS[speaker]), " >");
    g_text_c(G_W / 2, foot_y + 1, b, C_DIM);
    display_flush();
}

static void feed_text(const char *s, int show)
{
    for (; *s; s++) {
        if (show) put_char(*s);
        step(char_index(*s));
    }
}

static void new_scene(void)
{
    char prompt[40] = "\n";
    g_cat(g_cat(prompt, SPEAKERS[speaker]), ":\n");
    reset_state();
    if (cur_col) put_char('\n');
    put_char('\n');
    feed_text(prompt + 1, 1);
}

static int sample_next(void)
{
    float p[V];
    for (int v = 0; v < V; v++) p[v] = logit[v];
    ml_softmax(p, V, (float)temp10 / 10.0f);
    return ml_sample(p, V, (float)(g_rand() >> 8) * (1.0f / 16777216.0f));
}

static void about(void)
{
    char b[300] = "";
    int params = V * 3 * H + 3 * H * H + V * H;
    g_cat_int(g_cat(b, "A GRU neural network with "), H);
    g_cat(b, "\nhidden units, ");
    g_cat_int(b, params / 1000);
    g_cat(b, "K int8 weights, trained on\nShakespeare's plays (public domain,\nthe tinyshakespeare corpus).\n\n");
    g_cat(b, "It writes one character at a time,\nsampling what it thinks comes next.\nRuns on this console, offline.\n\n");
    g_cat(g_cat_int(g_cat(b, "Characters written: "), (int)total_chars), "");
    g_message("ABOUT THE BARD", b);
}

int main(void)
{
    g_init();
    g_seed_random();
    cols = (G_W - 8) / G_FONT_ADV;
    if (cols > COLS_MAX) cols = COLS_MAX;
    speaker = (int)g_rand_n(NSPEAKERS);
    page_clear();
    new_scene();
    draw();

    uint32_t last_draw = g_now(), win_t0 = g_now(), win_n = 0;
    for (;;) {
        uint32_t k = g_key();
        if (k) {
            if (k == K_A) paused = !paused;
            else if (k == K_LEFT) speaker = (speaker + NSPEAKERS - 1) % NSPEAKERS;
            else if (k == K_RIGHT) speaker = (speaker + 1) % NSPEAKERS;
            else if (k == K_UP && temp10 < 16) temp10++;
            else if (k == K_DOWN && temp10 > 2) temp10--;
            else if (k == K_Y) show_think = !show_think;
            else if (k == K_X) {
                new_scene();
                paused = 0;
            } else if (k == K_B || k == K_HOME) {
                static const char *const items[] = {"Continue", "About the bard", "Clear the page", "Quit"};
                int r = g_menu("POCKET BARD", items, 4, 0, 0);
                if (r == 1) about();
                else if (r == 2) {
                    page_clear();
                    new_scene();
                } else if (r == 3) break;
                win_t0 = g_now();
                win_n = 0;
            }
            draw();
            last_draw = g_now();
            continue;
        }
        if (paused) {
            delay(20000);
            continue;
        }
        int c = sample_next();
        put_char(BARD_VOCAB[c]);
        step(c);
        total_chars++;
        win_n++;
        uint32_t now = g_now();
        if (now - win_t0 >= 1000) {
            float r = (float)win_n * 1000.0f / (float)(now - win_t0);
            rate = rate > 0.0f ? rate * 0.6f + r * 0.4f : r;
            win_t0 = now;
            win_n = 0;
        }
        if (now - last_draw >= 140) {
            draw();
            last_draw = g_now();
        }
    }
    display_clear(0);
    display_flush();
    return 0;
}
