/*
 * usb_rubber_ducky - DuckyScript HID payload injector for AkiraOS
 *
 * Presents AkiraConsole as a USB keyboard to the host.
 * Loads DuckyScript .txt payloads from the app sandbox (payloads/ dir).
 * Safety interlock: A must be held for 2 seconds before execution starts.
 *
 * DuckyScript commands implemented:
 *   REM, DELAY, DEFAULTDELAY/DEFAULT_DELAY, STRING, STRINGLN,
 *   REPEAT, ENTER/RETURN, ESC/ESCAPE, BACKSPACE, DELETE, TAB, SPACE,
 *   UP/DOWN/LEFT/RIGHT, HOME, END, PAGEUP/PAGEDOWN, INSERT,
 *   F1-F12, PRINTSCREEN, CAPSLOCK, NUMLOCK, SCROLLLOCK, APP/MENU,
 *   PAUSE/BREAK, GUI/WINDOWS/COMMAND, CTRL/CONTROL, ALT, SHIFT
 *   (modifier combinations: CTRL ALT DELETE, GUI r, SHIFT F5, ...)
 *
 * Controls:
 *   UP/DOWN  — select payload
 *   A        — enter confirm screen (hold 2s to execute)
 *   B        — back / exit
 *   Y (list) — reload payload list
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stdint.h>

/* ── Limits ────────────────────────────────────────────────────────────── */

#define MAX_PAYLOADS    16
#define MAX_FILENAME    48
#define PAYLOAD_MAX     8192
#define LINE_MAX        256
#define PAYLOAD_DIR     "payloads"
#define HOLD_DURATION   2000u   /* ms hold required to confirm */
#define INTER_KEY_MS    5       /* ms between key press and release */

/* ── States ─────────────────────────────────────────────────────────────── */

#define STATE_LIST      0   /* select payload */
#define STATE_CONFIRM   1   /* 2-second safety hold */
#define STATE_EXEC      2   /* running */
#define STATE_DONE      3   /* finished */

/* ── Display ─────────────────────────────────────────────────────────────── */

static int32_t DW = 320; /* runtime display size — set in main() */
static int32_t DH = 240;
#define HDR_H 16
#define ROW_H 14
#define BAR_Y (DH - 22)
#define FOT_H 14
#define FOT_Y (DH - FOT_H)

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

/* ── Globals ─────────────────────────────────────────────────────────────── */

static char g_files[MAX_PAYLOADS][MAX_FILENAME];
static int  g_file_count;
static int  g_sel;
static int  g_scroll;
static int  g_state = STATE_LIST;

/* Execution state */
static char     g_payload[PAYLOAD_MAX];
static int      g_payload_len;
static int      g_exec_pos;          /* byte offset into payload */
static int      g_exec_line;         /* line index (0-based) */
static int      g_total_lines;
static int      g_default_delay;     /* ms between every command */
static uint32_t g_delay_until;       /* uptime_ms when next cmd runs */
static int      g_exec_done;
static int      g_exec_error;        /* line number of first error (-1=none) */

/* Last command for REPEAT */
#define LAST_NONE   0
#define LAST_KEY    1
#define LAST_STRING 2
static int g_last_type;
static int g_last_mod;
static int g_last_keycode;
static char g_last_str[LINE_MAX];

/* Safety hold */
static uint32_t g_hold_start;

/* Status text for DONE screen */
static char g_status[64];

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
    for (int i = 0; i < n && i < buflen - 1; i++) buf[i] = tmp[n - 1 - i];
    buf[n < buflen - 1 ? n : buflen - 1] = '\0';
}

/* Copy at most dst_max-1 chars from src (up to src_len), NUL-terminate */
static void my_strlcpy(char *dst, const char *src, int src_len, int dst_max)
{
    int n = src_len < dst_max - 1 ? src_len : dst_max - 1;
    for (int i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = '\0';
}

/* Check if str ends with .txt (case-insensitive) */
static int ends_with_txt(const char *s, int len)
{
    if (len < 4) return 0;
    char a = s[len-4], b = s[len-3], c = s[len-2], d = s[len-1];
    return a == '.' &&
           (b == 't' || b == 'T') &&
           (c == 'x' || c == 'X') &&
           (d == 't' || d == 'T');
}

/* ── DuckyScript parser ─────────────────────────────────────────────────── */

/*
 * Token matching: case-insensitive, up to tlen chars of tok vs a keyword literal.
 * kwlen = strlen of keyword (compile-time constant).
 */
static int tok_eq(const char *tok, int tlen, const char *kw, int kwlen)
{
    if (tlen != kwlen) return 0;
    for (int i = 0; i < tlen; i++) {
        char a = tok[i], b = kw[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

/* Parse a decimal integer starting at line[pos]. Returns value; advances *pos. */
static int parse_int(const char *line, int *pos, int len)
{
    while (*pos < len && line[*pos] == ' ') (*pos)++;
    int v = 0;
    while (*pos < len && line[*pos] >= '0' && line[*pos] <= '9') {
        v = v * 10 + (line[*pos] - '0');
        (*pos)++;
    }
    return v;
}

/* Returns next space-delimited token. Returns token length (0 = end). */
static int next_token(const char *line, int *pos, int len, const char **tok_start)
{
    while (*pos < len && line[*pos] == ' ') (*pos)++;
    if (*pos >= len) return 0;
    *tok_start = line + *pos;
    int start = *pos;
    while (*pos < len && line[*pos] != ' ' && line[*pos] != '\r' && line[*pos] != '\n')
        (*pos)++;
    return *pos - start;
}

/* Return HID modifier bit for keyword, or 0 if not a modifier */
static int tok_to_mod(const char *tok, int tlen)
{
    if (tok_eq(tok, tlen, "CTRL",    4)) return HID_MOD_LEFT_CTRL;
    if (tok_eq(tok, tlen, "CONTROL", 7)) return HID_MOD_LEFT_CTRL;
    if (tok_eq(tok, tlen, "SHIFT",   5)) return HID_MOD_LEFT_SHIFT;
    if (tok_eq(tok, tlen, "ALT",     3)) return HID_MOD_LEFT_ALT;
    if (tok_eq(tok, tlen, "GUI",     3)) return HID_MOD_LEFT_GUI;
    if (tok_eq(tok, tlen, "WINDOWS", 7)) return HID_MOD_LEFT_GUI;
    if (tok_eq(tok, tlen, "COMMAND", 7)) return HID_MOD_LEFT_GUI;
    return 0;
}

/* Return HID keycode for keyword, or -1 if not a keycode */
static int tok_to_key(const char *tok, int tlen)
{
    if (tok_eq(tok, tlen, "ENTER",       5)) return HID_KEY_ENTER;
    if (tok_eq(tok, tlen, "RETURN",      6)) return HID_KEY_ENTER;
    if (tok_eq(tok, tlen, "ESC",         3)) return HID_KEY_ESC;
    if (tok_eq(tok, tlen, "ESCAPE",      6)) return HID_KEY_ESC;
    if (tok_eq(tok, tlen, "BACKSPACE",   9)) return HID_KEY_BACKSPACE;
    if (tok_eq(tok, tlen, "TAB",         3)) return HID_KEY_TAB;
    if (tok_eq(tok, tlen, "SPACE",       5)) return HID_KEY_SPACE;
    if (tok_eq(tok, tlen, "DELETE",      6)) return HID_KEY_DELETE;
    if (tok_eq(tok, tlen, "DEL",         3)) return HID_KEY_DELETE;
    if (tok_eq(tok, tlen, "INSERT",      6)) return HID_KEY_INSERT;
    if (tok_eq(tok, tlen, "HOME",        4)) return HID_KEY_HOME;
    if (tok_eq(tok, tlen, "END",         3)) return HID_KEY_END;
    if (tok_eq(tok, tlen, "PAGEUP",      6)) return HID_KEY_PAGEUP;
    if (tok_eq(tok, tlen, "PAGEDOWN",    8)) return HID_KEY_PAGEDOWN;
    if (tok_eq(tok, tlen, "UP",          2)) return HID_KEY_UP;
    if (tok_eq(tok, tlen, "DOWN",        4)) return HID_KEY_DOWN;
    if (tok_eq(tok, tlen, "LEFT",        4)) return HID_KEY_LEFT;
    if (tok_eq(tok, tlen, "RIGHT",       5)) return HID_KEY_RIGHT;
    if (tok_eq(tok, tlen, "PRINTSCREEN", 11)) return HID_KEY_PRINTSCREEN;
    if (tok_eq(tok, tlen, "PRTSCN",      6)) return HID_KEY_PRINTSCREEN;
    if (tok_eq(tok, tlen, "PAUSE",       5)) return HID_KEY_PAUSE;
    if (tok_eq(tok, tlen, "BREAK",       5)) return HID_KEY_PAUSE;
    if (tok_eq(tok, tlen, "CAPSLOCK",    8)) return HID_KEY_CAPSLOCK;
    if (tok_eq(tok, tlen, "NUMLOCK",     7)) return HID_KEY_NUMLOCK;
    if (tok_eq(tok, tlen, "SCROLLLOCK", 10)) return HID_KEY_SCROLLLOCK;
    if (tok_eq(tok, tlen, "APP",         3)) return HID_KEY_APP;
    if (tok_eq(tok, tlen, "MENU",        4)) return HID_KEY_APP;
    /* F1-F12 */
    if (tlen == 2 && (tok[0] == 'F' || tok[0] == 'f') &&
        tok[1] >= '1' && tok[1] <= '9')
        return HID_KEY_F1 + (tok[1] - '1');
    if (tlen == 3 && (tok[0] == 'F' || tok[0] == 'f') && tok[1] == '1') {
        if (tok[2] == '0') return HID_KEY_F10;
        if (tok[2] == '1') return HID_KEY_F11;
        if (tok[2] == '2') return HID_KEY_F12;
    }
    /* Single character (a-z, 0-9) — DuckyScript lower-case extension */
    if (tlen == 1) {
        char c = tok[0];
        if (c >= 'a' && c <= 'z') return HID_KEY_A + (c - 'a');
        if (c >= 'A' && c <= 'Z') return HID_KEY_A + (c - 'A');
        if (c >= '1' && c <= '9') return HID_KEY_1 + (c - '1');
        if (c == '0') return HID_KEY_0;
    }
    return -1;
}

/* Press modifier+key, then release all. Records as last command. */
static void do_key(int mod, int keycode)
{
    if (mod) hid_set_modifiers(mod);
    if (keycode >= 0) hid_key_press(keycode);
    delay(INTER_KEY_MS);
    hid_key_release_all();
    g_last_type    = LAST_KEY;
    g_last_mod     = mod;
    g_last_keycode = keycode;
}

/* Type a string. Records as last command. */
static void do_string(const char *s, int with_enter)
{
    hid_type_string(s);
    if (with_enter) {
        hid_key_press(HID_KEY_ENTER);
        delay(INTER_KEY_MS);
        hid_key_release_all();
    }
    g_last_type = LAST_STRING;
    int slen = my_strlen(s);
    my_strlcpy(g_last_str, s, slen, LINE_MAX);
    g_last_str[LINE_MAX - 1] = '\0';
}

/* ── Execute one DuckyScript line ─────────────────────────────────────── */

/* line_ptr: start of line; line_len: length (excluding \n) */
static void exec_ducky_line(const char *line, int line_len)
{
    if (line_len == 0) return;
    /* Skip CR if present */
    if (line_len > 0 && line[line_len-1] == '\r') line_len--;
    if (line_len == 0) return;

    int pos = 0;
    const char *cmd;
    int cmd_len = next_token(line, &pos, line_len, &cmd);
    if (cmd_len == 0) return;

    /* REM — comment */
    if (tok_eq(cmd, cmd_len, "REM", 3)) return;

    /* DELAY <ms> */
    if (tok_eq(cmd, cmd_len, "DELAY", 5)) {
        int ms = parse_int(line, &pos, line_len);
        if (ms > 0) {
            uint32_t now = (uint32_t)rtc_get_uptime_ms();
            g_delay_until = now + (uint32_t)ms;
        }
        return;
    }

    /* DEFAULT_DELAY / DEFAULTDELAY */
    if (tok_eq(cmd, cmd_len, "DEFAULT_DELAY", 13) ||
        tok_eq(cmd, cmd_len, "DEFAULTDELAY",  12)) {
        g_default_delay = parse_int(line, &pos, line_len);
        return;
    }

    /* REPEAT <n> — repeat last command */
    if (tok_eq(cmd, cmd_len, "REPEAT", 6)) {
        int n = parse_int(line, &pos, line_len);
        if (n < 1) n = 1;
        for (int i = 0; i < n; i++) {
            if (g_last_type == LAST_KEY) {
                do_key(g_last_mod, g_last_keycode);
            } else if (g_last_type == LAST_STRING) {
                hid_type_string(g_last_str);
            }
            if (g_default_delay > 0) delay(g_default_delay);
        }
        /* REPEAT itself doesn't update the last command */
        return;
    }

    /* STRING / STRINGLN */
    if (tok_eq(cmd, cmd_len, "STRING", 6) ||
        tok_eq(cmd, cmd_len, "STRINGLN", 8)) {
        int with_enter = tok_eq(cmd, cmd_len, "STRINGLN", 8);
        /* Rest of line after command+space is the string */
        while (pos < line_len && line[pos] == ' ') pos++;
        int slen = line_len - pos;
        if (slen > 0) {
            char tmp[LINE_MAX];
            my_strlcpy(tmp, line + pos, slen, LINE_MAX);
            do_string(tmp, with_enter);
        } else if (with_enter) {
            hid_key_press(HID_KEY_ENTER);
            delay(INTER_KEY_MS);
            hid_key_release_all();
        }
        return;
    }

    /*
     * Modifier+key combos and standalone special keys.
     * Parse all tokens on the line; accumulate modifiers, collect the
     * first non-modifier token as keycode.
     */
    int combined_mod = 0;
    int keycode = -1;

    /* Re-scan from the beginning of the line including cmd */
    pos = 0;
    while (1) {
        const char *tok;
        int tlen = next_token(line, &pos, line_len, &tok);
        if (tlen == 0) break;

        int mod = tok_to_mod(tok, tlen);
        if (mod) {
            combined_mod |= mod;
            continue;
        }
        int kc = tok_to_key(tok, tlen);
        if (kc >= 0 && keycode < 0) {
            keycode = kc;
        }
        /* Unknown tokens ignored */
    }

    /* Execute: if we got at least a modifier or a key */
    if (combined_mod || keycode >= 0) {
        do_key(combined_mod, keycode);
    }
}

/* ── Payload execution tick ─────────────────────────────────────────────── */

/* Called each main-loop iteration while in STATE_EXEC.
 * Advances execution by one line per call (unless blocked by delay). */
static void exec_tick(void)
{
    if (g_exec_done) return;

    /* Check if waiting for DELAY to expire */
    uint32_t now = (uint32_t)rtc_get_uptime_ms();
    if (g_delay_until && now < g_delay_until) return;
    g_delay_until = 0;

    /* Find next line */
    while (g_exec_pos < g_payload_len) {
        /* Find end of current line */
        int start = g_exec_pos;
        while (g_exec_pos < g_payload_len &&
               g_payload[g_exec_pos] != '\n' &&
               g_payload[g_exec_pos] != '\0') {
            g_exec_pos++;
        }
        int line_len = g_exec_pos - start;
        if (g_exec_pos < g_payload_len) g_exec_pos++; /* skip \n */

        g_exec_line++;

        /* Execute the line */
        exec_ducky_line(g_payload + start, line_len);

        /* Apply default delay after every non-whitespace command */
        if (g_default_delay > 0) {
            g_delay_until = (uint32_t)rtc_get_uptime_ms() + (uint32_t)g_default_delay;
            return; /* come back after delay */
        }
        return; /* one line per tick */
    }

    /* All lines done */
    g_exec_done = 1;
    hid_key_release_all();
    g_status[0] = 'O'; g_status[1] = 'K'; g_status[2] = ' ';
    char num[8];
    my_itoa(g_exec_line, num, sizeof(num));
    int ni = 0;
    while (num[ni] && ni < 8) { g_status[3 + ni] = num[ni]; ni++; }
    g_status[3 + ni] = ' ';
    g_status[4 + ni] = 'l';
    g_status[5 + ni] = 'i';
    g_status[6 + ni] = 'n';
    g_status[7 + ni] = 'e';
    g_status[8 + ni] = 's';
    g_status[9 + ni] = '\0';
    g_state = STATE_DONE;
}

/* ── Payload loading ─────────────────────────────────────────────────────── */

static void load_file_list(void)
{
    g_file_count = 0;
    g_sel        = 0;
    g_scroll     = 0;

    /* Ensure payloads directory exists */
    fs_mkdir(PAYLOAD_DIR);

    char dir_buf[1024];
    int n = fs_readdir(PAYLOAD_DIR, dir_buf, sizeof(dir_buf));
    if (n <= 0) return;

    /* Parse newline-separated names, keep only .txt files */
    int pos = 0;
    while (pos < n && g_file_count < MAX_PAYLOADS) {
        int start = pos;
        while (pos < n && dir_buf[pos] != '\n' && dir_buf[pos] != '\0') pos++;
        int entry_len = pos - start;
        if (pos < n) pos++;
        if (entry_len == 0) continue;
        if (!ends_with_txt(dir_buf + start, entry_len)) continue;
        my_strlcpy(g_files[g_file_count], dir_buf + start, entry_len, MAX_FILENAME);
        g_file_count++;
    }
}

static int load_payload(int idx)
{
    if (idx < 0 || idx >= g_file_count) return -1;

    /* Build full path: payloads/<filename> */
    char path[MAX_FILENAME + 12];
    int plen = 0;
    const char *pdir = PAYLOAD_DIR "/";
    while (pdir[plen]) { path[plen] = pdir[plen]; plen++; }
    int flen = my_strlen(g_files[idx]);
    for (int i = 0; i < flen && plen < (int)sizeof(path) - 1; i++)
        path[plen++] = g_files[idx][i];
    path[plen] = '\0';

    int fd = fs_open(path, AKIRA_FS_O_READ);
    if (fd < 0) return fd;

    int total = 0;
    while (total < PAYLOAD_MAX) {
        int n = fs_read(fd, g_payload + total, PAYLOAD_MAX - total);
        if (n <= 0) break;
        total += n;
    }
    fs_close(fd);
    g_payload_len = total;

    /* Count lines for progress */
    g_total_lines = 0;
    for (int i = 0; i < total; i++) {
        if (g_payload[i] == '\n') g_total_lines++;
    }
    if (total > 0 && g_payload[total - 1] != '\n') g_total_lines++;

    /* Reset execution state */
    g_exec_pos       = 0;
    g_exec_line      = 0;
    g_default_delay  = 0;
    g_delay_until    = 0;
    g_exec_done      = 0;
    g_exec_error     = -1;
    g_last_type      = LAST_NONE;
    g_status[0]      = '\0';
    return 0;
}

/* ── Progress bar ─────────────────────────────────────────────────────────── */

static void draw_progress(int y, int h, uint32_t filled, uint32_t total, uint16_t color)
{
    int w = total > 0 ? (int)((uint32_t)(DW - 8) * filled / total) : 0;
    display_rect(4, y, DW - 8, h, COL_SEP);
    if (w > 0) display_rect(4, y, w, h, color);
}

/* ── Display functions ────────────────────────────────────────────────────── */

static void draw_hdr(const char *title)
{
    /* Shared chrome: kit status bar (spans real display via get_size). */
    akira_ui_status_t sb = {
        .title = title,
        .clock = hid_is_connected() ? "USB OK" : "NO USB",
        .battery_pct = -1,
    };
    akira_ui_status_bar(&sb);
}

static void draw_footer(const char *hint)
{
    display_rect(0, FOT_Y, DW, FOT_H, COL_HDR);
    display_text(4, FOT_Y + 2, hint, COL_DIM);
}

static void draw_list(void)
{
    draw_hdr("RUBBER DUCKY");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int rows = (DH - HDR_H - FOT_H) / ROW_H;

    if (g_file_count == 0) {
        display_text(8, HDR_H + 12, "No payloads found.", COL_DIM);
        display_text(8, HDR_H + 28, "Copy .txt files to:", COL_DIM);
        display_text(8, HDR_H + 44, "payloads/", COL_ACC);
        display_text(8, HDR_H + 60, "(in app sandbox)", COL_DIM);
    }

    for (int i = 0; i < rows && (i + g_scroll) < g_file_count; i++) {
        int idx = i + g_scroll;
        int y   = HDR_H + i * ROW_H;
        uint16_t bg = (idx == g_sel) ? COL_SEL : COL_BG;
        display_rect(0, y, DW, ROW_H, bg);
        display_text(8, y + 3, g_files[idx], idx == g_sel ? COL_TXT : COL_DIM);
    }

    draw_footer("[A]Select [B]Exit [Y]Reload");
}

static void draw_confirm(void)
{
    draw_hdr("CONFIRM EXECUTE");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 8;
    display_text(8, y, "Payload:", COL_DIM);
    y += ROW_H;
    if (g_sel >= 0 && g_sel < g_file_count)
        display_text(8, y, g_files[g_sel], COL_ACC);
    y += ROW_H + 4;

    display_rect(0, y, DW, 1, COL_SEP);
    y += 6;
    display_text(8, y, "Hold [A] for 2 seconds", COL_WARN);
    y += ROW_H;
    display_text(8, y, "to inject keystrokes.", COL_WARN);
    y += ROW_H + 4;

    if (!hid_is_connected()) {
        display_text(8, y, "WARNING: USB not connected", COL_ERR);
        y += ROW_H;
    }

    /* Countdown progress bar */
    uint32_t now  = (uint32_t)rtc_get_uptime_ms();
    uint32_t held = (g_hold_start && now > g_hold_start)
                    ? now - g_hold_start : 0;
    if (held > HOLD_DURATION) held = HOLD_DURATION;
    draw_progress(y, 10, held, HOLD_DURATION, COL_OK);

    draw_footer("[A]Hold to confirm [B]Cancel");
}

static void draw_exec(void)
{
    draw_hdr("EXECUTING");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 8;

    char line_info[32];
    line_info[0] = 'L';
    line_info[1] = 'i';
    line_info[2] = 'n';
    line_info[3] = 'e';
    line_info[4] = ' ';
    char num[8];
    my_itoa(g_exec_line, num, sizeof(num));
    int ni = 0;
    while (num[ni]) { line_info[5 + ni] = num[ni]; ni++; }
    line_info[5 + ni] = ' ';
    line_info[6 + ni] = '/';
    line_info[7 + ni] = ' ';
    my_itoa(g_total_lines, num, sizeof(num));
    int nj = 0;
    while (num[nj]) { line_info[8 + ni + nj] = num[nj]; nj++; }
    line_info[8 + ni + nj] = '\0';

    display_text(8, y, line_info, COL_TXT);
    y += ROW_H + 4;

    /* Show current file name */
    if (g_sel >= 0 && g_sel < g_file_count)
        display_text(8, y, g_files[g_sel], COL_DIM);
    y += ROW_H + 4;

    display_rect(0, y, DW, 1, COL_SEP);
    y += 6;
    display_text(8, y, "DO NOT UNPLUG USB", COL_WARN);
    y += ROW_H + 8;

    /* Progress bar */
    int done  = g_total_lines > 0 ? g_exec_line : 0;
    int total = g_total_lines > 0 ? g_total_lines : 1;
    draw_progress(y, 12, (uint32_t)done, (uint32_t)total, COL_OK);

    draw_footer("Running... [B] has no effect");
}

static void draw_done(void)
{
    draw_hdr("DONE");
    display_rect(0, HDR_H, DW, DH - HDR_H - FOT_H, COL_BG);

    int y = HDR_H + 16;
    display_text(8, y, g_status[0] ? g_status : "Finished", COL_OK);
    y += ROW_H + 4;

    if (g_sel >= 0 && g_sel < g_file_count) {
        display_text(8, y, g_files[g_sel], COL_DIM);
        y += ROW_H;
    }

    draw_progress(y + 4, 8, 1, 1, COL_OK);

    draw_footer("[A/B]Back to list");
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    display_get_size(&DW, &DH); /* adapt layout to the real display width */

    /* Initialise USB HID keyboard */
    hid_init(HID_TRANSPORT_USB, HID_DEVICE_KEYBOARD);

    load_file_list();

    int needs_redraw = 1;
    uint32_t last_redraw = 0;
    int prev_btns = 0;

    while (1) {
        /* Execution tick (only in EXEC state) */
        if (g_state == STATE_EXEC) exec_tick();

        int btns    = input_get_buttons();
        int pressed = btns & ~prev_btns;
        prev_btns   = btns;

        if (pressed || btns) needs_redraw = 1;

        /* ── LIST state ── */
        if (g_state == STATE_LIST) {
            int rows = (DH - HDR_H - FOT_H) / ROW_H;
            if (pressed & AKIRA_BTN_UP)   { if (g_sel > 0) { g_sel--; if (g_sel < g_scroll) g_scroll = g_sel; } }
            if (pressed & AKIRA_BTN_DOWN)  { if (g_sel < g_file_count - 1) { g_sel++; if (g_sel >= g_scroll + rows) g_scroll = g_sel - rows + 1; } }
            if (pressed & AKIRA_BTN_Y)     { load_file_list(); }
            if (pressed & AKIRA_BTN_B)     { app_switch("supervisor"); return 0; }
            if ((pressed & AKIRA_BTN_A) && g_file_count > 0) {
                if (load_payload(g_sel) == 0) {
                    g_state      = STATE_CONFIRM;
                    g_hold_start = 0;
                }
            }
        }
        /* ── CONFIRM state ── */
        else if (g_state == STATE_CONFIRM) {
            /* Running a DuckyScript payload injects keystrokes into the USB
             * host — a restricted syscall. Gate it through the shared 3px
             * Capability-Guard dialog. */
            bool ok = akira_ui_confirm_dialog("USB_HID",
                                              "run DuckyScript payload?");
            g_state      = ok ? STATE_EXEC : STATE_LIST;
            g_hold_start = 0;
            prev_btns    = input_get_buttons(); /* swallow held buttons */
        }
        /* ── EXEC state ── */
        else if (g_state == STATE_EXEC) {
            /* No user interruption during execution */
            (void)pressed;
        }
        /* ── DONE state ── */
        else if (g_state == STATE_DONE) {
            if (pressed & (AKIRA_BTN_A | AKIRA_BTN_B)) {
                g_state = STATE_LIST;
            }
        }

        uint32_t now_ms = (uint32_t)rtc_get_uptime_ms();
        if (needs_redraw || (now_ms - last_redraw) > 80) {
            switch (g_state) {
            case STATE_LIST:    draw_list();    break;
            case STATE_CONFIRM: draw_confirm(); break;
            case STATE_EXEC:    draw_exec();    break;
            case STATE_DONE:    draw_done();    break;
            default: break;
            }
            display_flush();
            last_redraw  = now_ms;
            needs_redraw = 0;
        }

        delay(20);
    }

    return 0;
}
