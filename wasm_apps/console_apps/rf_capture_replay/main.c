/*
 * rf_capture_replay — Sub-GHz OOK/FSK signal capture and replay for AkiraOS
 *
 * FOR AUTHORIZED USE ONLY — use only on equipment you own or have explicit
 * written permission to test.
 *
 * Captures raw OOK/ASK pulse timings from 315/390/433/868/915 MHz remotes
 * (garage doors, key fobs, gate controllers) and replays them.
 *
 * Rolling-code detection: captures two consecutive button presses and
 * compares the pulse patterns.  If they differ → rolling code (flagged
 * as non-replayable).  A single-press heuristic is also applied based
 * on burst length and internal repetition count.
 *
 * Captures are saved as text files in the app sandbox under captures/.
 *
 * Controls:
 *   HOME     : UP/DOWN select, A confirm, B exit
 *   FREQ_SEL : UP/DOWN preset, A next, B back
 *   CAPTURE  : waiting for signal; B cancel
 *   CAPTURED : A save, X replay, Y capture again, B discard
 *   ROLLING  : X save-reference, B discard
 *   NAME     : UP/DOWN change char, LEFT/RIGHT cursor, A next, B done, X cancel
 *   LIBRARY  : UP/DOWN nav, A replay, X delete, Y rename, B back
 *   REPLAY   : in progress; B abort
 */

#include "akira_api.h"
#include <stdint.h>

/* ── Constants ────────────────────────────────────────────────────────────── */

#define CAP_BUF_SAMPLES   2048          /* uint16_t pulse timings per capture  */
#define MAX_CAPTURES       24           /* max files shown in library           */
#define FNAME_MAX          13           /* "ABCDE12345.rfc" + NUL              */
#define DISPLAY_NAME_MAX   12           /* user-visible chars                   */
#define GAP_US          20000u          /* silence ≥ 20 ms = burst separator   */
#define MIN_BURST_SAMPLES    8          /* ignore bursts shorter than this      */
#define ROLLING_DIFF_THRESH 10          /* # differing samples → rolling code   */
#define CAPTURE_TIMEOUT  5000           /* ms waiting for first signal          */
#define CAPTURES_DIR    "captures"

/* ── States ───────────────────────────────────────────────────────────────── */

#define STATE_HOME       0
#define STATE_FREQ_SEL   1
#define STATE_CAPTURE    2   /* waiting for signal */
#define STATE_CAPTURED   3   /* signal received, review */
#define STATE_ROLLING    4   /* rolling code detected */
#define STATE_NAME       5   /* enter save name */
#define STATE_LIBRARY    6
#define STATE_REPLAY     7
#define STATE_DONE       8

/* ── Display ──────────────────────────────────────────────────────────────── */

#define DW      320
#define DH      240
#define HDR_H    16
#define ROW_H    14
#define FOT_H    14
#define FOT_Y   (DH - FOT_H)

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

/* ── Frequency presets ────────────────────────────────────────────────────── */

typedef struct { uint32_t hz; const char *label; } freq_preset_t;

static const freq_preset_t FREQ_PRESETS[] = {
    { 300000000u, "300.0 MHz" },
    { 315000000u, "315.0 MHz" },
    { 390000000u, "390.0 MHz" },
    { 418000000u, "418.0 MHz" },
    { 433920000u, "433.9 MHz" },
    { 434650000u, "434.6 MHz" },
    { 868350000u, "868.3 MHz" },
    { 915000000u, "915.0 MHz" },
};
#define N_FREQ_PRESETS 8

/* ── Modulation presets ───────────────────────────────────────────────────── */

typedef struct { int mod; const char *label; } mod_preset_t;

static const mod_preset_t MOD_PRESETS[] = {
    { RADIO_MOD_OOK,  "OOK"  },
    { RADIO_MOD_FSK,  "FSK"  },
    { RADIO_MOD_GFSK, "GFSK" },
};
#define N_MOD_PRESETS 3

/* ── String helpers ───────────────────────────────────────────────────────── */

static int my_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }

static void my_itoa(uint32_t v, char *buf, int buflen)
{
    char tmp[12]; int n=0;
    if (v == 0) { tmp[n++]='0'; }
    while (v > 0 && n < 11) { tmp[n++]='0'+(int)(v%10); v/=10; }
    int out = n < buflen-1 ? n : buflen-1;
    for (int i=0; i<out; i++) buf[i]=tmp[n-1-i];
    buf[out]='\0';
}

static void my_strlcpy(char *dst, const char *src, int srclen, int dstmax)
{
    int n = srclen < dstmax-1 ? srclen : dstmax-1;
    for (int i=0; i<n; i++) dst[i]=src[i];
    dst[n]='\0';
}

/* Append string to buf at *pos, returns new pos */
static int scat(char *buf, int pos, int buflen, const char *s)
{
    while (*s && pos < buflen-1) buf[pos++]=*s++;
    buf[pos]='\0';
    return pos;
}

/* ── Globals ──────────────────────────────────────────────────────────────── */

static int g_state = STATE_HOME;
static int g_needs_redraw = 1;

/* Frequency / modulation config */
static int g_freq_idx = 4;   /* default: 433.920 MHz */
static int g_mod_idx  = 0;   /* default: OOK */

/* Config sub-field (freq/mod selector on freq_sel screen) */
static int g_cfg_field = 0;  /* 0=frequency, 1=modulation */

/* Signal buffers */
static uint16_t g_cap_buf[CAP_BUF_SAMPLES];   /* live capture */
static uint16_t g_cmp_buf[CAP_BUF_SAMPLES];   /* second capture for rolling detection */
static int      g_cap_samples = 0;
static int      g_cmp_samples = 0;

/* Capture state */
static int      g_capture_pass = 0;   /* 0=first press, 1=second press */
static int      g_is_rolling   = 0;
static int      g_rssi         = 0;
static char     g_cap_info[48];       /* "N samples, M bursts" */

/* Waveform rendering */
#define WAVE_W   (DW - 16)
#define WAVE_H   32
#define WAVE_Y   (HDR_H + 52)

/* Name entry */
static char g_cap_name[DISPLAY_NAME_MAX + 1];
static int  g_name_len;
static int  g_name_cur;  /* cursor position */

/* Name entry charset: A-Z, 0-9, space, hyphen */
static const char NAME_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
#define N_NAME_CHARS 38
static int g_name_char_idx[DISPLAY_NAME_MAX]; /* index into NAME_CHARS per position */

/* Library */
static char g_lib_files[MAX_CAPTURES][FNAME_MAX];
static int  g_lib_count;
static int  g_lib_sel;
static int  g_lib_scroll;

/* Home menu */
static int g_home_sel = 0;
#define N_HOME_ITEMS 3
static const char *HOME_ITEMS[] = { "Capture Signal", "Library", "Settings" };

/* Replay */
static int g_replay_repeat = 3;   /* default 3 repeats */

/* ── Rolling-code detection ──────────────────────────────────────────────── */

/*
 * Split pulse buffer into bursts at gaps > GAP_US.
 * Returns the number of samples in the first burst (0 = nothing).
 */
static int first_burst_len(const uint16_t *buf, int n)
{
    for (int i = 0; i < n; i++) {
        if (buf[i] >= (uint16_t)(GAP_US > 65535 ? 65535 : GAP_US))
            return i;
    }
    return n;
}

/*
 * Count how many bursts are in the capture and whether they repeat.
 * Returns: 0 = only 1 burst (inconclusive), >0 = multi-burst with
 *          that many repetitions.  Sets *all_equal if all bursts match burst 0.
 */
static int count_bursts(const uint16_t *buf, int n, int *all_equal)
{
    *all_equal = 1;
    int bursts = 0;
    int first_start = 0, first_len = 0;
    int i = 0;

    while (i < n) {
        /* Skip leading gap */
        while (i < n && buf[i] >= (uint16_t)GAP_US) i++;
        if (i >= n) break;

        int start = i;
        /* Scan to next gap */
        while (i < n && buf[i] < (uint16_t)GAP_US) i++;
        int blen = i - start;

        if (blen < MIN_BURST_SAMPLES) { i++; continue; }

        if (bursts == 0) {
            first_start = start;
            first_len   = blen;
        } else if (*all_equal) {
            /* Compare with first burst */
            if (blen != first_len) { *all_equal = 0; }
            else {
                int diff = 0;
                for (int j = 0; j < blen; j++) {
                    int d = (int)buf[first_start + j] - (int)buf[start + j];
                    if (d < 0) d = -d;
                    if (d > 50) diff++;  /* 50µs tolerance */
                }
                if (diff > ROLLING_DIFF_THRESH) *all_equal = 0;
            }
        }
        bursts++;
    }
    return bursts;
}

/*
 * Compare two captures to detect rolling codes.
 * Returns 1 if rolling code detected, 0 if likely fixed code.
 */
static int detect_rolling(const uint16_t *a, int an,
                            const uint16_t *b, int bn)
{
    /* Compare the first burst of each capture */
    int fa = first_burst_len(a, an);
    int fb = first_burst_len(b, bn);

    if (fa < MIN_BURST_SAMPLES || fb < MIN_BURST_SAMPLES) return 0;

    /* Length mismatch → different code → rolling */
    if (fa != fb) return 1;

    int diff = 0;
    for (int i = 0; i < fa; i++) {
        int d = (int)a[i] - (int)b[i];
        if (d < 0) d = -d;
        if (d > 80) diff++;  /* 80µs tolerance for timing jitter */
    }
    return (diff > ROLLING_DIFF_THRESH) ? 1 : 0;
}

/* Single-capture rolling heuristic */
static int single_capture_rolling_heuristic(const uint16_t *buf, int n)
{
    int all_equal = 0;
    int bursts = count_bursts(buf, n, &all_equal);

    /* Only 1 burst and it's long → likely rolling (KeeLoq etc.) */
    if (bursts <= 1 && n > 300) return 1;

    /* Multiple bursts but they all differ → rolling */
    if (bursts > 1 && !all_equal) return 1;

    return 0;
}

/* ── File format ──────────────────────────────────────────────────────────── */

/*
 * .rfcap text format:
 *   freq_hz=433920000
 *   mod=OOK
 *   rssi=-72
 *   rolling=0
 *   name=Garage
 *   n=256
 *   400,200,400,200,...\n    (decimal uint16_t, comma-separated, one line)
 */

static int save_capture(const char *name, const uint16_t *buf, int n,
                         int is_rolling)
{
    /* Build path: captures/<name>.rfcap */
    char path[DISPLAY_NAME_MAX + 20];
    int pp = 0;
    pp = scat(path, pp, sizeof(path), CAPTURES_DIR "/");
    for (int i = 0; name[i] && pp < DISPLAY_NAME_MAX + 8; i++) {
        char c = name[i];
        if (c == ' ') c = '_';
        path[pp++] = c;
    }
    pp = scat(path, pp, sizeof(path), ".rfcap");

    int fd = fs_open(path, AKIRA_FS_O_WRITE | AKIRA_FS_O_CREATE | AKIRA_FS_O_TRUNC);
    if (fd < 0) return fd;

    /* Header lines */
    char line[64];
    int lp;

    lp = scat(line, 0, sizeof(line), "freq_hz=");
    char hz[12]; my_itoa(FREQ_PRESETS[g_freq_idx].hz, hz, sizeof(hz));
    lp = scat(line, lp, sizeof(line), hz);
    line[lp++]='\n'; line[lp]='\0';
    fs_write(fd, line, my_strlen(line));

    lp = scat(line, 0, sizeof(line), "mod=");
    lp = scat(line, lp, sizeof(line), MOD_PRESETS[g_mod_idx].label);
    line[lp++]='\n'; line[lp]='\0';
    fs_write(fd, line, my_strlen(line));

    lp = scat(line, 0, sizeof(line), "rssi=");
    char rssi_s[8]; my_itoa((uint32_t)(g_rssi < 0 ? -g_rssi : g_rssi), rssi_s, sizeof(rssi_s));
    if (g_rssi < 0) {
        char tmp[8]; tmp[0]='-';
        int j = 0;
        while (rssi_s[j]) { tmp[j+1]=rssi_s[j]; j++; }
        tmp[j+1]='\0';
        my_strlcpy(rssi_s, tmp, j+2, sizeof(rssi_s));
    }
    lp = scat(line, lp, sizeof(line), rssi_s);
    line[lp++]='\n'; line[lp]='\0';
    fs_write(fd, line, my_strlen(line));

    lp = scat(line, 0, sizeof(line), is_rolling ? "rolling=1\n" : "rolling=0\n");
    fs_write(fd, line, my_strlen(line));

    lp = scat(line, 0, sizeof(line), "name=");
    lp = scat(line, lp, sizeof(line), name);
    line[lp++]='\n'; line[lp]='\0';
    fs_write(fd, line, my_strlen(line));

    lp = scat(line, 0, sizeof(line), "n=");
    char ns[8]; my_itoa((uint32_t)n, ns, sizeof(ns));
    lp = scat(line, lp, sizeof(line), ns);
    line[lp++]='\n'; line[lp]='\0';
    fs_write(fd, line, my_strlen(line));

    /* Pulse data — comma-separated decimal */
    char data_line[1024];
    int dlp = 0;
    for (int i = 0; i < n; i++) {
        char vs[8]; my_itoa((uint32_t)buf[i], vs, sizeof(vs));
        dlp = scat(data_line, dlp, sizeof(data_line)-2, vs);
        if (i < n-1) data_line[dlp++]=',';
        /* Flush line when nearly full */
        if (dlp > 900 || i == n-1) {
            data_line[dlp++]='\n'; data_line[dlp]='\0';
            fs_write(fd, data_line, dlp);
            dlp=0;
        }
    }

    fs_close(fd);
    return 0;
}

static int load_capture(const char *fname, uint16_t *buf, int max_n,
                         char *name_out, int *rolling_out)
{
    char path[FNAME_MAX + 12];
    int pp = scat(path, 0, sizeof(path), CAPTURES_DIR "/");
    pp = scat(path, pp, sizeof(path), fname);

    int fd = fs_open(path, AKIRA_FS_O_READ);
    if (fd < 0) return fd;

    /* Read whole file into a temp buffer */
    char fbuf[4096];
    int total = 0;
    while (total < (int)sizeof(fbuf)-1) {
        int r = fs_read(fd, fbuf + total, sizeof(fbuf)-1 - total);
        if (r <= 0) break;
        total += r;
    }
    fs_close(fd);
    fbuf[total] = '\0';

    /* Parse lines */
    int n = 0;
    int rolling = 0;
    if (name_out) name_out[0]='\0';

    int pos = 0;
    while (pos < total) {
        int lstart = pos;
        while (pos < total && fbuf[pos] != '\n') pos++;
        int llen = pos - lstart;
        if (pos < total) pos++;
        if (llen == 0) continue;

        char *ln = fbuf + lstart;

        /* Identify key=value lines */
        if (llen > 2 && ln[0]=='n' && ln[1]=='=') {
            /* n=256 — we'll parse samples on the next line */
            (void)0;
        } else if (llen > 8 && ln[0]=='r' && ln[1]=='o' && ln[2]=='l') {
            rolling = (llen > 8 && ln[8]=='1') ? 1 : 0;
        } else if (llen > 5 && ln[0]=='n' && ln[1]=='a' && ln[2]=='m') {
            if (name_out) my_strlcpy(name_out, ln+5, llen-5, DISPLAY_NAME_MAX+1);
        } else if (llen > 0 && (ln[0]>='0' && ln[0]<='9')) {
            /* Pulse data line */
            int dp = 0;
            while (dp < llen && n < max_n) {
                uint32_t v = 0;
                while (dp < llen && ln[dp]>='0' && ln[dp]<='9')
                    v = v*10 + (uint32_t)(ln[dp++]-'0');
                if (dp < llen && ln[dp]==',') dp++;
                buf[n++] = (uint16_t)(v > 65535 ? 65535 : v);
            }
        }
    }

    if (rolling_out) *rolling_out = rolling;
    return n;
}

static void load_library(void)
{
    g_lib_count  = 0;
    g_lib_sel    = 0;
    g_lib_scroll = 0;

    fs_mkdir(CAPTURES_DIR);

    char dir_buf[1024];
    int n = fs_readdir(CAPTURES_DIR, dir_buf, sizeof(dir_buf));
    if (n <= 0) return;

    int pos = 0;
    while (pos < n && g_lib_count < MAX_CAPTURES) {
        int start = pos;
        while (pos < n && dir_buf[pos] != '\n' && dir_buf[pos] != '\0') pos++;
        int elen = pos - start;
        if (pos < n) pos++;
        if (elen < 7) continue; /* min: "x.rfcap" */
        /* Check extension .rfcap */
        if (elen < 6 || dir_buf[start+elen-6] != '.' ||
            dir_buf[start+elen-5] != 'r' || dir_buf[start+elen-4] != 'f' ||
            dir_buf[start+elen-3] != 'c' || dir_buf[start+elen-2] != 'a' ||
            dir_buf[start+elen-1] != 'p') continue;
        my_strlcpy(g_lib_files[g_lib_count], dir_buf + start, elen, FNAME_MAX);
        g_lib_count++;
    }
}

/* ── Waveform renderer ────────────────────────────────────────────────────── */

static void draw_waveform(const uint16_t *buf, int n, int y, int h, uint16_t col)
{
    if (n == 0) return;
    display_rect(8, y, WAVE_W, h, COL_SEP);

    /* Find total duration to scale */
    uint32_t total_us = 0;
    for (int i = 0; i < n; i++) total_us += buf[i];
    if (total_us == 0) return;

    int x = 8;
    for (int i = 0; i < n && x < 8 + WAVE_W; i++) {
        uint32_t w = (uint32_t)WAVE_W * buf[i] / total_us;
        if (w == 0) w = 1;
        int lvl = (i & 1) ? 0 : 1;  /* even=mark(high), odd=space(low) */
        int py  = lvl ? y : (y + h/2);
        int ph  = lvl ? h/2 : h/2;
        if (x + (int)w > 8 + WAVE_W) w = (uint32_t)(8 + WAVE_W - x);
        display_rect(x, py, (int)w, ph, col);
        x += (int)w;
    }
}

/* ── UI helpers ────────────────────────────────────────────────────────────── */

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

/* ── Screen renderers ─────────────────────────────────────────────────────── */

static void draw_home(void)
{
    draw_hdr("RF CAPTURE+REPLAY");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);

    int y = HDR_H + 12;
    display_text(8, y, FREQ_PRESETS[g_freq_idx].label, COL_DIM);
    display_text(90, y, MOD_PRESETS[g_mod_idx].label, COL_DIM);
    display_text(130, y, "CC1101/CC1121", COL_DIM);
    y += ROW_H + 4;
    display_rect(0, y, DW, 1, COL_SEP);
    y += 6;

    for (int i = 0; i < N_HOME_ITEMS; i++) {
        display_rect(4, y, DW-8, ROW_H, i==g_home_sel ? COL_SEL : COL_BG);
        display_text(12, y+3, HOME_ITEMS[i], i==g_home_sel ? COL_TXT : COL_DIM);
        y += ROW_H;
    }

    draw_footer("[A]Select [UP/DN]Nav [B]Exit");
}

static void draw_freq_sel(void)
{
    draw_hdr("RF CAPTURE+REPLAY — CONFIG");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);

    int y = HDR_H + 8;
    display_text(8, y, "Frequency:", COL_DIM);
    y += ROW_H;

    int rows = (DH - HDR_H - FOT_H - ROW_H*3 - 20) / ROW_H;
    int scroll = g_freq_idx > rows/2 ? g_freq_idx - rows/2 : 0;
    for (int i = 0; i < N_FREQ_PRESETS && y < FOT_Y - ROW_H*2 - 20; i++) {
        int idx = i + scroll;
        if (idx >= N_FREQ_PRESETS) break;
        int sel = (idx == g_freq_idx) && (g_cfg_field == 0);
        display_rect(4, y, DW/2-8, ROW_H, sel ? COL_SEL : COL_BG);
        display_text(12, y+3, FREQ_PRESETS[idx].label, sel ? COL_TXT : COL_DIM);
        y += ROW_H;
    }

    y = FOT_Y - ROW_H*2 - 8;
    display_rect(0, y, DW, 1, COL_SEP);
    y += 4;
    display_text(8, y, "Modulation:", COL_DIM);
    y += ROW_H;
    for (int i = 0; i < N_MOD_PRESETS; i++) {
        int sel = (i == g_mod_idx) && (g_cfg_field == 1);
        display_rect(4 + i*80, y, 72, ROW_H, sel ? COL_SEL : COL_BG);
        display_text(12 + i*80, y+3, MOD_PRESETS[i].label, sel ? COL_TXT : COL_DIM);
    }

    draw_footer("[UP/DN]Select [LEFT/RIGHT]Field [A]Capture [B]Back");
}

static void draw_capture(void)
{
    draw_hdr(g_capture_pass == 0 ? "RF CAPTURE — PRESS REMOTE (1/2)"
                                 : "RF CAPTURE — PRESS AGAIN  (2/2)");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);

    int y = HDR_H + 10;
    display_text(8, y, FREQ_PRESETS[g_freq_idx].label, COL_ACC);
    display_text(90, y, MOD_PRESETS[g_mod_idx].label, COL_ACC);
    y += ROW_H + 8;
    display_text(8, y, "Point remote at AkiraConsole", COL_DIM);
    y += ROW_H;
    display_text(8, y, "and press the button.", COL_DIM);
    y += ROW_H + 4;

    if (g_capture_pass == 0) {
        display_text(8, y, "Listening...", COL_WARN);
    } else {
        display_text(8, y, "Press button AGAIN to verify", COL_WARN);
        y += ROW_H;
        char info[32];
        int ip = scat(info, 0, sizeof(info), "First: ");
        char ns[8]; my_itoa((uint32_t)g_cap_samples, ns, sizeof(ns));
        ip = scat(info, ip, sizeof(info), ns);
        ip = scat(info, ip, sizeof(info), " samples");
        display_text(8, y, info, COL_DIM);
    }

    /* RSSI bar */
    int rssi_abs = g_rssi < 0 ? -g_rssi : 0;
    if (rssi_abs > 120) rssi_abs = 120;
    int bar_w = (DW-16) - (DW-16) * rssi_abs / 120;
    display_rect(8, FOT_Y-16, DW-16, 10, COL_SEP);
    if (bar_w > 0) display_rect(8, FOT_Y-16, bar_w, 10, COL_ACC);

    draw_footer("[B]Cancel");
}

static void build_cap_info(const uint16_t *buf, int n)
{
    int all_eq = 0;
    int bursts  = count_bursts(buf, n, &all_eq);
    int ip = 0;
    char ns[8]; my_itoa((uint32_t)n, ns, sizeof(ns));
    ip = scat(g_cap_info, ip, sizeof(g_cap_info), ns);
    ip = scat(g_cap_info, ip, sizeof(g_cap_info), " samp, ");
    char bs[4]; my_itoa((uint32_t)bursts, bs, sizeof(bs));
    ip = scat(g_cap_info, ip, sizeof(g_cap_info), bs);
    ip = scat(g_cap_info, ip, sizeof(g_cap_info), " burst");
    if (bursts != 1) { g_cap_info[ip++]='s'; g_cap_info[ip]='\0'; }
    if (bursts > 1 && all_eq) { ip = scat(g_cap_info, ip, sizeof(g_cap_info), " (fixed)"); }
}

static void draw_captured(void)
{
    draw_hdr(g_is_rolling ? "CAPTURED — ROLLING CODE!" : "CAPTURED — FIXED CODE");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);

    int y = HDR_H + 6;
    display_text(8, y, FREQ_PRESETS[g_freq_idx].label, COL_DIM);
    display_text(90, y, MOD_PRESETS[g_mod_idx].label, COL_DIM);
    y += ROW_H;
    display_text(8, y, g_cap_info[0] ? g_cap_info : "...", COL_DIM);
    y += ROW_H + 2;

    /* Waveform */
    draw_waveform(g_cap_buf, g_cap_samples, y, WAVE_H,
                  g_is_rolling ? COL_WARN : COL_OK);
    y += WAVE_H + 6;

    if (g_is_rolling) {
        display_text(8, y, "ROLLING CODE — NOT REPLAYABLE", COL_ERR);
    }

    draw_footer("[A]Save [X]Replay [Y]Capture again [B]Discard");
}

static void draw_rolling(void)
{
    draw_hdr("ROLLING CODE DETECTED");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);

    int y = HDR_H + 10;
    display_text(8, y, "This remote uses a rolling code.", COL_WARN);
    y += ROW_H;
    display_text(8, y, "Each transmission is unique —", COL_DIM);
    y += ROW_H;
    display_text(8, y, "replay attacks will NOT work.", COL_DIM);
    y += ROW_H + 8;
    display_text(8, y, "You can save as reference only.", COL_DIM);
    y += ROW_H + 4;

    draw_waveform(g_cap_buf, g_cap_samples, y, WAVE_H, COL_WARN);

    draw_footer("[X]Save reference [B]Discard");
}

static void draw_name(void)
{
    draw_hdr("RF CAPTURE+REPLAY — NAME CAPTURE");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);

    int y = HDR_H + 12;
    display_text(8, y, "Enter capture name:", COL_DIM);
    y += ROW_H + 4;

    /* Render current name with cursor */
    char disp[DISPLAY_NAME_MAX*2 + 4];
    int dp = 0;
    for (int i = 0; i < g_name_len || i <= g_name_cur; i++) {
        if (i == g_name_cur) disp[dp++]='[';
        if (i < g_name_len) disp[dp++]=g_cap_name[i];
        else                disp[dp++]=' ';
        if (i == g_name_cur) disp[dp++]=']';
    }
    disp[dp]='\0';
    display_text(8, y, disp, COL_ACC);
    y += ROW_H + 8;

    display_text(8, y, "[UP/DN]Char [LT/RT]Cursor", COL_DIM);
    y += ROW_H;
    display_text(8, y, "[B]Done  [X]Cancel", COL_DIM);

    draw_footer("[UP/DN]Char [LEFT/RIGHT]Move [B]Done [X]Cancel");
}

static void draw_library(void)
{
    draw_hdr("RF CAPTURE+REPLAY — LIBRARY");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);

    if (g_lib_count == 0) {
        display_text(8, HDR_H+12, "No captures saved.", COL_DIM);
        display_text(8, HDR_H+28, "Go to Capture to record a signal.", COL_DIM);
    }

    int rows = (DH-HDR_H-FOT_H) / ROW_H;
    for (int i = 0; i < rows && (i+g_lib_scroll) < g_lib_count; i++) {
        int idx = i + g_lib_scroll;
        int sel = (idx == g_lib_sel);
        int y   = HDR_H + i * ROW_H;
        display_rect(0, y, DW, ROW_H, sel ? COL_SEL : COL_BG);

        /* Show filename without extension */
        char short_name[FNAME_MAX];
        int sn = my_strlen(g_lib_files[idx]);
        if (sn > 6) sn -= 6; /* strip ".rfcap" */
        my_strlcpy(short_name, g_lib_files[idx], sn, FNAME_MAX);
        display_text(8, y+3, short_name, sel ? COL_TXT : COL_DIM);
    }

    draw_footer("[A]Replay [X]Delete [B]Back [UP/DN]Nav");
}

static void draw_replay(void)
{
    draw_hdr("RF REPLAY — TRANSMITTING");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);

    int y = HDR_H + 12;
    char r_s[8]; my_itoa((uint32_t)g_replay_repeat, r_s, sizeof(r_s));
    char info[32];
    int ip = scat(info, 0, sizeof(info), "Sending x");
    ip = scat(info, ip, sizeof(info), r_s);
    display_text(8, y, info, COL_WARN);
    y += ROW_H;
    display_text(8, y, FREQ_PRESETS[g_freq_idx].label, COL_DIM);
    y += ROW_H + 4;
    display_text(8, y, "DO NOT MOVE REMOTE", COL_WARN);

    draw_footer("[B]Abort (if held)");
}

static void draw_done(void)
{
    draw_hdr("RF REPLAY — DONE");
    display_rect(0, HDR_H, DW, DH-HDR_H-FOT_H, COL_BG);
    display_text(8, HDR_H+20, "Transmission complete.", COL_OK);
    draw_footer("[A/B]Back");
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    rf_select(AKIRA_RF_CHIP_CC1101);

    /* Ensure captures directory exists */
    fs_mkdir(CAPTURES_DIR);

    /* Init name entry state */
    g_cap_name[0] = NAME_CHARS[0];
    g_cap_name[1] = '\0';
    g_name_len    = 1;
    g_name_cur    = 0;
    for (int i = 0; i < DISPLAY_NAME_MAX; i++) g_name_char_idx[i] = 0;

    int prev_btns = 0;

    while (1) {
        int btns    = input_get_buttons();
        int pressed = btns & ~prev_btns;
        prev_btns   = btns;

        if (pressed) g_needs_redraw = 1;

        switch (g_state) {

        /* ── HOME ── */
        case STATE_HOME:
            if (pressed & AKIRA_BTN_UP)   { if (g_home_sel>0) g_home_sel--; }
            if (pressed & AKIRA_BTN_DOWN)  { if (g_home_sel<N_HOME_ITEMS-1) g_home_sel++; }
            if (pressed & AKIRA_BTN_B)     { app_switch("supervisor"); return 0; }
            if (pressed & AKIRA_BTN_A) {
                if (g_home_sel == 0) { g_state = STATE_FREQ_SEL; g_cfg_field = 0; }
                else if (g_home_sel == 1) { load_library(); g_state = STATE_LIBRARY; }
                else  { g_state = STATE_FREQ_SEL; g_cfg_field = 0; } /* settings → freq sel */
            }
            break;

        /* ── FREQ_SEL ── */
        case STATE_FREQ_SEL:
            if (pressed & AKIRA_BTN_LEFT)  g_cfg_field = 0;
            if (pressed & AKIRA_BTN_RIGHT)  g_cfg_field = 1;
            if (g_cfg_field == 0) {
                if (pressed & AKIRA_BTN_UP)   { if (g_freq_idx>0) g_freq_idx--; }
                if (pressed & AKIRA_BTN_DOWN)  { if (g_freq_idx<N_FREQ_PRESETS-1) g_freq_idx++; }
            } else {
                if (pressed & AKIRA_BTN_UP)   { if (g_mod_idx>0) g_mod_idx--; }
                if (pressed & AKIRA_BTN_DOWN)  { if (g_mod_idx<N_MOD_PRESETS-1) g_mod_idx++; }
            }
            if (pressed & AKIRA_BTN_B)     { g_state = STATE_HOME; }
            if (pressed & AKIRA_BTN_A) {
                /* Apply settings and start capture */
                rf_set_frequency((int32_t)FREQ_PRESETS[g_freq_idx].hz);
                rf_set_modulation(MOD_PRESETS[g_mod_idx].mod);
                rf_set_bandwidth(650000u);

                g_capture_pass = 0;
                g_cap_samples  = 0;
                g_cmp_samples  = 0;
                g_state        = STATE_CAPTURE;
            }
            break;

        /* ── CAPTURE ── */
        case STATE_CAPTURE: {
            if (pressed & AKIRA_BTN_B) { g_state = STATE_HOME; break; }

            /* Poll RSSI for the meter */
            int16_t raw_rssi = 0;
            rf_get_rssi(&raw_rssi);
            int new_rssi = (int)raw_rssi;
            if (new_rssi != g_rssi) { g_rssi = new_rssi; g_needs_redraw = 1; }

            /* Capture */
            uint16_t *dst     = (g_capture_pass == 0) ? g_cap_buf : g_cmp_buf;
            int       *dst_n  = (g_capture_pass == 0) ? &g_cap_samples : &g_cmp_samples;
            int n = rf_raw_capture(dst, CAP_BUF_SAMPLES, CAPTURE_TIMEOUT);

            if (n > 0) {
                *dst_n = n;
                if (g_capture_pass == 0) {
                    /* Got first press — check single-capture heuristic */
                    int heuristic_rolling = single_capture_rolling_heuristic(
                                                g_cap_buf, g_cap_samples);
                    if (heuristic_rolling) {
                        /* Skip second capture; flag immediately */
                        g_is_rolling = 1;
                        build_cap_info(g_cap_buf, g_cap_samples);
                        g_state = STATE_CAPTURED;
                    } else {
                        /* Request second press */
                        g_capture_pass = 1;
                        g_needs_redraw = 1;
                    }
                } else {
                    /* Got second press — compare */
                    g_is_rolling = detect_rolling(g_cap_buf, g_cap_samples,
                                                   g_cmp_buf, g_cmp_samples);
                    build_cap_info(g_cap_buf, g_cap_samples);
                    g_state = STATE_CAPTURED;
                }
            } else if (n == -ETIMEDOUT) {
                /* Show timeout feedback but stay in capture */
                g_needs_redraw = 1;
            }
            break;
        }

        /* ── CAPTURED ── */
        case STATE_CAPTURED:
            if (pressed & AKIRA_BTN_A) {
                if (g_is_rolling) {
                    /* Can save as reference; go through name entry */
                }
                /* Init name entry */
                g_name_len = 1;
                g_name_cur = 0;
                g_cap_name[0] = NAME_CHARS[0];
                g_cap_name[1] = '\0';
                for (int i=0; i<DISPLAY_NAME_MAX; i++) g_name_char_idx[i]=0;
                g_state = STATE_NAME;
            }
            if (pressed & AKIRA_BTN_X) {
                if (!g_is_rolling) {
                    draw_replay();
                    display_flush();
                    rf_raw_replay(g_cap_buf, (uint32_t)g_cap_samples,
                                  g_replay_repeat);
                    g_state = STATE_DONE;
                }
            }
            if (pressed & AKIRA_BTN_Y) {
                /* Capture again */
                g_capture_pass = 0;
                g_cap_samples  = 0;
                g_cmp_samples  = 0;
                g_state        = STATE_CAPTURE;
            }
            if (pressed & AKIRA_BTN_B) { g_state = STATE_HOME; }
            break;

        /* ── ROLLING ── */
        case STATE_ROLLING:
            if (pressed & AKIRA_BTN_X) {
                g_name_len = 1; g_name_cur = 0;
                g_cap_name[0] = NAME_CHARS[0]; g_cap_name[1]='\0';
                for (int i=0; i<DISPLAY_NAME_MAX; i++) g_name_char_idx[i]=0;
                g_state = STATE_NAME;
            }
            if (pressed & AKIRA_BTN_B) { g_state = STATE_HOME; }
            break;

        /* ── NAME ── */
        case STATE_NAME: {
            int ci = g_name_char_idx[g_name_cur];
            if (pressed & AKIRA_BTN_UP) {
                ci = (ci + 1) % N_NAME_CHARS;
                g_name_char_idx[g_name_cur] = ci;
                if (g_name_cur < g_name_len) g_cap_name[g_name_cur] = NAME_CHARS[ci];
            }
            if (pressed & AKIRA_BTN_DOWN) {
                ci = (ci - 1 + N_NAME_CHARS) % N_NAME_CHARS;
                g_name_char_idx[g_name_cur] = ci;
                if (g_name_cur < g_name_len) g_cap_name[g_name_cur] = NAME_CHARS[ci];
            }
            if (pressed & AKIRA_BTN_RIGHT) {
                if (g_name_cur < DISPLAY_NAME_MAX - 1) {
                    if (g_name_cur >= g_name_len - 1 && g_name_len < DISPLAY_NAME_MAX) {
                        g_name_len++;
                        g_cap_name[g_name_len-1] = NAME_CHARS[0];
                        g_cap_name[g_name_len]   = '\0';
                        g_name_char_idx[g_name_cur+1] = 0;
                    }
                    g_name_cur++;
                }
            }
            if (pressed & AKIRA_BTN_LEFT) {
                if (g_name_cur > 0) g_name_cur--;
            }
            if (pressed & AKIRA_BTN_B) {
                /* Done — save */
                if (g_name_len > 0) {
                    save_capture(g_cap_name, g_cap_buf, g_cap_samples, g_is_rolling);
                }
                g_state = STATE_HOME;
            }
            if (pressed & AKIRA_BTN_X) {
                /* Cancel */
                g_state = STATE_CAPTURED;
            }
            break;
        }

        /* ── LIBRARY ── */
        case STATE_LIBRARY: {
            int rows = (DH-HDR_H-FOT_H)/ROW_H;
            if (pressed & AKIRA_BTN_UP) {
                if (g_lib_sel>0) { g_lib_sel--; if(g_lib_sel<g_lib_scroll) g_lib_scroll=g_lib_sel; }
            }
            if (pressed & AKIRA_BTN_DOWN) {
                if (g_lib_sel<g_lib_count-1) {
                    g_lib_sel++;
                    if (g_lib_sel>=g_lib_scroll+rows) g_lib_scroll=g_lib_sel-rows+1;
                }
            }
            if (pressed & AKIRA_BTN_B) { g_state = STATE_HOME; }
            if ((pressed & AKIRA_BTN_A) && g_lib_count > 0) {
                /* Load and replay */
                char name_buf[DISPLAY_NAME_MAX+1];
                int rolling = 0;
                int n = load_capture(g_lib_files[g_lib_sel], g_cap_buf,
                                     CAP_BUF_SAMPLES, name_buf, &rolling);
                if (n > 0 && !rolling) {
                    g_cap_samples = n;
                    draw_replay();
                    display_flush();
                    rf_raw_replay(g_cap_buf, (uint32_t)n, g_replay_repeat);
                    g_state = STATE_DONE;
                } else if (rolling) {
                    /* Show rolling warning inline */
                    g_cap_samples = n > 0 ? n : 0;
                    g_is_rolling  = 1;
                    g_state       = STATE_CAPTURED;
                }
            }
            if ((pressed & AKIRA_BTN_X) && g_lib_count > 0) {
                /* Delete */
                char path[FNAME_MAX+12];
                int pp = scat(path, 0, sizeof(path), CAPTURES_DIR "/");
                pp = scat(path, pp, sizeof(path), g_lib_files[g_lib_sel]);
                fs_unlink(path);
                load_library();
            }
            break;
        }

        /* ── REPLAY ── */
        case STATE_REPLAY:
            /* Replay is blocking in the native layer; state transitions
             * happen synchronously before entering this state. */
            break;

        /* ── DONE ── */
        case STATE_DONE:
            if (pressed & (AKIRA_BTN_A | AKIRA_BTN_B)) {
                g_state = STATE_HOME;
            }
            break;

        default: break;
        }

        if (g_needs_redraw) {
            switch (g_state) {
            case STATE_HOME:     draw_home();     break;
            case STATE_FREQ_SEL: draw_freq_sel(); break;
            case STATE_CAPTURE:  draw_capture();  break;
            case STATE_CAPTURED: draw_captured(); break;
            case STATE_ROLLING:  draw_rolling();  break;
            case STATE_NAME:     draw_name();     break;
            case STATE_LIBRARY:  draw_library();  break;
            case STATE_REPLAY:   draw_replay();   break;
            case STATE_DONE:     draw_done();     break;
            default: break;
            }
            display_flush();
            g_needs_redraw = 0;
        }

        /* Force redraw while in CAPTURE so RSSI updates */
        if (g_state == STATE_CAPTURE) g_needs_redraw = 1;

        delay(20);
    }

    return 0;
}
