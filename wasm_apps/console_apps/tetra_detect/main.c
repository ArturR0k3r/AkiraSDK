/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file tetra_detect.c
 * @brief Passive police-presence monitor for the Moldovan TETRA (PPDR) band.
 *
 * Receive-only RSSI energy detector on the LR2021 for the 380-400 MHz PPDR
 * band. Never transmits and never decodes: it only watches signal ENERGY to
 * answer "is the police TETRA network near me, and is a police radio
 * transmitting right now?".
 *
 *   - TETRA base stations broadcast continuously on 390-400 MHz (downlink):
 *     a persistent carrier above the noise floor => "TETRA SITE" (green).
 *   - Police radios transmit in TDMA bursts (~14 ms per 56.67 ms frame) on
 *     380-390 MHz (uplink): burst-shaped energy => "POLICE RADIO" (red).
 *
 * Architecture (single-loop interleaved scheduler + adaptive watchlist):
 *   The main loop performs ONE radio operation per frame and rotates through
 *   SCAN / WATCH / PROBE ops in a superframe, so the UI never stalls:
 *     SCAN  - tune to the next band channel, one RSSI read (EMA + noise floor)
 *     WATCH - high-rate RSSI sampling (5x2ms) of a watchlisted uplink channel
 *             to catch 14 ms TDMA bursts
 *     PROBE - visit uplink channels not yet watched, to discover activity
 *   Uplink channels that show a transient hit are promoted to the watchlist
 *   (max 4); quiet entries decay out. A 1-minute burst ring drives the red
 *   alert and the geiger blink.
 *
 * Screens:
 *   LIVE  - streaming waterfall, dual banners, geiger dot. L/R = band preset
 *           (ALL 380-400 / UP 380-390 / DOWN 390-400), A = WATCH strongest,
 *           Y = LIST, B = exit
 *   WATCH - locked channel, big RSSI, burst counter/rate, duty. L/R +/-1 ch,
 *           U/D +/-5 ch, B = back
 *   LIST  - flagged channels (class, duty, peak). U/D cursor, A = WATCH row,
 *           B = back
 *
 * Capabilities: display.write, input.read, rf.transceive
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ---------------------------------------------------------------------------
 * Band / channel model
 * ------------------------------------------------------------------------- */
#define BAND_START_HZ   380000000u
#define BAND_STOP_HZ    400000000u
#define UPLINK_HI_HZ    390000000u   /* idx 0..199 uplink, 200..400 downlink */
#define STEP_HZ         50000u
#define NCH             ((int)((BAND_STOP_HZ - BAND_START_HZ) / STEP_HZ) + 1)
#define UPLINK_N        ((int)((UPLINK_HI_HZ - BAND_START_HZ) / STEP_HZ))
#define RSSI_FLOOR      (-128)
#define RSSI_BAD        (-32)        /* >= this => assume I/O error, use floor */

#define HIT_DB          8            /* signal above floor => "hit"           */
#define TOWER_DB        6            /* persistent carrier above floor        */
#define TOWER_PERSIST   3            /* consecutive passes to call it a tower */

static int32_t chan_freq(int32_t i)
{
    return (int32_t)(BAND_START_HZ + (uint32_t)i * STEP_HZ);
}

static int is_uplink(int32_t i) { return i < UPLINK_N; }

/* Per-channel RAM state (no persistence - minimal app). */
static int8_t floor_db[NCH];   /* adaptive noise floor                        */
static int8_t ema_db[NCH];     /* smoothed RSSI                              */
static int8_t cur_db[NCH];     /* last raw RSSI (waterfall source)           */
static uint8_t persist[NCH];   /* consecutive tower-qualifying passes         */
static uint8_t visited[NCH];   /* 1 = floor initialized                      */

/* ---------------------------------------------------------------------------
 * Watchlist - adaptive uplink activity tracking
 * ------------------------------------------------------------------------- */
#define WATCH_MAX        4
#define WATCH_SAMPLES    15      /* RSSI samples per watch visit (2ms each)   */
#define SAMPLE_US        2000u
#define TARGET_SAMPLES   600     /* samples before classification (~10s)      */
#define QUIET_EXPIRE_MS  4000u   /* drop entry after this long without hits   */
#define BURST_MIN_MS     6       /* TDMA slot ~14.17ms; accept 6..30ms runs   */
#define BURST_MAX_MS     30

typedef enum { CLS_NONE = 0, CLS_CONT, CLS_BURST } class_t;

typedef struct {
    int32_t  chan;          /* -1 = empty slot                                  */
    uint16_t samples_hi;
    uint16_t samples_n;
    uint16_t runs_sum;      /* sum of run lengths in samples                    */
    uint16_t runs_n;
    uint16_t cur_run;
    int8_t   peak;
    uint32_t last_hit_ms;
    uint8_t  cls;
} watch_t;

static watch_t watch[WATCH_MAX];

static watch_t *watch_find(int32_t chan)
{
    for (int i = 0; i < WATCH_MAX; i++) {
        if (watch[i].chan == chan) {
            return &watch[i];
        }
    }
    return 0;
}

static watch_t *watch_free_slot(void)
{
    for (int i = 0; i < WATCH_MAX; i++) {
        if (watch[i].chan < 0) {
            return &watch[i];
        }
    }
    return 0;
}

/* Evict the least interesting entry: lowest activity, oldest hit. */
static watch_t *watch_evict_target(void)
{
    watch_t *w = &watch[0];
    for (int i = 1; i < WATCH_MAX; i++) {
        if (watch[i].last_hit_ms < w->last_hit_ms ||
            (watch[i].last_hit_ms == w->last_hit_ms && watch[i].samples_hi < w->samples_hi)) {
            w = &watch[i];
        }
    }
    return w;
}

static void watch_promote(int32_t chan, int rssi, uint32_t now_ms)
{
    if (watch_find(chan)) {
        return;
    }
    watch_t *w = watch_free_slot();
    if (!w) {
        w = watch_evict_target();
    }
    w->chan        = chan;
    w->samples_hi  = 0;
    w->samples_n   = 0;
    w->runs_sum    = 0;
    w->runs_n      = 0;
    w->cur_run     = 0;
    w->peak        = (int8_t)rssi;
    w->last_hit_ms = now_ms;
    w->cls         = CLS_NONE;
}

static int watch_count(void)
{
    int n = 0;
    for (int i = 0; i < WATCH_MAX; i++) {
        if (watch[i].chan >= 0) {
            n++;
        }
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * Burst ring: bursts-per-minute, 60 one-second buckets
 * ------------------------------------------------------------------------- */
#define RING_SECS 60
static uint8_t burst_bucket[RING_SECS];
static int     burst_sec_now = -1;

/* Advance the ring to the current second (zeroing buckets we pass). Called
 * every frame so the rate decays when activity stops. */
static void burst_ring_advance(void)
{
    int sec = (int)((uint32_t)rtc_get_uptime_ms() / 1000u) % RING_SECS;
    if (burst_sec_now < 0) {
        burst_sec_now = sec;
        return;
    }
    while (burst_sec_now != sec) {
        burst_sec_now = (burst_sec_now + 1) % RING_SECS;
        burst_bucket[burst_sec_now] = 0;
    }
}

static void burst_count(void)
{
    burst_ring_advance();
    if (burst_bucket[burst_sec_now] < 250) {
        burst_bucket[burst_sec_now]++;
    }
}

static int bursts_per_min(void)
{
    int sum = 0;
    for (int i = 0; i < RING_SECS; i++) {
        sum += burst_bucket[i];
    }
    return sum;
}

/* Feed one RSSI sample into a watch entry's run/duty accumulators.
 * Returns 1 if the sample ended a qualifying TETRA-shaped burst. */
static int watch_feed_sample(watch_t *w, int rssi, int32_t chan)
{
    int high = (rssi > floor_db[chan] + HIT_DB) ? 1 : 0;
    w->samples_n++;
    if (rssi > w->peak) {
        w->peak = (int8_t)rssi;
    }
    int burst = 0;
    if (high) {
        w->samples_hi++;
        w->cur_run++;
        w->last_hit_ms = (uint32_t)rtc_get_uptime_ms();
    } else {
        if (w->cur_run > 0) {
            int run_ms = (int)w->cur_run * (int)(SAMPLE_US / 1000u);
            if (run_ms >= BURST_MIN_MS && run_ms <= BURST_MAX_MS) {
                burst = 1;
                burst_count();
            }
            w->runs_sum += w->cur_run;
            w->runs_n++;
            w->cur_run = 0;
        }
    }
    return burst;
}

/* Classify a watch entry once enough samples accumulated. */
static void watch_classify(watch_t *w)
{
    int duty = (w->samples_n > 0) ? (int)((w->samples_hi * 100u) / w->samples_n) : 0;
    uint32_t avg_run_ms = (w->runs_n > 0) ? ((w->runs_sum * (SAMPLE_US / 1000u)) / w->runs_n) : 0u;

    if (duty > 85) {
        w->cls = CLS_CONT;
    } else if (w->runs_n > 0 && avg_run_ms >= (uint32_t)BURST_MIN_MS &&
               avg_run_ms <= (uint32_t)BURST_MAX_MS && duty >= 5) {
        w->cls = CLS_BURST;
    } else {
        w->cls = CLS_NONE;
    }
}

/* ---------------------------------------------------------------------------
 * Radio operations (one per frame)
 * ------------------------------------------------------------------------- */
typedef enum { OP_SCAN = 0, OP_WATCH, OP_PROBE } op_t;

static int32_t scan_idx = 0;     /* round-robin channel cursor               */
static int32_t probe_idx = 0;    /* round-robin uplink probe cursor          */

static int read_rssi_safe(void)
{
    int r = rf_get_rssi();
    /* Errno values live in [-20, 0] and would masquerade as huge signals;
     * anything that hot is also physically implausible at 380 MHz, so treat
     * it as "no reading" and keep the previous floor. */
    if (r >= RSSI_BAD) {
        return RSSI_FLOOR;
    }
    if (r < RSSI_FLOOR) {
        r = RSSI_FLOOR;
    }
    return r;
}

static void touch_channel(int32_t chan, int rssi)
{
    if (rssi == RSSI_FLOOR) {
        /* I/O error sentinel: show as floor on the waterfall but never let
         * it poison the noise floor or EMA (would drift them to -128 and
         * turn every real signal into a false hit). */
        cur_db[chan] = RSSI_FLOOR;
        return;
    }
    if (!visited[chan]) {
        floor_db[chan] = (int8_t)rssi;
        ema_db[chan]   = (int8_t)rssi;
        visited[chan]  = 1;
        return;
    }
    cur_db[chan] = (int8_t)rssi;
    /* Slow EMA for tower detection. */
    ema_db[chan] = (int8_t)(ema_db[chan] + ((rssi - ema_db[chan]) >> 3));
    /* Noise floor: keep the quietest reading, drift up slowly to recover. */
    if (rssi < floor_db[chan]) {
        floor_db[chan] = (int8_t)rssi;
    }
}

/* Every 2048 frames, nudge all floors up 1 dB so a vanished carrier's
 * baseline decays back to the true noise floor. */
static uint32_t g_frame = 0;
static void drift_floors(void)
{
    if ((g_frame & 0x7FFu) != 0) {
        return;
    }
    for (int i = 0; i < NCH; i++) {
        if (visited[i] && floor_db[i] < -40) {
            floor_db[i]++;
        }
    }
}

static void op_scan(uint32_t now_ms)
{
    int32_t i = scan_idx;
    scan_idx  = (scan_idx + 1) % NCH;

    rf_set_frequency((uint32_t)chan_freq(i));
    delay(100);
    int rssi = read_rssi_safe();
    touch_channel(i, rssi);

    if (is_uplink(i)) {
        if (rssi > floor_db[i] + HIT_DB) {
            watch_promote(i, rssi, now_ms);
        }
    } else {
        /* Downlink: persistent carrier above floor => tower. */
        if (ema_db[i] > floor_db[i] + TOWER_DB) {
            if (persist[i] < 250) {
                persist[i]++;
            }
        } else if (persist[i] > 0) {
            persist[i]--;
        }
    }
}

static void op_probe(uint32_t now_ms)
{
    /* Visit uplink channels not currently watched, in order. */
    for (int tries = 0; tries < UPLINK_N; tries++) {
        int32_t i = probe_idx;
        probe_idx = (probe_idx + 1) % UPLINK_N;
        if (watch_find(i)) {
            continue;
        }
        rf_set_frequency((uint32_t)chan_freq(i));
        delay(100);
        int rssi = read_rssi_safe();
        touch_channel(i, rssi);
        if (rssi > floor_db[i] + HIT_DB) {
            watch_promote(i, rssi, now_ms);
        }
        return;
    }
}

/* One watch visit: high-rate samples on the next watchlisted channel. */
static int watch_visit(void)
{
    static int wcursor = 0;
    for (int k = 0; k < WATCH_MAX; k++) {
        int idx = (wcursor + k) % WATCH_MAX;
        watch_t *w = &watch[idx];
        if (w->chan < 0) {
            continue;
        }
        wcursor = (idx + 1) % WATCH_MAX;
        rf_set_frequency((uint32_t)chan_freq(w->chan));
        delay(100);
        int burst = 0;
        for (int s = 0; s < WATCH_SAMPLES; s++) {
            int r = read_rssi_safe();
            burst |= watch_feed_sample(w, r, w->chan);
            delay(SAMPLE_US);
        }
        if (w->samples_n >= TARGET_SAMPLES) {
            watch_classify(w);
            w->samples_hi = 0;
            w->samples_n  = 0;
            w->runs_sum   = 0;
            w->runs_n     = 0;
        }
        /* Drop entries that went fully quiet. */
        uint32_t now = (uint32_t)rtc_get_uptime_ms();
        if (now - w->last_hit_ms > QUIET_EXPIRE_MS) {
            w->chan = -1;
        }
        return burst;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Live state / alerts
 * ------------------------------------------------------------------------- */
typedef enum { PRESET_ALL = 0, PRESET_UP, PRESET_DOWN } preset_t;
static preset_t preset = PRESET_ALL;

static int32_t view_start(void)
{
    return (preset == PRESET_DOWN) ? UPLINK_N : 0;
}

static int32_t view_n(void)
{
    if (preset == PRESET_UP) {
        return UPLINK_N;
    }
    if (preset == PRESET_DOWN) {
        return NCH - UPLINK_N;
    }
    return NCH;
}

static int tower_count(void)
{
    int n = 0;
    for (int i = UPLINK_N; i < NCH; i++) {
        if (visited[i] && persist[i] >= TOWER_PERSIST) {
            n++;
        }
    }
    return n;
}

static int strongest_tower_dbm(void)
{
    int best = RSSI_FLOOR;
    for (int i = UPLINK_N; i < NCH; i++) {
        if (visited[i] && persist[i] >= TOWER_PERSIST && ema_db[i] > best) {
            best = ema_db[i];
        }
    }
    return best;
}

/* ---------------------------------------------------------------------------
 * Waterfall (320 wide, quantized, expanded to RGB565 at blit time)
 * ------------------------------------------------------------------------- */
#define WF_H 96
#define WF_W 320
static uint8_t wf_hist[WF_H][WF_W];
static int    wf_rows = 0;

#define DISP_MIN (-120)
#define DISP_MAX (-30)

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t heat565(int v) /* 0..255 -> blue..cyan..green..yellow..red */
{
    static const int ts[] = { 0, 64, 128, 192, 255 };
    static const int r5[] = { 0, 0, 1, 31, 31 };
    static const int g6[] = { 0, 31, 31, 31, 0 };
    static const int b5[] = { 15, 31, 0, 0, 0 };
    v = clampi(v, 0, 255);
    for (int i = 0; i < 4; i++) {
        if (v >= ts[i] && v <= ts[i + 1]) {
            int span = ts[i + 1] - ts[i];
            int f = (span > 0) ? ((v - ts[i]) * 256) / span : 0;
            int r = r5[i] + (((r5[i + 1] - r5[i]) * f) >> 8);
            int g = g6[i] + (((g6[i + 1] - g6[i]) * f) >> 8);
            int b = b5[i] + (((b5[i + 1] - b5[i]) * f) >> 8);
            return (uint32_t)(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
        }
    }
    return 0x0000u;
}

static void waterfall_push(void)
{
    for (int y = wf_rows - 1; y > 0; y--) {
        for (int x = 0; x < SCR_W; x++) {
            wf_hist[y][x] = wf_hist[y - 1][x];
        }
    }
    int32_t vs = view_start();
    int32_t vn = view_n();
    int32_t w  = (SCR_W < WF_W) ? SCR_W : WF_W;
    for (int x = 0; x < w; x++) {
        int32_t i = vs + (int32_t)(((int64_t)x * vn) / w);
        int v = ((int)cur_db[i] - DISP_MIN) * 255 / (DISP_MAX - DISP_MIN);
        wf_hist[0][x] = (uint8_t)clampi(v, 0, 255);
    }
}

static void waterfall_blit(int y0)
{
    uint16_t line[WF_W];
    int32_t w = (SCR_W < WF_W) ? SCR_W : WF_W;
    for (int y = 0; y < wf_rows && y0 + y < SCR_H; y++) {
        for (int x = 0; x < w; x++) {
            line[x] = (uint16_t)heat565(wf_hist[y][x]);
        }
        display_raw_write(0, y0 + y, w, 1, line, sizeof(line));
    }
}

/* ---------------------------------------------------------------------------
 * Helpers / screens
 * ------------------------------------------------------------------------- */
static void fmt_mhz(char *buf, int32_t hz)
{
    int32_t mhz = hz / 1000000;
    int32_t frac = (hz % 1000000) / 10000; /* two decimals */
    char tmp[8];
    int n = 0;
    if (mhz == 0) {
        tmp[n++] = '0';
    }
    while (mhz > 0) {
        tmp[n++] = (char)('0' + mhz % 10);
        mhz /= 10;
    }
    int j = 0;
    while (n > 0) {
        buf[j++] = tmp[--n];
    }
    buf[j++] = '.';
    buf[j++] = (char)('0' + (frac / 10) % 10);
    buf[j++] = (char)('0' + frac % 10);
    buf[j]   = '\0';
}

static const char *preset_name(void)
{
    return (preset == PRESET_ALL) ? "ALL" : (preset == PRESET_UP) ? "UP" : "DOWN";
}

/* Geiger blink: flash period scales with burst rate (radar-detector feel). */
static int blink_period_frames(int rate)
{
    if (rate <= 0) {
        return 0; /* no blink */
    }
    return clampi(24 - rate / 2, 2, 24);
}

/* --- LIVE --- */
static void draw_live(int rate, int towers, int tower_dbm, int blink_on)
{
    int32_t y = 0;
    char lbl[28];

    /* Header */
    display_rect(0, y, SCR_W, 14, COLOR_BLACK);
    display_text(4, y + 2, "TETRA DETECT MD", COLOR_CYAN);
    display_text(SCR_W - 92, y + 2, preset_name(), COLOR_YELLOW);
    fmt_mhz(lbl, chan_freq(view_start()));
    display_text(SCR_W - 66, y + 2, lbl, COLOR_WHITE);
    y += 14;

    /* Banners: green site / red activity */
    display_rect(0, y, SCR_W, 13, COLOR_BLACK);
    if (towers > 0) {
        display_rect(2, y + 1, 118, 11, COLOR_GREEN);
        display_text(5, y + 2, "TETRA SITE", COLOR_BLACK);
        display_number(78, y + 2, tower_dbm, COLOR_BLACK);
        display_text(108, y + 2, "dBm", COLOR_BLACK);
    }
    if (rate > 0) {
        display_rect(SCR_W - 128, y + 1, 126, 11, COLOR_RED);
        display_text(SCR_W - 126, y + 2, "POLICE RADIO", COLOR_BLACK);
        display_number(SCR_W - 44, y + 2, rate, COLOR_BLACK);
        display_text(SCR_W - 24, y + 2, "/m", COLOR_BLACK);
    }
    y += 13;

    /* Waterfall */
    waterfall_blit(y);
    y += wf_rows;

    /* Tower ticks + band boundary on top of the waterfall (y = 14 header +
     * 13 banners = 27). */
    int32_t wf_y0 = 27;
    if (preset == PRESET_ALL) {
        int32_t bx = (int32_t)(((int64_t)(UPLINK_N - view_start()) * SCR_W) / view_n());
        display_vline(bx, wf_y0, wf_rows, COLOR_GRAY);
    }
    for (int i = UPLINK_N; i < NCH; i++) {
        if (visited[i] && persist[i] >= TOWER_PERSIST) {
            int32_t tx = (int32_t)(((int64_t)(i - view_start()) * SCR_W) / view_n());
            if (tx >= 0 && tx < SCR_W) {
                display_vline(tx, wf_y0, 3, COLOR_GREEN);
            }
        }
    }

    /* Geiger dot */
    int32_t dy = y + 8;
    if (rate > 0) {
        if (blink_on) {
            display_circle_fill(SCR_W / 2, dy, 6, COLOR_RED);
        } else {
            display_circle(SCR_W / 2, dy, 6, COLOR_RED);
        }
    } else if (towers > 0) {
        display_circle_fill(SCR_W / 2, dy, 6, COLOR_GREEN);
    } else {
        display_circle(SCR_W / 2, dy, 6, COLOR_GRAY);
    }
    y += 18;

    /* Footer */
    display_rect(0, SCR_H - 13, SCR_W, 13, COLOR_BLACK);
    display_text(4, SCR_H - 11, "L/R:band A:watch Y:list B:exit", COLOR_GRAY);
}

/* --- WATCH --- */
typedef struct {
    int32_t  chan;
    uint16_t samples_hi;
    uint16_t samples_n;
    uint16_t cur_run;
    uint16_t runs_sum;
    uint16_t runs_n;
    int8_t   peak;
} watchlock_t;

static watchlock_t wl;
static int         watch_mode = 0;

static void watchlock_feed(int rssi)
{
    int high = (rssi > floor_db[wl.chan] + HIT_DB) ? 1 : 0;
    wl.samples_n++;
    if (rssi > wl.peak) {
        wl.peak = (int8_t)rssi;
    }
    if (high) {
        wl.samples_hi++;
        wl.cur_run++;
    } else if (wl.cur_run > 0) {
        int run_ms = (int)wl.cur_run * (int)(SAMPLE_US / 1000u);
        if (run_ms >= BURST_MIN_MS && run_ms <= BURST_MAX_MS) {
            burst_count();
        }
        wl.runs_sum += wl.cur_run;
        wl.runs_n++;
        wl.cur_run = 0;
    }
}

static void draw_watch(int rate, int blink_on)
{
    display_clear(COLOR_BLACK);
    char fbuf[12];
    fmt_mhz(fbuf, chan_freq(wl.chan));

    display_rect(0, 0, SCR_W, 16, COLOR_WHITE);
    display_text(4, 3, "WATCH", COLOR_BLACK);
    display_text(SCR_W - 76, 3, fbuf, COLOR_BLACK);
    display_text(SCR_W - 44, 3, is_uplink(wl.chan) ? "UP" : "DN", COLOR_BLACK);

    display_text(SCR_W / 2 - 60, 24, "RSSI dBm", COLOR_GRAY);
    display_number(SCR_W / 2 - 40, 38, wl.peak, COLOR_WHITE);
    display_text_huge(SCR_W / 2 - 70, 60, "     ", COLOR_BLACK);

    int duty = (wl.samples_n > 0) ? (int)((wl.samples_hi * 100u) / wl.samples_n) : 0;
    display_text(8, 104, "DUTY", COLOR_GRAY);
    display_progress_bar(60, 108, 120, 8, duty, 100, COLOR_CYAN, COLOR_DARK_GRAY);
    display_number(192, 104, duty, COLOR_CYAN);
    display_text(222, 104, "%", COLOR_GRAY);

    display_text(8, 126, "BURSTS", COLOR_GRAY);
    display_number(72, 126, rate, COLOR_RED);
    display_text(116, 126, "/min", COLOR_GRAY);

    /* geiger blink */
    if (rate > 0) {
        if (blink_on) {
            display_circle_fill(SCR_W - 24, 130, 10, COLOR_RED);
        } else {
            display_circle(SCR_W - 24, 130, 10, COLOR_RED);
        }
    } else {
        display_circle(SCR_W - 24, 130, 10, COLOR_GRAY);
    }

    int towers = tower_count();
    if (towers > 0) {
        display_text(8, 148, "SITE", COLOR_GREEN);
        display_number(48, 148, strongest_tower_dbm(), COLOR_GREEN);
        display_text(90, 148, "dBm", COLOR_GREEN);
    }

    display_rect(0, SCR_H - 13, SCR_W, 13, COLOR_BLACK);
    display_text(4, SCR_H - 11, "L/R:+/-1ch U/D:+/-5 B:back", COLOR_GRAY);
}

static void watchlock_enter(int32_t chan)
{
    wl.chan       = chan;
    wl.samples_hi = 0;
    wl.samples_n  = 0;
    wl.cur_run    = 0;
    wl.runs_sum   = 0;
    wl.runs_n     = 0;
    wl.peak       = RSSI_FLOOR;
    watch_mode    = 1;
    /* Seed the channel's floor/EMA so the first samples compare against a
     * real reading instead of the zero-initialized array. */
    if (!visited[chan]) {
        rf_set_frequency((uint32_t)chan_freq(chan));
        delay(100);
        touch_channel(chan, read_rssi_safe());
    }
}

/* --- LIST --- */
static int list_sel = 0;

static void draw_list(int rate, int towers)
{
    display_clear(COLOR_BLACK);
    display_rect(0, 0, SCR_W, 16, COLOR_WHITE);
    display_text(4, 3, "FLAGGED CHANNELS", COLOR_BLACK);
    display_text(SCR_W - 120, 3, "towers:", COLOR_BLACK);
    display_number(SCR_W - 68, 3, towers, COLOR_BLACK);
    display_text(SCR_W - 48, 3, "rate:", COLOR_BLACK);
    display_number(SCR_W - 8, 3, rate, COLOR_BLACK);

    int shown = 0;
    int y = 22;
    for (int i = 0; i < WATCH_MAX && y < SCR_H - 20; i++) {
        watch_t *w = &watch[i];
        if (w->chan < 0) {
            continue;
        }
        shown++;
        char fbuf[12];
        fmt_mhz(fbuf, chan_freq(w->chan));
        uint32_t fg = (shown - 1 == list_sel) ? COLOR_BLACK : COLOR_WHITE;
        uint32_t bg = (shown - 1 == list_sel) ? COLOR_WHITE : COLOR_BLACK;
        display_rect(2, y - 2, SCR_W - 4, 16, bg);
        display_text(4, y, fbuf, fg);
        if (w->cls == CLS_BURST) {
            display_text(90, y, "BURST", COLOR_RED);
        } else if (w->cls == CLS_CONT) {
            display_text(90, y, "CONT", COLOR_CYAN);
        } else {
            display_text(90, y, "???", COLOR_GRAY);
        }
        int duty = (w->samples_n > 0) ? (int)((w->samples_hi * 100u) / w->samples_n) : 0;
        display_rect(140, y + 3, 80, 6, COLOR_DARK_GRAY);
        display_rect(140, y + 3, (80 * clampi(duty, 0, 100)) / 100, 6, COLOR_YELLOW);
        display_number(230, y, w->peak, fg);
        y += 18;
    }
    if (shown == 0) {
        display_text(6, y, "no flagged channels yet", COLOR_GRAY);
    }
    if (list_sel < 0) {
        list_sel = 0;
    }
    if (list_sel >= shown && shown > 0) {
        list_sel = shown - 1;
    }

    display_rect(0, SCR_H - 13, SCR_W, 13, COLOR_BLACK);
    display_text(4, SCR_H - 11, "U/D:sel A:watch B:back", COLOR_GRAY);
}

/* ---------------------------------------------------------------------------
 * Scheduler / main
 * ------------------------------------------------------------------------- */
static const op_t SUPER[8] = {
    OP_SCAN, OP_WATCH, OP_SCAN, OP_WATCH,
    OP_PROBE, OP_SCAN, OP_WATCH, OP_PROBE,
};

typedef enum { SCR_LIVE = 0, SCR_LIST } screen_t;
static screen_t screen = SCR_LIVE;

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);
    wf_rows = (SCR_H < WF_H) ? SCR_H : WF_H;
    wf_rows = (wf_rows > 110) ? 110 : wf_rows; /* leave room for chrome */

    if (rf_select(AKIRA_RF_CHIP_LR2021) < 0) {
        display_clear(COLOR_BLACK);
        display_text(8, SCR_H / 2 - 8, "LR2021 not available", COLOR_WHITE);
        display_flush();
        while (!AKIRA_BTN_PRESSED((uint32_t)input_get_buttons(), AKIRA_BTN_B)) {
            delay(50000);
        }
        return -1;
    }
    rf_set_modulation(RADIO_MOD_GFSK);
    rf_set_bandwidth(38500u); /* 38.5 kHz: catches a 25 kHz TETRA carrier */

    for (int i = 0; i < WATCH_MAX; i++) {
        watch[i].chan = -1;
    }
    for (int i = 0; i < NCH; i++) {
        cur_db[i] = RSSI_FLOOR; /* waterfall starts quiet, not hot */
    }

    int prev = input_get_buttons();
    int blink_on = 1;

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;
        uint32_t now_ms = (uint32_t)rtc_get_uptime_ms();

        if (watch_mode) {
            /* Dedicate the radio to the locked channel - real-time burst view. */
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
                watch_mode = 0;
                screen     = SCR_LIVE;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT)) {
                wl.chan = clampi(wl.chan - 1, 0, NCH - 1);
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT)) {
                wl.chan = clampi(wl.chan + 1, 0, NCH - 1);
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP)) {
                wl.chan = clampi(wl.chan + 5, 0, NCH - 1);
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN)) {
                wl.chan = clampi(wl.chan - 5, 0, NCH - 1);
            }

            rf_set_frequency((uint32_t)chan_freq(wl.chan));
            delay(100);
            for (int s = 0; s < WATCH_SAMPLES; s++) {
                watchlock_feed(read_rssi_safe());
                delay(SAMPLE_US);
            }
            if (wl.samples_n > 800) { /* keep the duty window fresh */
                wl.samples_hi = wl.samples_hi >> 1;
                wl.samples_n  = wl.samples_n >> 1;
                wl.runs_sum   = wl.runs_sum >> 1;
                wl.runs_n     = wl.runs_n >> 1;
            }

            drift_floors();
            burst_ring_advance();
            int rate = bursts_per_min();
            int period = blink_period_frames(rate);
            if (period > 0 && (g_frame % (uint32_t)period) == 0) {
                blink_on = !blink_on;
            } else if (period == 0) {
                blink_on = 1;
            }
            draw_watch(rate, blink_on);
            display_flush();
            g_frame++;
            delay(20000);
            continue;
        }

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            if (screen == SCR_LIVE) {
                return 0;
            }
            screen = SCR_LIVE;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_Y)) {
            screen = (screen == SCR_LIVE) ? SCR_LIST : SCR_LIVE;
        }

        if (screen == SCR_LIVE) {
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT)) {
                preset = (preset_t)((preset + 2) % 3); /* -1 */
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT)) {
                preset = (preset_t)((preset + 1) % 3);
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
                /* Watch the most interesting channel: best burst entry, else
                 * best continuous, else strongest tower, else band center. */
                int32_t best = -1;
                int     best_rank = -1;
                for (int i = 0; i < WATCH_MAX; i++) {
                    if (watch[i].chan < 0) {
                        continue;
                    }
                    int rank = (watch[i].cls == CLS_BURST) ? 3 :
                               (watch[i].cls == CLS_CONT) ? 2 : 1;
                    if (rank > best_rank) {
                        best_rank = rank;
                        best      = watch[i].chan;
                    }
                }
                if (best < 0) {
                    int tdbm = strongest_tower_dbm();
                    if (tdbm > RSSI_FLOOR) {
                        for (int i = UPLINK_N; i < NCH; i++) {
                            if (visited[i] && ema_db[i] == tdbm) {
                                best = i;
                                break;
                            }
                        }
                    }
                }
                if (best < 0) {
                    best = UPLINK_N / 2;
                }
                watchlock_enter(best);
            }
        } else { /* SCR_LIST */
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP)) {
                list_sel--;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN)) {
                list_sel++;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
                int shown = 0;
                for (int i = 0; i < WATCH_MAX; i++) {
                    if (watch[i].chan < 0) {
                        continue;
                    }
                    if (shown == list_sel) {
                        watchlock_enter(watch[i].chan);
                        break;
                    }
                    shown++;
                }
            }
        }

        /* One radio op per frame via the superframe rotation. */
        op_t op = SUPER[(int)(g_frame % 8)];
        if (op == OP_SCAN) {
            op_scan(now_ms);
        } else if (op == OP_WATCH) {
            if (watch_count() == 0) {
                op_probe(now_ms); /* nothing to watch: discover instead */
            } else {
                watch_visit();
            }
        } else {
            op_probe(now_ms);
        }
        drift_floors();
        burst_ring_advance();

        g_frame++;
        if ((g_frame % 8) == 0) {
            waterfall_push();
        }

        int rate   = bursts_per_min();
        int towers = tower_count();
        int period = blink_period_frames(rate);
        if (period > 0 && (g_frame % (uint32_t)period) == 0) {
            blink_on = !blink_on;
        } else if (period == 0) {
            blink_on = 1;
        }

        if (screen == SCR_LIVE) {
            draw_live(rate, towers, strongest_tower_dbm(), blink_on);
        } else {
            draw_list(rate, towers);
        }
        display_flush();
        delay(20000);
    }
    return 0;
}
