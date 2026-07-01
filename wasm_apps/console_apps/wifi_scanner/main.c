/**
 * @file main.c
 * @brief wifi.scanner — Passive 802.11 AP scanner for AkiraOS
 *
 * Sweeps all 2.4 GHz channels via the ESP32 built-in WiFi radio (no
 * association, no auth) and renders a live list of visible access points.
 *
 * List view (UP/DOWN to scroll, A to open detail, Y to re-scan):
 *   CH  SSID                   SEC   signal
 *   06  MyHomeNetwork          WPA2  ▁▃▅▇█
 *   11  OfficeAP               WPA3  ▁▃▅▇░
 *   01  OpenHotspot            OPEN  ▁▃░░░
 *   ...
 *   ┄ channel-utilization histogram (ch 1–13) ┄
 *
 * Detail view (B to go back):
 *   SSID, BSSID, channel + MHz, RSSI, security, last-seen delta
 *
 * Controls:
 *   UP / DOWN    scroll list
 *   A            open detail view
 *   B            back to list (from detail)
 *   Y            trigger new passive scan
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stddef.h>   /* NULL */

/* ── Layout ──────────────────────────────────────────────────────────── */
#define SCR_W   CONSOLE_WIDTH     /* 320 */
#define SCR_H   CONSOLE_HEIGHT    /* 240 */

#define HDR_Y    0
#define HDR_H   20
#define LIST_Y  20                /* first list row */
#define ROW_H   20                /* pixels per row */
#define VROWS    7                /* visible rows in list */
#define LIST_H  (VROWS * ROW_H)  /* 140 */
#define HIST_Y  (LIST_Y + LIST_H + 2)   /* 162 */
#define HIST_H  36                       /* bar area height */
#define FOOT_Y  (SCR_H - HDR_H)         /* 220 */
#define FOOT_H  HDR_H                    /* 20 */

/* ── Colors (re-use console theme) ──────────────────────────────────── */
#define COL_BG      CONSOLE_COLOR_BG        /* 0x0000 black       */
#define COL_HDR     CONSOLE_COLOR_HEADER    /* 0x2104 dark gray   */
#define COL_SEP     CONSOLE_COLOR_SEP       /* 0x39E7 separator   */
#define COL_ACCENT  CONSOLE_COLOR_ACCENT    /* 0x07FF cyan        */
#define COL_TEXT    CONSOLE_COLOR_TEXT      /* 0xFFFF white       */
#define COL_DIM     CONSOLE_COLOR_DIM       /* 0x7BEF gray        */
#define COL_SEL     CONSOLE_COLOR_SEL_BG    /* 0x001F blue        */
#define COL_OK      CONSOLE_COLOR_OK        /* 0x07E0 green       */
#define COL_WARN    CONSOLE_COLOR_WARN      /* 0xFD20 orange      */
#define COL_ERR     CONSOLE_COLOR_ERR       /* 0xF800 red         */

/* ── Column X positions ──────────────────────────────────────────────── */
#define COL_CH       2    /* channel  "06"       2 chars */
#define COL_SSID    22    /* SSID  up to 22 chars        */
#define COL_SEC    200    /* security  "WPA2"  4 chars   */
#define COL_BARS   236    /* 5 signal bars  (5×10px=50)  */

/* ── Histogram geometry ──────────────────────────────────────────────── */
/* 13 channels, 24px per channel (18px bar + 6px gap). Left margin 4px. */
#define HIST_CH_W   24
#define HIST_BAR_W  18
#define HIST_MAX_H  (HIST_H - 12)   /* reserve 12px for label row */
#define HIST_LBL_Y  (HIST_Y + HIST_MAX_H + 2)

/* ── State machine ───────────────────────────────────────────────────── */
#define STATE_SCANNING  0
#define STATE_LIST      1
#define STATE_DETAIL    2

/* ── Max APs ─────────────────────────────────────────────────────────── */
#define MAX_APS 64

/* ── State ───────────────────────────────────────────────────────────── */
static akira_wifi_ap_t g_aps[MAX_APS];
static int     g_ap_count;
static int     g_scroll;   /* index of first visible row */
static int     g_cursor;   /* absolute selected index */
static uint8_t g_state;
static int     g_tmr;

/* Button edge detection */
static uint32_t g_prev_btns;

/* ── String helpers ──────────────────────────────────────────────────── */

static void sncopy(char *dst, const char *src, int maxch)
{
    int i = 0;
    while (i < maxch - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void fmt_bssid(char *buf, const uint8_t *mac)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        buf[i*3]     = hex[(mac[i] >> 4) & 0xF];
        buf[i*3 + 1] = hex[mac[i] & 0xF];
        buf[i*3 + 2] = (i < 5) ? ':' : '\0';
    }
}

/* Write decimal @val into buf; return chars written. */
static int fmt_dec(char *buf, int val)
{
    if (val < 0) { *buf++ = '-'; return 1 + fmt_dec(buf, -val); }
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12]; int n = 0;
    while (val) { tmp[n++] = '0' + val % 10; val /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

/* Zero-padded 2-digit decimal. */
static void fmt_dec2(char *buf, int val)
{
    buf[0] = '0' + (val / 10) % 10;
    buf[1] = '0' + val % 10;
    buf[2] = '\0';
}

/* ── Security label ──────────────────────────────────────────────────── */

static const char *sec_label(uint8_t s)
{
    switch (s) {
    case WIFI_SEC_OPEN: return "OPEN";
    case WIFI_SEC_WEP:  return "WEP ";
    case WIFI_SEC_WPA:  return "WPA ";
    case WIFI_SEC_WPA2: return "WPA2";
    case WIFI_SEC_WPA3: return "WPA3";
    default:            return "ENT ";
    }
}

static uint32_t sec_color(uint8_t s)
{
    switch (s) {
    case WIFI_SEC_OPEN: return COL_ERR;   /* red — unencrypted */
    case WIFI_SEC_WEP:  return COL_WARN;  /* orange — weak     */
    default:            return COL_OK;    /* green — WPA+      */
    }
}

/* ── RSSI helpers ────────────────────────────────────────────────────── */

static int rssi_bars(int8_t r)
{
    if (r >= -50) return 5;
    if (r >= -60) return 4;
    if (r >= -70) return 3;
    if (r >= -80) return 2;
    if (r >= -90) return 1;
    return 0;
}

static uint32_t rssi_color(int8_t r)
{
    if (r >= -60) return COL_OK;
    if (r >= -75) return COL_WARN;
    return COL_ERR;
}

/*
 * Draw 5 signal-strength bars at (x, y_row).
 * Each bar is 7px wide, bottom-aligned within the row.
 */
static void draw_signal_bars(int x, int y_row, int8_t rssi)
{
    int bars = rssi_bars(rssi);
    uint32_t col = rssi_color(rssi);
    static const uint8_t bar_h[5] = {4, 6, 9, 12, 15};

    for (int i = 0; i < 5; i++) {
        uint32_t c = (i < bars) ? col : COL_SEP;
        int bh = bar_h[i];
        display_rect(x + i * 10, y_row + ROW_H - 2 - bh, 7, bh, c);
    }
}

/* ── Sort by RSSI descending (insertion sort) ────────────────────────── */

static void sort_aps(void)
{
    for (int i = 1; i < g_ap_count; i++) {
        akira_wifi_ap_t key = g_aps[i];
        int j = i - 1;
        while (j >= 0 && g_aps[j].rssi < key.rssi) {
            g_aps[j + 1] = g_aps[j];
            j--;
        }
        g_aps[j + 1] = key;
    }
}

/* ── Channel histogram ───────────────────────────────────────────────── */

static void draw_histogram(void)
{
    /* Count APs per channel */
    uint8_t cnt[14] = {0};
    for (int i = 0; i < g_ap_count; i++) {
        uint8_t ch = g_aps[i].channel;
        if (ch >= 1 && ch <= 13) cnt[ch]++;
    }

    /* Find max for scaling */
    uint8_t mx = 1;
    for (int i = 1; i <= 13; i++) if (cnt[i] > mx) mx = cnt[i];

    for (int ch = 1; ch <= 13; ch++) {
        int xb = 4 + (ch - 1) * HIST_CH_W;

        /* Highlight non-overlapping channels 1, 6, 11 in accent */
        uint32_t col = (ch == 1 || ch == 6 || ch == 11)
                       ? COL_ACCENT : COL_DIM;
        if (cnt[ch] == 0) col = COL_SEP;

        int bh = (cnt[ch] > 0) ? ((int)cnt[ch] * HIST_MAX_H / mx) : 0;
        if (bh < 2 && cnt[ch] > 0) bh = 2;

        /* Bar outline */
        display_rect_outline(xb, HIST_Y, HIST_BAR_W, HIST_MAX_H, COL_SEP);

        /* Fill from bottom */
        if (bh > 0) {
            display_rect(xb, HIST_Y + HIST_MAX_H - bh, HIST_BAR_W, bh, col);
        }

        /* AP count label inside bar when > 0 */
        if (cnt[ch] > 0) {
            char n[3];
            n[0] = '0' + cnt[ch];
            n[1] = '\0';
            display_text(xb + 5, HIST_Y + HIST_MAX_H - bh, n, COL_BG);
        }

        /* Channel number below bar */
        char lbl[3];
        fmt_dec2(lbl, ch);
        int lx = (ch < 10) ? xb + 5 : xb + 1;
        display_text(lx, HIST_LBL_Y, (ch < 10) ? lbl + 1 : lbl, COL_DIM);
    }
}

/* ── Header bar ──────────────────────────────────────────────────────── */

static void draw_header(const char *title, const char *right)
{
    /* Shared chrome: the kit's inverted top bar (title left, count right). */
    akira_ui_status_t sb = {
        .title = title,
        .clock = right,
        .battery_pct = -1,
    };
    akira_ui_status_bar(&sb);
}

/* ── Footer bar ──────────────────────────────────────────────────────── */

static void draw_footer(const char *hint)
{
    display_hline(0, FOOT_Y - 1, SCR_W, COL_SEP);
    display_rect(0, FOOT_Y, SCR_W, FOOT_H, COL_HDR);
    display_text(6, FOOT_Y + 6, hint, COL_DIM);
}

/* ── Scanning screen ─────────────────────────────────────────────────── */

static void render_scanning(void)
{
    display_clear(COL_BG);
    draw_header("WIFI SCANNER", NULL);

    /* Concentric arcs — just draw some circles for visual interest */
    display_circle(SCR_W / 2, 120, 60, COL_SEP);
    display_circle(SCR_W / 2, 120, 40, COL_SEP);
    display_circle(SCR_W / 2, 120, 20, COL_ACCENT);
    display_circle_fill(SCR_W / 2, 120, 5, COL_ACCENT);

    display_text(SCR_W / 2 - 38, 88, "SCANNING...", COL_ACCENT);
    display_text(SCR_W / 2 - 56, 175, "Sweeping 2.4 GHz channels", COL_DIM);
    display_text(SCR_W / 2 - 42, 191, "Passive, no association", COL_DIM);

    draw_footer("Please wait (up to 8 s)");
    display_flush();
}

/* ── List row ────────────────────────────────────────────────────────── */

static void draw_row(int idx, int y, int selected)
{
    const akira_wifi_ap_t *ap = &g_aps[idx];

    /* SSID — truncate to 22 chars */
    char ssid_buf[24];
    sncopy(ssid_buf, (const char *)ap->ssid, 23);
    if (slen((const char *)ap->ssid) > 22) {
        ssid_buf[20] = '.'; ssid_buf[21] = '.'; ssid_buf[22] = '\0';
    }

    /* Meta: "ch06" + security label (e.g. "ch06 WPA2"). */
    char meta[10];
    meta[0] = 'c'; meta[1] = 'h';
    fmt_dec2(meta + 2, ap->channel);
    meta[4] = ' ';
    const char *sl = sec_label(ap->security);
    meta[5] = sl[0]; meta[6] = sl[1]; meta[7] = sl[2]; meta[8] = sl[3];
    meta[9] = '\0';

    /* Shared row: selection inverts, block meter encodes signal (0..5). */
    akira_ui_list_row(y, ROW_H, ssid_buf, meta, rssi_bars(ap->rssi), selected);
}

/* ── List view ───────────────────────────────────────────────────────── */

static void render_list(void)
{
    display_clear(COL_BG);

    /* Header */
    char hdr_r[16];
    int hl = fmt_dec(hdr_r, g_ap_count);
    hdr_r[hl] = ' '; hdr_r[hl+1] = 'A'; hdr_r[hl+2] = 'P'; hdr_r[hl+3] = 's';
    hdr_r[hl+4] = '\0';
    draw_header("WIFI SCANNER", hdr_r);

    /* Column labels */
    display_rect(0, LIST_Y, SCR_W, 10, COL_BG);
    display_text(COL_CH,   LIST_Y,      "CH", COL_SEP);
    display_text(COL_SSID, LIST_Y,      "SSID",     COL_SEP);
    display_text(COL_SEC,  LIST_Y,      "SEC",      COL_SEP);
    display_text(COL_BARS, LIST_Y,      "SIG",      COL_SEP);
    display_hline(0, LIST_Y + 10, SCR_W, COL_SEP);

    if (g_ap_count == 0) {
        display_text(SCR_W / 2 - 56, LIST_Y + 60,
                     "No networks found.", COL_DIM);
        display_text(SCR_W / 2 - 42, LIST_Y + 76,
                     "Press Y to scan.", COL_DIM);
    } else {
        for (int v = 0; v < VROWS; v++) {
            int idx = g_scroll + v;
            if (idx >= g_ap_count) break;
            int y = LIST_Y + 11 + v * ROW_H;
            draw_row(idx, y, idx == g_cursor);
        }

        /* Scroll indicator */
        if (g_ap_count > VROWS) {
            int total_h = LIST_H - 11;
            int thumb_h = total_h * VROWS / g_ap_count;
            if (thumb_h < 4) thumb_h = 4;
            int thumb_y = LIST_Y + 11 + total_h * g_scroll / g_ap_count;
            display_rect(SCR_W - 3, LIST_Y + 11, 3, total_h, COL_SEP);
            display_rect(SCR_W - 3, thumb_y, 3, thumb_h, COL_ACCENT);
        }
    }

    /* Separator before histogram */
    display_hline(0, HIST_Y - 2, SCR_W, COL_SEP);

    /* Channel histogram */
    draw_histogram();

    /* Footer */
    draw_footer("UP/DN:scroll  A:detail  Y:scan");
    display_flush();
}

/* ── Detail view ─────────────────────────────────────────────────────── */

static void render_detail(int idx)
{
    const akira_wifi_ap_t *ap = &g_aps[idx];
    display_clear(COL_BG);
    draw_header("AP DETAIL", "B:back");

    int y = 30;

    /* SSID */
    display_text(6,  y, "SSID",     COL_DIM);
    display_text(80, y, (const char *)ap->ssid, COL_TEXT);
    y += 18;

    /* BSSID */
    char bssid_buf[18];
    fmt_bssid(bssid_buf, ap->bssid);
    display_text(6,  y, "BSSID",    COL_DIM);
    display_text(80, y, bssid_buf,  COL_ACCENT);
    y += 18;

    /* Channel + frequency */
    char ch_info[24];
    int freq_mhz = 2412 + (ap->channel - 1) * 5;
    int n = fmt_dec(ch_info, ap->channel);
    ch_info[n++] = ' '; ch_info[n++] = '(';
    n += fmt_dec(ch_info + n, freq_mhz);
    ch_info[n++] = ' '; ch_info[n++] = 'M';
    ch_info[n++] = 'H'; ch_info[n++] = 'z';
    ch_info[n++] = ')'; ch_info[n]   = '\0';
    display_text(6,  y, "Channel",  COL_DIM);
    display_text(80, y, ch_info,    COL_TEXT);
    y += 18;

    /* RSSI + bar */
    char rssi_str[8];
    fmt_dec(rssi_str, (int)ap->rssi);
    int rssi_len = slen(rssi_str);
    /* append " dBm" */
    rssi_str[rssi_len++] = ' '; rssi_str[rssi_len++] = 'd';
    rssi_str[rssi_len++] = 'B'; rssi_str[rssi_len++] = 'm';
    rssi_str[rssi_len]   = '\0';
    display_text(6,  y, "RSSI",     COL_DIM);
    display_text(80, y, rssi_str,   rssi_color(ap->rssi));
    draw_signal_bars(160, y - 3, ap->rssi);
    y += 18;

    /* Security */
    display_text(6,  y, "Security", COL_DIM);
    display_text(80, y, sec_label(ap->security), sec_color(ap->security));
    y += 18;

    /* Last seen */
    uint32_t now_ms   = (uint32_t)rtc_get_uptime_ms();
    uint32_t delta_ms = now_ms - ap->last_seen_ms;
    uint32_t delta_s  = delta_ms / 1000;
    char seen_str[24];
    int sn = fmt_dec(seen_str, (int)delta_s);
    seen_str[sn++] = 's'; seen_str[sn++] = ' ';
    seen_str[sn++] = 'a'; seen_str[sn++] = 'g';
    seen_str[sn++] = 'o'; seen_str[sn]   = '\0';
    display_text(6,  y, "Last seen", COL_DIM);
    display_text(80, y, seen_str,    COL_DIM);
    y += 24;

    /* Separator + raw dBm progress bar */
    display_hline(6, y, SCR_W - 12, COL_SEP);
    y += 8;
    /* -100 dBm = 0%, 0 dBm = 100% */
    int strength_pct = (int)(ap->rssi + 100);
    if (strength_pct < 0) strength_pct = 0;
    if (strength_pct > 100) strength_pct = 100;
    display_text(6, y, "Signal strength:", COL_DIM);
    display_progress_bar(6, y + 12, SCR_W - 12, 8,
                         strength_pct, 100,
                         rssi_color(ap->rssi), COL_SEP);

    draw_footer("B:back to list  Y:new scan");
    display_flush();
}

/* ── Scan + sort ─────────────────────────────────────────────────────── */

static void do_scan(void)
{
    g_state = STATE_SCANNING;
    render_scanning();

    int n = wifi_scan_aps(g_aps, sizeof(g_aps));
    g_ap_count = (n > 0) ? n : 0;
    sort_aps();

    g_scroll = 0;
    g_cursor = 0;
    g_state  = STATE_LIST;

    printf("wifi_scanner: scan done, %d APs", g_ap_count);
}

/* ── Input ───────────────────────────────────────────────────────────── */

static void handle_input(void)
{
    uint32_t btns = (uint32_t)input_get_buttons();
    uint32_t edge = btns & ~g_prev_btns;  /* newly pressed this frame */

    if (g_state == STATE_LIST) {
        if (edge & AKIRA_BTN_UP) {
            if (g_cursor > 0) {
                g_cursor--;
                if (g_cursor < g_scroll) g_scroll = g_cursor;
            }
        }
        if (edge & AKIRA_BTN_DOWN) {
            if (g_cursor < g_ap_count - 1) {
                g_cursor++;
                if (g_cursor >= g_scroll + VROWS)
                    g_scroll = g_cursor - VROWS + 1;
            }
        }
        if ((edge & AKIRA_BTN_A) && g_ap_count > 0) {
            g_state = STATE_DETAIL;
        }
        if (edge & AKIRA_BTN_Y) {
            do_scan();
        }
    } else if (g_state == STATE_DETAIL) {
        if (edge & AKIRA_BTN_B) {
            g_state = STATE_LIST;
        }
        if (edge & AKIRA_BTN_Y) {
            do_scan();
        }
    }

    g_prev_btns = btns;
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("wifi.scanner v1.0 — passive 802.11 scanner");

    g_tmr = timer_create();
    timer_start(g_tmr);
    g_prev_btns = 0;

    /* Initial scan on launch */
    do_scan();

    while (1) {
        handle_input();

        if (g_state == STATE_LIST) {
            render_list();
        } else if (g_state == STATE_DETAIL) {
            render_detail(g_cursor);
        }

        /* ~10 fps */
        int used = timer_elapsed(g_tmr);
        timer_start(g_tmr);
        if (used < 100) delay((uint32_t)(100 - used) * 1000);
    }

    return 0;
}
