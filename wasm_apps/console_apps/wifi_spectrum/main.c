/*
 * wifi_spectrum — Real-time 2.4 GHz spectrum analyzer for AkiraOS
 *
 * Continuously sweeps WiFi channels 1-14 using wifi_scan_rssi() and
 * displays a real-time RSSI heatmap. Shows busiest and quietest
 * channels, auto-refreshes every ~250ms.
 *
 * Controls:
 *   A          — pause / resume scanning
 *   B          — exit
 *   UP / DOWN  — adjust refresh speed (SLOW / NORMAL / FAST)
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stdint.h>

/* ── Display ────────────────────────────────────────────────────────────── */

static int32_t DW = 320;
static int32_t DH = 240;
#define HDR_H    16
#define FOT_H    14
#define FOT_Y    (DH - FOT_H)

/* ── Colors ─────────────────────────────────────────────────────────────── */

#define COL_BG   CONSOLE_COLOR_BG
#define COL_TXT  CONSOLE_COLOR_TEXT
#define COL_DIM  CONSOLE_COLOR_DIM
#define COL_ACC  CONSOLE_COLOR_ACCENT
#define COL_OK   CONSOLE_COLOR_OK
#define COL_WARN CONSOLE_COLOR_WARN
#define COL_ERR  CONSOLE_COLOR_ERR
#define COL_SEP  CONSOLE_COLOR_SEP

/* ── Spectrum constants ────────────────────────────────────────────────── */

#define NUM_CHANNELS    14
#define CH_MIN          1
#define CH_MAX          14

/* Bar layout */
#define BAR_LEFT        4
#define BAR_WIDTH       20      /* per-channel column */
#define BAR_GAP         2       /* gap between columns */
#define BAR_STEP        (BAR_WIDTH + BAR_GAP)
#define BAR_MAX_H       (DH - HDR_H - FOT_H - 24)  /* max bar height in px */

/* RSSI range for display */
#define RSSI_MIN        -100
#define RSSI_MAX        -20
#define RSSI_RANGE      (RSSI_MAX - RSSI_MIN) /* 80 dB */

/* Speed presets (delay between scans in microseconds) */
#define SPEED_SLOW      500000
#define SPEED_NORMAL    250000
#define SPEED_FAST      80000

/* ── Globals ─────────────────────────────────────────────────────────────── */

static int8_t  g_rssi[NUM_CHANNELS];       /* latest RSSI per channel */
static int8_t  g_rssi_peak[NUM_CHANNELS];  /* max RSSI seen per channel */
static int8_t  g_rssi_min_ch[NUM_CHANNELS]; /* min RSSI per channel */
static int     g_paused;
static int     g_speed_idx;    /* 0=slow, 1=normal, 2=fast */
static int     g_scan_count;
static int     g_needs_redraw;

/* Speed labels */
static const char *g_speed_labels[] = { "SLOW", "NORM", "FAST" };
static const int   g_speed_vals[]   = { SPEED_SLOW, SPEED_NORMAL, SPEED_FAST };

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void my_itoa(int v, char *buf, int buflen)
{
    if (buflen < 2) return;
    if (v < 0) { buf[0] = '-'; v = -v; buf++; buflen--; }
    char tmp[12];
    int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    while (v > 0 && n < 11) { tmp[n++] = '0' + v % 10; v /= 10; }
    int out = n < buflen - 1 ? n : buflen - 1;
    for (int i = 0; i < out; i++) buf[i] = tmp[n - 1 - i];
    buf[out] = '\0';
}

static void draw_hdr(void)
{
    display_rect(0, 0, DW, HDR_H, COL_BG);
    display_text(4, 3, "SPECTRUM", COL_TXT);

    /* Speed indicator */
    display_text(DW - 80, 3, g_speed_labels[g_speed_idx], g_paused ? COL_DIM : COL_ACC);

    /* Scan count */
    char sc[8]; my_itoa(g_scan_count, sc, sizeof(sc));
    display_text(DW - 48, 3, sc, COL_DIM);

    /* Pause indicator */
    if (g_paused) {
        display_text(80, 3, "PAUSED", COL_WARN);
    }
}

static void draw_footer(void)
{
    display_rect(0, FOT_Y, DW, FOT_H, COL_BG);
    display_text(4, FOT_Y + 2, "[A]Pause [UP/DN]Speed [B]Exit", COL_DIM);
}

/* ── Find busiest / quietest channels ────────────────────────────────────── */

static int find_extreme(int find_max)
{
    int idx = 0;
    int8_t val = find_max ? -128 : 127;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (g_rssi[i] == -128) continue; /* no data */
        if (find_max) {
            if (g_rssi[i] > val) { val = g_rssi[i]; idx = i; }
        } else {
            if (g_rssi[i] < val) { val = g_rssi[i]; idx = i; }
        }
    }
    return idx;
}

/* ── Map RSSI to color ───────────────────────────────────────────────────── */

static uint32_t rssi_color(int8_t rssi)
{
    if (rssi < -90) return COL_BG;
    if (rssi < -80) return 0x0020U;   /* very dim green */
    if (rssi < -70) return 0x0480U;   /* dim green */
    if (rssi < -60) return COL_OK;    /* green */
    if (rssi < -50) return COL_WARN;  /* orange */
    return COL_ERR;                    /* red — strong signal */
}

/* ── Draw main spectrum view ─────────────────────────────────────────────── */

static void draw_spectrum(void)
{
    draw_hdr();

    int content_y = HDR_H;
    int content_h = FOT_Y - HDR_H;

    /* Clear */
    display_rect(0, content_y, DW, content_h, COL_BG);

    /* Busy / quiet labels at top */
    int busy_ch = find_extreme(1) + CH_MIN;
    int quiet_ch = find_extreme(0) + CH_MIN;

    char stat[24];
    int sp = 0;
    stat[sp++]='B'; stat[sp++]='u'; stat[sp++]='s'; stat[sp++]='y';
    stat[sp++]=':'; stat[sp++]=' '; stat[sp++]='c'; stat[sp++]='h';
    char bc[4]; my_itoa(busy_ch, bc, sizeof(bc));
    for (int j = 0; bc[j]; j++) stat[sp++] = bc[j];
    stat[sp++]=' '; stat[sp++]='|'; stat[sp++]=' ';
    stat[sp++]='Q'; stat[sp++]='u'; stat[sp++]='i'; stat[sp++]='e';
    stat[sp++]='t'; stat[sp++]=':'; stat[sp++]=' '; stat[sp++]='c';
    stat[sp++]='h';
    char qc[4]; my_itoa(quiet_ch, qc, sizeof(qc));
    for (int j = 0; qc[j]; j++) stat[sp++] = qc[j];
    stat[sp] = '\0';
    display_text(4, content_y + 2, stat, COL_DIM);

    int bar_area_y = content_y + 14;
    int bar_area_h = content_h - 14;

    /* Channel bars */
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        int8_t rssi = g_rssi[ch];
        int x = BAR_LEFT + ch * BAR_STEP;

        /* Channel number label below bar */
        char lbl[4];
        my_itoa(ch + CH_MIN, lbl, sizeof(lbl));
        display_text(x + 2, bar_area_y + BAR_MAX_H + 2, lbl, COL_DIM);

        if (rssi == -128) continue; /* no data yet */

        /* Bar height proportional to RSSI */
        int clamped = rssi < RSSI_MIN ? RSSI_MIN : (rssi > RSSI_MAX ? RSSI_MAX : rssi);
        int bar_h = (int)((int32_t)(clamped - RSSI_MIN) * BAR_MAX_H / RSSI_RANGE);
        if (bar_h < 1) bar_h = 1;
        int bar_y = bar_area_y + BAR_MAX_H - bar_h;

        uint32_t color = rssi_color(rssi);
        display_rect(x, bar_y, BAR_WIDTH - 2, bar_h, color);
    }

    /* Peak markers (small dots above bars) */
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        if (g_rssi_peak[ch] == -128) continue;
        int x = BAR_LEFT + ch * BAR_STEP + (BAR_WIDTH - 2) / 2;
        int clamped = g_rssi_peak[ch] < RSSI_MIN ? RSSI_MIN : (g_rssi_peak[ch] > RSSI_MAX ? RSSI_MAX : g_rssi_peak[ch]);
        int peak_h = (int)((int32_t)(clamped - RSSI_MIN) * BAR_MAX_H / RSSI_RANGE);
        if (peak_h < 1) peak_h = 1;
        int px = x;
        int py = bar_area_y + BAR_MAX_H - peak_h - 2;
        display_rect(px - 1, py, 3, 2, COL_ACC);
    }

    /* RSSI scale labels on right */
    char scale[6];
    my_itoa(RSSI_MAX, scale, sizeof(scale));
    display_text(DW - 24, bar_area_y, scale, COL_DIM);
    my_itoa(RSSI_MIN, scale, sizeof(scale));
    display_text(DW - 28, bar_area_y + BAR_MAX_H - 8, scale, COL_DIM);

    draw_footer();
}

/* ── Perform scan ────────────────────────────────────────────────────────── */

static void do_scan(void)
{
    int8_t buf[14];
    int ret = wifi_scan_rssi(buf, sizeof(buf));
    if (ret != 14) return;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        g_rssi[i] = buf[i];
        if (buf[i] != -128) {
            if (g_rssi_peak[i] == -128 || buf[i] > g_rssi_peak[i]) {
                g_rssi_peak[i] = buf[i];
            }
        }
    }
    g_scan_count++;
    g_needs_redraw = 1;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    display_get_size(&DW, &DH);

    int prev_btns = 0;

    /* Init: no data */
    for (int i = 0; i < NUM_CHANNELS; i++) {
        g_rssi[i] = -128;
        g_rssi_peak[i] = -128;
    }
    g_speed_idx = 1; /* NORMAL */
    g_paused = 0;
    g_scan_count = 0;
    g_needs_redraw = 1;

    int scan_timer = 0;
    int scan_interval = g_speed_vals[g_speed_idx];

    while (1) {
        /* ── Scan tick ── */
        if (!g_paused) {
            scan_timer += 20000;
            if (scan_timer >= scan_interval) {
                scan_timer = 0;
                do_scan();
            }
        }

        /* ── Input ── */
        int btns    = input_get_buttons();
        int pressed = btns & ~prev_btns;
        prev_btns   = btns;

        if (pressed) g_needs_redraw = 1;

        if (pressed & AKIRA_BTN_A) {
            g_paused = !g_paused;
        }
        if (pressed & AKIRA_BTN_UP) {
            if (g_speed_idx < 2) g_speed_idx++;
            scan_interval = g_speed_vals[g_speed_idx];
        }
        if (pressed & AKIRA_BTN_DOWN) {
            if (g_speed_idx > 0) g_speed_idx--;
            scan_interval = g_speed_vals[g_speed_idx];
        }
        if (pressed & AKIRA_BTN_B) {
            return 0; /* exit to shell */
        }

        /* ── Redraw ── */
        if (g_needs_redraw) {
            draw_spectrum();
            display_flush();
            g_needs_redraw = 0;
        }

        delay(20000);
    }

    return 0;
}
