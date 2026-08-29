/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ppdr_monitor.c
 * @brief Passive RSSI presence/pattern detector for the PPDR band on the LR2021.
 *
 * Receive-only: never calls rf_send / rf_receive / rf_recv_pop / any transmit
 * function. Reads instantaneous RSSI per channel only - no demodulation, no
 * decoding, no attempt to identify content, individuals, or vehicles.
 *
 * Two interleaved passes each tick:
 *   - coarse sweep: retune+RSSI across every channel in the configured band,
 *     compared against a fixed noise floor. Channels above
 *     floor+threshold get queued.
 *   - dwell service: each queued channel gets a short RSSI-sampling visit;
 *     stats accumulate across repeated visits (one radio can't hold N
 *     channels open at once) until enough samples exist to classify
 *     continuous-carrier vs TDMA-like burst vs noise.
 *
 *   Y — cycle screen (LIVE/LIST/LOG/SETTINGS)   A — mark event / edit
 *   UP/DOWN — select settings field             LEFT/RIGHT — adjust value
 *   B — back to LIVE, or exit from LIVE
 *
 * Capabilities: display.write, input.read, rf.transceive, storage.read, storage.write
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ---- band / channel model ---------------------------------------------- */
#define MAX_CHANNELS 1024

typedef struct {
    uint32_t band_start_hz;
    uint32_t band_stop_hz;
    uint32_t step_hz;
    int      threshold_db;   /* flag when rssi > noise_floor_dbm + threshold_db */
    int      close_dbm;      /* absolute "close" threshold                  */
    int      score_cutoff;   /* alert when score >= this                    */
    int      noise_floor_dbm; /* fixed absolute RSSI floor for an unoccupied channel */
} config_t;

static config_t cfg = {
    .band_start_hz   = 380000000u,
    .band_stop_hz    = 400000000u,
    .step_hz         = 25000u,
    .threshold_db    = 12,
    .close_dbm       = -55,
    .score_cutoff    = 4,
    .noise_floor_dbm = -110,
};

static int32_t chan_count(void)
{
    int32_t n = (int32_t)((cfg.band_stop_hz - cfg.band_start_hz) / cfg.step_hz) + 1;
    if (n < 1) {
        n = 1;
    }
    if (n > MAX_CHANNELS) {
        n = MAX_CHANNELS;
    }
    return n;
}

static uint32_t chan_freq(int32_t i)
{
    return cfg.band_start_hz + (uint32_t)i * cfg.step_hz;
}

/* ---- RSSI capture ---------------------------------------------------------- */
static int8_t  cur_rssi[MAX_CHANNELS];

#define RSSI_FLOOR (-128)

/* ---- calibration log (append-only, on-device only) ----------------------- */
typedef struct {
    int32_t unix_ts;
    uint32_t freq_hz;
    int16_t  rssi_dbm;
    uint8_t  cls; /* 0=CONT 1=BURST */
    uint8_t  score;
} calib_rec_t;

#define CALIB_LOG_MAX 200
static void append_calib_event(uint32_t freq_hz, int rssi_dbm, int cls, int score)
{
    calib_rec_t rec;
    rec.unix_ts  = rtc_get_unix_time();
    rec.freq_hz  = freq_hz;
    rec.rssi_dbm = (int16_t)rssi_dbm;
    rec.cls      = (uint8_t)cls;
    rec.score    = (uint8_t)score;

    int fd = storage_open("ppdr_calib_log.bin", STORAGE_O_APPEND);
    if (fd < 0) {
        return;
    }
    storage_write(fd, &rec, (int)sizeof(rec));
    storage_close(fd);
}

/* One record per completed waterfall row: timestamp + the same quantized
 * bytes waterfall_blit() renders on screen, so an offline decoder can
 * reproduce the exact waterfall image later via gradient565(). */
static void log_waterfall_row(const uint8_t *row, int row_len)
{
    int32_t ts = rtc_get_unix_time();
    int fd = storage_open("ppdr_waterfall.bin", STORAGE_O_APPEND);
    if (fd < 0) {
        return;
    }
    storage_write(fd, &ts, (int)sizeof(ts));
    storage_write(fd, row, row_len);
    storage_close(fd);
}

#define CALIB_VIEW_MAX 8
static calib_rec_t calib_view[CALIB_VIEW_MAX];
static int         calib_view_n = 0;

static void load_calib_view(void)
{
    calib_view_n = 0;
    int fd = storage_open("ppdr_calib_log.bin", STORAGE_O_READ);
    if (fd < 0) {
        return;
    }
    calib_rec_t rec;
    calib_rec_t ring[CALIB_VIEW_MAX];
    int n = 0;
    while (storage_read(fd, &rec, (int)sizeof(rec)) == (int)sizeof(rec)) {
        ring[n % CALIB_VIEW_MAX] = rec;
        n++;
    }
    storage_close(fd);
    int shown = (n < CALIB_VIEW_MAX) ? n : CALIB_VIEW_MAX;
    for (int i = 0; i < shown; i++) {
        /* newest first */
        calib_view[i] = ring[(n - 1 - i) % CALIB_VIEW_MAX];
    }
    calib_view_n = shown;
}

/* ---- dwell / classify queue ----------------------------------------------- */
#define DWELL_MAX        16
#define SAMPLE_PERIOD_US 2000u   /* 2ms per RSSI sample                     */
/* rf_set_frequency() forces the LR2021 back to Standby every call, so
 * re-entering Rx costs the visit's first sample a few ms of settle time -
 * keep VISIT_SAMPLES small so one dwell visit doesn't dominate the tick. */
#define VISIT_SAMPLES    20
#define TARGET_SAMPLES   1500    /* ~3s of accumulated on-channel sampling  */
#define QUIET_VISITS_MAX 3
#define EXPIRE_MS        4000u
#define BURST_MIN_MS     6
#define BURST_MAX_MS     30

typedef enum { SLOT_EMPTY = 0, SLOT_DWELLING, SLOT_FINALIZED } slot_state_t;
typedef enum { CLS_NONE = 0, CLS_CONTINUOUS, CLS_BURST, CLS_NOISE } class_t;

typedef struct {
    slot_state_t state;
    int32_t  chan_idx;
    uint32_t samples_high;
    uint32_t samples_total;
    uint32_t cur_run;
    uint32_t sum_runs;
    uint32_t count_runs;
    int      quiet_visits;
    int      peak_rssi;
    uint32_t last_seen_ms;
    class_t  cls;
    int      score;
} dwell_t;

static dwell_t dwell[DWELL_MAX];

static int find_slot_for_chan(int32_t chan_idx)
{
    for (int i = 0; i < DWELL_MAX; i++) {
        if (dwell[i].state != SLOT_EMPTY && dwell[i].chan_idx == chan_idx) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < DWELL_MAX; i++) {
        if (dwell[i].state == SLOT_EMPTY) {
            return i;
        }
    }
    return -1;
}

static void finalize_slot(int idx)
{
    dwell_t *d = &dwell[idx];
    int duty = (d->samples_total > 0) ? (int)((d->samples_high * 100u) / d->samples_total) : 0;
    uint32_t avg_run_ms = (d->count_runs > 0) ? ((d->sum_runs * 2u) / d->count_runs) : 0u; /* 2ms/sample */

    if (duty > 90) {
        d->cls = CLS_CONTINUOUS;
    } else if (d->count_runs > 0 && avg_run_ms >= BURST_MIN_MS && avg_run_ms <= BURST_MAX_MS) {
        d->cls = CLS_BURST;
    } else {
        d->cls = CLS_NOISE;
    }

    if (d->cls == CLS_NOISE) {
        d->state = SLOT_EMPTY;
        return;
    }

    int score = 1; /* in configured PPDR band */
    if (d->cls == CLS_BURST) {
        score += 2;
    }
    if (d->peak_rssi > cfg.close_dbm) {
        score += 1;
    }
    if (d->cls != CLS_CONTINUOUS) {
        score += 1;
    }
    d->score = score;
    d->state = SLOT_FINALIZED;
}

/* one sample fed into a dwelling slot's run-length/duty accumulators */
static void dwell_feed_sample(dwell_t *d, int rssi)
{
    int high = (rssi > cfg.noise_floor_dbm + cfg.threshold_db) ? 1 : 0;
    d->samples_total++;
    if (rssi > d->peak_rssi) {
        d->peak_rssi = rssi;
    }
    if (high) {
        d->samples_high++;
        d->cur_run++;
    } else {
        if (d->cur_run > 0) {
            d->sum_runs += d->cur_run;
            d->count_runs++;
            d->cur_run = 0;
        }
    }
}

/* ---- RF sweep ------------------------------------------------------------- */
/* Chunked so coarse_sweep_pass() can bail out mid-pass on a button press
 * (poll_input() is checked every channel, not just once per chunk) - the
 * chunk size itself only controls how many ticks a full pass takes: a real
 * hop costs ~3ms (CalibFE skipped when frequency delta is small, RX-reentry
 * sleep at 1ms). 40 channels/tick keeps a full 800-channel pass (one
 * waterfall row) to ~20 ticks. */
#define COARSE_CHUNK 40
static int32_t sweep_cursor = 0;

/* Button state is polled every channel inside coarse_sweep_pass(), not
 * just once per main-loop tick, so worst-case input latency is one hop
 * (~3ms) regardless of COARSE_CHUNK. Edges accumulate in g_btn_pending
 * until main()'s loop consumes them. */
static uint32_t g_btn_prev    = 0;
static uint32_t g_btn_pending = 0;

static int poll_input(void)
{
    /* Level-diffing input_get_buttons() alone misses a press+release that
     * completes entirely between two poll_input() calls (e.g. across a
     * display_flush() or a CalibFE stall) - the edge queue is the only
     * record of that tap, so build the pressed mask from it instead of
     * draining and discarding. */
    uint32_t pressed = 0;
    akira_input_event_t ev;
    while (input_poll_event(&ev, sizeof(ev)) == 1) {
        if (ev.pressed) {
            pressed |= (1U << ev.button_id);
        }
    }

    uint32_t held = (uint32_t)input_get_buttons();
    pressed      |= held & ~g_btn_prev;
    g_btn_prev    = held;
    g_btn_pending |= pressed;
    return pressed != 0;
}

/* coarse_sweep_pass() only advances COARSE_CHUNK channels per call, so
 * cur_rssi[] is freshest right behind sweep_cursor and stale everywhere
 * ahead of it. A caller that redraws the waterfall on every call (instead
 * of once per full pass) paints that freshness boundary as a straight
 * diagonal across stacked rows - a "growing triangle" instead of a clean
 * row. Return 1 exactly when sweep_cursor has just wrapped (a full pass
 * completed) so the caller can gate the redraw on that instead. */
static int coarse_sweep_pass(void)
{
    int32_t n = chan_count();
    int32_t steps = (n < COARSE_CHUNK) ? n : COARSE_CHUNK;
    int pass_complete = 0;
    for (int32_t k = 0; k < steps; k++) {
        if (poll_input()) {
            return pass_complete; /* resume from sweep_cursor next tick */
        }
        int32_t i = sweep_cursor;
        sweep_cursor = (sweep_cursor + 1) % n;
        rf_set_frequency(chan_freq(i));
        delay(100); /* PLL_LOCK typ 32us + SPI margin */
        int r = rf_get_rssi();
        cur_rssi[i] = (int8_t)((r < -128) ? -128 : (r > 127 ? 127 : r));

        int existing = find_slot_for_chan(i);
        if (cur_rssi[i] > cfg.noise_floor_dbm + cfg.threshold_db) {
            if (existing >= 0) {
                dwell[existing].last_seen_ms = (uint32_t)rtc_get_uptime_ms();
                dwell[existing].quiet_visits = 0;
            } else {
                int slot = find_free_slot();
                if (slot >= 0) {
                    dwell_t *d = &dwell[slot];
                    d->state         = SLOT_DWELLING;
                    d->chan_idx      = i;
                    d->samples_high  = 0;
                    d->samples_total = 0;
                    d->cur_run       = 0;
                    d->sum_runs      = 0;
                    d->count_runs    = 0;
                    d->quiet_visits  = 0;
                    d->peak_rssi     = cur_rssi[i];
                    d->last_seen_ms  = (uint32_t)rtc_get_uptime_ms();
                    d->cls           = CLS_NONE;
                    d->score         = 0;
                }
            }
        } else if (existing >= 0 && dwell[existing].state == SLOT_FINALIZED) {
            uint32_t now = (uint32_t)rtc_get_uptime_ms();
            if (now - dwell[existing].last_seen_ms > EXPIRE_MS) {
                dwell[existing].state = SLOT_EMPTY;
            }
        }
        if (sweep_cursor == 0) {
            pass_complete = 1;
            break; /* stop mid-chunk: don't sample next pass into this row */
        }
    }
    return pass_complete;
}

/* One slot serviced per tick (round-robin) - keeps a tick short. Stats
 * accumulate across many visits regardless, per the dwell-queue design. */
#define DWELL_SERVICE_PER_TICK 1
static int dwell_service_cursor = 0;

static void service_dwell_queue(void)
{
    int serviced = 0;
    for (int k = 0; k < DWELL_MAX && serviced < DWELL_SERVICE_PER_TICK; k++) {
        int i = dwell_service_cursor;
        dwell_service_cursor = (dwell_service_cursor + 1) % DWELL_MAX;

        dwell_t *d = &dwell[i];
        if (d->state != SLOT_DWELLING) {
            continue;
        }
        if (poll_input()) {
            return; /* same slot retried next tick - nothing lost */
        }
        serviced++;

        rf_set_frequency(chan_freq(d->chan_idx));
        delay(100);

        int high_this_visit = 0;
        for (int s = 0; s < VISIT_SAMPLES; s++) {
            if (poll_input()) {
                break; /* partial visit still counts toward the slot's stats */
            }
            int r = rf_get_rssi();
            dwell_feed_sample(d, r);
            if (r > cfg.noise_floor_dbm + cfg.threshold_db) {
                high_this_visit = 1;
            }
            delay(SAMPLE_PERIOD_US);
        }

        if (!high_this_visit) {
            d->quiet_visits++;
        } else {
            d->quiet_visits = 0;
        }

        if (d->samples_total >= TARGET_SAMPLES || d->quiet_visits >= QUIET_VISITS_MAX) {
            finalize_slot(i);
        }
    }
}

/* ---- waterfall (quantized 1-byte history, expanded to RGB565 at blit time) */
#define WF_W 320
#define WF_H_MAX 240
/* display_raw_write() hits GRAM directly; display_text()/display_rect() go
 * through the OS framebuffer and only reach GRAM on display_flush() - which
 * pushes the whole framebuffer and would blank out the raw-written rows.
 * Keeping the overlay labels in their own top/bottom strips (never touched
 * by raw_write) is what lets both coexist without one erasing the other. */
#define WF_OVERLAY_TOP_H    20
#define WF_OVERLAY_BOTTOM_H 20
static uint8_t wf_hist[WF_H_MAX][WF_W];
static int     wf_rows = 0;
static int     wf_y0   = 0;

#define RSSI_DISP_MIN (-120)
#define RSSI_DISP_MAX (-30)

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t gradient565(int intensity /* 0..255 */)
{
    static const int   stops_t[] = { 0, 46, 87, 128, 168, 204, 255 };
    static const int   stops_r[] = { 5, 16, 28, 31, 217, 232, 227 };
    static const int   stops_g[] = { 7, 35, 90, 158, 194, 134, 59 };
    static const int   stops_b[] = { 10, 58, 122, 107, 54, 44, 46 };
    intensity = clampi(intensity, 0, 255);
    int n = (int)(sizeof(stops_t) / sizeof(stops_t[0]));
    for (int i = 0; i < n - 1; i++) {
        if (intensity >= stops_t[i] && intensity <= stops_t[i + 1]) {
            int span = stops_t[i + 1] - stops_t[i];
            int f    = (span > 0) ? ((intensity - stops_t[i]) * 256) / span : 0;
            int r = stops_r[i] + (((stops_r[i + 1] - stops_r[i]) * f) >> 8);
            int g = stops_g[i] + (((stops_g[i + 1] - stops_g[i]) * f) >> 8);
            int b = stops_b[i] + (((stops_b[i + 1] - stops_b[i]) * f) >> 8);
            return (uint32_t)((((uint32_t)r & 0xF8u) << 8) | (((uint32_t)g & 0xFCu) << 3) | ((uint32_t)b >> 3));
        }
    }
    return 0x0000u;
}

/* Set whenever wf_hist changes (a row landed) or the GRAM region needs a
 * repaint (re-entering LIVE from another screen, which clears the whole
 * framebuffer) - draw_live() only pays for waterfall_blit()'s per-pixel
 * display_raw_write() calls when this is set, instead of every tick. */
static int wf_dirty = 1;

static void waterfall_push_row(void)
{
    wf_dirty = 1;
    /* shift every row down by one (row 0 becomes the newest reading) */
    for (int y = wf_rows - 1; y > 0; y--) {
        for (int x = 0; x < WF_W; x++) {
            wf_hist[y][x] = wf_hist[y - 1][x];
        }
    }

    int32_t n = chan_count();
    uint8_t row[WF_W];
    for (int x = 0; x < WF_W; x++) {
        row[x] = 0;
    }
    for (int32_t i = 0; i < n; i++) {
        int col = (int)(((int64_t)i * WF_W) / n);
        int intensity = ((int)cur_rssi[i] - RSSI_DISP_MIN) * 255 / (RSSI_DISP_MAX - RSSI_DISP_MIN);
        intensity = clampi(intensity, 0, 255);
        if ((uint8_t)intensity > row[col]) {
            row[col] = (uint8_t)intensity;
        }
    }
    for (int x = 0; x < WF_W; x++) {
        wf_hist[0][x] = row[x];
    }
    log_waterfall_row(row, WF_W);
}

static void waterfall_blit(void)
{
    uint16_t line[WF_W];
    for (int y = 0; y < wf_rows; y++) {
        for (int x = 0; x < WF_W; x++) {
            line[x] = (uint16_t)gradient565(wf_hist[y][x]);
        }
        display_raw_write(0, wf_y0 + y, WF_W, 1, line, sizeof(line));
    }
}

/* ---- screens --------------------------------------------------------------- */
typedef enum { SCR_LIVE = 0, SCR_LIST, SCR_LOG, SCR_SETTINGS } screen_t;
static screen_t screen = SCR_LIVE;

static void fmt_mhz(char *buf, uint32_t hz)
{
    uint32_t mhz  = hz / 1000000u;
    uint32_t frac = (hz % 1000000u) / 1000u; /* three decimals */
    char tmp[12];
    int  i = 0;
    if (mhz == 0) {
        tmp[i++] = '0';
    }
    while (mhz > 0) {
        tmp[i++] = (char)('0' + mhz % 10);
        mhz /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j++] = '.';
    buf[j++] = (char)('0' + (frac / 100) % 10);
    buf[j++] = (char)('0' + (frac / 10) % 10);
    buf[j++] = (char)('0' + frac % 10);
    buf[j]   = '\0';
}

static int any_alert(void)
{
    for (int i = 0; i < DWELL_MAX; i++) {
        if (dwell[i].state == SLOT_FINALIZED && dwell[i].score >= cfg.score_cutoff) {
            return 1;
        }
    }
    return 0;
}

/* -1 forces a redraw (fresh entry to LIVE); otherwise only redrawn when the
 * alert state flips, so display_flush() isn't called every tick - see the
 * raw_write/framebuffer note above WF_OVERLAY_TOP_H. */
static int live_overlay_alert = -1;

static void draw_live(void)
{
    int alert_now = any_alert();
    if (alert_now != live_overlay_alert) {
        char lbl[24];
        char lo[12], hi[12];
        fmt_mhz(lo, cfg.band_start_hz);
        fmt_mhz(hi, cfg.band_stop_hz);
        lbl[0] = '\0';
        int p = 0;
        for (int k = 0; lo[k]; k++) lbl[p++] = lo[k];
        lbl[p++] = '-';
        for (int k = 0; hi[k]; k++) lbl[p++] = hi[k];
        lbl[p] = '\0';

        display_rect(0, 0, SCR_W, WF_OVERLAY_TOP_H, COLOR_BLACK);
        display_text(6, 6, lbl, COLOR_CYAN);

        if (alert_now) {
            display_rect(SCR_W - 150, 4, 146, 14, COLOR_RED);
            display_text(SCR_W - 148, 6, "PPDR ACTIVITY NEARBY", COLOR_BLACK);
        } else {
            display_rect(SCR_W - 74, 4, 70, 14, COLOR_BLACK);
            display_text(SCR_W - 72, 6, "MONITORING", COLOR_GRAY);
        }

        display_rect(0, SCR_H - WF_OVERLAY_BOTTOM_H, SCR_W, WF_OVERLAY_BOTTOM_H, COLOR_BLACK);
        display_text(6, SCR_H - 14, "L/R:band A:mark Y:menu B:exit", COLOR_GRAY);

        display_flush();
        live_overlay_alert = alert_now;
    }

    if (wf_dirty) {
        waterfall_blit();
        wf_dirty = 0;
    }
}

static void draw_list(void)
{
    display_clear(COLOR_BLACK);
    display_rect(0, 0, SCR_W, 20, COLOR_WHITE);
    display_text(6, 4, "FLAGGED CHANNELS", COLOR_BLACK);

    int y = 26;
    for (int i = 0; i < DWELL_MAX && y < SCR_H - 20; i++) {
        if (dwell[i].state != SLOT_FINALIZED) {
            continue;
        }
        dwell_t *d = &dwell[i];
        char fbuf[12];
        fmt_mhz(fbuf, chan_freq(d->chan_idx));
        display_text(6, y, fbuf, COLOR_WHITE);
        display_text(96, y, d->cls == CLS_BURST ? "BURST" : "CONT", d->cls == CLS_BURST ? COLOR_RED : COLOR_CYAN);
        int duty = (d->samples_total > 0) ? (int)((d->samples_high * 100u) / d->samples_total) : 0;
        display_rect(150, y + 2, 90, 6, COLOR_DARK_GRAY);
        display_rect(150, y + 2, (90 * clampi(duty, 0, 100)) / 100, 6,
                     d->cls == CLS_BURST ? COLOR_RED : COLOR_CYAN);
        display_number(250, y, d->score, COLOR_YELLOW);
        display_number(280, y, d->peak_rssi, COLOR_GRAY);
        y += 18;
    }
    if (y == 26) {
        display_text(6, y, "no flagged channels", COLOR_GRAY);
    }

    display_rect(0, SCR_H - 16, SCR_W, 16, COLOR_WHITE);
    display_text(6, SCR_H - 14, "Y:menu B:back", COLOR_BLACK);
    display_flush();
}

static void draw_log(void)
{
    display_clear(COLOR_BLACK);
    display_rect(0, 0, SCR_W, 20, COLOR_WHITE);
    display_text(6, 4, "CALIBRATION LOG (local only)", COLOR_BLACK);

    int y = 26;
    for (int i = 0; i < calib_view_n; i++) {
        char fbuf[12];
        fmt_mhz(fbuf, calib_view[i].freq_hz);
        display_number(6, y, calib_view[i].unix_ts, COLOR_GRAY);
        display_text(90, y, fbuf, COLOR_WHITE);
        display_text(150, y, calib_view[i].cls == 1 ? "BURST" : "CONT",
                     calib_view[i].cls == 1 ? COLOR_RED : COLOR_CYAN);
        display_number(220, y, calib_view[i].score, COLOR_YELLOW);
        y += 16;
    }
    if (calib_view_n == 0) {
        display_text(6, y, "no events yet - hold A on LIVE", COLOR_GRAY);
    }

    display_rect(0, SCR_H - 16, SCR_W, 16, COLOR_WHITE);
    display_text(6, SCR_H - 14, "Y:menu B:back", COLOR_BLACK);
    display_flush();
}

#define SETTINGS_N 7
static int settings_sel = 0;

static void draw_settings(void)
{
    display_clear(COLOR_BLACK);
    display_rect(0, 0, SCR_W, 20, COLOR_WHITE);
    display_text(6, 4, "SETTINGS", COLOR_BLACK);

    const char *labels[SETTINGS_N] = { "BAND START", "BAND STOP", "STEP", "THRESHOLD", "CLOSE", "CUTOFF", "FLOOR" };
    int y = 30;
    for (int i = 0; i < SETTINGS_N; i++) {
        uint32_t fg = (i == settings_sel) ? COLOR_BLACK : COLOR_WHITE;
        uint32_t bg = (i == settings_sel) ? COLOR_WHITE : COLOR_BLACK;
        display_rect(4, y - 2, SCR_W - 8, 20, bg);
        display_text(8, y, labels[i], fg);

        char val[16];
        switch (i) {
        case 0: fmt_mhz(val, cfg.band_start_hz); break;
        case 1: fmt_mhz(val, cfg.band_stop_hz); break;
        default: val[0] = '\0'; break;
        }
        if (i == 0 || i == 1) {
            display_text(180, y, val, fg);
        } else if (i == 2) {
            display_number(180, y, (int)(cfg.step_hz / 1000u), fg);
        } else if (i == 3) {
            display_number(180, y, cfg.threshold_db, fg);
        } else if (i == 4) {
            display_number(180, y, cfg.close_dbm, fg);
        } else if (i == 5) {
            display_number(180, y, cfg.score_cutoff, fg);
        } else if (i == 6) {
            display_number(180, y, cfg.noise_floor_dbm, fg);
        }
        y += 24;
    }

    display_rect(0, SCR_H - 16, SCR_W, 16, COLOR_WHITE);
    display_text(6, SCR_H - 14, "UP/DN:field L/R:value", COLOR_BLACK);
    display_flush();
}

static void settings_adjust(int dir)
{
    switch (settings_sel) {
    case 0: cfg.band_start_hz = (uint32_t)((int64_t)cfg.band_start_hz + dir * 1000000); break;
    case 1: cfg.band_stop_hz  = (uint32_t)((int64_t)cfg.band_stop_hz + dir * 1000000); break;
    case 2: {
        static const uint32_t steps[] = { 12500u, 25000u, 50000u };
        int idx = 1;
        for (int i = 0; i < 3; i++) {
            if (steps[i] == cfg.step_hz) {
                idx = i;
            }
        }
        idx = clampi(idx + dir, 0, 2);
        cfg.step_hz = steps[idx];
        break;
    }
    case 3: cfg.threshold_db    = clampi(cfg.threshold_db + dir, 1, 40); break;
    case 4: cfg.close_dbm       = clampi(cfg.close_dbm + dir, -100, -20); break;
    case 5: cfg.score_cutoff    = clampi(cfg.score_cutoff + dir, 1, 5); break;
    case 6: cfg.noise_floor_dbm = clampi(cfg.noise_floor_dbm + dir, -128, -60); break;
    default: break;
    }
}

/* ---- main ------------------------------------------------------------------ */
int main(void)
{
    display_get_size(&SCR_W, &SCR_H);
    wf_y0   = WF_OVERLAY_TOP_H;
    wf_rows = SCR_H - WF_OVERLAY_TOP_H - WF_OVERLAY_BOTTOM_H;
    if (wf_rows > WF_H_MAX) {
        wf_rows = WF_H_MAX;
    }
    if (wf_rows < 1) {
        wf_rows = 1;
    }

    /* Zero-init would read as 0dBm (impossibly strong) for every channel the
     * rotating coarse-sweep cursor hasn't reached yet - floods the dwell
     * queue with placeholders and paints the waterfall solid red. */
    for (int32_t i = 0; i < MAX_CHANNELS; i++) {
        cur_rssi[i] = RSSI_FLOOR;
    }

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
    rf_set_bandwidth(25000u);

    screen_t prev_screen = (screen_t)-1; /* != SCR_LIVE, forces first overlay draw */

    g_btn_prev = (uint32_t)input_get_buttons();
    while (1) {
        poll_input(); /* catches presses missed while other screens run */
        int pressed = (int)g_btn_pending;
        g_btn_pending = 0;
        int held = (int)g_btn_prev;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_Y)) {
            screen = (screen_t)(screen == SCR_SETTINGS ? SCR_LIVE : screen + 1);
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            if (screen == SCR_LIVE) {
                return 0;
            }
            screen = SCR_LIVE;
        }

        if (screen == SCR_LIVE) {
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
                int best = -1;
                for (int i = 0; i < DWELL_MAX; i++) {
                    if (dwell[i].state == SLOT_FINALIZED &&
                        (best < 0 || dwell[i].score > dwell[best].score)) {
                        best = i;
                    }
                }
                if (best >= 0) {
                    append_calib_event(chan_freq(dwell[best].chan_idx), dwell[best].peak_rssi,
                                        dwell[best].cls == CLS_BURST ? 1 : 0, dwell[best].score);
                }
            }
        } else if (screen == SCR_LOG) {
            if (pressed) {
                load_calib_view();
            }
        } else if (screen == SCR_SETTINGS) {
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP)) {
                settings_sel = clampi(settings_sel - 1, 0, SETTINGS_N - 1);
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN)) {
                settings_sel = clampi(settings_sel + 1, 0, SETTINGS_N - 1);
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT)) {
                settings_adjust(-1);
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT)) {
                settings_adjust(1);
            }
        }

        uint32_t t_tick0 = (uint32_t)rtc_get_uptime_ms();
        int pass_done = coarse_sweep_pass();
        uint32_t t_after_coarse = (uint32_t)rtc_get_uptime_ms();
        service_dwell_queue();
        uint32_t t_after_dwell = (uint32_t)rtc_get_uptime_ms();
        if (pass_done) {
            waterfall_push_row();
        }
        /* this platform's printf() only implements %d/%s (akira_api.h) -
         * any other specifier silently skips va_arg() and misaligns
         * every argument after it, so %d is used for everything here. */
        printf("tick: coarse_ms=%d dwell_ms=%d total_ms=%d rssi0=%d floor=%d held=%d\n",
               (int)(t_after_coarse - t_tick0), (int)(t_after_dwell - t_after_coarse),
               (int)(t_after_dwell - t_tick0), (int)cur_rssi[0], cfg.noise_floor_dbm, held);

        if (screen == SCR_LIVE && prev_screen != SCR_LIVE) {
            live_overlay_alert = -1; /* other screens clear+flush the whole panel */
            wf_dirty = 1;
        }
        prev_screen = screen;

        switch (screen) {
        case SCR_LIVE:     draw_live(); break;
        case SCR_LIST:     draw_list(); break;
        case SCR_LOG:       if (calib_view_n == 0) { load_calib_view(); } draw_log(); break;
        case SCR_SETTINGS: draw_settings(); break;
        default: break;
        }
    }
    return 0;
}
