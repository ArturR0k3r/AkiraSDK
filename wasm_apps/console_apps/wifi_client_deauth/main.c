/*
 * wifi_client_deauth — Enumerate AP clients by passive sniffing, then
 * targeted-deauth a single client (or broadcast all).
 *
 * FOR AUTHORIZED USE ONLY — use only on networks you own or have
 * explicit written permission to test.
 *
 * Flow:
 *   SCAN    → scan APs
 *   APLIST  → select target AP
 *   SNIFF   → wifi_scan_clients() passive sniff (~4 s)
 *   CLLIST  → select client (or "ALL CLIENTS")
 *   CONFIRM → hold A → safety dialog
 *   DEAUTH  → wifi_deauth() targeted frames
 *   DONE    → show result, B to restart
 *
 * Controls (all screens):
 *   UP / DOWN — navigate list
 *   A         — confirm / advance
 *   B         — back / cancel / restart
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stdint.h>

#define MAX_APS      32
#define MAX_CLIENTS  48
#define HOLD_DURATION 2000u    /* ms hold required on CONFIRM screen */
#define SNIFF_MS     4000      /* passive sniff window */
#define DEAUTH_COUNT 60        /* frames per targeted deauth */
#define DEAUTH_INTV  20        /* ms between frames */

#define STATE_SCAN    0
#define STATE_APLIST  1
#define STATE_SNIFF   2
#define STATE_CLLIST  3
#define STATE_CONFIRM 4
#define STATE_DEAUTH  5
#define STATE_DONE    6

/* Special client-list index: broadcast (deauth everyone) */
#define SEL_ALL_CLIENTS (-1)

static int32_t DW = 320;
static int32_t DH = 240;
#define HDR_H    16
#define ROW_H    14
#define FOT_H    14
#define FOT_Y    (DH - FOT_H)

#define COL_BG   CONSOLE_COLOR_BG
#define COL_TXT  CONSOLE_COLOR_TEXT
#define COL_DIM  CONSOLE_COLOR_DIM
#define COL_ACC  CONSOLE_COLOR_ACCENT
#define COL_WARN CONSOLE_COLOR_WARN
#define COL_OK   CONSOLE_COLOR_OK
#define COL_ERR  CONSOLE_COLOR_ERR
#define COL_SEL  CONSOLE_COLOR_SEL_BG

static int g_state = STATE_SCAN;
static int g_needs_redraw = 1;

static akira_wifi_ap_t     g_aps[MAX_APS];
static int                 g_ap_count;
static akira_wifi_client_t g_clients[MAX_CLIENTS];
static int                 g_client_count;

static int g_sel;      /* list cursor (AP list or client list) */
static int g_scroll;
static int g_target_ap_idx;
static int g_target_cli;     /* index into g_clients, or SEL_ALL_CLIENTS */
static uint32_t g_hold_start;
static char g_status_msg[48];
static char g_result_msg[48];

/* ── String helpers (no libc) ──────────────────────────────────────────── */

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

static void fmt_rssi(int8_t rssi, char *buf, int len)
{
    if (rssi == 0) { buf[0]='?'; buf[1]='\0'; return; }
    my_itoa((int)rssi, buf, len);
}

static void set_msg(char *dst, const char *msg)
{
    int i = 0;
    while (msg[i] && i < 46) { dst[i] = msg[i]; i++; }
    dst[i] = '\0';
}

/* ── Screen renderers ────────────────────────────────────────────────────── */

static void draw_hdr(const char *title)
{
    display_rect(0, 0, DW, HDR_H, COL_BG);
    display_text(4, 3, title, COL_TXT);
}

static void draw_footer(const char *hint)
{
    display_rect(0, FOT_Y, DW, FOT_H, COL_BG);
    display_text(4, FOT_Y + 2, hint, COL_DIM);
}

static void draw_bar(int y, int h, uint32_t done, uint32_t total, uint16_t col)
{
    int w = total > 0 ? (int)((uint32_t)(DW - 8) * done / total) : 0;
    display_rect_outline(4, y, DW - 8, h, COL_TXT);
    if (w > 0) display_rect(4, y, w, h, col);
}

static void draw_scan(void)
{
    draw_hdr("WIFI CLIENTS — SCANNING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    display_text(8, HDR_H + 16, g_status_msg[0] ? g_status_msg : "Scanning...", COL_DIM);
    draw_footer("Please wait...");
}

static void draw_aplist(void)
{
    draw_hdr("WIFI CLIENTS — SELECT AP");
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
        display_rect(0, y, DW, ROW_H, sel ? COL_TXT : COL_BG);

        char ssid[21];
        int slen = my_strlen((const char *)ap->ssid);
        if (slen > 20) slen = 20;
        for (int j = 0; j < slen; j++) ssid[j] = (char)ap->ssid[j];
        ssid[slen] = '\0';
        display_text(8, y + 3, ssid, sel ? COL_BG : COL_DIM);

        char info[24];
        char rssi_s[6];
        fmt_rssi(ap->rssi, rssi_s, sizeof(rssi_s));
        char ch_s[4];
        my_itoa(ap->channel, ch_s, sizeof(ch_s));
        int ip = 0;
        info[ip++]='c'; info[ip++]='h';
        for (int j = 0; ch_s[j]; j++) info[ip++] = ch_s[j];
        info[ip++]=' ';
        for (int j = 0; rssi_s[j]; j++) info[ip++] = rssi_s[j];
        info[ip++]='d'; info[ip++]='B'; info[ip++]='m'; info[ip]='\0';
        display_text(DW - 84, y + 3, info, sel ? COL_BG : COL_DIM);
    }

    draw_footer("[A]Sniff [B]Rescan [UP/DN]Nav");
}

static void draw_sniff(void)
{
    draw_hdr("WIFI CLIENTS — SNIFFING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 10;
    display_text(8, y, (const char *)g_aps[g_target_ap_idx].ssid, COL_ACC);
    y += ROW_H + 4;
    display_text(8, y, "Passive sniff (no TX)...", COL_DIM);
    y += ROW_H + 8;

    uint32_t elapsed = (uint32_t)(rtc_get_uptime_ms() % SNIFF_MS);
    draw_bar(y, 10, elapsed, SNIFF_MS, COL_ACC);

    draw_footer("Passive listen...");
}

static void draw_cllist(void)
{
    draw_hdr("WIFI CLIENTS — SELECT CLIENT");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int rows = (DH - HDR_H - FOT_H) / ROW_H;

    if (g_client_count == 0) {
        display_text(8, HDR_H + 12, "No clients seen.", COL_DIM);
        display_text(8, HDR_H + 28, "[B] back to AP list", COL_DIM);
    }

    /* Row 0 = "ALL CLIENTS" (broadcast deauth) */
    for (int i = 0; i < rows; i++) {
        int idx = i + g_scroll;
        int y   = HDR_H + i * ROW_H;
        int sel = (idx == g_sel);

        display_rect(0, y, DW, ROW_H, sel ? COL_TXT : COL_BG);

        if (idx == 0) {
            display_text(8, y + 3, "ALL CLIENTS (broadcast)", sel ? COL_BG : COL_WARN);
            continue;
        }
        int ci = idx - 1;
        if (ci >= g_client_count) continue;

        char mac_s[18];
        fmt_mac(g_clients[ci].mac, mac_s);
        display_text(8, y + 3, mac_s, sel ? COL_BG : COL_DIM);

        char rssi_s[6];
        fmt_rssi(g_clients[ci].rssi, rssi_s, sizeof(rssi_s));
        display_text(DW - 44, y + 3, rssi_s, sel ? COL_BG : COL_DIM);
    }

    draw_footer("[A]Select [B]Back [UP/DN]Nav");
}

static void draw_confirm(void)
{
    draw_hdr("WIFI CLIENTS — CONFIRM");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 8;
    display_text(8, y, "AP:", COL_DIM);
    display_text(28, y, (const char *)g_aps[g_target_ap_idx].ssid, COL_WARN);
    y += ROW_H;

    char mac_s[18];
    if (g_target_cli == SEL_ALL_CLIENTS) {
        display_text(8, y, "Client:", COL_DIM);
        display_text(56, y, "ALL (broadcast)", COL_WARN);
    } else {
        fmt_mac(g_clients[g_target_cli].mac, mac_s);
        display_text(8, y, "Client:", COL_DIM);
        display_text(56, y, mac_s, COL_WARN);
    }
    y += ROW_H + 4;

    display_rect(0, y, DW, 1, COL_DIM);
    y += 6;
    display_text(8, y, "Sending deauth frames", COL_DIM);
    y += ROW_H;
    char cnt[4];
    my_itoa(DEAUTH_COUNT, cnt, sizeof(cnt));
    display_text(8, y, cnt, COL_DIM);
    display_text(28, y, "x to client", COL_DIM);
    y += ROW_H + 4;

    display_text(8, y, "AUTHORIZED USE ONLY", COL_WARN);
    y += ROW_H;
    display_text(8, y, "Hold [A] 2s to start", COL_WARN);
    y += ROW_H + 6;

    uint32_t now  = (uint32_t)rtc_get_uptime_ms();
    uint32_t held = (g_hold_start && now > g_hold_start) ? now - g_hold_start : 0;
    if (held > HOLD_DURATION) held = HOLD_DURATION;
    draw_bar(y, 10, held, HOLD_DURATION, COL_WARN);

    draw_footer("[A]Hold 2s to confirm [B]Cancel");
}

static void draw_deauth(void)
{
    draw_hdr("WIFI CLIENTS — DEAUTHING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 10;
    display_text(8, y, (const char *)g_aps[g_target_ap_idx].ssid, COL_ACC);
    y += ROW_H;
    if (g_target_cli == SEL_ALL_CLIENTS) {
        display_text(8, y, "Broadcast: all clients", COL_DIM);
    } else {
        char mac_s[18];
        fmt_mac(g_clients[g_target_cli].mac, mac_s);
        display_text(8, y, mac_s, COL_DIM);
    }
    y += ROW_H + 8;

    uint32_t elapsed = (uint32_t)(rtc_get_uptime_ms() % (DEAUTH_COUNT * DEAUTH_INTV));
    draw_bar(y, 10, elapsed, (uint32_t)(DEAUTH_COUNT * DEAUTH_INTV), COL_ERR);

    draw_footer("Deauthing...");
}

static void draw_done(void)
{
    draw_hdr("WIFI CLIENTS — RESULT");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    display_text(8, HDR_H + 16, g_result_msg, COL_DIM);
    draw_footer("[B]Restart");
}

/* ── Actions ─────────────────────────────────────────────────────────────── */

static void do_scan(void)
{
    set_msg(g_status_msg, "Scanning...");
    int n = wifi_scan_aps(g_aps, sizeof(g_aps));
    g_ap_count = (n > 0) ? (n < MAX_APS ? n : MAX_APS) : 0;
    g_sel    = 0;
    g_scroll = 0;

    if (g_ap_count == 0) {
        set_msg(g_status_msg, "No APs found.");
    } else {
        char num[4]; my_itoa(g_ap_count, num, sizeof(num));
        char msg[48];
        int p = 0;
        for (int j = 0; num[j]; j++) msg[p++] = num[j];
        const char *suf = " APs found";
        for (int j = 0; suf[j]; j++) msg[p++] = suf[j];
        msg[p] = '\0';
        set_msg(g_status_msg, msg);
    }
    g_state = STATE_APLIST;
    g_needs_redraw = 1;
}

static void do_sniff(void)
{
    const akira_wifi_ap_t *ap = &g_aps[g_target_ap_idx];
    g_client_count = wifi_scan_clients(ap->bssid, g_clients,
                                       sizeof(g_clients),
                                       (int32_t)ap->channel,
                                       SNIFF_MS);
    if (g_client_count < 0) {
        char msg[48];
        const char *pre = "Sniff failed (";
        int p = 0;
        for (int j = 0; pre[j]; j++) msg[p++] = pre[j];
        char err[4]; my_itoa(g_client_count, err, sizeof(err));
        for (int j = 0; err[j]; j++) msg[p++] = err[j];
        msg[p++] = ')'; msg[p] = '\0';
        set_msg(g_result_msg, msg);
        g_state = STATE_DONE;
        g_needs_redraw = 1;
        return;
    }
    if (g_client_count > MAX_CLIENTS) g_client_count = MAX_CLIENTS;
    g_sel    = 0;
    g_scroll = 0;
    g_state = STATE_CLLIST;
    g_needs_redraw = 1;
}

static void do_deauth(void)
{
    const akira_wifi_ap_t *ap = &g_aps[g_target_ap_idx];

    if (g_target_cli == SEL_ALL_CLIENTS) {
        wifi_deauth(ap->bssid, WIFI_MAC_BROADCAST,
                    (int32_t)ap->channel, DEAUTH_COUNT, DEAUTH_INTV);
    } else {
        wifi_deauth(ap->bssid, g_clients[g_target_cli].mac,
                    (int32_t)ap->channel, DEAUTH_COUNT, DEAUTH_INTV);
    }

    set_msg(g_result_msg, "Deauth sent");
    g_state = STATE_DONE;
    g_needs_redraw = 1;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    display_get_size(&DW, &DH);

    int prev_btns = 0;

    while (1) {
        if (g_state == STATE_SCAN) {
            do_scan();
        }

        /* ── Blocking phase: sniff ── */
        if (g_state == STATE_SNIFF) {
            draw_sniff();
            display_flush();
            do_sniff();
            continue;
        }

        /* ── Blocking phase: deauth ── */
        if (g_state == STATE_DEAUTH) {
            draw_deauth();
            display_flush();
            do_deauth();
            continue;
        }

        /* ── Input ── */
        int btns    = input_get_buttons();
        int pressed = btns & ~prev_btns;
        prev_btns   = btns;

        if (pressed) g_needs_redraw = 1;

        /* Animate the confirm hold-bar while A is held down. */
        if (g_state == STATE_CONFIRM && g_hold_start) {
            g_needs_redraw = 1;
        }

        int rows = (DH - HDR_H - FOT_H) / ROW_H;

        switch (g_state) {

        case STATE_APLIST:
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
                g_state = STATE_SCAN;
            }
            if ((pressed & AKIRA_BTN_A) && g_ap_count > 0) {
                g_target_ap_idx = g_sel;
                g_state = STATE_SNIFF;
            }
            break;

        case STATE_CLLIST: {
            int total = g_client_count + 1; /* +1 for ALL CLIENTS row */
            if (pressed & AKIRA_BTN_UP) {
                if (g_sel > 0) { g_sel--; if (g_sel < g_scroll) g_scroll = g_sel; }
            }
            if (pressed & AKIRA_BTN_DOWN) {
                if (g_sel < total - 1) {
                    g_sel++;
                    if (g_sel >= g_scroll + rows) g_scroll = g_sel - rows + 1;
                }
            }
            if (pressed & AKIRA_BTN_B) {
                g_state = STATE_APLIST;
            }
            if ((pressed & AKIRA_BTN_A) && total > 0) {
                g_target_cli = (g_sel == 0) ? SEL_ALL_CLIENTS : (g_sel - 1);
                g_hold_start = 0;
                g_state = STATE_CONFIRM;
            }
            break;
        }

        case STATE_CONFIRM: {
            if (pressed & AKIRA_BTN_B) {
                g_hold_start = 0;
                g_state = STATE_CLLIST;
            }
            if (pressed & AKIRA_BTN_A) {
                if (!g_hold_start) {
                    g_hold_start = rtc_get_uptime_ms();
                }
            } else if (!(btns & AKIRA_BTN_A)) {
                g_hold_start = 0; /* A released — restart the hold */
            }
            uint32_t now = rtc_get_uptime_ms();
            uint32_t held = (g_hold_start && now > g_hold_start) ? now - g_hold_start : 0;
            if (held >= HOLD_DURATION) {
                bool ok = akira_ui_confirm_dialog("WIFI_DEAUTH",
                                                   "deauth this client?");
                if (ok) {
                    g_state = STATE_DEAUTH;
                } else {
                    g_hold_start = 0;
                }
                prev_btns = input_get_buttons();
            }
            break;
        }

        case STATE_DONE:
            if (pressed & AKIRA_BTN_B) {
                g_state = STATE_SCAN;
            }
            break;

        default:
            break;
        }

        /* ── Redraw ── */
        if (g_needs_redraw) {
            switch (g_state) {
            case STATE_SCAN:    draw_scan();    break;
            case STATE_APLIST:  draw_aplist();  break;
            case STATE_CLLIST:  draw_cllist();  break;
            case STATE_CONFIRM: draw_confirm(); break;
            case STATE_DONE:    draw_done();    break;
            default: break;
            }
            display_flush();
            g_needs_redraw = 0;
        }

        delay(20000); /* 20 ms */
    }
    return 0;
}
