/*
 * wifi_rogue_detect — Evil Twin / Rogue AP detector for AkiraOS
 *
 * Passive 802.11 scan analysis: detects suspicious APs including:
 *   - Same SSID broadcast on multiple channels (evil twin)
 *   - Duplicate BSSID with different SSID (MAC cloning)
 *   - Open (unencrypted) AP with same SSID as encrypted AP (rogue AP)
 *
 * FOR AUTHORIZED USE ONLY — test only on networks you own or have
 * explicit written permission to audit.
 *
 * Controls:
 *   UP / DOWN  — scroll list
 *   A          — detail view
 *   B          — exit / back
 *   Y          — rescan
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stdint.h>

/* ── Limits ─────────────────────────────────────────────────────────────── */

#define MAX_APS         64
#define MAX_GROUPS      16      /* unique SSIDs for evil-twin grouping */

/* ── States ─────────────────────────────────────────────────────────────── */

#define STATE_SCAN      0
#define STATE_LIST      1
#define STATE_DETAIL    2

/* ── Display ────────────────────────────────────────────────────────────── */

static int32_t DW = 320;
static int32_t DH = 240;
#define HDR_H    16
#define ROW_H    14
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
#define COL_SEL  CONSOLE_COLOR_SEL_BG
#define COL_HDR  CONSOLE_COLOR_HEADER
#define COL_SEP  CONSOLE_COLOR_SEP

/* ── Threat flags ───────────────────────────────────────────────────────── */

#define THREAT_NONE     0
#define THREAT_EVIL_TWIN    (1<<0)  /* same SSID on multiple channels */
#define THREAT_MAC_CLONE    (1<<1)  /* same BSSID, different SSID      */
#define THREAT_ROGUE_OPEN   (1<<2)  /* open AP mimicking encrypted SSID */
#define THREAT_MULTI_CHAN   (1<<3)  /* AP's SSID seen on >1 channel via group */

/* ── Globals ─────────────────────────────────────────────────────────────── */

static akira_wifi_ap_t g_aps[MAX_APS];
static int             g_ap_count;
static uint8_t         g_threats[MAX_APS]; /* THREAT_* bitmask per AP */

/* Groups: SSID → metadata for cross-referencing */
typedef struct {
    char     ssid[33];
    int      channels[14];    /* which channels this SSID was seen on */
    int      n_channels;
    int      ap_indices[8];   /* indices into g_aps[] for this SSID */
    int      n_aps;
    uint8_t  has_open;        /* any instance is open? */
    uint8_t  has_encrypted;   /* any instance has security? */
} ssid_group_t;
static ssid_group_t g_groups[MAX_GROUPS];
static int          g_n_groups;

/* List state */
static int g_sel;
static int g_scroll;
static int g_state;

/* Detail view */
static int g_detail_idx;

/* ── String helpers ──────────────────────────────────────────────────────── */

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

/* ── Analysis engine ─────────────────────────────────────────────────────── */

static void fmt_mac(const uint8_t *mac, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        buf[i*3]   = hex[(mac[i] >> 4) & 0xF];
        buf[i*3+1] = hex[mac[i] & 0xF];
        buf[i*3+2] = (i < 5) ? ':' : '\0';
    }
}

static int ssid_cmp(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 33; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static int bssid_cmp(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 6; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

static int is_open(uint8_t sec)
{
    return (sec == WIFI_SEC_OPEN);
}

static void run_analysis(void)
{
    /* Clear threats */
    for (int i = 0; i < g_ap_count; i++) {
        g_threats[i] = THREAT_NONE;
    }

    /* Build SSID groups */
    g_n_groups = 0;
    for (int i = 0; i < g_ap_count && g_n_groups < MAX_GROUPS; i++) {
        const akira_wifi_ap_t *ap = &g_aps[i];
        int found = -1;
        for (int g = 0; g < g_n_groups; g++) {
            if (ssid_cmp(ap->ssid, (const uint8_t *)g_groups[g].ssid) == 0) {
                found = g;
                break;
            }
        }
        if (found < 0) {
            found = g_n_groups++;
            ssid_group_t *grp = &g_groups[found];
            int s = 0;
            while (ap->ssid[s] && s < 32) { grp->ssid[s] = (char)ap->ssid[s]; s++; }
            grp->ssid[s] = '\0';
            grp->n_channels = 0;
            grp->n_aps = 0;
            grp->has_open = 0;
            grp->has_encrypted = 0;
        }
        ssid_group_t *grp = &g_groups[found];

        /* Track unique channels */
        int ch = (int)ap->channel;
        int ch_found = 0;
        for (int c = 0; c < grp->n_channels; c++) {
            if (grp->channels[c] == ch) { ch_found = 1; break; }
        }
        if (!ch_found && grp->n_channels < 14) {
            grp->channels[grp->n_channels++] = ch;
        }

        /* Track open vs encrypted */
        if (is_open(ap->security)) grp->has_open = 1;
        else                       grp->has_encrypted = 1;

        /* Store AP index */
        if (grp->n_aps < 8) {
            grp->ap_indices[grp->n_aps++] = i;
        }
    }

    /* Pass 2: assign threats based on group analysis */
    for (int g = 0; g < g_n_groups; g++) {
        ssid_group_t *grp = &g_groups[g];

        /* (1) Evil twin: same SSID on >1 channel OR >1 AP on same channel
         *     with different BSSID */
        int multi_ch = (grp->n_channels > 1);
        int multi_ap = (grp->n_aps > 1);

        for (int a = 0; a < grp->n_aps; a++) {
            int idx = grp->ap_indices[a];
            if (multi_ch) g_threats[idx] |= THREAT_EVIL_TWIN;
            if (multi_ap) {
                /* Check if any sibling has different BSSID */
                for (int b = 0; b < grp->n_aps; b++) {
                    if (a == b) continue;
                    int other = grp->ap_indices[b];
                    if (bssid_cmp(g_aps[idx].bssid, g_aps[other].bssid) != 0) {
                        g_threats[idx] |= THREAT_EVIL_TWIN;
                        break;
                    }
                }
            }
        }

        /* (2) Rogue open: open AP masquerading as encrypted SSID */
        if (grp->has_open && grp->has_encrypted) {
            for (int a = 0; a < grp->n_aps; a++) {
                int idx = grp->ap_indices[a];
                if (is_open(g_aps[idx].security)) {
                    g_threats[idx] |= THREAT_ROGUE_OPEN;
                }
            }
        }
    }

    /* (3) MAC clone: same BSSID but different SSID */
    for (int i = 0; i < g_ap_count; i++) {
        for (int j = i + 1; j < g_ap_count; j++) {
            if (bssid_cmp(g_aps[i].bssid, g_aps[j].bssid) == 0 &&
                ssid_cmp(g_aps[i].ssid, g_aps[j].ssid) != 0) {
                g_threats[i] |= THREAT_MAC_CLONE;
                g_threats[j] |= THREAT_MAC_CLONE;
            }
        }
    }
}

/* ── Security label ──────────────────────────────────────────────────────── */

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

/* ── Redraw flag (declared early — used by do_scan) ────────────────────────── */

static int g_needs_redraw;

/* ── Draw helpers ────────────────────────────────────────────────────────── */

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

static void draw_signal_bars(int x, int y, int8_t rssi)
{
    int bars = 0;
    if (rssi >= -50) bars = 5;
    else if (rssi >= -60) bars = 4;
    else if (rssi >= -70) bars = 3;
    else if (rssi >= -80) bars = 2;
    else if (rssi >= -90) bars = 1;
    static const uint8_t bh[5] = {4, 6, 9, 12, 15};
    for (int i = 0; i < 5; i++) {
        display_rect(x + i * 6, y + ROW_H - 2 - bh[i], 5, bh[i],
                     (i < bars) ? COL_OK : COL_BG);
    }
}

/* ── Screen renderers ────────────────────────────────────────────────────── */

static void draw_scan(void)
{
    draw_hdr("ROGUE DETECT — SCANNING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    display_text(8, HDR_H + 16, "Scanning...", COL_DIM);
    draw_footer("Please wait...");
}

static void draw_list(void)
{
    char title[28];
    int sp = 0;
    const char *pre = "ROGUE DETECT — ";
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
        int y  = HDR_H + i * ROW_H;
        int sel = (idx == g_sel);
        uint8_t thr = g_threats[idx];

        /* Background — red if rogue, orange if twin/clone, white if selected */
        uint32_t bg = COL_BG;
        if (sel) bg = COL_SEL;
        else if (thr & THREAT_ROGUE_OPEN) bg = 0x4000U;   /* dark red */
        else if (thr & (THREAT_EVIL_TWIN | THREAT_MAC_CLONE)) bg = 0x4020U; /* dark orange */
        display_rect(0, y, DW, ROW_H, bg);

        uint32_t tc = sel ? COL_TXT : COL_DIM;

        /* Threat icon */
        int tx = 4;
        if (thr & THREAT_ROGUE_OPEN) {
            display_text(tx, y + 2, "R", COL_ERR);
            tx += 12;
        } else if (thr & THREAT_EVIL_TWIN) {
            display_text(tx, y + 2, "T", COL_WARN);
            tx += 12;
        } else if (thr & THREAT_MAC_CLONE) {
            display_text(tx, y + 2, "C", COL_WARN);
            tx += 12;
        }

        /* SSID (truncated) */
        char ssid[19];
        int sl = slen((const char *)ap->ssid);
        if (sl > 18) sl = 18;
        for (int j = 0; j < sl; j++) ssid[j] = (char)ap->ssid[j];
        ssid[sl] = '\0';
        display_text(tx, y + 2, ssid, tc);

        /* Right: ch + sec */
        char info[14];
        int ip = 0;
        info[ip++] = 'c'; info[ip++] = 'h';
        char ch_s[4]; my_itoa(ap->channel, ch_s, sizeof(ch_s));
        for (int j = 0; ch_s[j]; j++) info[ip++] = ch_s[j];
        info[ip++] = ' ';
        const char *slbl = sec_label(ap->security);
        for (int j = 0; slbl[j]; j++) info[ip++] = slbl[j];
        info[ip] = '\0';
        display_text(DW - 56, y + 2, info, tc);

        /* Signal bars left of label */
        draw_signal_bars(DW - 64, y, ap->rssi);
    }

    draw_footer("[A]Detail [Y]Rescan [UP/DN]Nav [B]Exit");
}

static void draw_detail(void)
{
    draw_hdr("ROGUE DETECT — DETAIL");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);
    int y = HDR_H + 4;
    const akira_wifi_ap_t *ap = &g_aps[g_detail_idx];
    uint8_t thr = g_threats[g_detail_idx];

    /* SSID */
    display_text(8, y, (const char *)ap->ssid, COL_TXT); y += ROW_H;

    /* Threat flags */
    if (thr & THREAT_ROGUE_OPEN) {
        display_text(8, y, "⚠ ROGUE: Open AP mimics encrypted", COL_ERR); y += ROW_H;
    }
    if (thr & THREAT_EVIL_TWIN) {
        display_text(8, y, "⚠ EVIL TWIN: SSID on multiple APs", COL_WARN); y += ROW_H;
    }
    if (thr & THREAT_MAC_CLONE) {
        display_text(8, y, "⚠ CLONE: BSSID has multiple SSIDs", COL_WARN); y += ROW_H;
    }
    if (thr == THREAT_NONE) {
        display_text(8, y, "✓ No threats detected", COL_OK); y += ROW_H;
    }
    y += 2;
    display_rect(4, y, DW - 8, 1, COL_SEP);
    y += 6;

    /* BSSID */
    char mac_s[18]; fmt_mac(ap->bssid, mac_s);
    display_text(8, y, "BSSID:", COL_DIM);
    display_text(52, y, mac_s, COL_TXT); y += ROW_H;

    /* Channel */
    display_text(8, y, "Channel:", COL_DIM);
    char ch_s[4]; my_itoa(ap->channel, ch_s, sizeof(ch_s));
    display_text(60, y, ch_s, COL_TXT); y += ROW_H;

    /* RSSI */
    display_text(8, y, "RSSI:", COL_DIM);
    char rssi_s[8]; my_itoa((int)ap->rssi, rssi_s, sizeof(rssi_s));
    display_text(52, y, rssi_s, COL_TXT);
    display_text(60, y, " dBm", COL_DIM); y += ROW_H;

    /* Security */
    display_text(8, y, "Security:", COL_DIM);
    display_text(60, y, sec_label(ap->security), COL_ACC); y += ROW_H;

    /* Show group members if multi-AP SSID */
    int show_group = 0;
    for (int g = 0; g < g_n_groups; g++) {
        for (int a = 0; a < g_groups[g].n_aps; a++) {
            if (g_groups[g].ap_indices[a] == g_detail_idx && g_groups[g].n_aps > 1) {
                show_group = 1;
                y += 2;
                display_rect(4, y, DW - 8, 1, COL_SEP);
                y += 6;
                display_text(8, y, "Other instances:", COL_DIM); y += ROW_H;
                for (int b = 0; b < g_groups[g].n_aps; b++) {
                    int oi = g_groups[g].ap_indices[b];
                    if (oi == g_detail_idx) continue;
                    const akira_wifi_ap_t *oap = &g_aps[oi];
                    char line[32];
                    int lp = 0;
                    line[lp++]='c'; line[lp++]='h';
                    char oc[4]; my_itoa(oap->channel, oc, sizeof(oc));
                    for (int j = 0; oc[j]; j++) line[lp++] = oc[j];
                    line[lp++]=' ';
                    const char *osl = sec_label(oap->security);
                    for (int j = 0; osl[j]; j++) line[lp++] = osl[j];
                    line[lp++]=' ';
                    char ors[6]; my_itoa((int)oap->rssi, ors, sizeof(ors));
                    for (int j = 0; ors[j]; j++) line[lp++] = ors[j];
                    line[lp++]='d'; line[lp++]='B'; line[lp]='\0';
                    display_text(12, y, line, COL_DIM); y += ROW_H;
                }
                break;
            }
        }
        if (show_group) break;
    }

    draw_footer("[B]Back");
}

/* ── Scan ────────────────────────────────────────────────────────────────── */

static void do_scan(void)
{
    g_needs_redraw = 1;
    draw_scan();
    display_flush();

    int n = wifi_scan_aps(g_aps, sizeof(g_aps));
    g_ap_count = (n > 0) ? (n < MAX_APS ? n : MAX_APS) : 0;
    g_sel = 0;
    g_scroll = 0;

    if (g_ap_count > 0) {
        run_analysis();
    }

    g_state = STATE_LIST;
    g_needs_redraw = 1;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    display_get_size(&DW, &DH);

    int prev_btns = 0;

    do_scan();

    while (1) {
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
            if (pressed & AKIRA_BTN_Y) {
                do_scan(); /* returns with g_state == STATE_LIST */
            }
            if (pressed & AKIRA_BTN_B) {
                return 0; /* exit to shell */
            }
            if ((pressed & AKIRA_BTN_A) && g_ap_count > 0) {
                g_detail_idx = g_sel;
                g_state = STATE_DETAIL;
            }
            break;

        case STATE_DETAIL:
            if (pressed & AKIRA_BTN_B) {
                g_state = STATE_LIST;
            }
            break;

        default:
            break;
        }

        /* ── Redraw ── */
        if (g_needs_redraw) {
            switch (g_state) {
            case STATE_SCAN:    draw_scan();    break;
            case STATE_LIST:    draw_list();    break;
            case STATE_DETAIL:  draw_detail();  break;
            default: break;
            }
            display_flush();
            g_needs_redraw = 0;
        }

        delay(20000);
    }

    return 0;
}
