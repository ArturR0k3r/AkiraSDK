/**
 * @file main.c
 * @brief Wand — teach the console your own motion gestures, then cast spells.
 *
 * Hold A and move the console to record an example of a gesture; give each
 * of the four spells a few examples.  The console learns from those few
 * examples on the spot: every recording from the 6-axis IMU (LSM6DS3 accel +
 * gyro) is resampled to a fixed length, and a new gesture is matched against
 * all stored examples with dynamic time warping, which forgives doing the
 * move faster or slower.  After each change it runs a leave-one-out
 * self-test (every example classified against the others) and learns how far
 * a gesture may stray before it says "not sure".  Your gestures are saved in
 * the app's storage sandbox.
 *
 * Controls (teach screen):
 *   UP / DOWN   pick a spell        LEFT / RIGHT  rename its gesture
 *   hold A      record an example   X             forget its examples
 *   Y           cast mode           B / HOME      menu
 * Cast mode: hold A, do a gesture, release.  B goes back.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_game.h"
#include "../../common/akira_ml.h"

#define NCLS   4   /* spells                         */
#define NEX    6   /* examples per spell             */
#define L      32  /* frames after resampling        */
#define NCH    6   /* ax ay az gx gy gz              */
#define MAXS   128 /* raw samples per recording      */
#define BAND   8   /* DTW warping window             */
#define SAMPLE_MS 20

static const char *const SPELL[NCLS] = {"Fireball", "Lightning", "Frost", "Healing"};
static const uint16_t SPELL_COL[NCLS] = {0xFC00, 0x5D9F, 0x9F1F, 0x47E8};
static const char *const GESTURE[] = {"Circle", "Shake", "Flick", "Twist", "Wave", "Push", "Swipe", "Zigzag", "Tap", "Lift"};
#define NGESTURE 10

/* ── Stored examples (persisted) ────────────────────────────────────── */
#define DB_MAGIC 0x574E4431u /* "WND1" */
typedef struct {
    uint32_t magic;
    uint8_t name[NCLS];
    uint8_t count[NCLS];
    uint8_t next[NCLS]; /* ring slot to overwrite when full */
    float ex[NCLS][NEX][L][NCH];
} db_t;
static db_t DB;

static float thr[NCLS];  /* per-spell "not sure" distance           */
#define MIN_MOTION 0.06f  /* below this rms the console barely moved */
static int selftest = -1; /* leave-one-out accuracy %, -1 = n/a   */

/* ── Recording ──────────────────────────────────────────────────────── */
static float raw[MAXS][NCH];
static uint32_t raw_t[MAXS];
static int nraw;
static int imu_ok = 1;

static int read_imu(float *v)
{
    static const int32_t CH[NCH] = {SENSOR_CHAN_ACCEL_X, SENSOR_CHAN_ACCEL_Y, SENSOR_CHAN_ACCEL_Z,
                                    SENSOR_CHAN_GYRO_X, SENSOR_CHAN_GYRO_Y, SENSOR_CHAN_GYRO_Z};
    for (int k = 0; k < NCH; k++) {
        int r = sensor_read(CH[k]);
        if (r == AKIRA_SENSOR_ERROR) return -1;
        /* accel in g, gyro in rad/s scaled to a similar range */
        v[k] = k < 3 ? (float)r / 9806.65f : (float)r / 2500.0f;
    }
    return 0;
}

/* Resample the recording to L frames, evenly spaced in time. */
static void resample(float out[L][NCH])
{
    uint32_t t0 = raw_t[0], span = raw_t[nraw - 1] - raw_t[0];
    int j = 0;
    for (int i = 0; i < L; i++) {
        float t = (float)t0 + (float)span * (float)i / (float)(L - 1);
        while (j < nraw - 2 && (float)raw_t[j + 1] < t) j++;
        float dt = (float)(raw_t[j + 1] - raw_t[j]);
        float f = dt > 0.0f ? (t - (float)raw_t[j]) / dt : 0.0f;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        for (int c = 0; c < NCH; c++) out[i][c] = raw[j][c] + (raw[j + 1][c] - raw[j][c]) * f;
    }
}

/* ── Matching ───────────────────────────────────────────────────────── */
#define BIG 1e30f

static float dtw(const float a[L][NCH], const float b[L][NCH])
{
    static float prev[L + 1], cur[L + 1];
    for (int j = 0; j <= L; j++) prev[j] = BIG;
    prev[0] = 0.0f;
    for (int i = 1; i <= L; i++) {
        for (int j = 0; j <= L; j++) cur[j] = BIG;
        int lo = i - BAND < 1 ? 1 : i - BAND, hi = i + BAND > L ? L : i + BAND;
        for (int j = lo; j <= hi; j++) {
            float cost = 0.0f;
            for (int c = 0; c < NCH; c++) cost += ml_absf(a[i - 1][c] - b[j - 1][c]);
            float m = prev[j - 1];
            if (prev[j] < m) m = prev[j];
            if (cur[j - 1] < m) m = cur[j - 1];
            cur[j] = cost + m;
        }
        for (int j = 0; j <= L; j++) prev[j] = cur[j];
    }
    return prev[L] / (float)(2 * L);
}

/* Nearest distance per spell; skip one stored example (for self-test). */
static void distances(const float q[L][NCH], float *d, int skip_c, int skip_e)
{
    for (int c = 0; c < NCLS; c++) {
        d[c] = BIG;
        for (int e = 0; e < DB.count[c]; e++) {
            if (c == skip_c && e == skip_e) continue;
            float v = dtw(q, (const float(*)[NCH])DB.ex[c][e]);
            if (v < d[c]) d[c] = v;
        }
    }
}

static int nearest(const float *d)
{
    int best = -1;
    for (int c = 0; c < NCLS; c++) {
        if (d[c] < BIG && (best < 0 || d[c] < d[best])) best = c;
    }
    return best;
}

static int trained_classes(void)
{
    int n = 0;
    for (int c = 0; c < NCLS; c++) n += DB.count[c] > 0;
    return n;
}

/* How much the recording moves: rms deviation from its own mean. */
static float motion_rms(const float q[L][NCH])
{
    float s = 0.0f;
    for (int c = 0; c < NCH; c++) {
        float m = 0.0f;
        for (int i = 0; i < L; i++) m += q[i][c];
        m /= (float)L;
        for (int i = 0; i < L; i++) s += (q[i][c] - m) * (q[i][c] - m);
    }
    return ml_sqrtf(s / (float)(L * NCH));
}

/* Self-test, and each spell's "not sure" limit from how far its own
 * examples are from each other (leave-one-out). */
static void learn(void)
{
    int tot = 0, ok = 0;
    float worst[NCLS], spread = 0.0f;
    int nspread = 0;
    for (int c = 0; c < NCLS; c++) {
        worst[c] = 0.0f;
        for (int e = 0; e < DB.count[c]; e++) {
            float d[NCLS];
            distances((const float(*)[NCH])DB.ex[c][e], d, c, e);
            if (d[c] >= BIG) continue; /* only example of its spell */
            if (d[c] > worst[c]) worst[c] = d[c];
            if (trained_classes() < 2) continue;
            tot++;
            ok += nearest(d) == c;
        }
        if (worst[c] > 0.0f) {
            spread += worst[c];
            nspread++;
        }
    }
    selftest = tot ? ok * 100 / tot : -1;
    for (int c = 0; c < NCLS; c++) {
        /* a single example borrows the typical spread of the others */
        float w = worst[c] > 0.0f ? worst[c] : nspread ? spread / (float)nspread : 0.4f;
        /* few examples underestimate the spread: widen the margin */
        int n = DB.count[c] > 1 ? DB.count[c] : 2;
        thr[c] = w * (1.8f + 0.8f / (float)(n - 1));
        if (thr[c] < 0.12f) thr[c] = 0.12f;
    }
}

/* ── Persistence ────────────────────────────────────────────────────── */
static void db_reset(void)
{
    uint8_t *p = (uint8_t *)&DB;
    for (unsigned i = 0; i < sizeof(DB); i++) p[i] = 0;
    DB.magic = DB_MAGIC;
    for (int c = 0; c < NCLS; c++) DB.name[c] = (uint8_t)c;
}

static void db_load(void)
{
    int fd = storage_open("gestures.bin", STORAGE_O_READ);
    if (fd >= 0) {
        int n = storage_read(fd, &DB, sizeof(DB));
        storage_close(fd);
        if (n == (int)sizeof(DB) && DB.magic == DB_MAGIC) return;
    }
    db_reset();
}

static void db_save(void)
{
    int fd = storage_open("gestures.bin", STORAGE_O_WRITE);
    if (fd >= 0) {
        storage_write(fd, &DB, sizeof(DB));
        storage_close(fd);
    }
}

/* ── Drawing ────────────────────────────────────────────────────────── */
#define C_BG    0x0841
#define C_HDR   0x18E4
#define C_PANEL 0x10A3
#define C_TEXT  0xFFFF
#define C_DIM   0x8CB5
#define C_GOLD  0xFE87
static const uint16_t TRACE_COL[NCH] = {0xF800, 0x07E0, 0x001F | 0x4208, 0xFD20, 0x07FF, 0xF81F};

static void header(const char *title, const char *right)
{
    display_rect(0, 0, G_W, 14, C_HDR);
    display_text(4, 2, title, C_GOLD);
    if (right) g_text_r(G_W - 4, 2, right, C_DIM);
}

/* Live IMU traces of the recording so far. */
static void traces(int x, int y, int w, int h)
{
    display_rounded_rect_fill(x, y, w, h, 4, C_PANEL);
    display_hline(x + 2, y + h / 2, w - 4, C_HDR);
    int n = nraw < 2 ? 0 : nraw;
    for (int c = 0; c < NCH; c++) {
        int px = 0, py = 0;
        for (int i = 0; i < n; i++) {
            float v = raw[i][c] * (c < 3 ? 0.25f : 0.2f);
            if (v > 0.48f) v = 0.48f;
            if (v < -0.48f) v = -0.48f;
            int sx = x + 3 + i * (w - 6) / (MAXS - 1);
            int sy = y + h / 2 - (int)(v * (float)h);
            if (i) display_line(px, py, sx, sy, TRACE_COL[c]);
            px = sx;
            py = sy;
        }
    }
}

static void spell_icon(int cx, int cy, int c)
{
    uint16_t col = SPELL_COL[c];
    if (c == 0) {
        display_circle_fill(cx, cy, 6, col);
        display_circle_fill(cx, cy, 3, 0xFFE0);
    } else if (c == 1) {
        display_line(cx + 2, cy - 7, cx - 3, cy, col);
        display_line(cx - 3, cy, cx + 3, cy, col);
        display_line(cx + 3, cy, cx - 2, cy + 7, col);
    } else if (c == 2) {
        for (int k = 0; k < 3; k++) {
            static const int8_t D[3][2] = {{0, 7}, {6, 4}, {6, -4}};
            display_line(cx - D[k][0], cy - D[k][1], cx + D[k][0], cy + D[k][1], col);
        }
    } else {
        display_rect(cx - 2, cy - 7, 5, 15, col);
        display_rect(cx - 7, cy - 2, 15, 5, col);
    }
}

static int sel;

/* Store @p q as a new example of spell @p c (oldest replaced when full). */
static void add_example(int c, const float q[L][NCH])
{
    int e = DB.count[c] < NEX ? DB.count[c]++ : DB.next[c]++ % NEX;
    for (int i = 0; i < L; i++)
        for (int ch = 0; ch < NCH; ch++) DB.ex[c][e][i][ch] = q[i][ch];
    learn();
    db_save();
}

static void draw_teach(const char *note)
{
    char b[48];
    display_clear(C_BG);
    b[0] = '\0';
    if (selftest >= 0) g_cat(g_cat_int(g_cat(b, "self-test "), selftest), "%");
    header("WAND - teach", b[0] ? b : 0);

    for (int c = 0; c < NCLS; c++) {
        int y = 22 + c * 38;
        if (c == sel) display_rounded_rect_fill(4, y, G_W - 8, 34, 5, C_PANEL);
        if (c == sel) display_rounded_rect(4, y, G_W - 8, 34, 5, SPELL_COL[c]);
        spell_icon(22, y + 17, c);
        display_text(40, y + 5, SPELL[c], SPELL_COL[c]);
        b[0] = '\0';
        g_cat(g_cat(g_cat(b, c == sel ? "< " : ""), GESTURE[DB.name[c]]), c == sel ? " >" : "");
        display_text(40, y + 19, b, C_TEXT);
        for (int e = 0; e < NEX; e++) {
            int dx = G_W - 18 - (NEX - 1 - e) * 12;
            if (e < DB.count[c]) display_circle_fill(dx, y + 17, 4, SPELL_COL[c]);
            else display_circle(dx, y + 17, 4, C_DIM);
        }
    }
    int fy = 22 + NCLS * 38 + 2;
    if (note) {
        g_text_c(G_W / 2, fy, note, C_GOLD);
    } else {
        g_text_c(G_W / 2, fy, "Hold A and move: add an example", C_TEXT);
    }
    g_text_c(G_W / 2, fy + 13, trained_classes() >= 2 ? "Y: cast   X: forget   B: menu" : "Teach 2 spells to cast  X: forget",
             C_DIM);
    display_flush();
}

/* Record while A is held.  Returns 1 with a usable recording. */
static int record(const char *title, uint16_t col)
{
    uint32_t start = g_now(), next = start, last_draw = 0;
    nraw = 0;
    while (((uint32_t)input_get_buttons() & AKIRA_BTN_A) && nraw < MAXS) {
        uint32_t now = g_now();
        if ((int32_t)(now - next) >= 0) {
            if (read_imu(raw[nraw]) < 0) {
                imu_ok = 0;
                break;
            }
            raw_t[nraw++] = now;
            next += SAMPLE_MS;
            if ((int32_t)(now - next) > 0) next = now + SAMPLE_MS; /* fell behind */
        }
        if (now - last_draw >= 150) {
            /* timestamps keep the resampling honest across this pause */
            display_clear(C_BG);
            header(title, "release A to stop");
            g_text_large_c(G_W / 2, 24, "RECORDING", col);
            traces(6, 52, G_W - 12, G_H - 60);
            display_flush();
            last_draw = g_now();
        }
        delay(2000);
    }
    g_flush_input();
    return imu_ok && nraw >= 8 && raw_t[nraw - 1] - raw_t[0] >= 250;
}

static void effect(int c)
{
    for (int f = 0; f < 9; f++) {
        display_clear(0x0000);
        int cx = G_W / 2, cy = G_H / 2 + 10;
        if (c == 0) {
            for (int k = 0; k <= f; k++) {
                display_circle_fill(cx, cy, 4 + k * 10, k == f ? 0xFFE0 : (k & 1 ? 0xFC00 : 0xF800));
            }
        } else if (c == 1) {
            for (int bolt = 0; bolt < 2; bolt++) {
                int x = cx - 60 + (int)g_rand_n(120), y = 30;
                while (y < G_H) {
                    int nx = x - 15 + (int)g_rand_n(31), ny = y + 12 + (int)g_rand_n(14);
                    display_line(x, y, nx, ny, 0xFFFF);
                    display_line(x + 1, y, nx + 1, ny, 0x5D9F);
                    x = nx;
                    y = ny;
                }
            }
        } else if (c == 2) {
            int r = 10 + f * 9;
            static const int8_t D[6][2] = {{0, 10}, {9, 5}, {9, -5}, {0, -10}, {-9, -5}, {-9, 5}};
            for (int k = 0; k < 6; k++) {
                int ex = cx + D[k][0] * r / 10, ey = cy + D[k][1] * r / 10;
                display_line(cx, cy, ex, ey, 0xDFFF);
                display_line(ex, ey, cx + D[(k + 1) % 6][0] * r / 20, cy + D[(k + 1) % 6][1] * r / 20, 0x9F1F);
            }
        } else {
            for (int k = 0; k < 14; k++) {
                int x = (int)g_rand_n((uint32_t)G_W), y = G_H - (int)((k * 37 + f * 25) % (uint32_t)G_H);
                display_rect(x - 1, y - 5, 3, 11, 0x47E8);
                display_rect(x - 5, y - 1, 11, 3, 0x47E8);
            }
        }
        g_text_large_c(G_W / 2, 18, SPELL[c], SPELL_COL[c]);
        display_flush();
        delay(60000);
    }
}

static void cast_mode(void)
{
    char b[48];
    const char *line = "Hold A and cast a spell";
    int last = -2, guess = -1;
    float d[NCLS];
    static float q[L][NCH];
    g_flush_input();
    for (;;) {
        display_clear(C_BG);
        header("WAND - cast", "B: back");
        if (last == -3) {
            g_text_large_c(G_W / 2, 24, "No motion", C_DIM);
        } else if (last >= 0 || last == -1) {
            /* result + why */
            const char *name = last >= 0 ? SPELL[last] : "Not sure";
            g_text_large_c(G_W / 2, 24, name, last >= 0 ? SPELL_COL[last] : C_DIM);
            float sim[NCLS], s = 0.0f, dmin = d[nearest(d)];
            for (int c = 0; c < NCLS; c++) {
                sim[c] = d[c] < BIG ? ml_expf(-(d[c] - dmin) / (0.15f * dmin + 0.02f)) : 0.0f;
                s += sim[c];
            }
            for (int c = 0; c < NCLS; c++) {
                int y = 60 + c * 22;
                display_text(12, y, SPELL[c], SPELL_COL[c]);
                display_text(92, y, GESTURE[DB.name[c]], C_DIM);
                int w = (int)((float)(G_W - 190) * (s > 0.0f ? sim[c] / s : 0.0f));
                display_rect(170, y, G_W - 190, 9, C_HDR);
                if (w > 0) display_rect(170, y, w, 9, SPELL_COL[c]);
            }
            b[0] = '\0';
            int nc = nearest(d);
            g_cat_int(g_cat(g_cat_int(g_cat(b, "distance "), (int)(dmin * 100.0f)), "  limit "), (int)(thr[nc] * 100.0f));
            g_text_c(G_W / 2, 158, b, C_DIM);
        }
        g_text_c(G_W / 2, G_H - 30, line, C_TEXT);
        if (guess >= 0) {
            b[0] = '\0';
            g_cat(g_cat(g_cat(b, "X: that was "), SPELL[guess]), " - learn it");
            g_text_c(G_W / 2, G_H - 16, b, C_DIM);
        }
        display_flush();

        uint32_t k = g_wait_key();
        if (k == K_B || k == K_HOME) return;
        if (k == K_X && guess >= 0) {
            add_example(guess, (const float(*)[NCH])q);
            line = "Learned - thanks!";
            guess = -1;
            continue;
        }
        if (k != K_A) continue;
        guess = -1;
        if (!record("WAND - cast", C_GOLD)) {
            line = imu_ok ? "Too short - hold A while you move" : "IMU not available";
            last = -2;
            continue;
        }
        resample(q);
        if (motion_rms((const float(*)[NCH])q) < MIN_MOTION) {
            last = -3;
            line = "Move the console while holding A";
            continue;
        }
        distances((const float(*)[NCH])q, d, -1, -1);
        int c = nearest(d);
        last = (c >= 0 && d[c] <= thr[c]) ? c : -1;
        guess = c;
        line = "Hold A and cast again";
        if (last >= 0) effect(last);
    }
}

static const char HELP[] =
    "Teach: pick a spell, hold A and\n"
    "do its gesture, release. Give each\n"
    "spell 3-5 examples, done the way\n"
    "you'll really cast it.\n"
    "\n"
    "Cast: hold A, gesture, release.\n"
    "\n"
    "The console compares your move with\n"
    "every example (dynamic time warping,\n"
    "so speed doesn't matter much) and\n"
    "says 'not sure' if nothing is close.";

int main(void)
{
    g_init();
    g_seed_random();
    db_load();
    learn();
    const char *note = 0;
    float probe[NCH];
    if (read_imu(probe) < 0) imu_ok = 0;

    for (;;) {
        draw_teach(imu_ok ? note : "IMU not available");
        note = 0;
        uint32_t k = g_wait_key();
        if (k == K_UP) sel = (sel + NCLS - 1) % NCLS;
        else if (k == K_DOWN) sel = (sel + 1) % NCLS;
        else if (k == K_LEFT || k == K_RIGHT) {
            DB.name[sel] = (uint8_t)((DB.name[sel] + (k == K_LEFT ? NGESTURE - 1 : 1)) % NGESTURE);
        } else if (k == K_A) {
            static float q[L][NCH];
            if (record("WAND - teach", SPELL_COL[sel])) {
                resample(q);
                if (motion_rms((const float(*)[NCH])q) < MIN_MOTION) {
                    note = "Barely moved - try a bigger gesture";
                } else {
                    add_example(sel, (const float(*)[NCH])q);
                    note = "Learned!";
                }
            } else {
                note = imu_ok ? "Too short - hold A while you move" : "IMU not available";
            }
        } else if (k == K_X) {
            DB.count[sel] = 0;
            DB.next[sel] = 0;
            learn();
            db_save();
            note = "Forgotten";
        } else if (k == K_Y) {
            if (trained_classes() >= 2) {
                cast_mode();
            } else {
                note = "Teach at least two spells first";
            }
        } else if (k == K_B || k == K_HOME) {
            static const char *const items[] = {"Resume", "How to use", "Forget everything", "Quit"};
            int r = g_menu("WAND", items, 4, 0, 0);
            if (r == 1) g_message("HOW TO USE", HELP);
            else if (r == 2) {
                db_reset();
                learn();
                db_save();
            } else if (r == 3) break;
        }
    }
    db_save();
    display_clear(0);
    display_flush();
    return 0;
}
