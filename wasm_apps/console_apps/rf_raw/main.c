/*
 * rf_raw — Raw OOK capture/replay for CC1121 (AkiraOS WASM app)
 *
 * Captures raw OOK bitstream at configurable sample rate from Sub-GHz
 * remotes (car keys, garage doors, gate controllers) and replays them.
 * Default frequency is 433.92 MHz for automotive key fob testing.
 *
 * FOR AUTHORIZED USE ONLY — test only on equipment you own or have
 * explicit written permission to test.
 */

#include "akira_api.h"
#include <stdint.h>

/* ── Button GPIO pins (akiraconsole_prod) ─────────────────────────────── */
#define BTN_OK    0    /* GPIO0,  active-low, pull-up  — zephyr,code 0 */
#define BTN_UP    4    /* GPIO4,  active-high, pull-down — zephyr,code 2 */
#define BTN_DOWN  5    /* GPIO5,  active-high, pull-down — zephyr,code 3 */
#define BTN_LEFT  7    /* GPIO7,  active-high, pull-down — physically RIGHT */
#define BTN_RIGHT 6    /* GPIO6,  active-high, pull-down — physically LEFT */
#define BTN_A     15   /* GPIO15, active-high, pull-down — zephyr,code 6 */
#define BTN_B     16   /* GPIO16, active-high, pull-down — zephyr,code 7 */
#define BTN_X     17   /* GPIO17, active-high, pull-down — zephyr,code 8 */
#define BTN_Y     41   /* GPIO41, active-high, pull-down — zephyr,code 9 */

/* Read all buttons, return bitmask matching AKIRA_BTN_* macros.
 * Bit N = 1 << zephyr,code for each currently-held button. */
static int read_buttons(void)
{
    int mask = 0;
    if (!gpio_read(BTN_OK))    mask |= (1 << 0);   /* active-low */
    if (gpio_read(BTN_UP))     mask |= (1 << 2);
    if (gpio_read(BTN_DOWN))   mask |= (1 << 3);
    if (gpio_read(BTN_LEFT))   mask |= (1 << 5);  /* physically LEFT button → AKIRA_BTN_RIGHT */
    if (gpio_read(BTN_RIGHT))  mask |= (1 << 4);  /* physically RIGHT button → AKIRA_BTN_LEFT */
    if (gpio_read(BTN_A))      mask |= (1 << 6);
    if (gpio_read(BTN_B))      mask |= (1 << 7);
    if (gpio_read(BTN_X))      mask |= (1 << 8);
    if (gpio_read(BTN_Y))      mask |= (1 << 9);
    return mask;
}

/* Home button mask (GPIO0, zephyr,code 0) */
#define HOME_MASK (1 << 0)

/* ── Display (populated at startup) ───────────────────────────────────── */
#define DW_DEFAULT 400
#define DH_DEFAULT 240
static int32_t DW = DW_DEFAULT;
static int32_t DH = DH_DEFAULT;
#define HDR_H  16
#define FOT_H  14
#define FOT_Y  (DH - FOT_H)
#define ROW_H  14

/* ── Raw capture limits ───────────────────────────────────────────────── */
#define CAP_BUF_BYTES   2048    /* capture buffer (≤ RF_RAW_MAX_BYTES)      */
#define CAPTURE_TIMEOUT 5000    /* ms waiting for signal onset              */
#define FILE_BUF_SIZE   4096    /* read buffer for loading captures from FS */

/* Default sample rate for OOK envelope — fast enough to capture key fob
 * pulse edges; 38400 sps gives ~26 µs resolution. */
#define DEFAULT_SAMPLE_RATE 38400u

/* Captures directory */
#define CAPTURES_DIR    "rfraw"

/* Screen states */
#define STATE_HOME      0
#define STATE_CAPTURE   1
#define STATE_CAPTURED  2
#define STATE_SETTINGS  3
#define STATE_SAVE_NAME 4
#define STATE_LIBRARY   5
#define STATE_REPLAY    6
#define STATE_VIEW      7

/* ── Frequency presets ────────────────────────────────────────────────── */

typedef struct { uint32_t hz; const char *label; } freq_preset_t;

static const freq_preset_t FREQ_PRESETS[] = {
    { 300000000u, "300.0 MHz" },
    { 315000000u, "315.0 MHz" },
    { 390000000u, "390.0 MHz" },
    { 418000000u, "418.0 MHz" },
    { 433920000u, "433.9 MHz" },   /* default — car keys, TPMS */
    { 434650000u, "434.6 MHz" },
    { 868350000u, "868.3 MHz" },
    { 915000000u, "915.0 MHz" },
};
#define N_FREQ_PRESETS 8

/* ── Modulation presets ───────────────────────────────────────────────── */

typedef struct { int mod; const char *label; } mod_preset_t;

static const mod_preset_t MOD_PRESETS[] = {
    { RADIO_MOD_OOK,  "OOK"  },
    { RADIO_MOD_FSK,  "FSK"  },
    { RADIO_MOD_GFSK, "GFSK" },
};
#define N_MOD_PRESETS 3

/* ── Sample rate presets ──────────────────────────────────────────────── */

typedef struct { uint32_t rate; const char *label; } rate_preset_t;

static const rate_preset_t RATE_PRESETS[] = {
    {  9600u, "9.6k sps"  },
    { 19200u, "19.2k sps" },
    { 38400u, "38.4k sps" },  /* default */
    { 76800u, "76.8k sps" },
    {153600u, "153.6k sps" },
};
#define N_RATE_PRESETS 5

/* ── Monochrome colors (Sharp: 0x0000=black/pixel ON, !=0=white/OFF) ──── */
#define C_BG    0xFFFF   /* white — background pixel OFF */
#define C_FG    0x0000   /* black — text pixel ON */
#define C_SEL   0x0000   /* selected row: black bg */
#define C_SEP   0x0000

/* ── String helpers ───────────────────────────────────────────────────── */

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void my_itoa(uint32_t v, char *buf, int buflen)
{
    char tmp[12];
    int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    while (v > 0 && n < 11) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    int out = n < buflen - 1 ? n : buflen - 1;
    for (int i = 0; i < out; i++) buf[i] = tmp[n - 1 - i];
    buf[out] = '\0';
}

static void scat(char *dst, int *pos, int dstmax, const char *src)
{
    while (*src && *pos < dstmax - 1) dst[(*pos)++] = *src++;
    dst[*pos] = '\0';
}

/* ── Globals ──────────────────────────────────────────────────────────── */

static int g_state        = STATE_HOME;
static int g_needs_redraw = 1;

/* Config */
static int g_freq_idx  = 4;    /* 433.92 MHz */
static int g_mod_idx   = 0;    /* OOK        */
static int g_rate_idx  = 2;    /* 38.4k sps  */
static int g_repeat    = 3;    /* replay count */

/* Capture state */
static uint8_t  g_cap_buf[CAP_BUF_BYTES];
static int      g_cap_bytes = 0;
static char     g_cap_name[16];
static int      g_name_len  = 0;

/* Settings cursor */
static int g_set_field = 0;     /* 0=freq, 1=mod, 2=rate */

/* View state */
static int g_view_scroll = 0;
static int g_scroll_repeat = 0;

/* Library */
#define MAX_LIB_FILES 20
#define FNAME_MAX     32
static char g_lib_files[MAX_LIB_FILES][FNAME_MAX];
static int  g_lib_count  = 0;
static int  g_lib_sel    = 0;

/* Home menu */
#define N_HOME_ITEMS 3
static const char *HOME_ITEMS[] = {
    "Capture Signal",
    "Library",
    "Settings",
};
static int g_home_sel = 0;

/* ── UI helpers ───────────────────────────────────────────────────────── */

static void draw_header(const char *title)
{
    display_rect(0, 0, DW, HDR_H, C_FG);
    display_text(4, 3, title, C_BG);
}

static void draw_footer(const char *hint)
{
    display_rect(0, FOT_Y, DW, FOT_H, C_FG);
    display_text(4, FOT_Y + 2, hint, C_BG);
}

/* ── File I/O ─────────────────────────────────────────────────────────── */

static int save_capture(const char *name, const uint8_t *buf, int n)
{
    char path[48];
    int pp = 0;
    scat(path, &pp, sizeof(path), CAPTURES_DIR "/");
    for (int i = 0; name[i] && pp < (int)sizeof(path) - 6; i++) {
        char c = name[i];
        if (c == ' ') c = '_';
        path[pp++] = c;
    }
    scat(path, &pp, sizeof(path), ".raw");

    int fd = fs_open(path, AKIRA_FS_O_WRITE | AKIRA_FS_O_CREATE | AKIRA_FS_O_TRUNC);
    if (fd < 0) return fd;

    /* Header */
    char line[64];
    int lp;

    lp = 0;
    scat(line, &lp, sizeof(line), "freq_hz=");
    char hz[12]; my_itoa(FREQ_PRESETS[g_freq_idx].hz, hz, sizeof(hz));
    scat(line, &lp, sizeof(line), hz);
    line[lp++] = '\n'; line[lp] = '\0';
    fs_write(fd, line, slen(line));

    lp = 0;
    scat(line, &lp, sizeof(line), "mod=");
    scat(line, &lp, sizeof(line), MOD_PRESETS[g_mod_idx].label);
    line[lp++] = '\n'; line[lp] = '\0';
    fs_write(fd, line, slen(line));

    lp = 0;
    scat(line, &lp, sizeof(line), "sps=");
    char rs[12]; my_itoa(RATE_PRESETS[g_rate_idx].rate, rs, sizeof(rs));
    scat(line, &lp, sizeof(line), rs);
    line[lp++] = '\n'; line[lp] = '\0';
    fs_write(fd, line, slen(line));

    lp = 0;
    scat(line, &lp, sizeof(line), "bytes=");
    char ns[8]; my_itoa((uint32_t)n, ns, sizeof(ns));
    scat(line, &lp, sizeof(line), ns);
    line[lp++] = '\n'; line[lp] = '\0';
    fs_write(fd, line, slen(line));

    /* Raw bytes */
    fs_write(fd, (const char *)buf, n);
    fs_close(fd);
    return 0;
}

static int load_capture(const char *fname, uint8_t *buf, int max_n,
                         uint32_t *freq_out, int *mod_out,
                         uint32_t *sps_out)
{
    char path[FNAME_MAX + 12];
    int pp = 0;
    scat(path, &pp, sizeof(path), CAPTURES_DIR "/");
    scat(path, &pp, sizeof(path), fname);

    int fd = fs_open(path, AKIRA_FS_O_READ);
    if (fd < 0) return fd;

    char fbuf[FILE_BUF_SIZE];
    int total = 0, r;
    while (total < (int)sizeof(fbuf) - 1 && (r = fs_read(fd, fbuf + total, sizeof(fbuf) - 1 - total)) > 0)
        total += r;
    fs_close(fd);
    fbuf[total] = '\0';

    /* Parse header lines until we hit a newline after "bytes=" */
    int pos = 0, data_start = 0, n = 0;
    while (pos < total) {
        int ls = pos;
        while (pos < total && fbuf[pos] != '\n') pos++;
        if (pos < total) pos++; /* skip \n */

        if (fbuf[ls] == 'f' && fbuf[ls + 1] == 'r') {
            /* freq_hz= */
            uint32_t v = 0;
            for (int i = 8; ls + i < pos - 1 && fbuf[ls + i] >= '0' && fbuf[ls + i] <= '9'; i++)
                v = v * 10 + (uint32_t)(fbuf[ls + i] - '0');
            if (freq_out) *freq_out = v;
        } else if (fbuf[ls] == 'm' && fbuf[ls + 1] == 'o') {
            /* mod= */
            if (mod_out) {
                if (fbuf[ls + 4] == 'O') *mod_out = RADIO_MOD_OOK;
                else if (fbuf[ls + 4] == 'G') *mod_out = RADIO_MOD_GFSK;
                else *mod_out = RADIO_MOD_FSK;
            }
        } else if (fbuf[ls] == 's' && fbuf[ls + 1] == 'p') {
            /* sps= */
            uint32_t v = 0;
            for (int i = 4; ls + i < pos - 1 && fbuf[ls + i] >= '0' && fbuf[ls + i] <= '9'; i++)
                v = v * 10 + (uint32_t)(fbuf[ls + i] - '0');
            if (sps_out) *sps_out = v;
        } else if (fbuf[ls] == 'b' && fbuf[ls + 1] == 'y') {
            /* bytes=N — data follows after this newline */
            data_start = pos;
            break;
        }
    }

    /* Copy raw bytes */
    if (data_start > 0 && data_start < total) {
        n = total - data_start;
        if (n > max_n) n = max_n;
        for (int i = 0; i < n; i++) buf[i] = (uint8_t)fbuf[data_start + i];
    }
    return n;
}

static void load_library(void)
{
    g_lib_count = 0;
    g_lib_sel   = 0;
    fs_mkdir(CAPTURES_DIR);

    char dir_buf[1024];
    int n = fs_readdir(CAPTURES_DIR, dir_buf, sizeof(dir_buf));
    if (n <= 0) return;

    int pos = 0;
    while (pos < n && g_lib_count < MAX_LIB_FILES) {
        int start = pos;
        while (pos < n && dir_buf[pos] != '\n' && dir_buf[pos] != '\0') pos++;
        int elen = pos - start;
        if (pos < n) pos++;
        if (elen < 5) continue;
        /* .raw extension */
        if (elen < 4 || dir_buf[start + elen - 4] != '.' ||
            dir_buf[start + elen - 3] != 'r' ||
            dir_buf[start + elen - 2] != 'a' ||
            dir_buf[start + elen - 1] != 'w') continue;
        int cp = elen < FNAME_MAX - 1 ? elen : FNAME_MAX - 1;
        for (int i = 0; i < cp; i++) g_lib_files[g_lib_count][i] = dir_buf[start + i];
        g_lib_files[g_lib_count][cp] = '\0';
        g_lib_count++;
    }
}

/* ── Screen renderers ─────────────────────────────────────────────────── */

static void draw_home(void)
{
    draw_header("RF RAW — CC1121 OOK Capture/Replay");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, C_BG);

    int y = HDR_H + 10;
    display_text(8, y, FREQ_PRESETS[g_freq_idx].label, C_FG);
    display_text(100, y, MOD_PRESETS[g_mod_idx].label, C_FG);
    char info[32]; int ip = 0;
    scat(info, &ip, sizeof(info), RATE_PRESETS[g_rate_idx].label);
    display_text(155, y, info, C_FG);
    y += ROW_H + 4;
    display_rect(0, y, DW, 1, C_SEP);
    y += 6;

    for (int i = 0; i < N_HOME_ITEMS; i++) {
        display_rect(4, y, DW - 8, ROW_H, i == g_home_sel ? C_SEL : C_BG);
        display_text(12, y + 3, HOME_ITEMS[i], i == g_home_sel ? C_BG : C_FG);
        y += ROW_H;
    }

    draw_footer("[A]Select [UP/DN]Nav [HOME]Exit");
}

static void draw_settings(void)
{
    draw_header("SETTINGS");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, C_BG);

    int y = HDR_H + 8;

    /* Frequency */
    display_text(8, y, "Frequency:", g_set_field == 0 ? C_FG : C_FG);
    y += ROW_H;
    for (int i = 0; i < N_FREQ_PRESETS; i++) {
        int sel = (i == g_freq_idx && g_set_field == 0);
        display_rect(4, y, 110, ROW_H, sel ? C_SEL : C_BG);
        display_text(8, y + 3, FREQ_PRESETS[i].label, sel ? C_BG : C_FG);
        y += ROW_H;
    }
    y += 4;
    display_rect(0, y, DW, 1, C_SEP);
    y += 4;

    /* Modulation */
    display_text(8, y, "Modulation:", g_set_field == 1 ? C_FG : C_FG);
    y += ROW_H;
    for (int i = 0; i < N_MOD_PRESETS; i++) {
        int sel = (i == g_mod_idx && g_set_field == 1);
        display_rect(4 + i * 80, y, 72, ROW_H, sel ? C_SEL : C_BG);
        display_text(12 + i * 80, y + 3, MOD_PRESETS[i].label, sel ? C_BG : C_FG);
    }
    y += ROW_H + 4;
    display_rect(0, y, DW, 1, C_SEP);
    y += 4;

    /* Sample rate */
    display_text(8, y, "Sample rate:", g_set_field == 2 ? C_FG : C_FG);
    y += ROW_H;
    for (int i = 0; i < N_RATE_PRESETS; i++) {
        int sel = (i == g_rate_idx && g_set_field == 2);
        display_rect(4 + i * 64, y, 60, ROW_H, sel ? C_SEL : C_BG);
        display_text(8 + i * 64, y + 3, RATE_PRESETS[i].label, sel ? C_BG : C_FG);
    }

    draw_footer("[UP/DN]Select [L/R]Field [HOME]Back");
}

static void draw_capture(void)
{
    draw_header("CAPTURING — point remote & press button");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, C_BG);

    int y = HDR_H + 2;
    display_text(4, y, FREQ_PRESETS[g_freq_idx].label, C_FG);
    char info[40]; int ip = 0;
    scat(info, &ip, sizeof(info), " ");
    scat(info, &ip, sizeof(info), MOD_PRESETS[g_mod_idx].label);
    scat(info, &ip, sizeof(info), " @ ");
    scat(info, &ip, sizeof(info), RATE_PRESETS[g_rate_idx].label);
    display_text(80, y, info, C_FG);
    char ns[8]; my_itoa((uint32_t)g_cap_bytes, ns, sizeof(ns));
    display_text(DW - 50, y, ns, C_FG);

    int total_bits = g_cap_bytes * 8;
    int visible_bits = DW / 5;  /* 5px per bit */
    if (visible_bits < 1) visible_bits = 1;

    /* Clamp scroll: show newest by default */
    int max_scroll = total_bits > visible_bits ? total_bits - visible_bits : 0;
    if (g_view_scroll > max_scroll) g_view_scroll = max_scroll;
    if (g_view_scroll < 0) g_view_scroll = 0;

    if (total_bits > 0) {
        int trace_y = HDR_H + ROW_H + 6;
        int low_y = trace_y + 25;

        /* Draw each visible bit as a timing diagram trace */
        for (int col = 0; col < visible_bits; col++) {
            int bit_idx = g_view_scroll + col;
            if (bit_idx >= total_bits) break;
            int byte_idx = bit_idx / 8;
            int bit_pos  = bit_idx % 8;
            int on = (g_cap_buf[byte_idx] >> (7 - bit_pos)) & 1;
            int prev_on = -1;
            if (bit_idx > 0) {
                int p_byte = (bit_idx - 1) / 8;
                int p_bit  = (bit_idx - 1) % 8;
                prev_on = (g_cap_buf[p_byte] >> (7 - p_bit)) & 1;
            }
            int px = col * 5;
            int cur_y = on ? trace_y : low_y;

            /* Horizontal segment */
            display_line(px, cur_y, px + 5, cur_y, C_FG);

            /* Vertical edge at transition */
            if (prev_on >= 0 && prev_on != on) {
                int prev_y = prev_on ? trace_y : low_y;
                display_line(px, prev_y, px, cur_y, C_FG);
            }
        }

        /* Draw baseline */
        display_line(0, low_y, DW, low_y, C_FG);

        /* 0/1 labels below each bit */
        int label_y = low_y + 4;
        for (int col = 0; col < visible_bits; col++) {
            int bit_idx = g_view_scroll + col;
            if (bit_idx >= total_bits) break;
            int byte_idx = bit_idx / 8;
            int bit_pos  = bit_idx % 8;
            int on = (g_cap_buf[byte_idx] >> (7 - bit_pos)) & 1;
            display_text(4 + col * 5, label_y, on ? "1" : "0", C_FG);
        }

        /* Position indicator */
        char pos[24]; int pp = 0;
        scat(pos, &pp, sizeof(pos), "Bit ");
        char bs[8]; my_itoa((uint32_t)g_view_scroll, bs, sizeof(bs));
        scat(pos, &pp, sizeof(pos), bs);
        scat(pos, &pp, sizeof(pos), "/");
        my_itoa((uint32_t)total_bits, bs, sizeof(bs));
        scat(pos, &pp, sizeof(pos), bs);
        display_text(4, FOT_Y - ROW_H, pos, C_FG);
    } else {
        display_text(4, HDR_H + ROW_H + 20, "Listening... press remote button", C_FG);
    }
    draw_footer("[HOME]Stop [L/R]Scroll");
}

static void draw_captured(void)
{
    draw_header("CAPTURE COMPLETE");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, C_BG);

    int y = HDR_H + 10;
    display_text(8, y, FREQ_PRESETS[g_freq_idx].label, C_FG);
    display_text(100, y, MOD_PRESETS[g_mod_idx].label, C_FG);
    y += ROW_H;

    char info[32]; int ip = 0;
    char ns[8]; my_itoa((uint32_t)g_cap_bytes, ns, sizeof(ns));
    scat(info, &ip, sizeof(info), ns);
    scat(info, &ip, sizeof(info), " bytes captured");
    display_text(8, y, info, C_FG);
    y += ROW_H + 8;

    display_text(8, y, "[A] Save to file", C_FG);
    y += ROW_H;
    display_text(8, y, "[X] Replay now", C_FG);
    y += ROW_H;
    display_text(8, y, "[Y] View data", C_FG);
    y += ROW_H;
    display_text(8, y, "[B] Capture again", C_FG);
    y += ROW_H;
    display_text(8, y, "[HOME] Discard & back", C_FG);

    draw_footer("[A]Save [X]Replay [Y]View [B]Recap [HOME]Back");
}

static void draw_save_name(void)
{
    draw_header("SAVE CAPTURE — enter name");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, C_BG);

    int y = HDR_H + 16;
    display_text(8, y, "Name:", C_FG);
    y += ROW_H;

    /* Show name with cursor */
    char disp[24]; int dp = 0;
    for (int i = 0; i <= g_name_len && i < 12; i++) {
        if (i == g_name_len) disp[dp++] = '[';
        if (i < g_name_len)  disp[dp++] = g_cap_name[i];
        else                 disp[dp++] = ' ';
        if (i == g_name_len) disp[dp++] = ']';
    }
    disp[dp] = '\0';
    display_text(8, y, disp, C_FG);
    y += ROW_H + 8;

    display_text(8, y, "[UP/DN] Change character", C_FG);
    y += ROW_H;
    display_text(8, y, "[LEFT/RIGHT] Move cursor", C_FG);
    y += ROW_H;
    display_text(8, y, "[B] Save & return", C_FG);
    y += ROW_H;
    display_text(8, y, "[X] Cancel", C_FG);

    draw_footer("[B]Save [X]Cancel");
}

static void draw_library(void)
{
    draw_header("LIBRARY — saved captures");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, C_BG);

    if (g_lib_count == 0) {
        display_text(8, HDR_H + 12, "No captures saved yet.", C_FG);
        display_text(8, HDR_H + 28, "Capture a signal first.", C_FG);
    }

    int rows = (DH - HDR_H - FOT_H) / ROW_H;
    for (int i = 0; i < rows && i < g_lib_count; i++) {
        int sel = (i == g_lib_sel);
        int y   = HDR_H + i * ROW_H;
        display_rect(0, y, DW, ROW_H, sel ? C_SEL : C_BG);

        /* Strip .raw extension for display */
        char short_name[FNAME_MAX];
        int sn = slen(g_lib_files[i]);
        if (sn > 4 && g_lib_files[i][sn - 4] == '.') sn -= 4;
        for (int j = 0; j < sn && j < FNAME_MAX - 1; j++)
            short_name[j] = g_lib_files[i][j];
        short_name[sn] = '\0';
        display_text(8, y + 3, short_name, sel ? C_BG : C_FG);
    }

    draw_footer("[A]Replay [X]Delete [HOME]Back [UP/DN]Nav");
}

static void draw_replay(void)
{
    draw_header("REPLAYING — transmitting");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, C_BG);

    int y = HDR_H + 12;
    display_text(8, y, FREQ_PRESETS[g_freq_idx].label, C_FG);
    y += ROW_H;

    char info[40]; int ip = 0;
    char rs[4]; my_itoa((uint32_t)g_repeat, rs, sizeof(rs));
    scat(info, &ip, sizeof(info), "Repeating x");
    scat(info, &ip, sizeof(info), rs);
    display_text(8, y, info, C_FG);
    y += ROW_H + 8;

    display_text(8, y, "Transmitting — hold remote near", C_FG);
    y += ROW_H;
    display_text(8, y, "target device.", C_FG);

    draw_footer("Transmitting...");
}

/* ── View captured data as waterfall ──────────────────────────────────── */

static void draw_view(void)
{
    draw_header("DATA VIEW — scroll L/R 1 bit");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, C_BG);

    if (g_cap_bytes == 0) {
        display_text(8, HDR_H + 12, "No data captured.", C_FG);
        draw_footer("[HOME]Back");
        return;
    }

    int total_bits = g_cap_bytes * 8;
    int visible_bits = DW / 5;
    if (visible_bits < 1) visible_bits = 1;
    int max_scroll = total_bits > visible_bits ? total_bits - visible_bits : 0;
    if (g_view_scroll > max_scroll) g_view_scroll = max_scroll;
    if (g_view_scroll < 0) g_view_scroll = 0;

    int trace_y = HDR_H + ROW_H + 6;
    int low_y = trace_y + 25;

    /* Timing diagram trace */
    for (int col = 0; col < visible_bits; col++) {
        int bit_idx = g_view_scroll + col;
        if (bit_idx >= total_bits) break;
        int byte_idx = bit_idx / 8;
        int bit_pos  = bit_idx % 8;
        int on = (g_cap_buf[byte_idx] >> (7 - bit_pos)) & 1;
        int prev_on = -1;
        if (bit_idx > 0) {
            int p_byte = (bit_idx - 1) / 8;
            int p_bit  = (bit_idx - 1) % 8;
            prev_on = (g_cap_buf[p_byte] >> (7 - p_bit)) & 1;
        }
        int px = col * 5;
        int cur_y = on ? trace_y : low_y;

        display_line(px, cur_y, px + 5, cur_y, C_FG);
        if (prev_on >= 0 && prev_on != on) {
            int prev_y = prev_on ? trace_y : low_y;
            display_line(px, prev_y, px, cur_y, C_FG);
        }
    }
    display_line(0, low_y, DW, low_y, C_FG);

    /* 0/1 labels */
    int label_y = low_y + 4;
    for (int col = 0; col < visible_bits; col++) {
        int bit_idx = g_view_scroll + col;
        if (bit_idx >= total_bits) break;
        int byte_idx = bit_idx / 8;
        int bit_pos  = bit_idx % 8;
        int on = (g_cap_buf[byte_idx] >> (7 - bit_pos)) & 1;
        display_text(4 + col * 5, label_y, on ? "1" : "0", C_FG);
    }

    /* Position indicator */
    char pos[24]; int pp = 0;
    scat(pos, &pp, sizeof(pos), "Bit ");
    char bs[8]; my_itoa((uint32_t)g_view_scroll, bs, sizeof(bs));
    scat(pos, &pp, sizeof(pos), bs);
    scat(pos, &pp, sizeof(pos), "/");
    my_itoa((uint32_t)total_bits, bs, sizeof(bs));
    scat(pos, &pp, sizeof(pos), bs);
    draw_footer(pos);
}

/* ── Name entry charset ───────────────────────────────────────────────── */

static const char NAME_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";
#define N_NAME_CHARS (sizeof(NAME_CHARS) - 1)

/* ── Apply current config to radio ────────────────────────────────────── */

static int apply_config(void)
{
    int ret;
    ret = rf_set_frequency(FREQ_PRESETS[g_freq_idx].hz);
    if (ret < 0) return ret;
    ret = rf_set_modulation(MOD_PRESETS[g_mod_idx].mod);
    return ret;
}

/* ── Entry point ──────────────────────────────────────────────────────── */

int main(void)
{
    /* Get actual display dimensions */
    display_get_size(&DW, &DH);

    /* Select CC1121 — required for raw mode */
    if (rf_select(AKIRA_RF_CHIP_CC1121) < 0) {
        display_clear(C_BG);
        draw_header("RF RAW — ERROR");
        display_text(8, HDR_H + 20, "CC1121 not available.", C_FG);
        display_text(8, HDR_H + 40, "Requires firmware with", C_FG);
        display_text(8, HDR_H + 55, "CONFIG_AKIRA_CC1121=y", C_FG);
        draw_footer("[HOME]Exit");
        display_flush();
        while (!((uint32_t)read_buttons() & HOME_MASK))
            delay(50000);
        return -1;
    }

    apply_config();
    fs_mkdir(CAPTURES_DIR);

    /* Configure button GPIOs */
    gpio_configure(BTN_OK,    GPIO_INPUT | GPIO_PULL_UP);
    gpio_configure(BTN_UP,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_DOWN,  GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_LEFT,  GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_RIGHT, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_A,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_B,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_X,     GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_Y,     GPIO_INPUT | GPIO_PULL_DOWN);

    int prev_btns = read_buttons();
    g_needs_redraw = 1;   /* force initial render */

    while (1) {
        int btns    = read_buttons();
        int pressed = btns & ~prev_btns;
        prev_btns   = btns;

        if (pressed) g_needs_redraw = 1;

        switch (g_state) {

        /* ── HOME ── */
        case STATE_HOME:
            if (pressed & AKIRA_BTN_UP)   { if (g_home_sel > 0) g_home_sel--; }
            if (pressed & AKIRA_BTN_DOWN) { if (g_home_sel < N_HOME_ITEMS - 1) g_home_sel++; }
            if (pressed & HOME_MASK)         { app_switch("supervisor"); return 0; }
            if (pressed & AKIRA_BTN_A) {
                if (g_home_sel == 0) {
                    /* Capture */
                    g_cap_bytes = 0;
                    g_state = STATE_CAPTURE;
                } else if (g_home_sel == 1) {
                    load_library();
                    g_state = STATE_LIBRARY;
                } else {
                    g_state = STATE_SETTINGS;
                }
            }
            break;

        /* ── CAPTURE ── */
        case STATE_CAPTURE: {
            if (btns & HOME_MASK) {
                if (g_cap_bytes > 0) { g_state = STATE_CAPTURED; }
                else { g_state = STATE_HOME; }
                g_needs_redraw = 1;
                break;
            }
            if (pressed & AKIRA_BTN_LEFT)  { if (g_view_scroll > 0) g_view_scroll--; g_needs_redraw = 1; }
            if (pressed & AKIRA_BTN_RIGHT) { g_view_scroll++; g_needs_redraw = 1; }
            if (btns & AKIRA_BTN_LEFT)  { g_scroll_repeat++; if (g_scroll_repeat >= 3) { g_scroll_repeat = 0; if (g_view_scroll > 0) g_view_scroll--; g_needs_redraw = 1; } }
            if (btns & AKIRA_BTN_RIGHT) { g_scroll_repeat++; if (g_scroll_repeat >= 3) { g_scroll_repeat = 0; g_view_scroll++; g_needs_redraw = 1; } }
            if (!(btns & (AKIRA_BTN_LEFT | AKIRA_BTN_RIGHT))) g_scroll_repeat = 0;

            /* Draw waveform of what we have so far */
            if (g_needs_redraw) {
                draw_capture();
                display_flush();
                g_needs_redraw = 0;
            }

            /* Only capture more if buffer has room */
            if (g_cap_bytes < CAP_BUF_BYTES) {
                apply_config();
                int room = CAP_BUF_BYTES - g_cap_bytes;
                if (room > 256) room = 256;  /* small chunks for live view */
                int n = rf_raw_capture(g_cap_buf + g_cap_bytes,
                                       (uint32_t)room,
                                       RATE_PRESETS[g_rate_idx].rate,
                                       250);  /* short timeout */
                if (n > 0) {
                    g_cap_bytes += n;
                    /* Auto-scroll to show newest */
                    int total_bits = g_cap_bytes * 8;
                    int vis = DW / 5;
                    if (vis < 1) vis = 1;
                    g_view_scroll = total_bits > vis ? total_bits - vis : 0;
                    g_needs_redraw = 1;
                }
                /* n==0: no signal in this window, keep polling */
                /* n<0: error, stop */
                if (n < 0 && g_cap_bytes == 0) {
                    g_state = STATE_CAPTURED;  /* show error */
                    g_needs_redraw = 1;
                }
            } else {
                /* Buffer full — go to captured screen */
                g_state = STATE_CAPTURED;
                g_needs_redraw = 1;
            }
            break;
        }

        /* ── CAPTURED ── */
        case STATE_CAPTURED:
            if (pressed & HOME_MASK) { g_state = STATE_HOME; g_needs_redraw = 1; break; }
            if (pressed & AKIRA_BTN_A) {
                /* Init name entry from freq */
                g_name_len = 0;
                int fl = slen(FREQ_PRESETS[g_freq_idx].label);
                for (int i = 0; i < fl && i < 12; i++)
                    g_cap_name[i] = FREQ_PRESETS[g_freq_idx].label[i];
                g_name_len = fl < 12 ? fl : 12;
                g_cap_name[g_name_len] = '\0';
                g_state = STATE_SAVE_NAME;
                break;
            }
            if (pressed & AKIRA_BTN_X) {
                apply_config();
                g_state = STATE_REPLAY;
                break;
            }
            if (pressed & AKIRA_BTN_Y) {
                /* View data: scroll to latest */
                int total_bits = g_cap_bytes * 8;
                int vis = DW / 5;
                if (vis < 1) vis = 1;
                g_view_scroll = total_bits > vis ? total_bits - vis : 0;
                g_state = STATE_VIEW;
                g_needs_redraw = 1;
                break;
            }
            if (pressed & AKIRA_BTN_B) {
                g_cap_bytes = 0;
                g_state = STATE_CAPTURE;
                break;
            }
            break;

        /* ── VIEW DATA ── */
        case STATE_VIEW:
            if (pressed & HOME_MASK) { g_state = STATE_CAPTURED; g_needs_redraw = 1; break; }
            if (pressed & AKIRA_BTN_LEFT)  { if (g_view_scroll > 0) g_view_scroll--; g_scroll_repeat = 0; g_needs_redraw = 1; }
            if (pressed & AKIRA_BTN_RIGHT) { g_view_scroll++; g_scroll_repeat = 0; g_needs_redraw = 1; }
            if (btns & (AKIRA_BTN_LEFT | AKIRA_BTN_RIGHT)) {
                g_scroll_repeat++;
                if (g_scroll_repeat >= 3) {
                    g_scroll_repeat = 0;
                    if (btns & AKIRA_BTN_LEFT)  { if (g_view_scroll > 0) g_view_scroll--; g_needs_redraw = 1; }
                    if (btns & AKIRA_BTN_RIGHT) { g_view_scroll++; g_needs_redraw = 1; }
                }
            } else {
                g_scroll_repeat = 0;
            }
            break;

        /* ── SAVE NAME ── */
        case STATE_SAVE_NAME:
            if (btns & HOME_MASK) {
                g_cap_name[g_name_len] = '\0';
                save_capture(g_cap_name, g_cap_buf, g_cap_bytes);
                g_state = STATE_HOME;
                break;
            }
            if (pressed & AKIRA_BTN_X) { g_state = STATE_CAPTURED; break; }
            if (pressed & AKIRA_BTN_UP) {
                if (g_name_len < 12) {
                    g_cap_name[g_name_len] = NAME_CHARS[0];
                    g_name_len++;
                }
            }
            if (pressed & AKIRA_BTN_DOWN) {
                if (g_name_len > 0) g_name_len--;
            }
            if (pressed & AKIRA_BTN_LEFT) {
                if (g_name_len > 0) {
                    /* Cycle current char backward */
                    char c = g_cap_name[g_name_len - 1];
                    int idx = 0;
                    for (int i = 0; i < (int)N_NAME_CHARS; i++)
                        if (NAME_CHARS[i] == c) { idx = i; break; }
                    idx = (idx - 1 + (int)N_NAME_CHARS) % (int)N_NAME_CHARS;
                    g_cap_name[g_name_len - 1] = NAME_CHARS[idx];
                }
            }
            if (pressed & AKIRA_BTN_RIGHT) {
                if (g_name_len > 0) {
                    /* Cycle current char forward */
                    char c = g_cap_name[g_name_len - 1];
                    int idx = 0;
                    for (int i = 0; i < (int)N_NAME_CHARS; i++)
                        if (NAME_CHARS[i] == c) { idx = i; break; }
                    idx = (idx + 1) % (int)N_NAME_CHARS;
                    g_cap_name[g_name_len - 1] = NAME_CHARS[idx];
                }
            }
            break;

        /* ── SETTINGS ── */
        case STATE_SETTINGS:
            if (pressed & HOME_MASK) { apply_config(); g_state = STATE_HOME; g_needs_redraw = 1; break; }
            if (pressed & AKIRA_BTN_LEFT)  { if (g_set_field > 0) g_set_field--; }
            if (pressed & AKIRA_BTN_RIGHT) { if (g_set_field < 2) g_set_field++; }
            if (pressed & AKIRA_BTN_UP) {
                if (g_set_field == 0 && g_freq_idx > 0) g_freq_idx--;
                if (g_set_field == 1 && g_mod_idx  > 0) g_mod_idx--;
                if (g_set_field == 2 && g_rate_idx > 0) g_rate_idx--;
            }
            if (pressed & AKIRA_BTN_DOWN) {
                if (g_set_field == 0 && g_freq_idx < N_FREQ_PRESETS - 1) g_freq_idx++;
                if (g_set_field == 1 && g_mod_idx  < N_MOD_PRESETS  - 1) g_mod_idx++;
                if (g_set_field == 2 && g_rate_idx < N_RATE_PRESETS - 1) g_rate_idx++;
            }
            break;

        /* ── LIBRARY ── */
        case STATE_LIBRARY:
            if (pressed & HOME_MASK)  { g_state = STATE_HOME; g_needs_redraw = 1; break; }
            if (pressed & AKIRA_BTN_UP) { if (g_lib_sel > 0) g_lib_sel--; }
            if (pressed & AKIRA_BTN_DOWN) { if (g_lib_sel < g_lib_count - 1) g_lib_sel++; }
            if (pressed & AKIRA_BTN_X) {
                /* Delete */
                if (g_lib_sel < g_lib_count) {
                    char path[FNAME_MAX + 12];
                    int pp = 0;
                    scat(path, &pp, sizeof(path), CAPTURES_DIR "/");
                    scat(path, &pp, sizeof(path), g_lib_files[g_lib_sel]);
                    fs_unlink(path);
                    load_library();
                    g_needs_redraw = 1;
                }
            }
            if (pressed & AKIRA_BTN_A) {
                if (g_lib_sel < g_lib_count) {
                    uint32_t freq; int mod; uint32_t sps;
                    int n = load_capture(g_lib_files[g_lib_sel],
                                         g_cap_buf, CAP_BUF_BYTES,
                                         &freq, &mod, &sps);
                    if (n > 0) {
                        g_cap_bytes = n;
                        /* Set radio to match file */
                        rf_set_frequency(freq);
                        rf_set_modulation(mod);
                        /* Find matching presets for display */
                        for (int i = 0; i < N_FREQ_PRESETS; i++)
                            if (FREQ_PRESETS[i].hz == freq) g_freq_idx = i;
                        for (int i = 0; i < N_MOD_PRESETS; i++)
                            if (MOD_PRESETS[i].mod == mod) g_mod_idx = i;
                        for (int i = 0; i < N_RATE_PRESETS; i++)
                            if (RATE_PRESETS[i].rate == sps) g_rate_idx = i;
                        g_state = STATE_REPLAY;
                    }
                }
            }
            break;

        /* ── REPLAY ── */
        case STATE_REPLAY: {
            /* Replay fires once on entry — draw the "transmitting"
             * screen first, flush it, then block on TX. */
            if (g_needs_redraw) {
                draw_replay();
                display_flush();
                g_needs_redraw = 0;
            }
            uint32_t rate = g_cap_bytes > 0
                ? RATE_PRESETS[g_rate_idx].rate
                : DEFAULT_SAMPLE_RATE;
            if (g_cap_bytes > 0) {
                rf_raw_replay(g_cap_buf, (uint32_t)g_cap_bytes,
                              rate, (int32_t)g_repeat);
            }
            /* After replay, show captured screen */
            g_state = STATE_CAPTURED;
            g_needs_redraw = 1;
            break;
        }

        } /* switch */

        /* ── Render ── */
        if (g_needs_redraw) {
            display_clear(C_BG);
            switch (g_state) {
            case STATE_HOME:       draw_home();       break;
            case STATE_CAPTURE:    draw_capture();    break;
            case STATE_CAPTURED:   draw_captured();   break;
            case STATE_SETTINGS:   draw_settings();   break;
            case STATE_SAVE_NAME:  draw_save_name();  break;
            case STATE_LIBRARY:    draw_library();    break;
            case STATE_REPLAY:     draw_replay();     break;
            case STATE_VIEW:       draw_view();       break;
            }
            display_flush();
            g_needs_redraw = 0;
        }

        delay(50); /* yield to scheduler */
    }
}
