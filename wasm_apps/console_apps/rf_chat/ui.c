/*
 * ui.c — RF Chat display rendering (monochrome, dynamic layout)
 * SPDX-License-Identifier: Apache-2.0
 */
#include "app.h"

/* ── Display WASM import declarations ──────────────────────────────────── */
extern int display_clear(uint32_t color);
extern int display_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
extern int display_rect_outline(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
extern int display_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern int display_text_large(int32_t x, int32_t y, const char *text, uint32_t color);
extern int display_hline(int32_t x, int32_t y, int32_t len, uint32_t color);
extern int display_vline(int32_t x, int32_t y, int32_t len, uint32_t color);
extern int display_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
extern int display_pixel(int32_t x, int32_t y, uint32_t color);
extern int display_flush(void);

/* ── Monochrome palette ─────────────────────────────────────────────────── */
#define BLK  0x0000u
#define WHT  0xFFFFu

/* ── Fixed chrome heights (pixels) ─────────────────────────────────────── */
#define HDR_H    20
#define FTR_H    20

/* Derived at runtime from GH:
 *   FTR_Y      = GH - FTR_H
 *   CONTENT_Y  = HDR_H
 *   CONTENT_H  = GH - HDR_H - FTR_H
 */
#define FTR_Y()      (GH - FTR_H)
#define CONTENT_Y()  (HDR_H)
#define CONTENT_H()  (GH - HDR_H - FTR_H)

/* ── Bubble geometry ────────────────────────────────────────────────────── */
#define BUBBLE_PAD_X   4
#define BUBBLE_PAD_Y   3
#define LINE_H        12   /* small font 10px + 2px gap */
#define MSG_GAP        4

/* Bubble max width = 65% of display width */
#define BUBBLE_MAX_W()   ((GW * 2) / 3)
#define BUBBLE_INNER_W() (BUBBLE_MAX_W() - BUBBLE_PAD_X * 2)
#define CHARS_PER_LINE() (BUBBLE_INNER_W() / 7)   /* 7px per char */

/* ── Keyboard geometry ──────────────────────────────────────────────────── */
#define KEY_W    26
#define KEY_H    28
#define KEY_GAP   2
#define KB_ROW_STEP  33   /* KEY_H + 5 */

/* Keyboard top = CONTENT_Y + 55 (2 preview lines + padding) */
#define KB_TOP()  (CONTENT_Y() + 55)

/* Row start X: center the row horizontally */
static int kb_row_x(int row) {
    static const int lens[] = { 10, 9, 7 };
    int n = lens[row];
    return (GW - (n * KEY_W + (n - 1) * KEY_GAP)) / 2;
}

/* Special-key row (row 3): evenly divide GW into 3 equal keys */
static void spk_rect(int col, int *kx, int *kw) {
    int pad = 8, gap = 5;
    int total = GW - 2 * pad - 2 * gap;
    *kw = total / 3;
    *kx = pad + col * (*kw + gap);
}

/* ── String helpers ─────────────────────────────────────────────────────── */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void fmt_uint(char *buf, uint32_t v, int *len) {
    if (!v) { buf[0] = '0'; buf[1] = '\0'; *len = 1; return; }
    char tmp[12]; int i = 0;
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    *len = i;
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

static void fmt_freq(char *buf, uint32_t hz) {
    uint32_t mhz = hz / 1000000u;
    uint32_t khz = (hz % 1000000u) / 1000u;
    int n;
    fmt_uint(buf, mhz, &n);
    buf[n++] = '.';
    buf[n++] = (char)('0' + khz / 100u);
    buf[n++] = (char)('0' + (khz / 10u) % 10u);
    buf[n++] = (char)('0' + khz % 10u);
    buf[n++] = ' '; buf[n++] = 'M'; buf[n++] = 'H'; buf[n++] = 'z';
    buf[n]   = '\0';
}

static void fmt_rssi(char *buf, int8_t rssi) {
    if (!rssi) { buf[0]='-'; buf[1]='-'; buf[2]='-'; buf[3]='d'; buf[4]='B'; buf[5]='\0'; return; }
    int n = 0, v = (int)rssi;
    if (v < 0) { buf[n++] = '-'; v = -v; }
    int tmp_n; fmt_uint(buf + n, (uint32_t)v, &tmp_n); n += tmp_n;
    buf[n++] = 'd'; buf[n++] = 'B'; buf[n] = '\0';
}

/* ── Shared header / footer ─────────────────────────────────────────────── */
static void draw_header(void) {
    display_rect(0, 0, GW, HDR_H, WHT);

    char fbuf[16], rbuf[10];
    fmt_freq(fbuf, g_freq_hz);
    fmt_rssi(rbuf, g_rssi);

    display_text(4, 5, "RF Chat", BLK);
    int flen = slen(fbuf);
    display_text((GW - flen * 7) / 2, 5, fbuf, BLK);
    int rlen = slen(rbuf);
    display_text(GW - rlen * 7 - 4, 5, rbuf, BLK);
}

static void draw_footer(const char *hint) {
    display_rect(0, FTR_Y(), GW, FTR_H, WHT);
    display_text(4, FTR_Y() + 5, hint, BLK);
}

/* ── Text block renderer ────────────────────────────────────────────────── */
static void draw_text_block(int x, int y, const char *text, int len,
                             uint32_t color, int *lines_out) {
    int cpl = CHARS_PER_LINE();
    if (cpl < 1) cpl = 1;
    char buf[64];
    int lines = 0, off = 0;
    while (off < len) {
        int chunk = len - off;
        if (chunk > cpl) chunk = cpl;
        if (chunk > 63)  chunk = 63;
        for (int i = 0; i < chunk; i++) buf[i] = text[off + i];
        buf[chunk] = '\0';
        display_text(x, y + lines * LINE_H, buf, color);
        off += chunk; lines++;
    }
    if (lines_out) *lines_out = lines;
}

static int msg_height(const chat_msg_t *m) {
    int cpl = CHARS_PER_LINE();
    if (cpl < 1) cpl = 1;
    int lines = (m->len + cpl - 1) / cpl;
    if (lines < 1) lines = 1;
    return lines * LINE_H + BUBBLE_PAD_Y * 2;
}

/* ── Chat screen ────────────────────────────────────────────────────────── */
static void draw_chat(void) {
    display_clear(BLK);
    draw_header();
    draw_footer("[A] Write  [X] Options");

    if (!g_msg_count) {
        int cx = (GW - 15 * 7) / 2;
        display_text(cx, CONTENT_Y() + CONTENT_H() / 2, "No messages yet", WHT);
        display_flush();
        return;
    }

    int y       = FTR_Y() - MSG_GAP;
    int end_idx = g_msg_count - 1 - g_scroll;
    if (end_idx < 0) end_idx = 0;

    for (int i = end_idx; i >= 0; i--) {
        const chat_msg_t *m = &g_msgs[i];
        if (!m->len) continue;

        int bw         = BUBBLE_MAX_W();
        int bh         = msg_height(m);
        int has_sender = (!m->sent && m->sender[0]) ? 1 : 0;
        y -= bh + (has_sender ? LINE_H : 0);
        if (y < CONTENT_Y()) break;

        if (m->sent) {
            /* Sent: inverted (white fill, black text), right-aligned */
            int bx = GW - 6 - bw;
            display_rect(bx, y, bw, bh, WHT);
            draw_text_block(bx + BUBBLE_PAD_X, y + BUBBLE_PAD_Y,
                            m->text, m->len, BLK, 0);
        } else {
            int bx = 6;
            if (has_sender)
                display_text(bx + BUBBLE_PAD_X, y, m->sender, WHT);
            int by = y + (has_sender ? LINE_H : 0);
            /* Received: white border, black fill, white text */
            display_rect(bx, by, bw, bh, WHT);
            display_rect(bx + 1, by + 1, bw - 2, bh - 2, BLK);
            draw_text_block(bx + BUBBLE_PAD_X, by + BUBBLE_PAD_Y,
                            m->text, m->len, WHT, 0);
        }

        y -= MSG_GAP;
    }

    if (g_scroll > 0)
        display_text(GW - 28, CONTENT_Y() + 2, "^ up", WHT);
    if (g_msg_count > 0 && g_scroll < g_msg_count - 1)
        display_text(GW - 28, FTR_Y() - 12, "v dn", WHT);

    display_flush();
}

/* ── QWERTY keyboard ────────────────────────────────────────────────────── */
static const char *KB_ROW_CHARS[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
static const int   KB_ROW_LENS[]  = { 10, 9, 7, 3 };
static const char *SPK_LABELS[]   = { "SPC", "DEL", "SEND" };

static void draw_keyboard(void) {
    for (int row = 0; row < 4; row++) {
        int ncols = KB_ROW_LENS[row];
        int ky    = KB_TOP() + row * KB_ROW_STEP;

        for (int col = 0; col < ncols; col++) {
            int kx, kw;
            if (row < 3) {
                kx = kb_row_x(row) + col * (KEY_W + KEY_GAP);
                kw = KEY_W;
            } else {
                spk_rect(col, &kx, &kw);
            }

            int selected = (row == g_kb_row && col == g_kb_col);

            if (selected) {
                /* Selected: white fill, black text */
                display_rect(kx, ky, kw, KEY_H, WHT);
            } else {
                /* Normal: black fill, white border */
                display_rect(kx, ky, kw, KEY_H, WHT);
                display_rect(kx + 1, ky + 1, kw - 2, KEY_H - 2, BLK);
            }

            uint32_t tc = selected ? BLK : WHT;

            if (row < 3) {
                char lbl[2] = { KB_ROW_CHARS[row][col], '\0' };
                display_text(kx + (kw - 7) / 2, ky + (KEY_H - 10) / 2, lbl, tc);
            } else {
                const char *sp  = SPK_LABELS[col];
                int          sl = slen(sp);
                display_text(kx + (kw - sl * 7) / 2,
                             ky + (KEY_H - 10) / 2, sp, tc);
            }
        }
    }
}

static void draw_compose(void) {
    display_clear(BLK);
    draw_header();
    draw_footer("[A] Select  [B] Backspace  [Y] Send");

    /* Preview box */
    int box_h = 46;
    display_rect(4, CONTENT_Y() + 2, GW - 8, box_h, WHT);
    display_rect(5, CONTENT_Y() + 3, GW - 10, box_h - 2, BLK);

    /* Compose text + cursor */
    char prev[MAX_MSG_LEN + 2];
    for (int i = 0; i < g_compose_len; i++) prev[i] = g_compose[i];
    prev[g_compose_len]     = '_';
    prev[g_compose_len + 1] = '\0';

    int cpl = (GW - 16) / 7;
    if (cpl < 1) cpl = 1;
    char line[64];
    int  off = 0, py = CONTENT_Y() + 6, total = g_compose_len + 1;
    for (int ln = 0; ln < 3 && off < total; ln++) {
        int chunk = total - off;
        if (chunk > cpl) chunk = cpl;
        if (chunk > 63)  chunk = 63;
        for (int k = 0; k < chunk; k++) line[k] = prev[off + k];
        line[chunk] = '\0';
        display_text(8, py + ln * LINE_H, line, WHT);
        off += chunk;
    }

    draw_keyboard();
    display_flush();
}

/* ── Options screen ─────────────────────────────────────────────────────── */
static void draw_options(void) {
    display_clear(BLK);
    draw_header();
    draw_footer("^v:nav  L/R:adj  A:edit  B:save  X:back");

    int y = CONTENT_Y() + 4;
    display_text_large(8, y, "Settings", WHT);
    display_hline(0, y + 16, GW, WHT);
    y += 22;

    char freq_buf[16];
    fmt_freq(freq_buf, g_opt_freq);
    const char *chip_name = (g_opt_chip == RF_CHIP_CC1121) ? "CC1121" : "LR2021";

    const char *labels[3] = { "Frequency", "Radio Chip", "Name" };
    const char *values[3] = { freq_buf, chip_name, g_my_name };

    for (int r = 0; r < 3; r++) {
        int ry  = y + r * 34;
        int sel = (r == g_opt_cursor);
        if (sel) {
            display_rect(0, ry, GW, 30, WHT);
            display_text(6,  ry + 3,  labels[r], BLK);
            display_text_large(10, ry + 14, values[r], BLK);
            display_text(GW - 28, ry + 10, r < 2 ? "< >" : "[A]", BLK);
        } else {
            display_text(10, ry + 3,  labels[r], WHT);
            display_text_large(14, ry + 14, values[r], WHT);
        }
    }

    int info_y = y + 3 * 34 + 6;
    display_hline(0, info_y, GW, WHT);
    char rbuf[10];
    fmt_rssi(rbuf, g_rssi);
    display_text(8, info_y + 6, "RSSI:", WHT);
    display_text(50, info_y + 6, rbuf, WHT);

    display_flush();
}

/* ── Radio selection icons ──────────────────────────────────────────────── */
static void draw_chip_icon(int cx, int cy, uint32_t fg, uint32_t bg) {
    /* IC body */
    int bw = 36, bh = 28;
    int bx = cx - bw / 2, by = cy - bh / 2;
    display_rect(bx, by, bw, bh, fg);
    display_rect(bx + 1, by + 1, bw - 2, bh - 2, bg);
    /* 4 pins each side */
    for (int i = 0; i < 4; i++) {
        int py = by + 4 + i * 6;
        display_hline(bx - 7, py, 7, fg);
        display_hline(bx + bw, py, 7, fg);
    }
    /* pin dots */
    for (int i = 0; i < 4; i++) {
        display_pixel(bx - 8, by + 4 + i * 6, fg);
        display_pixel(bx + bw + 8, by + 4 + i * 6, fg);
    }
}

static void draw_antenna_icon(int cx, int cy, uint32_t fg) {
    /* Vertical pole */
    display_vline(cx, cy - 22, 32, fg);
    /* Base */
    display_hline(cx - 8, cy + 10, 17, fg);
    display_pixel(cx - 9, cy + 11, fg);
    display_pixel(cx + 9, cy + 11, fg);
    /* Signal waves: 3 pairs of diverging lines */
    display_line(cx, cy - 18, cx - 6,  cy - 6,  fg);
    display_line(cx, cy - 18, cx + 6,  cy - 6,  fg);
    display_line(cx, cy - 18, cx - 11, cy + 2,  fg);
    display_line(cx, cy - 18, cx + 11, cy + 2,  fg);
    display_line(cx, cy - 18, cx - 16, cy + 8,  fg);
    display_line(cx, cy - 18, cx + 16, cy + 8,  fg);
    /* Dot at tip */
    display_rect(cx - 1, cy - 24, 3, 3, fg);
}

/* ── Radio selection screen ─────────────────────────────────────────────── */
static void draw_radio_sel(void) {
    display_clear(WHT);   /* white background — lighter look */

    /* Header: black bar */
    display_rect(0, 0, GW, HDR_H, BLK);
    int tlen = slen("SELECT RADIO") * 7;
    display_text((GW - tlen) / 2, 3, "SELECT RADIO", WHT);

    /* Vertical divider */
    int mid = GW / 2;
    display_vline(mid, HDR_H, GH - HDR_H - FTR_H, BLK);

    /* Footer: black bar */
    display_rect(0, FTR_Y(), GW, FTR_H, BLK);
    display_text(4, FTR_Y() + 3, "L/R: select    A: confirm", WHT);

    int card_y = HDR_H + 6;
    int card_h = GH - HDR_H - FTR_H - 12;
    int icon_y = card_y + card_h / 2 - 24;

    /* ── CC1121 card (left) ── */
    int sel0 = (g_radio_cursor == 0);
    {
        int bx = 4, bw = mid - 8;
        if (sel0) {
            display_rect(bx, card_y, bw, card_h, BLK);
        } else {
            /* white fill already from display_clear(WHT), just draw border */
            display_rect_outline(bx, card_y, bw, card_h, BLK);
        }
        uint32_t fg = sel0 ? WHT : BLK;
        uint32_t bg = sel0 ? BLK : WHT;
        int ccx = mid / 2;
        draw_chip_icon(ccx, icon_y, fg, bg);
        int nl = slen("CC1121") * 7;
        display_text(ccx - nl / 2, icon_y + 26, "CC1121", fg);
        int fl = slen("Sub-GHz FSK") * 7;
        display_text(ccx - fl / 2, icon_y + 38, "Sub-GHz FSK", fg);
        if (sel0) {
            int sl = slen("[ SELECTED ]") * 7;
            display_text(ccx - sl / 2, card_y + card_h - 16, "[ SELECTED ]", WHT);
        }
    }

    /* ── LR2021 card (right) ── */
    int sel1 = (g_radio_cursor == 1);
    {
        int bx = mid + 4, bw = mid - 8;
        if (sel1) {
            display_rect(bx, card_y, bw, card_h, BLK);
        } else {
            display_rect_outline(bx, card_y, bw, card_h, BLK);
        }
        uint32_t fg = sel1 ? WHT : BLK;
        int lrx = mid + mid / 2;
        draw_antenna_icon(lrx, icon_y, fg);
        int nl = slen("LR2021") * 7;
        display_text(lrx - nl / 2, icon_y + 26, "LR2021", fg);
        int fl = slen("LoRa / FSK") * 7;
        display_text(lrx - fl / 2, icon_y + 38, "LoRa / FSK", fg);
        if (sel1) {
            int sl = slen("[ SELECTED ]") * 7;
            display_text(lrx - sl / 2, card_y + card_h - 16, "[ SELECTED ]", WHT);
        }
    }

    display_flush();
}

/* ── Name edit screen ───────────────────────────────────────────────────── */
static void draw_name_edit(void) {
    display_clear(BLK);
    draw_header();
    draw_footer("[A] Select  [B] Del/Back  [Y] Save");

    /* Label */
    display_text(8, CONTENT_Y() + 4, "Edit name (max 8):", WHT);

    /* Preview box */
    int box_h = 28;
    display_rect(4, CONTENT_Y() + 18, GW - 8, box_h, WHT);
    display_rect(5, CONTENT_Y() + 19, GW - 10, box_h - 2, BLK);

    /* Name text + cursor */
    char prev[MAX_NAME_LEN + 2];
    for (int i = 0; i < g_compose_len; i++) prev[i] = g_compose[i];
    prev[g_compose_len]     = '_';
    prev[g_compose_len + 1] = '\0';
    display_text(8, CONTENT_Y() + 26, prev, WHT);

    draw_keyboard();
    display_flush();
}

/* ── Entry point ────────────────────────────────────────────────────────── */
void ui_draw(void) {
    switch (g_state) {
    case STATE_RADIO_SEL: draw_radio_sel();  break;
    case STATE_CHAT:      draw_chat();       break;
    case STATE_COMPOSE:   draw_compose();    break;
    case STATE_OPTIONS:   draw_options();    break;
    case STATE_NAME_EDIT: draw_name_edit();  break;
    }
}
