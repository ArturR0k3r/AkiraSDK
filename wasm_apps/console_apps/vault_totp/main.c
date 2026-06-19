/**
 * @file main.c
 * @brief vault.totp — Hardware TOTP 2FA Authenticator for AkiraOS
 *
 * Implements RFC 6238 (TOTP) and RFC 4226 (HOTP) entirely within WASM —
 * no host-side crypto. HMAC-SHA1 and Base32 are self-contained.
 *
 * Secrets at rest:
 *   Stored in NVS settings (settings_set/get) which lives on ESP32-S3 flash.
 *   When CONFIG_EFUSE_VIRTUAL_KEEP_IN_FLASH + flash encryption is enabled in
 *   AkiraOS, the NVS partition is hardware-encrypted (AES-256-XTS). No secret
 *   ever leaves the device in plaintext.
 *
 * Provisioning (via akira-cli over USB serial):
 *   akira-cli settings set vault/count 2
 *   akira-cli settings set vault/0/n   "Gmail"
 *   akira-cli settings set vault/0/s   "JBSWY3DPEHPK3PXP"
 *   akira-cli settings set vault/1/n   "GitHub"
 *   akira-cli settings set vault/1/s   "NFZWY2LBNFZS6MRS"
 *
 * Controls:
 *   UP / DOWN    previous / next account
 *   A            detail / full-screen view (list → view)
 *   B            back (view → list)
 *   X            copy current code to sigint.dash via IPC
 *   Y            send all codes to sigint.dash
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include <stdint.h>
#include <stddef.h>

/* ═══════════════════════════════════════════════════════════════════════
 * Constants
 * ═══════════════════════════════════════════════════════════════════════ */

#define MAX_ACCOUNTS   16
#define NAME_MAX       32
#define SECRET_MAX     64   /* max base32 secret string (40 decoded bytes) */
#define KEY_MAX        40   /* max decoded key bytes */

#define TOTP_PERIOD    30   /* seconds per TOTP window */
#define TOTP_DIGITS    6

/* Settings key layout: vault/NN/n (name), vault/NN/s (secret) */
#define VAULT_COUNT_KEY "vault/count"

/* Display geometry */
#define SCR_W   320
#define SCR_H   240
#define HDR_H    20
#define FOOT_Y  220
#define CX      160
#define CY      125
#define R_OUT    80
#define R_IN     58

/* Colors */
#define COL_BG      CONSOLE_COLOR_BG
#define COL_HDR     CONSOLE_COLOR_HEADER
#define COL_SEP     CONSOLE_COLOR_SEP
#define COL_ACCENT  CONSOLE_COLOR_ACCENT
#define COL_TEXT    CONSOLE_COLOR_TEXT
#define COL_DIM     CONSOLE_COLOR_DIM
#define COL_OK      CONSOLE_COLOR_OK
#define COL_WARN    CONSOLE_COLOR_WARN
#define COL_ERR     CONSOLE_COLOR_ERR
#define COL_SEL     CONSOLE_COLOR_SEL_BG

/* App states */
#define STATE_LOCK   0   /* no accounts provisioned */
#define STATE_LIST   1   /* scrollable list */
#define STATE_VIEW   2   /* full-screen single account */

/* IPC event for sigint.dash */
#define IPC_TOPIC_DASH   "sigint.dash"
#define IPC_EVT_TOTP     0x20

/* List view layout */
#define ROW_H    20
#define VROWS     8
#define LIST_Y   (HDR_H + 1)

/* ═══════════════════════════════════════════════════════════════════════
 * State
 * ═══════════════════════════════════════════════════════════════════════ */

static int     g_state;
static int     g_count;          /* number of provisioned accounts */
static int     g_cursor;         /* selected account index */
static int     g_scroll;         /* first visible row in list */
static int     g_tmr;
static uint32_t g_prev_btns;

static char    g_names  [MAX_ACCOUNTS][NAME_MAX];
static char    g_secrets[MAX_ACCOUNTS][SECRET_MAX];
static char    g_codes  [MAX_ACCOUNTS][8];  /* "123456\0" */
static uint32_t g_last_T;    /* TOTP counter at last code computation */

/* IPC message published to sigint.dash */
typedef struct {
    uint8_t  evt;            /* IPC_EVT_TOTP = 0x20 */
    uint8_t  expires_secs;   /* seconds until this code expires */
    uint8_t  _pad[2];
    char     account[32];    /* null-terminated account name */
    char     code[8];        /* "123456\0" */
} vault_totp_ipc_t;          /* 44 bytes */

/* ═══════════════════════════════════════════════════════════════════════
 * Ring LUT — sin/cos × 100, 60 segments (6° each), 12 o'clock → clockwise
 * ═══════════════════════════════════════════════════════════════════════ */

static const int8_t SIN60[60] = {
      0,  10,  21,  31,  41,  50,  59,  67,  74,  81,
     87,  91,  95,  98,  99, 100,  99,  98,  95,  91,
     87,  81,  74,  67,  59,  50,  41,  31,  21,  10,
      0, -10, -21, -31, -41, -50, -59, -67, -74, -81,
    -87, -91, -95, -98, -99,-100, -99, -98, -95, -91,
    -87, -81, -74, -67, -59, -50, -41, -31, -21, -10
};
static const int8_t COS60[60] = {
    100,  99,  98,  95,  91,  87,  81,  74,  67,  59,
     50,  41,  31,  21,  10,   0, -10, -21, -31, -41,
    -50, -59, -67, -74, -81, -87, -91, -95, -98, -99,
   -100, -99, -98, -95, -91, -87, -81, -74, -67, -59,
    -50, -41, -31, -21, -10,   0,  10,  21,  31,  41,
     50,  59,  67,  74,  81,  87,  91,  95,  98,  99
};

/* ═══════════════════════════════════════════════════════════════════════
 * String helpers (no stdlib)
 * ═══════════════════════════════════════════════════════════════════════ */

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void sncopy(char *dst, const char *src, int max)
{
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_to_int(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

/* Zero-padded 6-digit string (TOTP code output) */
static void fmt6(char *buf, uint32_t v)
{
    buf[6] = '\0';
    for (int i = 5; i >= 0; i--) { buf[i] = '0' + (int)(v % 10); v /= 10; }
}

/* "123456" → "123 456" for readability (needs 8-byte buf) */
static void fmt6_spaced(char *dst, const char *src)
{
    dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2];
    dst[3]=' ';
    dst[4]=src[3]; dst[5]=src[4]; dst[6]=src[5];
    dst[7]='\0';
}

/* Build settings key: "vault/NN/s" or "vault/NN/n" */
static void build_key(char *buf, int idx, char slot)
{
    buf[0]='v'; buf[1]='a'; buf[2]='u'; buf[3]='l'; buf[4]='t'; buf[5]='/';
    int i = 6;
    if (idx >= 10) buf[i++] = '0' + idx / 10;
    buf[i++] = '0' + idx % 10;
    buf[i++] = '/';
    buf[i++] = slot;
    buf[i]   = '\0';
}

/* ═══════════════════════════════════════════════════════════════════════
 * SHA-1  (RFC 3174) — pure 32-bit, no FPU
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t h[5];
    uint8_t  blk[64];
    uint32_t blk_len;
    uint32_t total_bytes;  /* lo half; hi not needed for TOTP key lengths */
} sha1_ctx_t;

#define ROL32(n, x) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_compress(sha1_ctx_t *c)
{
    uint32_t w[80];
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)c->blk[i*4+0] << 24)
             | ((uint32_t)c->blk[i*4+1] << 16)
             | ((uint32_t)c->blk[i*4+2] <<  8)
             |  (uint32_t)c->blk[i*4+3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = ROL32(1, w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]);
    }

    uint32_t a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];

    for (i = 0; i < 80; i++) {
        uint32_t fn, k;
        if      (i < 20) { fn = (b & d) | (~b & e);          k = 0x5A827999U; }
        else if (i < 40) { fn = b ^ d ^ e;                   k = 0x6ED9EBA1U; }
        else if (i < 60) { fn = (b & d) | (b & e) | (d & e); k = 0x8F1BBCDCU; }
        else             { fn = b ^ d ^ e;                   k = 0xCA62C1D6U; }

        uint32_t tmp = ROL32(5, a) + fn + f + k + w[i];
        f = e; e = d; d = ROL32(30, b); b = a; a = tmp;
    }

    c->h[0] += a; c->h[1] += b; c->h[2] += d;
    c->h[3] += e; c->h[4] += f;
}

static void sha1_init(sha1_ctx_t *c)
{
    c->h[0] = 0x67452301U; c->h[1] = 0xEFCDAB89U; c->h[2] = 0x98BADCFEU;
    c->h[3] = 0x10325476U; c->h[4] = 0xC3D2E1F0U;
    c->blk_len = 0; c->total_bytes = 0;
}

static void sha1_update(sha1_ctx_t *c, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        c->blk[c->blk_len++] = data[i];
        c->total_bytes++;
        if (c->blk_len == 64) { sha1_compress(c); c->blk_len = 0; }
    }
}

static void sha1_final(sha1_ctx_t *c, uint8_t out[20])
{
    uint64_t bits = (uint64_t)c->total_bytes * 8U;

    c->blk[c->blk_len++] = 0x80;
    if (c->blk_len > 56) {
        while (c->blk_len < 64) c->blk[c->blk_len++] = 0;
        sha1_compress(c);
        c->blk_len = 0;
    }
    while (c->blk_len < 56) c->blk[c->blk_len++] = 0;

    /* Append bit-length big-endian 64-bit */
    for (int i = 0; i < 8; i++) {
        c->blk[56 + i] = (uint8_t)(bits >> ((7 - i) * 8));
    }
    sha1_compress(c);

    for (int i = 0; i < 5; i++) {
        out[i*4+0] = (uint8_t)(c->h[i] >> 24);
        out[i*4+1] = (uint8_t)(c->h[i] >> 16);
        out[i*4+2] = (uint8_t)(c->h[i] >>  8);
        out[i*4+3] = (uint8_t)(c->h[i]);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * HMAC-SHA1  (RFC 2104)
 * ═══════════════════════════════════════════════════════════════════════ */

static void hmac_sha1(const uint8_t *key, uint32_t key_len,
                      const uint8_t *msg, uint32_t msg_len,
                      uint8_t out[20])
{
    sha1_ctx_t ctx;
    uint8_t    k[64];
    uint8_t    pad[64];
    uint8_t    inner[20];

    /* Normalise key to exactly 64 bytes */
    if (key_len > 64) {
        sha1_init(&ctx);
        sha1_update(&ctx, key, key_len);
        sha1_final(&ctx, k);
        for (int i = 20; i < 64; i++) k[i] = 0;
    } else {
        uint32_t i;
        for (i = 0; i < key_len; i++) k[i] = key[i];
        for (; i < 64; i++) k[i] = 0;
    }

    /* Inner: SHA1(k⊕ipad ∥ msg) */
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha1_init(&ctx);
    sha1_update(&ctx, pad, 64);
    sha1_update(&ctx, msg, msg_len);
    sha1_final(&ctx, inner);

    /* Outer: SHA1(k⊕opad ∥ inner) */
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    sha1_init(&ctx);
    sha1_update(&ctx, pad, 64);
    sha1_update(&ctx, inner, 20);
    sha1_final(&ctx, out);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Base32 decode  (RFC 4648, no padding required)
 * ═══════════════════════════════════════════════════════════════════════ */

static int b32_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return 26 + (c - '2');
    return -1;
}

/* Returns bytes written, -1 on overflow */
static int b32_decode(const char *src, uint8_t *dst, int dst_max)
{
    uint32_t acc = 0;
    int bits = 0, out = 0;

    for (int i = 0; src[i] && src[i] != '='; i++) {
        int v = b32_val(src[i]);
        if (v < 0) continue;
        acc = (acc << 5) | (uint32_t)v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (out >= dst_max) return -1;
            dst[out++] = (uint8_t)(acc >> bits);
            acc &= (1u << bits) - 1u;
        }
    }
    return out;
}

/* ═══════════════════════════════════════════════════════════════════════
 * TOTP  (RFC 6238 §4, with HOTP RFC 4226 §5)
 * ═══════════════════════════════════════════════════════════════════════ */

static void totp_compute(const char *b32_secret, uint32_t T, char code[8])
{
    uint8_t key[KEY_MAX];
    int key_len = b32_decode(b32_secret, key, KEY_MAX);
    if (key_len <= 0) { code[0]='?'; code[1]='\0'; return; }

    /* 8-byte big-endian counter — upper 32 bits are zero for decades */
    uint8_t counter[8] = {
        0, 0, 0, 0,
        (uint8_t)(T >> 24), (uint8_t)(T >> 16),
        (uint8_t)(T >>  8), (uint8_t)(T)
    };

    uint8_t hmac[20];
    hmac_sha1(key, (uint32_t)key_len, counter, 8, hmac);

    /* Dynamic truncation (§5.4) */
    int      offset = hmac[19] & 0x0F;
    uint32_t otp    = (((uint32_t)(hmac[offset  ] & 0x7F) << 24)
                     | ((uint32_t) hmac[offset+1]         << 16)
                     | ((uint32_t) hmac[offset+2]         <<  8)
                     |  (uint32_t) hmac[offset+3])
                    % 1000000U;

    fmt6(code, otp);
}

/* Recompute all codes when T advances */
static void compute_all_codes(uint32_t T)
{
    for (int i = 0; i < g_count; i++) {
        totp_compute(g_secrets[i], T, g_codes[i]);
    }
    g_last_T = T;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Account storage (NVS settings)
 * ═══════════════════════════════════════════════════════════════════════ */

static void load_accounts(void)
{
    char buf[8];
    g_count = 0;

    if (settings_get(VAULT_COUNT_KEY, buf, (int32_t)sizeof(buf)) != 0) return;
    int n = str_to_int(buf);
    if (n <= 0 || n > MAX_ACCOUNTS) return;

    char key[16];
    for (int i = 0; i < n; i++) {
        build_key(key, i, 'n');
        if (settings_get(key, g_names[i], NAME_MAX) != 0) continue;

        build_key(key, i, 's');
        if (settings_get(key, g_secrets[i], SECRET_MAX) != 0) continue;

        g_count++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * IPC — publish one code to sigint.dash
 * ═══════════════════════════════════════════════════════════════════════ */

static void ipc_send_code(int idx, int expires_secs)
{
    vault_totp_ipc_t msg;
    msg.evt          = IPC_EVT_TOTP;
    msg.expires_secs = (uint8_t)(expires_secs < 0 ? 0 : expires_secs);
    msg._pad[0] = msg._pad[1] = 0;
    sncopy(msg.account, g_names[idx], (int)sizeof(msg.account));
    sncopy(msg.code,    g_codes[idx], 8);
    msg_publish(IPC_TOPIC_DASH, (const uint8_t *)&msg, (uint32_t)sizeof(msg));
}

/* ═══════════════════════════════════════════════════════════════════════
 * Ring renderer
 * secs_remaining: 0-30, full = 30 (all accent), empty = 0 (all dim)
 * Depletes clockwise from 12 o'clock.
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_ring(int secs_remaining, uint32_t accent)
{
    /* Segment 0 = 12 o'clock. 2 segs per second, 60 total. */
    int elapsed_segs = (TOTP_PERIOD - secs_remaining) * 2;

    display_circle_fill(CX, CY, R_OUT, COL_SEP);  /* dim base */

    for (int i = elapsed_segs; i < 60; i++) {
        int x0 = CX + (int)(R_OUT * SIN60[i] / 100);
        int y0 = CY - (int)(R_OUT * COS60[i] / 100);
        int j  = (i + 1) % 60;
        int x1 = CX + (int)(R_OUT * SIN60[j] / 100);
        int y1 = CY - (int)(R_OUT * COS60[j] / 100);
        display_triangle_fill(CX, CY, x0, y0, x1, y1, accent);
    }

    display_circle_fill(CX, CY, R_IN, COL_BG);     /* hollow centre */
    display_circle(CX, CY, R_OUT, COL_SEP);
    display_circle(CX, CY, R_IN,  COL_SEP);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Common header / footer
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_header(const char *left, const char *right)
{
    display_rect(0, 0, SCR_W, HDR_H, COL_HDR);
    display_text(6, 6, left, COL_TEXT);
    if (right) {
        int rx = SCR_W - slen(right) * 7 - 4;
        display_text(rx, 6, right, COL_ACCENT);
    }
    display_hline(0, HDR_H, SCR_W, COL_SEP);
}

static void draw_footer(const char *hint)
{
    display_hline(0, FOOT_Y - 1, SCR_W, COL_SEP);
    display_rect(0, FOOT_Y, SCR_W, SCR_H - FOOT_Y, COL_HDR);
    display_text(4, FOOT_Y + 6, hint, COL_DIM);
}

/* ═══════════════════════════════════════════════════════════════════════
 * STATE_LOCK — no accounts provisioned
 * ═══════════════════════════════════════════════════════════════════════ */

static void render_lock(void)
{
    display_clear(COL_BG);
    draw_header("VAULT TOTP", NULL);

    /* Padlock silhouette drawn with primitives */
    int lx = 140, ly = 50, lw = 40, lh = 28;
    /* shackle (arc = circle outline cropped) */
    display_circle(CX, ly + lh, 20, COL_DIM);
    display_rect(lx, ly + lh, lw, 8, COL_BG);  /* erase bottom half of circle */
    /* body */
    display_rect_outline(lx, ly + lh - 2, lw, lh, COL_DIM);
    display_rect(lx + lw/2 - 2, ly + lh + 6, 4, 8, COL_BG);  /* keyhole slot */
    display_circle_fill(CX, ly + lh + 4, 4, COL_DIM);         /* keyhole */

    display_text(CX - 66, 112, "No accounts configured.", COL_DIM);
    display_text(CX - 77, 130, "Provision via akira-cli USB serial:", COL_DIM);

    display_text(8, 150, "akira-cli settings set vault/count 1", COL_SEP);
    display_text(8, 162, "akira-cli settings set vault/0/n \"Gmail\"", COL_SEP);
    display_text(8, 174, "akira-cli settings set vault/0/s <base32>", COL_SEP);

    draw_footer("Y:reload settings");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════
 * STATE_LIST
 * ═══════════════════════════════════════════════════════════════════════ */

static void render_list(int secs_remaining)
{
    display_clear(COL_BG);

    /* Header: "VAULT TOTP" + account count */
    char hdr_r[8];
    hdr_r[0] = '0' + g_count / 10; hdr_r[1] = '0' + g_count % 10;
    hdr_r[2] = ' '; hdr_r[3] = 'A'; hdr_r[4] = 'C'; hdr_r[5] = '\0';
    if (g_count < 10) { hdr_r[0] = hdr_r[1]; hdr_r[1] = ' '; hdr_r[2] = 'A';
                        hdr_r[3] = 'C'; hdr_r[4] = '\0'; }
    draw_header("VAULT TOTP", hdr_r);

    /* Global countdown bar just under header */
    display_progress_bar(0, HDR_H + 1, SCR_W, 3,
                         secs_remaining, TOTP_PERIOD,
                         (secs_remaining > 5) ? COL_OK : COL_ERR, COL_SEP);

    for (int v = 0; v < VROWS; v++) {
        int idx = g_scroll + v;
        if (idx >= g_count) break;

        int y  = LIST_Y + 5 + v * ROW_H;
        int sel = (idx == g_cursor);

        display_rect(0, y - 2, SCR_W, ROW_H, sel ? COL_SEL : COL_BG);

        /* Account name (truncated to 14 chars) */
        char name_buf[16];
        sncopy(name_buf, g_names[idx], 15);
        display_text(6, y + 4, name_buf, sel ? COL_TEXT : COL_DIM);

        /* 6-digit code — spaced */
        char spaced[8];
        fmt6_spaced(spaced, g_codes[idx]);
        display_text(112, y + 4, spaced, COL_ACCENT);

        /* Mini countdown bar (rightmost 60px) */
        display_progress_bar(252, y + 6, 60, 6,
                             secs_remaining, TOTP_PERIOD,
                             (secs_remaining > 5) ? COL_OK : COL_WARN, COL_SEP);

        if (sel) display_rect_outline(0, y - 2, SCR_W - 1, ROW_H - 1, COL_ACCENT);
    }

    /* Scroll indicator */
    if (g_count > VROWS) {
        int total_h = VROWS * ROW_H;
        int thumb   = total_h * VROWS / g_count;
        if (thumb < 4) thumb = 4;
        int thumb_y = LIST_Y + 5 + total_h * g_scroll / g_count;
        display_rect(SCR_W - 3, LIST_Y + 5, 3, total_h, COL_SEP);
        display_rect(SCR_W - 3, thumb_y, 3, thumb, COL_ACCENT);
    }

    /* Seconds remaining label */
    char secs_str[12];
    secs_str[0] = 'R'; secs_str[1] = 'e'; secs_str[2] = 'f'; secs_str[3] = 'r';
    secs_str[4] = 'e'; secs_str[5] = 's'; secs_str[6] = 'h'; secs_str[7] = ' ';
    secs_str[8] = 'i'; secs_str[9] = 'n'; secs_str[10] = ' ';
    /* append seconds */
    int n = 11;
    if (secs_remaining >= 10) secs_str[n++] = '0' + secs_remaining / 10;
    secs_str[n++] = '0' + secs_remaining % 10;
    secs_str[n++] = 's'; secs_str[n] = '\0';
    int lx = SCR_W - n * 7 - 4;
    display_text(lx, FOOT_Y - 12, secs_str,
                 secs_remaining > 5 ? COL_DIM : COL_WARN);

    draw_footer("A:view  X:send  Y:send all  \x18\x19:scroll");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════
 * STATE_VIEW — full-screen single account with countdown ring
 * ═══════════════════════════════════════════════════════════════════════ */

static void render_view(int idx, int secs_remaining)
{
    display_clear(COL_BG);

    /* Header: account name + position */
    char pos[8]; /* "N / M\0" */
    int  pi = 0;
    if (g_count >= 10) pos[pi++] = '0' + (idx+1) / 10;
    pos[pi++] = '0' + (idx+1) % 10;
    pos[pi++] = '/';
    if (g_count >= 10) pos[pi++] = '0' + g_count / 10;
    pos[pi++] = '0' + g_count % 10;
    pos[pi]   = '\0';
    draw_header("VAULT TOTP", pos);

    /* Account name centred just above ring */
    int nlen = slen(g_names[idx]);
    int nx   = CX - nlen * 7 / 2;
    display_text(nx < 4 ? 4 : nx, 26, g_names[idx], COL_DIM);

    /* Countdown ring */
    uint32_t ring_col = (secs_remaining > 10) ? COL_ACCENT :
                        (secs_remaining >  5) ? COL_WARN   : COL_ERR;
    draw_ring(secs_remaining, ring_col);

    /* 6-digit code (large font, 11×18 per char) centred in ring */
    char spaced[8];
    fmt6_spaced(spaced, g_codes[idx]);
    /* 7 chars × 11px = 77px wide */
    display_text_large(CX - 38, CY - 9, spaced, COL_TEXT);

    /* Seconds label below code */
    char secs_s[6];
    int si = 0;
    if (secs_remaining >= 10) secs_s[si++] = '0' + secs_remaining / 10;
    secs_s[si++] = '0' + secs_remaining % 10;
    secs_s[si++] = 's'; secs_s[si] = '\0';
    display_text(CX - si * 7 / 2, CY + 14, secs_s, ring_col);

    /* Security label: "TOTP · SHA-1 · 6 digits · 30s" */
    display_text(4, FOOT_Y - 12, "TOTP  SHA-1  6 digits  30s", COL_SEP);

    draw_footer("B:list  X:send to dash  \x18\x19:account");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════
 * Input
 * ═══════════════════════════════════════════════════════════════════════ */

static void handle_input(int secs_remaining)
{
    uint32_t btns = (uint32_t)input_get_buttons();
    uint32_t edge = btns & ~g_prev_btns;
    g_prev_btns   = btns;

    if (g_state == STATE_LOCK) {
        if (edge & AKIRA_BTN_Y) {
            load_accounts();
            if (g_count > 0) {
                uint32_t T = (uint32_t)(rtc_get_unix_time() / TOTP_PERIOD);
                compute_all_codes(T);
                g_state = STATE_LIST;
            }
        }
        return;
    }

    if (g_state == STATE_LIST) {
        if (edge & AKIRA_BTN_UP) {
            if (g_cursor > 0) {
                g_cursor--;
                if (g_cursor < g_scroll) g_scroll = g_cursor;
            }
        }
        if (edge & AKIRA_BTN_DOWN) {
            if (g_cursor < g_count - 1) {
                g_cursor++;
                if (g_cursor >= g_scroll + VROWS) g_scroll = g_cursor - VROWS + 1;
            }
        }
        if (edge & AKIRA_BTN_A) {
            g_state = STATE_VIEW;
        }
        if (edge & AKIRA_BTN_X) {
            ipc_send_code(g_cursor, secs_remaining);
        }
        if (edge & AKIRA_BTN_Y) {
            for (int i = 0; i < g_count; i++) ipc_send_code(i, secs_remaining);
        }
    } else if (g_state == STATE_VIEW) {
        if (edge & AKIRA_BTN_B) { g_state = STATE_LIST; }
        if (edge & AKIRA_BTN_X) { ipc_send_code(g_cursor, secs_remaining); }
        if (edge & AKIRA_BTN_UP) {
            if (g_cursor > 0) g_cursor--;
        }
        if (edge & AKIRA_BTN_DOWN) {
            if (g_cursor < g_count - 1) g_cursor++;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════ */

int main(void)
{
    printf("vault.totp v1.0 — RFC 6238 hardware authenticator");

    g_tmr      = timer_create();
    g_prev_btns = 0;
    g_cursor   = 0;
    g_scroll   = 0;

    load_accounts();

    if (g_count == 0) {
        g_state = STATE_LOCK;
    } else {
        g_state = STATE_LIST;
        uint32_t T = (uint32_t)(rtc_get_unix_time() / TOTP_PERIOD);
        compute_all_codes(T);
    }

    while (1) {
        timer_start(g_tmr);

        int unix_t      = rtc_get_unix_time();
        uint32_t T      = (uint32_t)(unix_t / TOTP_PERIOD);
        int secs_used   = unix_t % TOTP_PERIOD;
        int secs_left   = TOTP_PERIOD - secs_used;

        /* Regenerate codes on window rollover */
        if (g_count > 0 && T != g_last_T) {
            compute_all_codes(T);
        }

        handle_input(secs_left);

        switch (g_state) {
        case STATE_LOCK: render_lock();                      break;
        case STATE_LIST: render_list(secs_left);             break;
        case STATE_VIEW: render_view(g_cursor, secs_left);   break;
        }

        /* Target ~10 fps */
        int used = timer_elapsed(g_tmr);
        if (used < 100) delay((uint32_t)(100 - used) * 1000);
    }

    return 0;
}
