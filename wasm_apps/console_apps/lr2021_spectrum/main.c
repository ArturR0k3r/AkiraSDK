/*
 * lr2021_spectrum — LR2021 spectrum analyzer + LoRa detector + sniffer.
 *
 * MODE SPECTRUM: steps the LR2021 across a LoRa ISM band, samples RSSI per
 * bin, draws a live bar graph with peak-hold and a scrolling waterfall.
 *
 * MODE DETECT: parks on a center frequency and cycles LoRa configs
 * (SF 7–12 × BW 125/250/500 kHz), flagging live receptions per config.
 * Cursor picks a config; A locks it and enters SNIFF.
 *
 * MODE SNIFF: continuous RX on the locked freq/SF/BW, dumps packets as
 * hex + ASCII with RSSI. Ring history scrollable with UP/DOWN.
 *
 *   UP/DOWN    — spectrum: mode switch / detect: SF cursor / sniff: history
 *   LEFT/RIGHT — detect: BW cursor (spectrum: band)
 *   A          — detect: lock config → sniff
 *   Y          — detect: next center frequency
 *   X          — detect: reset counters
 *   B          — sniff: back to detect / else: exit
 *
 * Capabilities: display.write, input.read, rf.transceive
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* ── Bands (spectrum mode) ─────────────────────────────────────────────── */

typedef struct {
    const char *name;
    uint32_t    start;
    uint32_t    stop;
} band_t;

static const band_t BANDS[] = {
    { "863-871 LoRa", 863000000u, 871000000u },
    { "915 US LoRa", 902000000u, 928000000u },
    { "780 CHN LoRa", 779000000u, 787000000u },
    { "470 CN",      470000000u, 510000000u },
    { "2.4G LoRa",   2400000000u, 2500000000u },
};
#define NBANDS ((int)(sizeof(BANDS) / sizeof(BANDS[0])))

#define NBINS     24
#define HIST_ROWS 32
#define RSSI_MIN  (-120)
#define RSSI_MAX  (-30)

/* ── Detect mode configs ───────────────────────────────────────────────── */

#define SF_MIN 7
#define SF_MAX 12
static const uint32_t BWS_HZ[] = { 125000u, 250000u, 500000u };
#define NBWS ((int)(sizeof(BWS_HZ) / sizeof(BWS_HZ[0])))

static const band_t DET_FREQS[] = {
    { "868.1M EU",  868100000u, 0u },
    { "868.5M EU",  868500000u, 0u },
    { "869.5M EU",  869525000u, 0u },
    { "915.0M US",  915000000u, 0u },
    { "923.0M JP",  923000000u, 0u },
    { "2.402G BLE", 2402000000u, 0u },
};
#define NDET ((int)(sizeof(DET_FREQS) / sizeof(DET_FREQS[0])))

#define RX_BUF_LEN 64
#define RX_TIMEOUT_MS 60

/* ── State ─────────────────────────────────────────────────────────────── */

static int cur[NBINS];
static int peak[NBINS];
static int hist[HIST_ROWS][NBINS]; /* waterfall rows, [0] = newest */
static int detect_hits[SF_MAX - SF_MIN + 1][NBWS];
static int detect_pass;
static uint8_t rx_buf[RX_BUF_LEN];

static int g_mode; /* 0 = spectrum, 1 = detect, 2 = sniff, 3 = jam */
static int g_det_freq_idx;

/* Detect cursor + sniff lock */
static int cur_sf = 10, cur_bw = 0;
static int sniff_sf = 10, sniff_bw = 0;
static uint32_t sniff_freq;

/* Sniff packet ring */
#define SNIFF_RING 16
static struct { uint16_t len; uint8_t data[RX_BUF_LEN]; } sniff_ring[SNIFF_RING];
static int sniff_head, sniff_count, sniff_sel;
static int sniff_total;
static int g_diff_sel, g_diff_off; /* frame-diff view state */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t bin_freq(const band_t *b, int i)
{
    return b->start + (uint32_t)(((uint64_t)(b->stop - b->start) * (uint32_t)i) / (NBINS - 1));
}

/* Minimal "MHz" formatter (no libc). */
static void fmt_mhz(char *buf, uint32_t hz)
{
    uint32_t mhz  = hz / 1000000u;
    uint32_t frac = (hz % 1000000u) / 10000u; /* two decimals */
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
    buf[j++] = (char)('0' + (frac / 10) % 10);
    buf[j++] = (char)('0' + frac % 10);
    buf[j]   = '\0';
}

/* RSSI → color ramp (RGB565): noise floor black → hot accent. */
static uint16_t rssi_color(int r)
{
    if (r <= -100) return 0x0000u;                /* black */
    if (r <= -85)  return 0x2945u;                /* dark gray */
    if (r <= -70)  return 0x7BEFu;                /* gray */
    if (r <= -55)  return 0xFFFFu;                /* white */
    return 0xF800u;                               /* red — strong */
}

static void reset_peaks(void)
{
    for (int i = 0; i < NBINS; i++) {
        peak[i] = RSSI_MIN;
        cur[i]  = RSSI_MIN;
    }
    for (int r = 0; r < HIST_ROWS; r++) {
        for (int i = 0; i < NBINS; i++) {
            hist[r][i] = RSSI_MIN;
        }
    }
}

/* ── Spectrum sweep + waterfall ────────────────────────────────────────── */

static void do_spectrum_pass(const band_t *b)
{
    for (int i = 0; i < NBINS; i++) {
        rf_set_frequency(bin_freq(b, i));
        delay(1000); /* PLL/CalibFe settle (µs) */
        int r  = clampi(rf_get_rssi(), RSSI_MIN, RSSI_MAX);
        cur[i] = r;
        if (r > peak[i]) peak[i] = r;
    }

    /* Shift history down, insert newest row at top. */
    for (int r = HIST_ROWS - 1; r > 0; r--) {
        for (int i = 0; i < NBINS; i++) {
            hist[r][i] = hist[r - 1][i];
        }
    }
    for (int i = 0; i < NBINS; i++) {
        hist[0][i] = cur[i];
    }
}

static void draw_spectrum(const band_t *b)
{
    display_clear(COLOR_BLACK);

    int hdr_h = SCR_H / 8;
    int ftr_h = SCR_H / 10;
    int plot_y = hdr_h;
    int plot_h = SCR_H - hdr_h - ftr_h;
    int bar_w  = SCR_W / NBINS;
    if (bar_w < 1) bar_w = 1;

    /* Header */
    display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
    display_text(6, 4, "LR2021 SPECTRUM", COLOR_BLACK);
    display_text(SCR_W / 2 - 30, 4, b->name, COLOR_BLACK);
    char lo[12], hi[12];
    fmt_mhz(lo, b->start);
    fmt_mhz(hi, b->stop);
    display_text(6, hdr_h - 12, lo, COLOR_BLACK);
    display_text(SCR_W - 56, hdr_h - 12, hi, COLOR_BLACK);

    /* Current pass bar graph + peak ticks */
    for (int i = 0; i < NBINS; i++) {
        int h_cur  = (cur[i]  - RSSI_MIN) * plot_h / (RSSI_MAX - RSSI_MIN);
        int h_peak = (peak[i] - RSSI_MIN) * plot_h / (RSSI_MAX - RSSI_MIN);
        int x      = i * bar_w;
        int bw     = bar_w - 1 > 0 ? bar_w - 1 : 1;
        if (h_cur > 0) {
            display_rect(x, plot_y + plot_h - h_cur, bw, h_cur, rssi_color(cur[i]));
        }
        int py = plot_y + plot_h - h_peak;
        display_hline(x, py, bw, COLOR_WHITE);
    }

    /* Waterfall strip below the bars: HIST_ROWS rows × NBINS cells. */
    int wf_y = plot_y + plot_h;
    int wf_h = SCR_H - ftr_h - wf_y;
    int cell_h = wf_h / HIST_ROWS;
    if (cell_h < 1) cell_h = 1;
    for (int r = 0; r < HIST_ROWS; r++) {
        int cy = wf_y + r * cell_h;
        if (cy + cell_h > SCR_H - ftr_h) break;
        for (int i = 0; i < NBINS; i++) {
            int x   = i * bar_w;
            int cbw = bar_w - 1 > 0 ? bar_w - 1 : 1;
            display_rect(x, cy, cbw, cell_h, rssi_color(hist[r][i]));
        }
    }

    /* Footer */
    display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
    display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                 "U/D:mode L/R:band A:reset B:exit", COLOR_BLACK);
    display_flush();
}

/* ── LoRa detector ─────────────────────────────────────────────────────── */

static void do_detect_pass(const band_t *f)
{
    detect_pass++;
    /* Same center freq for every SF/BW combo — calibrate once, not 18x. */
    rf_set_frequency(f->start);
    rf_set_modulation(RADIO_MOD_LORA);
    for (int sf = SF_MIN; sf <= SF_MAX; sf++) {
        for (int bw = 0; bw < NBWS; bw++) {
            rf_set_spreading_factor(sf);
            rf_set_bandwidth(BWS_HZ[bw]);
            delay(1000);
            int n = rf_receive(rx_buf, sizeof(rx_buf), RX_TIMEOUT_MS);
            if (n > 0) {
                detect_hits[sf - SF_MIN][bw]++;
            }
        }
    }
}

static void draw_detect(const band_t *f)
{
    display_clear(COLOR_BLACK);

    int hdr_h = SCR_H / 8;
    int ftr_h = SCR_H / 10;

    display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
    display_text(6, 4, "LR2021 LoRa DETECT", COLOR_BLACK);
    display_text(SCR_W / 2 - 30, 4, f->name, COLOR_BLACK);

    char pass[12];
    int p = 0;
    const char *pre = "pass ";
    for (int j = 0; pre[j]; j++) pass[p++] = pre[j];
    int v = detect_pass;
    char t[8]; int ti = 0;
    if (v == 0) t[ti++] = '0';
    while (v > 0 && ti < 7) { t[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti > 0) pass[p++] = t[--ti];
    pass[p] = '\0';
    display_text(SCR_W - 64, 4, pass, COLOR_BLACK);

    /* Grid: rows = SF (7..12), cols = BW. Cell shows hit count (0..9). */
    int gx = 10, gy = hdr_h + 8;
    int cw = 40, ch = 24, gap = 6;
    for (int sf = SF_MIN; sf <= SF_MAX; sf++) {
        int y = gy + (sf - SF_MIN) * (ch + gap);
        char lbl[4];
        lbl[0] = 'S'; lbl[1] = 'F'; lbl[2] = (char)('0' + sf); lbl[3] = '\0';
        display_text(gx, y + 6, lbl, COLOR_WHITE);

        for (int bw = 0; bw < NBWS; bw++) {
            int x = gx + 48 + bw * (cw + gap);
            int hits = detect_hits[sf - SF_MIN][bw];
            uint16_t col = hits > 0 ? (hits >= 3 ? 0xF800u : 0x07FFu) : 0x39E7u;
            display_rect_outline(x, y, cw, ch, col);
            if (hits > 0) {
                char h[2];
                h[0] = (char)('0' + (hits > 9 ? 9 : hits));
                h[1] = '\0';
                display_text(x + cw / 2 - 4, y + 6, h, col);
            }
            /* Cursor cell: filled */
            if (sf == cur_sf && bw == cur_bw) {
                display_rect(x + 2, y + 2, cw - 4, ch - 4, COLOR_WHITE);
                display_text(x + cw / 2 - 4, y + 6, ">>", COLOR_BLACK);
            }
        }
    }

    /* BW legend */
    display_text(gx + 48, gy + (SF_MAX - SF_MIN + 1) * (ch + gap) + 2,
                 "125k    250k    500k", COLOR_WHITE);

    display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
    display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                 "U/D:SF L/R:BW Y:freq X:reset A:sniff B:back", COLOR_BLACK);
    display_flush();
}

/* ── Sniffer ───────────────────────────────────────────────────────────── */

static void sniff_lock(void)
{
    sniff_sf  = cur_sf;
    sniff_bw  = cur_bw;
    sniff_freq = DET_FREQS[g_det_freq_idx].start;
    sniff_total = 0;
    sniff_head = 0;
    sniff_count = 0;
    sniff_sel = 0;

    rf_set_frequency(sniff_freq);
    rf_set_modulation(RADIO_MOD_LORA);
    rf_set_spreading_factor(sniff_sf);
    rf_set_bandwidth(BWS_HZ[sniff_bw]);
}

static void sniff_push(int n)
{
    int slot = sniff_head;
    sniff_ring[slot].len = (uint16_t)(n > RX_BUF_LEN ? RX_BUF_LEN : n);
    for (int i = 0; i < sniff_ring[slot].len; i++) {
        sniff_ring[slot].data[i] = rx_buf[i];
    }
    sniff_head = (sniff_head + 1) % SNIFF_RING;
    if (sniff_count < SNIFF_RING) sniff_count++;
    sniff_sel = 0; /* show newest */
}

static void do_sniff_pass(void)
{
    int n = rf_receive(rx_buf, sizeof(rx_buf), 200);
    if (n > 0) {
        sniff_total++;
        sniff_push(n);
    }
}

static void hex_byte(char *out, uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[(b >> 4) & 0xF];
    out[1] = hex[b & 0xF];
}

static void draw_sniff(void)
{
    display_clear(COLOR_BLACK);

    int hdr_h = SCR_H / 8;
    int ftr_h = SCR_H / 10;

    display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
    display_text(6, 4, "LR2021 SNIFF", COLOR_BLACK);

    char conf[24];
    char f[12];
    fmt_mhz(f, sniff_freq);
    int p = 0;
    for (int j = 0; f[j]; j++) conf[p++] = f[j];
    conf[p++] = 'M'; conf[p++] = ' ';
    conf[p++] = 'S'; conf[p++] = 'F';
    conf[p++] = (char)('0' + sniff_sf);
    conf[p++] = ' ';
    conf[p++] = 'B'; conf[p++] = 'W';
    uint32_t bw = BWS_HZ[sniff_bw] / 1000u;
    char k[5]; int ki = 0;
    if (bw == 0) k[ki++] = '0';
    while (bw > 0 && ki < 4) { k[ki++] = (char)('0' + bw % 10); bw /= 10; }
    while (ki > 0) conf[p++] = k[--ki];
    conf[p] = '\0';
    display_text(SCR_W / 2 - 50, 4, conf, COLOR_BLACK);

    char cnt[12];
    p = 0;
    const char *cpre = "pkt ";
    for (int j = 0; cpre[j]; j++) cnt[p++] = cpre[j];
    int tv = sniff_total;
    char t2[8]; int ti2 = 0;
    if (tv == 0) t2[ti2++] = '0';
    while (tv > 0 && ti2 < 7) { t2[ti2++] = (char)('0' + tv % 10); tv /= 10; }
    while (ti2 > 0) cnt[p++] = t2[--ti2];
    cnt[p] = '\0';
    display_text(SCR_W - 52, 4, cnt, COLOR_BLACK);

    int rssi = rf_get_rssi();
    char rs[8];
    p = 0;
    rs[p++] = 'R'; rs[p++] = 'S'; rs[p++] = 'S'; rs[p++] = 'I';
    rs[p++] = ':'; rs[p++] = ' ';
    if (rssi >= 0) {
        rs[p++] = '?'; rs[p] = '\0';
    } else {
        int rv = rssi;
        if (rv < 0) { rs[p++] = '-'; rv = -rv; }
        char t3[6]; int ti3 = 0;
        if (rv == 0) t3[ti3++] = '0';
        while (rv > 0 && ti3 < 5) { t3[ti3++] = (char)('0' + rv % 10); rv /= 10; }
        while (ti3 > 0) rs[p++] = t3[--ti3];
        rs[p] = '\0';
    }
    display_text(6, hdr_h - 10, rs, COLOR_BLACK);

    /* Packet body: hex rows (8 B each) + ASCII rows. */
    if (sniff_count == 0) {
        display_text(8, hdr_h + 12, "No packets yet — listening...", COLOR_WHITE);
    } else {
        int idx = (sniff_head - 1 - sniff_sel + SNIFF_RING) % SNIFF_RING;
        int len = sniff_ring[idx].len;
        int y = hdr_h + 2;
        char line[30];

        /* offset label */
        display_text(4, y, "off", COLOR_WHITE);
        display_text(30, y, "hex", COLOR_WHITE);
        display_text(150, y, "ascii", COLOR_WHITE);
        y += 10;

        for (int off = 0; off < len && y < SCR_H - ftr_h; off += 8) {
            int row = 0;
            for (int i = off; i < off + 8 && i < len; i++) {
                hex_byte(line + row * 3, sniff_ring[idx].data[i]);
                line[row * 3 + 2] = ' ';
                row++;
            }
            int line_len = row * 3;
            line[line_len] = '\0';
            display_text(4, y, line, COLOR_WHITE);

            /* ASCII column */
            char a[10];
            int ap = 0;
            for (int i = off; i < off + 8 && i < len; i++) {
                uint8_t b = sniff_ring[idx].data[i];
                a[ap++] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
            }
            a[ap] = '\0';
            display_text(150, y, a, COLOR_WHITE);
            y += 12;
        }

        /* packet meta */
        char meta[24];
        p = 0;
        const char *mp = "len ";
        for (int j = 0; mp[j]; j++) meta[p++] = mp[j];
        int lv = len;
        char t4[6]; int ti4 = 0;
        if (lv == 0) t4[ti4++] = '0';
        while (lv > 0 && ti4 < 5) { t4[ti4++] = (char)('0' + lv % 10); lv /= 10; }
        while (ti4 > 0) meta[p++] = t4[--ti4];
        meta[p] = '\0';
        display_text(4, SCR_H - ftr_h - 10, meta, COLOR_WHITE);
    }

    display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
    display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                 "U/D:hist A:save Y:diff X:jam B:detect", COLOR_BLACK);
    display_flush();
}

/* ── Frame diff analyzer ────────────────────────────────────────────────── */

/* Compare two captured frames byte-wise. Constant bytes (XOR=0) across a
 * reused nonce = keystream⊕known-plaintext anchors; differing bytes = the
 * counter/seq field showing through — the keystream-reuse signal. */

#define DIFF_DISP_OFF 0

static int g_diff_sel; /* history index to compare against newest (0=newest) */
static int g_diff_off; /* byte offset scroll */

static int diff_frame_idx(int sel)
{
    return (sniff_head - 1 - sel + SNIFF_RING) % SNIFF_RING;
}

static void draw_diff(void)
{
    display_clear(COLOR_BLACK);

    int hdr_h = SCR_H / 8;
    int ftr_h = SCR_H / 10;

    display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
    display_text(6, 4, "FRAME DIFF", COLOR_BLACK);

    char meta[28];
    int p = 0;
    const char *pre = "new vs -";
    for (int j = 0; pre[j]; j++) meta[p++] = pre[j];
    /* itoa diff sel */
    int v = g_diff_sel;
    char t[8]; int ti = 0;
    if (v == 0) t[ti++] = '0';
    while (v > 0 && ti < 7) { t[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti > 0) meta[p++] = t[--ti];
    meta[p] = '\0';
    display_text(SCR_W - 90, 4, meta, COLOR_BLACK);

    int a_idx = diff_frame_idx(0);
    int b_idx = diff_frame_idx(g_diff_sel);
    int alen = sniff_ring[a_idx].len;
    int blen = sniff_ring[b_idx].len;
    int len  = (alen < blen) ? alen : blen;
    int off  = g_diff_off;

    /* Const-across-ring counter for the visible window. */
    int const_cnt = 0, vis_cnt = 0;
    for (int i = off; i < len && i < off + 48; i++) {
        int is_const = 1;
        for (int r = 0; r < sniff_count; r++) {
            int idx = diff_frame_idx(r);
            if (sniff_ring[idx].len <= i ||
                sniff_ring[idx].data[i] != sniff_ring[a_idx].data[i]) {
                is_const = 0;
                break;
            }
        }
        vis_cnt++;
        if (is_const) const_cnt++;
    }

    char stat[24];
    p = 0;
    const char *sp = "const ";
    for (int j = 0; sp[j]; j++) stat[p++] = sp[j];
    v = const_cnt;
    ti = 0;
    if (v == 0) t[ti++] = '0';
    while (v > 0 && ti < 7) { t[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti > 0) stat[p++] = t[--ti];
    const char *sp2 = "/";
    for (int j = 0; sp2[j]; j++) stat[p++] = sp2[j];
    v = vis_cnt;
    ti = 0;
    if (v == 0) t[ti++] = '0';
    while (v > 0 && ti < 7) { t[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti > 0) stat[p++] = t[--ti];
    stat[p] = '\0';
    display_text(SCR_W - 120, hdr_h - 10, stat, COLOR_BLACK);

    int y = hdr_h + 2;
    char line[30];

    /* Row 1: newest frame bytes */
    display_text(4, y, "A", COLOR_WHITE);
    y += 10;
    for (int row_off = 0; row_off < 48 && off + row_off < len && y < SCR_H - ftr_h; row_off += 8) {
        int row = 0;
        for (int i = 0; i < 8 && off + row_off + i < len; i++) {
            hex_byte(line + row * 3, sniff_ring[a_idx].data[off + row_off + i]);
            line[row * 3 + 2] = ' ';
            row++;
        }
        line[row * 3] = '\0';
        display_text(4, y, line, COLOR_WHITE);
        y += 10;
    }

    /* Row 2: compared frame bytes */
    y += 2;
    display_text(4, y, "B", COLOR_WHITE);
    y += 10;
    for (int row_off = 0; row_off < 48 && off + row_off < len && y < SCR_H - ftr_h; row_off += 8) {
        int row = 0;
        for (int i = 0; i < 8 && off + row_off + i < len; i++) {
            hex_byte(line + row * 3, sniff_ring[b_idx].data[off + row_off + i]);
            line[row * 3 + 2] = ' ';
            row++;
        }
        line[row * 3] = '\0';
        display_text(4, y, line, COLOR_WHITE);
        y += 10;
    }

    /* Row 3: XOR — white where constant, red where differing. */
    y += 2;
    display_text(4, y, "^", COLOR_WHITE);
    y += 10;
    for (int row_off = 0; row_off < 48 && off + row_off < len && y < SCR_H - ftr_h; row_off += 8) {
        int row = 0;
        for (int i = 0; i < 8 && off + row_off + i < len; i++) {
            uint8_t x = sniff_ring[a_idx].data[off + row_off + i] ^
                        sniff_ring[b_idx].data[off + row_off + i];
            hex_byte(line + row * 3, x);
            line[row * 3 + 2] = ' ';
            row++;
        }
        line[row * 3] = '\0';

        /* Per-byte color: render two chars per byte. */
        for (int i = 0; i < row; i++) {
            uint8_t x = sniff_ring[a_idx].data[off + row_off + i] ^
                        sniff_ring[b_idx].data[off + row_off + i];
            uint16_t col = (x == 0) ? COLOR_WHITE : 0xF800u;
            char two[3];
            two[0] = line[i * 3];
            two[1] = line[i * 3 + 1];
            two[2] = '\0';
            display_text(4 + i * 12, y, two, col);
        }
        y += 10;
    }

    display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
    display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                 "U/D:hist L/R:scroll B:back", COLOR_BLACK);
    display_flush();
}

/* ── Jamming ────────────────────────────────────────────────────────────── */

#define JAM_POWER_DBM 17

static void draw_jam(void)
{
    display_clear(COLOR_BLACK);

    int hdr_h = SCR_H / 8;
    int ftr_h = SCR_H / 10;

    display_rect(0, 0, SCR_W, hdr_h, 0xF800u);
    display_text(6, 4, "LR2021 JAMMING", COLOR_BLACK);
    display_text(SCR_W / 2 - 30, 4, "CW carrier", COLOR_BLACK);

    char conf[24];
    char f[12];
    fmt_mhz(f, sniff_freq);
    int p = 0;
    for (int j = 0; f[j]; j++) conf[p++] = f[j];
    conf[p++] = 'M'; conf[p++] = 'H'; conf[p++] = 'z';
    conf[p] = '\0';
    display_text(SCR_W / 2 - 44, hdr_h - 12, conf, COLOR_BLACK);

    int y = hdr_h + 30;
    display_text(8, y, "> TRANSMITTING <", 0xF800u);
    y += 30;
    display_text(8, y, "Jams LoRa/WiFi/BLE on", COLOR_WHITE);
    y += 14;
    display_text(8, y, "this carrier band", COLOR_WHITE);
    y += 28;
    display_text(8, y, "AUTHORIZED USE ONLY", 0xF800u);
    y += 14;
    display_text(8, y, "illegal without permission", 0xF800u);

    display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
    display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                 "X/B:stop jam", COLOR_BLACK);
    display_flush();
}

static void jam_start(void)
{
    rf_set_frequency(sniff_freq);
    rf_set_power(JAM_POWER_DBM);
    rf_tx_cw_start();
}

static void jam_stop(void)
{
    rf_tx_cw_stop();
    rf_set_modulation(RADIO_MOD_LORA);
    rf_set_spreading_factor(sniff_sf);
    rf_set_bandwidth(BWS_HZ[sniff_bw]);
}

/* ── Auto lock pipeline ────────────────────────────────────────────────── */

/* Sync-word candidates: common LoRa values first, then full 0x00–0xFF. */
static const uint8_t SYNC_COMMON[] = { 0x12, 0x34, 0x56, 0xAB, 0x2C, 0x4B, 0x8E, 0xE3 };
#define SYNC_COMMON_N ((int)(sizeof(SYNC_COMMON) / sizeof(SYNC_COMMON[0])))

#define AUTO_SWEEP  0
#define AUTO_DETECT 1
#define AUTO_SYNC   2

static int g_auto_stage;
static int g_auto_step;    /* progress within stage */
static int g_auto_total;
static int g_sync_candidate; /* -1..255: -1 = still in common list */

static void draw_loading(const char *stage, int step, int total)
{
    display_clear(COLOR_BLACK);

    int hdr_h = SCR_H / 8;
    int ftr_h = SCR_H / 10;

    display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
    display_text(6, 4, "LR2021 AUTO LOCK", COLOR_BLACK);

    int y = hdr_h + 20;
    display_text(8, y, stage, COLOR_WHITE);
    y += 30;

    /* Progress bar */
    int w = (total > 0) ? (int)((uint32_t)(SCR_W - 32) * (uint32_t)step / (uint32_t)total) : 0;
    if (w > SCR_W - 32) w = SCR_W - 32;
    display_rect_outline(16, y, SCR_W - 32, 12, COLOR_WHITE);
    if (w > 0) display_rect(16, y, w, 12, 0x07FFu);
    y += 28;

    char pct[16];
    int p = 0;
    const char *pre = "step ";
    for (int j = 0; pre[j]; j++) pct[p++] = pre[j];
    int v = step;
    char t[8]; int ti = 0;
    if (v == 0) t[ti++] = '0';
    while (v > 0 && ti < 7) { t[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti > 0) pct[p++] = t[--ti];
    const char *mid = " / ";
    for (int j = 0; mid[j]; j++) pct[p++] = mid[j];
    v = total;
    ti = 0;
    if (v == 0) t[ti++] = '0';
    while (v > 0 && ti < 7) { t[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti > 0) pct[p++] = t[--ti];
    pct[p] = '\0';
    display_text(16, y, pct, COLOR_WHITE);
    y += 26;

    /* Current freq/SF/BW/sync status */
    char st[28];
    p = 0;
    char f[12];
    fmt_mhz(f, sniff_freq);
    for (int j = 0; f[j]; j++) st[p++] = f[j];
    st[p++] = 'M'; st[p++] = ' ';
    if (g_auto_stage >= AUTO_DETECT) {
        st[p++] = 'S'; st[p++] = 'F';
        st[p++] = (char)('0' + sniff_sf);
        st[p++] = ' ';
    }
    if (g_auto_stage >= AUTO_SYNC && g_sync_candidate >= 0) {
        st[p++] = 'S'; st[p++] = 'Y'; st[p++] = 'N'; st[p++] = 'C';
        st[p++] = ' ';
        st[p++] = '0'; st[p++] = 'x';
        static const char hex[] = "0123456789ABCDEF";
        st[p++] = hex[(g_sync_candidate >> 4) & 0xF];
        st[p++] = hex[g_sync_candidate & 0xF];
    }
    st[p] = '\0';
    display_text(8, y, st, COLOR_WHITE);

    display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
    display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                 "B:cancel auto", COLOR_BLACK);
    display_flush();
}

/* Stage 0: find the strongest occupied frequency in the wide LoRa band. */
static void auto_sweep(void)
{
    const band_t *b = &BANDS[0];
    int best_i = 0, best_r = RSSI_MIN;
    g_auto_total = NBINS;

    for (int i = 0; i < NBINS; i++) {
        rf_set_frequency(bin_freq(b, i));
        delay(1000);
        int r = clampi(rf_get_rssi(), RSSI_MIN, RSSI_MAX);
        cur[i] = r;
        if (r > peak[i]) peak[i] = r;
        if (r > best_r) { best_r = r; best_i = i; }
        g_auto_step = i + 1;
        draw_loading("SWEEPING 863-871...", g_auto_step, g_auto_total);
    }

    sniff_freq = bin_freq(b, best_i);
    g_auto_stage = AUTO_DETECT;
    g_auto_step  = 0;
    g_auto_total = (SF_MAX - SF_MIN + 1) * NBWS;
}

/* Stage 1: find which SF/BW the traffic uses, keep strongest count.
 * Tries the two dominant sync words (0x12 Semtech private, 0x34 LoRaWAN
 * public) per config — if traffic matches one, we've also found the sync. */
static void auto_detect(void)
{
    static const uint8_t try_sync[2] = { 0x12, 0x34 };
    int best_hits = -1, best_sf = SF_MIN, best_bw = 0, best_sync = 0x12;

    rf_set_frequency(sniff_freq);
    rf_set_modulation(RADIO_MOD_LORA);
    g_sync_candidate = -1;

    g_auto_total = (SF_MAX - SF_MIN + 1) * NBWS * 2;
    for (int sf = SF_MIN; sf <= SF_MAX; sf++) {
        for (int bw = 0; bw < NBWS; bw++) {
            rf_set_spreading_factor(sf);
            rf_set_bandwidth(BWS_HZ[bw]);
            delay(1000);
            int hits[2] = { 0, 0 };
            for (int s = 0; s < 2; s++) {
                rf_set_sync_word(try_sync[s]);
                int n = rf_receive(rx_buf, sizeof(rx_buf), RX_TIMEOUT_MS);
                if (n > 0) hits[s]++;
                detect_hits[sf - SF_MIN][bw] += (n > 0) ? 1 : 0;
                g_auto_step++;
                draw_loading("LOCKING SF/BW/SYNC...", g_auto_step, g_auto_total);
            }
            int h = hits[0] + hits[1];
            if (h > best_hits) {
                best_hits = h;
                best_sf = sf; best_bw = bw;
                best_sync = (hits[1] > hits[0]) ? try_sync[1] : try_sync[0];
            }
        }
    }

    sniff_sf  = best_sf;
    sniff_bw  = best_bw;
    if (best_hits > 0) {
        /* Sync already found — skip full brute force. */
        rf_set_sync_word(best_sync);
        g_auto_stage = 3;
    } else {
        g_auto_stage = AUTO_SYNC;
        g_auto_step  = 0;
        g_auto_total = SYNC_COMMON_N + 256;
        g_sync_candidate = -1;
    }
}

/* Stage 2: brute-force the 8-bit sync word — wrong sync yields no packets
 * (HW SYNC_FAIL), matching sync delivers, so first hit = the network's sync. */
static void auto_sync(void)
{
    rf_set_frequency(sniff_freq);
    rf_set_modulation(RADIO_MOD_LORA);
    rf_set_spreading_factor(sniff_sf);
    rf_set_bandwidth(BWS_HZ[sniff_bw]);

    /* Pass A: common syncs, dwell ≥ 2× packet period (1.3–3s cadence) so
     * a matching sync is guaranteed a reception. 8 × 4s ≈ 32s. */
    g_auto_total = SYNC_COMMON_N + 256;
    g_auto_step  = 0;

    for (int i = 0; i < SYNC_COMMON_N; i++) {
        uint8_t sync = SYNC_COMMON[i];
        g_sync_candidate = sync;

        if (rf_set_sync_word(sync) != 0) continue;

        int n = rf_receive(rx_buf, sizeof(rx_buf), 4000);
        g_auto_step++;
        draw_loading("BRUTE-FORCING SYNC...", g_auto_step, g_auto_total);
        if (n > 0) {
            sniff_total = 0;
            sniff_head  = 0;
            sniff_count = 0;
            sniff_sel   = 0;
            sniff_push(n);
            g_auto_stage = 3; /* done */
            return;
        }
    }

    /* Pass B: full 0x00–0xFF INCLUDING the commons again — a sparse sender
     * can miss a single window, so every value gets a fair re-try. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 256; i++) {
            uint8_t sync = (uint8_t)i;
            g_sync_candidate = sync;

            if (rf_set_sync_word(sync) != 0) continue;

            int n = rf_receive(rx_buf, sizeof(rx_buf), 800);
            g_auto_step++;
            draw_loading("BRUTE-FORCING SYNC...", g_auto_step, g_auto_total);
            if (n > 0) {
                sniff_total = 0;
                sniff_head  = 0;
                sniff_count = 0;
                sniff_sel   = 0;
                sniff_push(n);
                g_auto_stage = 3; /* done */
                return;
            }
        }
    }
    /* No sync found — reset to the driver default (0x12) rather than
     * leaving the chip on the last brute candidate (0xFF). */
    rf_set_sync_word(0x12);
    g_auto_stage = 3;
}

/* ── Main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    if (rf_select(AKIRA_RF_CHIP_LR2021) < 0) {
        display_clear(COLOR_BLACK);
        display_text(8, SCR_H / 2 - 8, "LR2021 not available", COLOR_WHITE);
        display_flush();
        while (!AKIRA_BTN_PRESSED((uint32_t)input_get_buttons(), AKIRA_BTN_B)) {
            delay(50000);
        }
        return -1;
    }

    int band = 0; /* spectrum band index */
    g_det_freq_idx = 0;
    sniff_freq = BANDS[0].start;

    /* Boot straight into AUTO: sweep → lock freq → SF/BW → sync brute → sniff */
    g_mode = 4;
    g_auto_stage = AUTO_SWEEP;
    g_auto_step  = 0;

    reset_peaks();
    for (int sf = 0; sf <= SF_MAX - SF_MIN; sf++) {
        for (int bw = 0; bw < NBWS; bw++) detect_hits[sf][bw] = 0;
    }

    /* Radio calls block the loop for up to seconds — polling held-state
     * misses press+release that happen mid-block.  Drain the firmware's
     * edge-event queue instead: every press/release is queued in the ISR
     * and survives the block.  g_edges accumulates them as an edge mask. */
    int prev = 0;
    uint32_t g_edges = 0;

    while (1) {
        /* Drain all queued input events → reliable edges despite blocking.
         * ABI is (*~)i = ptr + explicit length (SDK decl hides the len arg). */
        akira_input_event_t ev;
        while (input_poll_event(&ev, sizeof(ev)) == 1) {
            uint32_t bit = (1u << ev.button_id);
            if (ev.pressed) {
                g_edges |= bit;
            } else {
                g_edges &= ~bit;
            }
            /* Debug: log raw zephyr,code per edge so wiring is diagnosable. */
            char dbg[24];
            int p = 0;
            dbg[p++] = 'I'; dbg[p++] = 'N'; dbg[p++] = ' ';
            /* code-2 index: 2=UP 3=DN 4=LT 5=RT 6=A 7=Y 8=B 9=X */
            const char *names[] = { "UP", "DN", "LT", "RT",
                                    "A", "Y", "B", "X" };
            uint32_t c = ev.button_id;
            if (c >= 2 && c <= 9) {
                for (int j = 0; names[c - 2][j]; j++) dbg[p++] = names[c - 2][j];
            } else {
                dbg[p++] = '#';
                char t[6]; int ti = 0;
                if (c == 0) t[ti++] = '0';
                while (c > 0 && ti < 5) { t[ti++] = (char)('0' + c % 10); c /= 10; }
                while (ti > 0) dbg[p++] = t[--ti];
            }
            dbg[p++] = ev.pressed ? '+' : '-';
            dbg[p] = '\0';
            printf_native(dbg);
        }
        int pressed = g_edges;
        g_edges = 0; /* edges are one-shot */

        if (g_mode == 4) {
            /* AUTO: run the pipeline, B cancels to spectrum. */
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
                g_mode = 0;
                continue;
            }
            if (g_auto_stage == AUTO_SWEEP) {
                auto_sweep();
                continue;
            }
            if (g_auto_stage == AUTO_DETECT) {
                auto_detect();
                continue;
            }
            if (g_auto_stage == AUTO_SYNC) {
                auto_sync();
                continue;
            }
            /* Done → drop into SNIFF on the locked config. */
            g_mode = 2;
            sniff_total = 0;
            sniff_head  = 0;
            sniff_count = 0;
            sniff_sel   = 0;
            continue;
        }

        if (g_mode == 0 && AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            return 0; /* exit app only from SPECTRUM */
        }

        if (g_mode == 2) {
            /* SNIFF: history nav, back to detect */
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
                g_mode = 1;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP) && sniff_sel < sniff_count - 1) {
                sniff_sel++;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN) && sniff_sel > 0) {
                sniff_sel--;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_Y) && sniff_count >= 2) {
                g_diff_sel = 0;
                g_diff_off = 0;
                g_mode = 5;
                continue;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A) && sniff_count > 0) {
                /* Dump the whole ring to the SD sandbox for offline analysis. */
                int fd = storage_open("lora_capture.bin", STORAGE_O_WRITE);
                if (fd >= 0) {
                    for (int r = 0; r < sniff_count; r++) {
                        int idx = diff_frame_idx(r);
                        uint8_t hdr[2] = {
                            (uint8_t)(sniff_ring[idx].len >> 8),
                            (uint8_t)(sniff_ring[idx].len & 0xFF),
                        };
                        storage_write(fd, hdr, 2);
                        storage_write(fd, sniff_ring[idx].data,
                                      sniff_ring[idx].len);
                    }
                    storage_close(fd);
                    char msg[20];
                    int p = 0;
                    const char *pre = "SAVED ";
                    for (int j = 0; pre[j]; j++) msg[p++] = pre[j];
                    int v = sniff_count;
                    char t[6]; int ti = 0;
                    if (v == 0) t[ti++] = '0';
                    while (v > 0 && ti < 5) { t[ti++] = (char)('0' + v % 10); v /= 10; }
                    while (ti > 0) msg[p++] = t[--ti];
                    msg[p] = '\0';
                    printf_native(msg);
                } else {
                    printf_native("SAVE FAIL");
                }
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_X)) {
                jam_start();
                g_mode = 3;
            }
            do_sniff_pass();
            draw_sniff();
            continue;
        }

        if (g_mode == 5) {
            /* DIFF: compare newest vs history frame */
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
                g_mode = 2;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP) &&
                g_diff_sel < sniff_count - 1) {
                g_diff_sel++;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN) && g_diff_sel > 0) {
                g_diff_sel--;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT) && g_diff_off >= 8) {
                g_diff_off -= 8;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT)) {
                g_diff_off += 8;
            }
            draw_diff();
            delay(20000); /* SPI2 shared with LR2021 — throttle redraw */
            continue;
        }

        if (g_mode == 3) {
            /* JAM: redraw live (blink-free), stop on X/B.
             * Display shares SPI2 with LR2021 — throttle redraw or the
             * frame push starves the radio + shell rf commands. */
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_X) ||
                AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
                jam_stop();
                g_mode = 2;
            }
            draw_jam();
            delay(20000);
            continue;
        }

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
            if (g_mode == 0) {
                reset_peaks();
            } else if (g_mode == 1) {
                sniff_lock();
                g_mode = 2;
            }
        }

        if (g_mode == 0) {
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP)) {
                g_mode = 1;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN)) {
                g_mode = 1;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT)) {
                band = (band + NBANDS - 1) % NBANDS;
                reset_peaks();
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT)) {
                band = (band + 1) % NBANDS;
                reset_peaks();
            }
        } else if (g_mode == 1) {
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
                g_mode = 0; /* back to SPECTRUM */
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP) && cur_sf < SF_MAX) {
                cur_sf++;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN) && cur_sf > SF_MIN) {
                cur_sf--;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT) && cur_bw > 0) {
                cur_bw--;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT) && cur_bw < NBWS - 1) {
                cur_bw++;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_Y)) {
                g_det_freq_idx = (g_det_freq_idx + 1) % NDET;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_X)) {
                detect_pass = 0;
                for (int sf = 0; sf <= SF_MAX - SF_MIN; sf++) {
                    for (int bw = 0; bw < NBWS; bw++) detect_hits[sf][bw] = 0;
                }
            }
            /* Keep sniff lock in sync with cursor while in detect */
            sniff_sf = cur_sf;
            sniff_bw = cur_bw;
        }

        /* One full pass per loop iteration. */
        if (g_mode == 0) {
            do_spectrum_pass(&BANDS[band]);
            draw_spectrum(&BANDS[band]);
        } else {
            do_detect_pass(&DET_FREQS[g_det_freq_idx]);
            draw_detect(&DET_FREQS[g_det_freq_idx]);
        }
    }
    return 0;
}
