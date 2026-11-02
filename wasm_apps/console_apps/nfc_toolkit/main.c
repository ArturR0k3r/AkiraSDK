/**
 * @file nfc_toolkit.c
 * @brief NFC toolkit — program the console's dynamic NFC tag (AkiraOS)
 *
 * The platform NFC API is tag-side (ST25DV dynamic tag): the console *presents
 * itself as* an NFC tag to a phone/reader. It is NOT a reader — it cannot scan
 * or clone external tags. What this app does:
 *
 *   - Tag info     : UID + live RF-field (reader present) indicator
 *   - Write URL    : lay down an NDEF URI record (tap-to-open-link)
 *   - Write Text   : NDEF Text record
 *   - Hex viewer   : scroll the tag's user memory
 *   - Field monitor: count reader taps (RF field rising edges)
 *   - Erase        : empty NDEF message
 *
 * Needs an ST25DV-equipped board. On akiraconsole_prod Rev A.3 the tag was
 * replaced by the SE050, so nfc_* returns -ENODEV; the app says so clearly.
 *
 * Controls: UP/DOWN move · A select/act · B back · OK(settings) exit
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

#define BTN_UP 4
#define BTN_DOWN 5
#define BTN_LEFT 6
#define BTN_RIGHT 7
#define BTN_A 15
#define BTN_B 16
#define BTN_SETTINGS 0

#define COL_BG 0x0000
#define COL_HEADER_BG 0x0260 /* teal-green bar  */
#define COL_TITLE 0x07FF
#define COL_LABEL 0x8410
#define COL_VALUE 0xFFFF
#define COL_SEL_BG 0x0260
#define COL_OK 0x07E0
#define COL_WARN 0xFFE0
#define COL_BAD 0xF800
#define COL_HINT 0x4208
#define COL_MONO 0x05FF

/* ── small no-libc helpers ──────────────────────────────────────────────── */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void u2s(int v, char *b)
{
    char t[12]; int n = 0, p = 0;
    if (v < 0) { b[p++] = '-'; v = -v; }
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) b[p++] = t[--n];
    b[p] = '\0';
}
static int starts(const char *s, const char *pfx)
{
    while (*pfx) { if (*s++ != *pfx++) return 0; }
    return 1;
}
static char hexd(int v) { return (char)(v < 10 ? '0' + v : 'a' + v - 10); }

/* ── button edges ───────────────────────────────────────────────────────── */
typedef struct { int u, d, l, r, a, b, s; } btn_t;
static btn_t pb;
static void read_edges(btn_t *n)
{
    n->u = gpio_read(BTN_UP);  n->d = gpio_read(BTN_DOWN);
    n->l = gpio_read(BTN_LEFT); n->r = gpio_read(BTN_RIGHT);
    n->a = gpio_read(BTN_A);   n->b = gpio_read(BTN_B);
    n->s = gpio_read(BTN_SETTINGS);
}
#define EDGE(f) (now.f && !pb.f)

/* Human-readable errno for the common NFC failures. */
static const char *errstr(int ret)
{
    switch (ret)
    {
    case -19: return "-19 ENODEV: no NFC tag";
    case -5:  return "-5 EIO: tag I/O error";
    case -22: return "-22 EINVAL: bad args";
    case -1:  return "-1 EPERM: capability?";
    default:  return 0;
    }
}

/* Centered status/result overlay; blocks until A/B/OK. */
static void popup(const char *l1, const char *l2, uint32_t col)
{
    while (gpio_read(BTN_A)) delay(10000);
    delay(40000);
    while (1)
    {
        int mw = SCR_W - 60, mh = 86, mx = 30, my = (SCR_H - mh) / 2;
        display_rect(mx - 2, my - 2, mw + 4, mh + 4, col);
        display_rect(mx, my, mw, mh, 0x0821);
        display_text(mx + 10, my + 16, l1, col);
        if (l2) display_text(mx + 10, my + 38, l2, COL_VALUE);
        display_text(mx + 10, my + 64, "A / B to continue", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(a) || EDGE(b) || EDGE(s)) { pb = now; return; }
        pb = now;
        delay(30000);
    }
}

static void show_ret(const char *action, int ret)
{
    if (ret >= 0) { popup(action, "OK", COL_OK); return; }
    const char *e = errstr(ret);
    char num[16];
    if (!e) { u2s(ret, num); e = num; }
    popup("Failed", e, COL_BAD);
}

/* ── DPAD text entry (char picker) ──────────────────────────────────────── */
static int text_entry(const char *title, const char *charset, int cols,
                      char *out, int maxlen)
{
    int setn = slen(charset);
    int cur = 0, len = 0;
    out[0] = '\0';
    while (gpio_read(BTN_SETTINGS)) delay(10000);
    delay(50000);

    while (1)
    {
        display_clear(COL_BG);
        display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
        display_text(4, 6, title, COL_TITLE);

        display_rect_outline(8, 26, SCR_W - 16, 20, COL_LABEL);
        display_text(12, 31, out[0] ? out : "_", COL_VALUE);

        int cw = (SCR_W - 16) / cols, gy = 56, ch = 18;
        for (int i = 0; i < setn; i++)
        {
            int cx = 8 + (i % cols) * cw;
            int cyy = gy + (i / cols) * ch;
            if (i == cur) display_rect(cx, cyy - 1, cw, ch, COL_SEL_BG);
            char c[2] = {charset[i], 0};
            display_text(cx + cw / 2 - 3, cyy + 3, c, COL_VALUE);
        }
        display_text(4, SCR_H - 12, "A:add B:del OK:done", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(l) && cur > 0) cur--;
        if (EDGE(r) && cur < setn - 1) cur++;
        if (EDGE(u) && cur - cols >= 0) cur -= cols;
        if (EDGE(d) && cur + cols < setn) cur += cols;
        if (EDGE(a) && len < maxlen) { out[len++] = charset[cur]; out[len] = '\0'; }
        if (EDGE(b) && len > 0) out[--len] = '\0';
        if (EDGE(s)) break;
        pb = now;
        delay(30000);
    }
    return len;
}

/* ── NDEF builders (Type-5 tag: CC + NDEF TLV + terminator) ─────────────── */
/* 4-byte Capability Container: magic E1, v1.0 + RW (0x40), 512B area, no flags */
static int put_cc(uint8_t *o) { o[0]=0xE1; o[1]=0x40; o[2]=0x40; o[3]=0x00; return 4; }

/* NDEF URI record. Returns total image length. */
static int build_url(uint8_t *o, const char *uri)
{
    int code = 0x00; const char *rest = uri;
    if      (starts(uri, "https://www.")) { code = 0x02; rest = uri + 12; }
    else if (starts(uri, "http://www."))  { code = 0x01; rest = uri + 11; }
    else if (starts(uri, "https://"))     { code = 0x04; rest = uri + 8; }
    else if (starts(uri, "http://"))      { code = 0x03; rest = uri + 7; }
    int rl = slen(rest);
    int payload = 1 + rl;         /* id code + uri remainder */
    int ndef = 4 + payload;       /* D1 01 LEN 55 + payload  */

    int p = put_cc(o);
    o[p++] = 0x03;                /* NDEF TLV tag            */
    o[p++] = (uint8_t)ndef;       /* TLV length (short form) */
    o[p++] = 0xD1;               /* MB|ME|SR, TNF=well-known */
    o[p++] = 0x01;               /* type length             */
    o[p++] = (uint8_t)payload;    /* payload length          */
    o[p++] = 0x55;               /* 'U' URI record          */
    o[p++] = (uint8_t)code;       /* URI prefix code         */
    for (int i = 0; i < rl; i++) o[p++] = (uint8_t)rest[i];
    o[p++] = 0xFE;                /* TLV terminator          */
    return p;
}

/* NDEF Text record (UTF-8, lang "en"). Returns total image length. */
static int build_text(uint8_t *o, const char *txt)
{
    int tl = slen(txt);
    int payload = 3 + tl;         /* status + "en" + text    */
    int ndef = 4 + payload;

    int p = put_cc(o);
    o[p++] = 0x03;
    o[p++] = (uint8_t)ndef;
    o[p++] = 0xD1;
    o[p++] = 0x01;
    o[p++] = (uint8_t)payload;
    o[p++] = 0x54;               /* 'T' Text record          */
    o[p++] = 0x02;               /* status: UTF-8, lang len 2 */
    o[p++] = 'e'; o[p++] = 'n';
    for (int i = 0; i < tl; i++) o[p++] = (uint8_t)txt[i];
    o[p++] = 0xFE;
    return p;
}

/* ── Screens ────────────────────────────────────────────────────────────── */
static void screen_info(void)
{
    while (gpio_read(BTN_SETTINGS)) delay(10000);
    delay(40000);
    while (1)
    {
        display_clear(COL_BG);
        display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
        display_text(4, 6, "TAG INFO", COL_TITLE);

        uint8_t uid[8];
        int ret = nfc_uid(uid);
        display_text(8, 36, "UID:", COL_LABEL);
        if (ret < 0)
        {
            const char *e = errstr(ret); char num[16];
            if (!e) { u2s(ret, num); e = num; }
            display_text(48, 36, e, COL_BAD);
        }
        else
        {
            char hx[24]; int p = 0;
            for (int i = 0; i < 8; i++)
            { hx[p++] = hexd(uid[i] >> 4); hx[p++] = hexd(uid[i] & 0xf); hx[p++] = ' '; }
            hx[p] = '\0';
            display_text(48, 36, hx, COL_MONO);
        }

        int fld = nfc_field_present();
        display_text(8, 60, "RF field:", COL_LABEL);
        if (fld < 0) display_text(90, 60, "n/a", COL_BAD);
        else display_text(90, 60, fld ? "READER PRESENT" : "none",
                          fld ? COL_OK : COL_HINT);

        display_text(8, SCR_H - 12, "B / OK: back", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(b) || EDGE(s)) { pb = now; return; }
        pb = now;
        delay(120000);
    }
}

static void screen_write_url(void)
{
    const char *presets[] = { "https://akiraos.dev", "https://github.com/", "Custom URL..." };
    int n = 3, sel = 0;
    while (gpio_read(BTN_SETTINGS)) delay(10000);
    delay(40000);

    while (1)
    {
        display_clear(COL_BG);
        display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
        display_text(4, 6, "WRITE URL", COL_TITLE);
        for (int i = 0; i < n; i++)
        {
            int y = 40 + i * 26;
            if (i == sel) display_rect(0, y - 3, SCR_W, 24, COL_SEL_BG);
            display_text(12, y, presets[i], (i == sel) ? COL_VALUE : COL_LABEL);
        }
        display_text(8, SCR_H - 12, "A:write  B:back", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(b) || EDGE(s)) { pb = now; return; }
        if (EDGE(u) && sel > 0) sel--;
        if (EDGE(d) && sel < n - 1) sel++;
        if (EDGE(a))
        {
            pb = now;
            char url[96];
            uint8_t img[160];
            if (sel == n - 1)
            {
                char body[80];
                if (text_entry("URL (no https://)",
                    "abcdefghijklmnopqrstuvwxyz0123456789./:-_?=&", 11, body, 72) == 0)
                    continue;
                /* prefix https:// */
                int p = 0; const char *h = "https://";
                while (*h) url[p++] = *h++;
                for (int i = 0; body[i]; i++) url[p++] = body[i];
                url[p] = '\0';
            }
            else
            {
                int p = 0; for (const char *s = presets[sel]; *s; s++) url[p++] = *s; url[p] = '\0';
            }
            int len = build_url(img, url);
            int ret = nfc_write(0, img, (unsigned)len);
            show_ret("Wrote URL", ret);
            return;
        }
        pb = now;
        delay(30000);
    }
}

static void screen_write_text(void)
{
    char body[80];
    if (text_entry("Tag text",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,!?-", 13,
        body, 64) == 0)
        return;
    uint8_t img[160];
    int len = build_text(img, body);
    int ret = nfc_write(0, img, (unsigned)len);
    show_ret("Wrote text", ret);
}

static void screen_hexview(void)
{
    int addr = 0;
    const int PER = 8;   /* bytes per row */
    const int ROWS = 12;
    while (gpio_read(BTN_SETTINGS)) delay(10000);
    delay(40000);

    while (1)
    {
        uint8_t buf[PER * 12];
        int want = PER * ROWS;
        int ret = nfc_read(addr, buf, (unsigned)want);

        display_clear(COL_BG);
        display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
        display_text(4, 6, "MEMORY HEX", COL_TITLE);

        if (ret < 0)
        {
            const char *e = errstr(ret); char num[16];
            if (!e) { u2s(ret, num); e = num; }
            display_text(8, 40, e, COL_BAD);
            display_text(8, SCR_H - 12, "B / OK: back", COL_HINT);
            display_flush();
            btn_t now; read_edges(&now);
            if (EDGE(b) || EDGE(s)) { pb = now; return; }
            pb = now; delay(120000); continue;
        }

        for (int r = 0; r < ROWS; r++)
        {
            int y = 26 + r * 15;
            char line[64]; int p = 0;
            int a = addr + r * PER;
            line[p++] = hexd((a >> 8) & 0xf); line[p++] = hexd((a >> 4) & 0xf);
            line[p++] = hexd(a & 0xf); line[p++] = ':'; line[p++] = ' ';
            for (int c = 0; c < PER; c++)
            {
                uint8_t v = buf[r * PER + c];
                line[p++] = hexd(v >> 4); line[p++] = hexd(v & 0xf); line[p++] = ' ';
            }
            line[p++] = ' ';
            for (int c = 0; c < PER; c++)
            {
                uint8_t v = buf[r * PER + c];
                line[p++] = (v >= 32 && v < 127) ? (char)v : '.';
            }
            line[p] = '\0';
            display_text(6, y, line, COL_MONO);
        }
        display_text(8, SCR_H - 12, "UP/DN:scroll  B:back", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(b) || EDGE(s)) { pb = now; return; }
        if (EDGE(d)) addr += PER * ROWS;
        if (EDGE(u)) { addr -= PER * ROWS; if (addr < 0) addr = 0; }
        pb = now;
        delay(40000);
    }
}

static void screen_fieldmon(void)
{
    int last = 0, taps = 0;
    while (gpio_read(BTN_SETTINGS)) delay(10000);
    delay(40000);
    while (1)
    {
        int f = nfc_field_present();
        int bad = (f < 0);
        if (!bad && f && !last) taps++;
        if (!bad) last = f;

        display_clear(COL_BG);
        display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
        display_text(4, 6, "FIELD MONITOR", COL_TITLE);

        if (bad)
            display_text(SCR_W / 2 - 40, SCR_H / 2, "no NFC tag", COL_BAD);
        else
        {
            display_text_huge(SCR_W / 2 - 70, 60, f ? "FIELD" : " --- ",
                              f ? COL_OK : COL_HINT);
            display_text(SCR_W / 2 - 30, 110, f ? "reader near" : "no reader",
                         f ? COL_OK : COL_HINT);
        }
        char b[16]; u2s(taps, b);
        display_text(SCR_W / 2 - 30, 150, "taps:", COL_LABEL);
        display_text(SCR_W / 2 + 18, 150, b, COL_VALUE);

        display_text(8, SCR_H - 12, "B:back", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(b) || EDGE(s)) { pb = now; return; }
        pb = now;
        delay(60000);
    }
}

static void screen_erase(void)
{
    uint8_t img[8];
    int p = put_cc(img);
    img[p++] = 0x03; img[p++] = 0x00; img[p++] = 0xFE; /* empty NDEF message */
    int ret = nfc_write(0, img, (unsigned)p);
    show_ret("Erased NDEF", ret);
}

/* ── Main menu ──────────────────────────────────────────────────────────── */
int main(void)
{
    printf("AkiraOS NFC Toolkit");
    display_get_size(&SCR_W, &SCR_H);

    gpio_configure(BTN_UP, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_DOWN, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_LEFT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_RIGHT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_A, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_B, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    const char *items[] = {
        "Tag info & field",
        "Write URL",
        "Write text",
        "Memory hex view",
        "Field / tap monitor",
        "Erase tag (NDEF)",
    };
    const int N = 6;
    int sel = 0;

    while (1)
    {
        display_clear(COL_BG);
        display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
        display_text(4, 6, "NFC TOOLKIT", COL_TITLE);

        for (int i = 0; i < N; i++)
        {
            int y = 32 + i * 28;
            if (i == sel) display_rect(0, y - 4, SCR_W, 26, COL_SEL_BG);
            display_text(14, y, items[i], (i == sel) ? COL_VALUE : COL_LABEL);
        }
        display_text(6, SCR_H - 12, "A:open  OK:exit", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(s)) { app_switch("supervisor"); return 0; }
        if (EDGE(u) && sel > 0) sel--;
        if (EDGE(d) && sel < N - 1) sel++;
        if (EDGE(a))
        {
            pb = now;
            switch (sel)
            {
            case 0: screen_info(); break;
            case 1: screen_write_url(); break;
            case 2: screen_write_text(); break;
            case 3: screen_hexview(); break;
            case 4: screen_fieldmon(); break;
            case 5: screen_erase(); break;
            }
            continue;
        }
        pb = now;
        delay(40000);
    }
}
