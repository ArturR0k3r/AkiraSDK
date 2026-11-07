/**
 * @file totp.c
 * @brief TOTP authenticator (RFC 6238) for AkiraOS
 *
 * Time-based one-time passwords, compatible with Google Authenticator / GitHub
 * / AWS etc. (standard HMAC-SHA1, 6 digits, 30-second step).
 *
 * Security model:
 *   - A 4-digit PIN derives an AES-256 key (SHA-256 of the PIN).
 *   - Each account's secret is sealed at rest with AES-256-CTR (per-account
 *     random nonce) in NVS. A key verifier catches a wrong PIN.
 *   - SHA-1 / HMAC-SHA1 / base32 are implemented here because the platform
 *     crypto API only exposes HMAC-SHA256; the seed stays sealed either way.
 *
 * Controls:
 *   PIN screen   : UP/DOWN change digit, LEFT/RIGHT move, OK confirm
 *   Account list : UP/DOWN select, A add, B delete, OK exit
 *   Text entry   : DPAD move, A pick, B backspace, OK done
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
#define COL_HEADER_BG 0x3006 /* indigo bar            */
#define COL_TITLE 0x07FF
#define COL_LABEL 0x8410
#define COL_VALUE 0xFFFF
#define COL_CODE 0xFFFF
#define COL_SEL_BG 0x1800    /* dark red selection    */
#define COL_OK 0x07E0        /* green                 */
#define COL_WARN 0xFFE0      /* yellow                */
#define COL_BAD 0xF800       /* red                   */
#define COL_HINT 0x4208
#define COL_BAR 0x07E0

#define MAX_ACCOUNTS 8
#define MAX_LABEL 16
#define MAX_B32 64  /* base32 chars the user may enter */
#define MAX_KEYLEN 40 /* decoded secret bytes            */

/* ───────────────────────── small no-libc helpers ───────────────────────── */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void u2s(uint32_t v, char *b)
{
    char t[12]; int n = 0, p = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) b[p++] = t[--n];
    b[p] = '\0';
}
static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void to_hex(const uint8_t *in, int n, char *out)
{
    const char *h = "0123456789abcdef";
    int p = 0;
    for (int i = 0; i < n; i++) { out[p++] = h[in[i] >> 4]; out[p++] = h[in[i] & 0xf]; }
    out[p] = '\0';
}
static int from_hex(const char *in, uint8_t *out, int cap)
{
    int n = 0;
    while (in[0] && in[1] && n < cap)
    {
        int hi = hexv(in[0]), lo = hexv(in[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        in += 2;
    }
    return n;
}

/* ───────────────────────────── SHA-1 ───────────────────────────────────── */
typedef struct { uint32_t h0, h1, h2, h3, h4; uint64_t bits; uint8_t buf[64]; uint32_t idx; } sha1_ctx;
static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
static void sha1_block(sha1_ctx *c, const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = c->h0, b = c->h1, cc = c->h2, d = c->h3, e = c->h4;
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | ((~b) & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;                      k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d);   k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;                      k = 0xCA62C1D6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->h0 += a; c->h1 += b; c->h2 += cc; c->h3 += d; c->h4 += e;
}
static void sha1_init(sha1_ctx *c)
{
    c->h0 = 0x67452301; c->h1 = 0xEFCDAB89; c->h2 = 0x98BADCFE;
    c->h3 = 0x10325476; c->h4 = 0xC3D2E1F0; c->bits = 0; c->idx = 0;
}
static void sha1_update(sha1_ctx *c, const uint8_t *p, uint32_t n)
{
    c->bits += (uint64_t)n * 8;
    while (n)
    {
        uint32_t take = 64 - c->idx;
        if (take > n) take = n;
        for (uint32_t i = 0; i < take; i++) c->buf[c->idx + i] = p[i];
        c->idx += take; p += take; n -= take;
        if (c->idx == 64) { sha1_block(c, c->buf); c->idx = 0; }
    }
}
static void sha1_final(sha1_ctx *c, uint8_t out[20])
{
    uint64_t bits = c->bits; /* captured before padding perturbs the counter */
    uint8_t pad = 0x80; sha1_update(c, &pad, 1);
    uint8_t z = 0; while (c->idx != 56) sha1_update(c, &z, 1);
    uint8_t len[8]; for (int i = 7; i >= 0; i--) { len[i] = (uint8_t)(bits & 0xff); bits >>= 8; }
    sha1_update(c, len, 8);
    uint32_t h[5] = {c->h0, c->h1, c->h2, c->h3, c->h4};
    for (int i = 0; i < 5; i++)
    {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)h[i];
    }
}
static void hmac_sha1(const uint8_t *key, uint32_t klen,
                      const uint8_t *msg, uint32_t mlen, uint8_t out[20])
{
    uint8_t k[64], kh[20], ip[64], op[64], inner[20];
    if (klen > 64) { sha1_ctx c; sha1_init(&c); sha1_update(&c, key, klen); sha1_final(&c, kh); key = kh; klen = 20; }
    for (uint32_t i = 0; i < 64; i++) k[i] = (i < klen) ? key[i] : 0;
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha1_ctx c;
    sha1_init(&c); sha1_update(&c, ip, 64); sha1_update(&c, msg, mlen); sha1_final(&c, inner);
    sha1_init(&c); sha1_update(&c, op, 64); sha1_update(&c, inner, 20); sha1_final(&c, out);
}

/* ─────────────────────────── base32 (RFC 4648) ─────────────────────────── */
static int base32_decode(const char *in, uint8_t *out, int cap)
{
    uint32_t buf = 0; int bits = 0, n = 0;
    for (; *in; in++)
    {
        char c = *in; int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a';
        else if (c >= '2' && c <= '7') v = c - '2' + 26;
        else continue; /* skip spaces, '=', '-', anything else */
        buf = (buf << 5) | (uint32_t)v; bits += 5;
        if (bits >= 8) { bits -= 8; if (n < cap) out[n++] = (uint8_t)((buf >> bits) & 0xff); }
    }
    return n;
}

/* ─────────────────────────────── TOTP ──────────────────────────────────── */
static uint32_t totp_at(const uint8_t *key, uint32_t klen, uint64_t unix_t)
{
    uint64_t ctr = unix_t / 30;
    uint8_t msg[8]; for (int i = 7; i >= 0; i--) { msg[i] = (uint8_t)(ctr & 0xff); ctr >>= 8; }
    uint8_t mac[20]; hmac_sha1(key, klen, msg, 8, mac);
    int off = mac[19] & 0x0f;
    uint32_t bin = (((uint32_t)mac[off] & 0x7f) << 24) | ((uint32_t)mac[off+1] << 16) |
                   ((uint32_t)mac[off+2] << 8) | (uint32_t)mac[off+3];
    return bin % 1000000u;
}

/* ──────────────────────────── account store ────────────────────────────── */
typedef struct { char label[MAX_LABEL + 1]; uint8_t key[MAX_KEYLEN]; int klen; } acct_t;
static acct_t accts[MAX_ACCOUNTS];
static int acct_count = 0;
static uint8_t g_key[32]; /* AES key derived from the PIN */

static void kname(char *out, int idx, const char *suffix)
{
    char *p = out;
    const char *a = "totp/"; while (*a) *p++ = *a++;
    char num[12]; u2s((uint32_t)idx, num);
    char *q = num; while (*q) *p++ = *q++;
    *p++ = '/';
    while (*suffix) *p++ = *suffix++;
    *p = '\0';
}

static void derive_key(const char *pin) { crypto_sha256(pin, (uint32_t)slen(pin), g_key); }

/* verifier = first 8 bytes of SHA-256(g_key), hex */
static void key_verifier(char *out16)
{
    uint8_t v[32]; crypto_sha256(g_key, 32, v); to_hex(v, 8, out16);
}

static void persist_count(void)
{
    char b[8]; u2s((uint32_t)acct_count, b); settings_set("totp/cnt", b);
}

static void persist_account(int i)
{
    char key[40], val[160];
    uint8_t nonce[16], ct[MAX_KEYLEN];
    crypto_random(nonce, sizeof(nonce));
    crypto_aes256_ctr(g_key, nonce, accts[i].key, (uint32_t)accts[i].klen, ct);

    kname(key, i, "lbl"); settings_set(key, accts[i].label);
    kname(key, i, "non"); to_hex(nonce, 16, val); settings_set(key, val);
    kname(key, i, "ct");  to_hex(ct, accts[i].klen, val); settings_set(key, val);
    kname(key, i, "len"); u2s((uint32_t)accts[i].klen, val); settings_set(key, val);
}

static void delete_slot_keys(int i)
{
    char key[40];
    kname(key, i, "lbl"); settings_delete(key);
    kname(key, i, "non"); settings_delete(key);
    kname(key, i, "ct");  settings_delete(key);
    kname(key, i, "len"); settings_delete(key);
}

/* Rewrite the whole store from the in-RAM array (fresh nonces). */
static void persist_all(void)
{
    for (int i = 0; i < acct_count; i++) persist_account(i);
    for (int i = acct_count; i < MAX_ACCOUNTS; i++) delete_slot_keys(i);
    persist_count();
}

/* Load + decrypt all accounts after the PIN is verified. */
static void load_accounts(void)
{
    char b[8];
    acct_count = 0;
    if (settings_get("totp/cnt", b, sizeof(b)) == 0)
        for (int i = 0; b[i] >= '0' && b[i] <= '9'; i++)
            acct_count = acct_count * 10 + (b[i] - '0');
    if (acct_count > MAX_ACCOUNTS) acct_count = MAX_ACCOUNTS;

    for (int i = 0; i < acct_count; i++)
    {
        char key[40], val[160];
        kname(key, i, "lbl");
        if (settings_get(key, accts[i].label, MAX_LABEL + 1) != 0) accts[i].label[0] = '\0';

        uint8_t nonce[16], ct[MAX_KEYLEN];
        kname(key, i, "non");
        if (settings_get(key, val, sizeof(val)) != 0) { accts[i].klen = 0; continue; }
        from_hex(val, nonce, 16);

        kname(key, i, "len"); int klen = 0;
        if (settings_get(key, val, sizeof(val)) == 0)
            for (int j = 0; val[j] >= '0' && val[j] <= '9'; j++) klen = klen * 10 + (val[j] - '0');
        if (klen > MAX_KEYLEN) klen = MAX_KEYLEN;

        kname(key, i, "ct");
        if (settings_get(key, val, sizeof(val)) != 0) { accts[i].klen = 0; continue; }
        int ctn = from_hex(val, ct, MAX_KEYLEN);
        if (ctn < klen) klen = ctn;

        crypto_aes256_ctr(g_key, nonce, ct, (uint32_t)klen, accts[i].key); /* CTR: decrypt == encrypt */
        accts[i].klen = klen;
    }
}

/* ─────────────────────────── button edge helper ────────────────────────── */
typedef struct { int u, d, l, r, a, b, s; } btn_t;
static btn_t pb;
static void read_edges(btn_t *now)
{
    now->u = gpio_read(BTN_UP);  now->d = gpio_read(BTN_DOWN);
    now->l = gpio_read(BTN_LEFT); now->r = gpio_read(BTN_RIGHT);
    now->a = gpio_read(BTN_A);   now->b = gpio_read(BTN_B);
    now->s = gpio_read(BTN_SETTINGS);
}
#define EDGE(f) (now.f && !pb.f)

/* ───────────────────────── 4-digit PIN entry ───────────────────────────── */
/* Returns with pin[] = 4 digits + NUL. */
static void pin_entry(char *pin, const char *title)
{
    int d[4] = {0, 0, 0, 0};
    int cur = 0;

    /* wait for OK release if we arrived holding it */
    while (gpio_read(BTN_SETTINGS)) delay(10000);
    delay(50000);

    while (1)
    {
        display_clear(COL_BG);
        display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
        display_text(4, 6, "TOTP", COL_TITLE);
        display_text(SCR_W / 2 - slen(title) * 3, SCR_H * 28 / 100, title, COL_LABEL);

        int bw = 36, gap = 12;
        int total = 4 * bw + 3 * gap;
        int x0 = (SCR_W - total) / 2;
        int y = SCR_H / 2 - 20;
        for (int i = 0; i < 4; i++)
        {
            int x = x0 + i * (bw + gap);
            display_rect_outline(x, y, bw, 44, (i == cur) ? COL_OK : COL_LABEL);
            char c[2] = {(char)('0' + d[i]), 0};
            display_text_large(x + bw / 2 - 5, y + 14, c, COL_VALUE);
        }
        display_text(SCR_W / 2 - 78, SCR_H - 24,
                     "UP/DN digit  L/R move  OK done", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(u)) d[cur] = (d[cur] + 1) % 10;
        if (EDGE(d)) d[cur] = (d[cur] + 9) % 10;
        if (EDGE(l) && cur > 0) cur--;
        if (EDGE(r) && cur < 3) cur++;
        if (EDGE(a) && cur < 3) cur++;
        if (EDGE(s)) break;
        pb = now;
        delay(30000);
    }
    for (int i = 0; i < 4; i++) pin[i] = (char)('0' + d[i]);
    pin[4] = '\0';
}

/* ───────────────────── generic DPAD text entry ─────────────────────────── */
/* Returns 1 if the user confirmed a non-empty string, 0 if they left it empty. */
static int text_entry(const char *title, const char *charset, int cols,
                      char *out, int maxlen)
{
    int setn = slen(charset);
    int rows = (setn + cols - 1) / cols;
    int cur = 0;      /* index into charset grid */
    int len = 0;
    out[0] = '\0';

    while (gpio_read(BTN_SETTINGS)) delay(10000);
    delay(50000);

    while (1)
    {
        display_clear(COL_BG);
        display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
        display_text(4, 6, title, COL_TITLE);

        /* current string */
        display_rect_outline(8, 26, SCR_W - 16, 20, COL_LABEL);
        display_text(12, 31, out[0] ? out : "_", COL_VALUE);

        /* charset grid */
        int cw = (SCR_W - 16) / cols;
        int gy = 56;
        int ch = 18;
        for (int i = 0; i < setn; i++)
        {
            int cx = 8 + (i % cols) * cw;
            int cyy = gy + (i / cols) * ch;
            if (i == cur) display_rect(cx, cyy - 1, cw, ch, COL_SEL_BG);
            char c[2] = {charset[i], 0};
            display_text(cx + cw / 2 - 3, cyy + 3, c, COL_VALUE);
        }

        display_text(4, SCR_H - 12, "A:add  B:del  OK:done", COL_HINT);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(l) && cur > 0) cur--;
        if (EDGE(r) && cur < setn - 1) cur++;
        if (EDGE(u) && cur - cols >= 0) cur -= cols;
        if (EDGE(d) && cur + cols < setn) cur += cols;
        if (EDGE(a) && len < maxlen) { out[len++] = charset[cur]; out[len] = '\0'; }
        if (EDGE(b) && len > 0) { out[--len] = '\0'; }
        if (EDGE(s)) break;
        pb = now;
        delay(30000);
        (void)rows;
    }
    return len > 0;
}

/* ───────────────────────── add-account flow ────────────────────────────── */
static void add_account(void)
{
    if (acct_count >= MAX_ACCOUNTS) return;
    acct_t a;
    if (!text_entry("Label (e.g. GitHub)",
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-@",
                    13, a.label, MAX_LABEL))
        return;
    char b32[MAX_B32 + 1];
    if (!text_entry("Base32 secret",
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", 8, b32, MAX_B32))
        return;
    a.klen = base32_decode(b32, a.key, MAX_KEYLEN);
    if (a.klen <= 0) return; /* nothing decodable */

    accts[acct_count] = a;
    persist_account(acct_count);
    acct_count++;
    persist_count();
}

static void delete_account(int i)
{
    if (i < 0 || i >= acct_count) return;
    for (int j = i; j < acct_count - 1; j++) accts[j] = accts[j + 1];
    acct_count--;
    persist_all();
}

/* confirm yes/no overlay; returns 1 = yes */
static int confirm(const char *line)
{
    int sel = 0; /* 0=No,1=Yes */
    while (gpio_read(BTN_A)) delay(10000);
    delay(50000);
    while (1)
    {
        int mw = 200, mh = 80, mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
        display_rect(mx - 2, my - 2, mw + 4, mh + 4, COL_BAD);
        display_rect(mx, my, mw, mh, 0x0821);
        display_text(mx + 10, my + 12, line, COL_VALUE);
        const char *opt[2] = {"[ No ]", "[ Yes ]"};
        for (int i = 0; i < 2; i++)
            display_text(mx + 30 + i * 90, my + 48, opt[i], (i == sel) ? COL_OK : COL_LABEL);
        display_flush();

        btn_t now; read_edges(&now);
        if (EDGE(l) || EDGE(r)) sel ^= 1;
        if (EDGE(a)) { pb = now; return sel; }
        if (EDGE(s)) { pb = now; return 0; }
        pb = now;
        delay(30000);
    }
}

/* ─────────────────────────── account list UI ───────────────────────────── */
static int visible_rows(void) { return (SCR_H - 28 - 14) / 42; }

static void draw_list(int sel, int top, int now_unix, int clock_bad)
{
    display_clear(COL_BG);
    display_rect(0, 0, SCR_W, 20, COL_HEADER_BG);
    display_text(4, 6, "AUTHENTICATOR", COL_TITLE);

    if (clock_bad)
        display_text(SCR_W / 2 - 40, 6, "CLOCK NOT SET", COL_BAD);

    if (acct_count == 0)
    {
        display_text(SCR_W / 2 - 70, SCR_H / 2 - 16, "No accounts yet.", COL_LABEL);
        display_text(SCR_W / 2 - 78, SCR_H / 2 + 2, "Press A to add one.", COL_VALUE);
        display_text(4, SCR_H - 12, "A:add  OK:exit", COL_HINT);
        display_flush();
        return;
    }

    int secs_left = 30 - (now_unix % 30);
    int rows = visible_rows();
    char cbuf[8], disp[8];
    for (int r = 0; r < rows; r++)
    {
        int i = top + r;
        if (i >= acct_count) break;
        int y = 28 + r * 42;
        if (i == sel) display_rect(0, y - 2, SCR_W, 42, COL_SEL_BG);

        display_text(8, y, accts[i].label[0] ? accts[i].label : "(unnamed)", COL_LABEL);

        if (accts[i].klen <= 0)
        {
            display_text(8, y + 16, "decrypt error", COL_BAD);
            continue;
        }

        uint32_t code = totp_at(accts[i].key, (uint32_t)accts[i].klen, (uint64_t)now_unix);
        /* zero-pad to 6, render as "XXX XXX" */
        u2s(code, cbuf);
        int cl = slen(cbuf);
        int pad = 6 - cl;
        for (int k = 0; k < pad; k++) disp[k] = '0';
        for (int k = 0; k < cl; k++) disp[pad + k] = cbuf[k];
        char spaced[8];
        spaced[0]=disp[0];spaced[1]=disp[1];spaced[2]=disp[2];
        spaced[3]=' ';
        spaced[4]=disp[3];spaced[5]=disp[4];spaced[6]=disp[5];spaced[7]=0;
        display_text_large(8, y + 14, spaced, COL_CODE);

        /* countdown bar on the right */
        uint32_t cc = (secs_left > 10) ? COL_OK : (secs_left > 5 ? COL_WARN : COL_BAD);
        int bx = SCR_W - 96, bw = 88;
        display_rect(bx, y + 16, bw, 8, 0x1082);
        display_rect(bx, y + 16, bw * secs_left / 30, 8, cc);
        u2s((uint32_t)secs_left, cbuf);
        display_text(SCR_W - 20, y + 2, cbuf, cc);
    }

    display_text(4, SCR_H - 12, "A:add  B:del  OK:exit", COL_HINT);
    display_flush();
}

/* ─────────────────────────────── main ──────────────────────────────────── */
int main(void)
{
    printf("AkiraOS TOTP");
    display_get_size(&SCR_W, &SCR_H);

    gpio_configure(BTN_UP, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_DOWN, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_LEFT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_RIGHT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_A, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(BTN_B, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(BTN_SETTINGS, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);

    /* ── PIN gate ───────────────────────────────────────────────────────── */
    char stored_ver[32];
    int have_pin = (settings_get("totp/ver", stored_ver, sizeof(stored_ver)) == 0);
    char pin[6];

    if (!have_pin)
    {
        /* First run — set a PIN. */
        pin_entry(pin, "Set a 4-digit PIN");
        derive_key(pin);
        char ver[32]; key_verifier(ver);
        settings_set("totp/ver", ver);
        acct_count = 0;
        persist_count();
    }
    else
    {
        /* Unlock — up to 5 attempts. */
        int ok = 0;
        for (int tries = 0; tries < 5 && !ok; tries++)
        {
            pin_entry(pin, tries ? "Wrong PIN - retry" : "Enter PIN");
            derive_key(pin);
            char ver[32]; key_verifier(ver);
            int same = 1;
            for (int i = 0; i < 16; i++) if (ver[i] != stored_ver[i]) { same = 0; break; }
            ok = same;
        }
        if (!ok)
        {
            display_clear(COL_BG);
            display_text_large(SCR_W / 2 - 50, SCR_H / 2 - 8, "LOCKED", COL_BAD);
            display_flush();
            delay(2000000);
            app_switch("supervisor");
            return 0;
        }
        load_accounts();
    }

    /* ── Account list loop ──────────────────────────────────────────────── */
    int sel = 0, top = 0;
    while (1)
    {
        int now_unix = rtc_get_unix_time();
        int clock_bad = (now_unix <= 0);
        if (clock_bad) now_unix = 0;

        int rows = visible_rows();
        if (sel >= acct_count) sel = acct_count ? acct_count - 1 : 0;
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;

        draw_list(sel, top, now_unix, clock_bad);

        btn_t now; read_edges(&now);
        if (EDGE(s)) { app_switch("supervisor"); return 0; }
        if (EDGE(u) && sel > 0) sel--;
        if (EDGE(d) && sel < acct_count - 1) sel++;
        if (EDGE(a)) { pb = now; add_account(); continue; }
        if (EDGE(b) && acct_count > 0)
        {
            pb = now;
            if (confirm("Delete this account?")) delete_account(sel);
            continue;
        }
        pb = now;
        delay(120000); /* refresh ~8x/s so the countdown stays smooth */
    }
}
