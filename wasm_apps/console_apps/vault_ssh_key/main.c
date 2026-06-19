/**
 * @file main.c
 * @brief vault.ssh-key — Hardware SSH Signing Agent for AkiraOS
 *
 * AkiraConsole acts as an SSH agent over USB serial. The private key seed
 * (32 bytes) never leaves the device. Every sign request is shown on screen
 * and requires explicit A-button approval (or auto-denies after 30 s).
 *
 * Protocol
 * ────────
 * Implements a subset of the OpenSSH agent protocol (draft-miller-ssh-agent)
 * over UART0 at 115200 baud. Messages are raw SSH agent framing:
 *   4-byte big-endian payload length ∥ 1-byte message type ∥ payload
 *
 * A companion script bridges /dev/ttyUSB0 to a Unix domain socket so that
 * the standard `ssh` client works without modification:
 *   eval $(python3 tools/akira-ssh-agent.py --dev /dev/ttyUSB0)
 *   ssh-add -l   # lists keys
 *   ssh user@host
 *
 * SSH agent messages handled:
 *   SSH2_AGENTC_REQUEST_IDENTITIES (11) → SSH2_AGENT_IDENTITIES_ANSWER (12)
 *   SSH2_AGENTC_SIGN_REQUEST        (13) → SSH2_AGENT_SIGN_RESPONSE    (14)
 *                                          or SSH_AGENT_FAILURE          (5)
 *
 * Key type: ssh-ed25519 (Ed25519 pure EdDSA, RFC 8032)
 *   Private:   32-byte seed, AES-256-CBC encrypted, stored in NVS settings
 *   Public:    32-byte point, stored plaintext in NVS settings
 *   Signature: 64-byte (r∥s)
 *
 * Key provisioning (NVS settings keys):
 *   vault/ssh/count  → "N"               (0–4)
 *   vault/ssh/0/name → "GitHub personal"
 *   vault/ssh/0/wk   → 64-char hex       (AES-256 wrap key, hardware RNG)
 *   vault/ssh/0/iv   → 32-char hex       (AES-256 IV, 16 bytes)
 *   vault/ssh/0/enc  → 64-char hex       (AES-256-CBC(seed, wrap_key, iv))
 *   vault/ssh/0/pub  → 64-char hex       (Ed25519 public key, 32 bytes)
 *
 * Keys are generated on-device with A (in setup state). There is no import.
 *
 * Display controls:
 *   UP / DOWN    scroll key list
 *   A            approve pending sign request; generate new key (setup)
 *   B            deny pending sign request; back
 *   X            delete selected key (long-press style: hold + A to confirm)
 *   Y            show public key fingerprint detail
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

#define MAX_KEYS        4
#define KEY_NAME_MAX   32

/* NVS settings */
#define SSH_COUNT_KEY  "vault/ssh/count"

/* Display geometry */
#define SCR_W  320
#define SCR_H  240
#define HDR_H   20
#define FOOT_Y 220
#define ROW_H   22
#define VROWS    8
#define LIST_Y  (HDR_H + 2)

/* Colors */
#define COL_BG      CONSOLE_COLOR_BG
#define COL_HDR     CONSOLE_COLOR_HEADER
#define COL_SEP     CONSOLE_COLOR_SEP
#define COL_ACCENT  CONSOLE_COLOR_ACCENT
#define COL_TEXT    CONSOLE_COLOR_TEXT
#define COL_DIM     CONSOLE_COLOR_DIM
#define COL_SEL     CONSOLE_COLOR_SEL_BG
#define COL_OK      CONSOLE_COLOR_OK
#define COL_WARN    CONSOLE_COLOR_WARN
#define COL_ERR     CONSOLE_COLOR_ERR

/* App states */
#define STATE_SETUP    0   /* no keys generated yet */
#define STATE_LIST     1   /* key list view */
#define STATE_APPROVE  2   /* sign request pending approval */
#define STATE_DETAIL   3   /* public key fingerprint detail */
#define STATE_KEYGEN   4   /* generating key (blocking) */
#define STATE_CONFIRM_DELETE 5

/* SSH agent message types */
#define SSH_AGENT_FAILURE                  5
#define SSH_AGENT_SUCCESS                  6
#define SSH2_AGENT_IDENTITIES_ANSWER      12
#define SSH2_AGENT_SIGN_RESPONSE          14
#define SSH2_AGENTC_REQUEST_IDENTITIES    11
#define SSH2_AGENTC_SIGN_REQUEST          13

/* Sign request timeout */
#define SIGN_TIMEOUT_MS  30000

/* UART */
#define UART_PORT    0
#define UART_BAUD    115200

/* RX framing buffer */
#define RX_BUF_SIZE  4096
#define TX_BUF_SIZE   512

/* AES-256-CBC block / key sizes */
#define AES_BLOCK  16
#define AES_KEY    32

/* ═══════════════════════════════════════════════════════════════════════
 * State
 * ═══════════════════════════════════════════════════════════════════════ */

static int      g_state;
static int      g_key_count;
static int      g_cursor;
static int      g_scroll;
static int      g_uart;          /* UART handle */
static int      g_tmr;
static uint32_t g_prev_btns;

/* Per-key data */
static char    g_names  [MAX_KEYS][KEY_NAME_MAX];  /* display name */
static uint8_t g_pubkeys[MAX_KEYS][32];            /* Ed25519 public key */
/* Encrypted private seeds (AES-256-CBC, 32 bytes → 32 bytes, no padding needed) */
static uint8_t g_enc_seed [MAX_KEYS][32];
static uint8_t g_wrap_key [MAX_KEYS][AES_KEY];
static uint8_t g_wrap_iv  [MAX_KEYS][AES_BLOCK];

/* Pending sign request */
static int     g_sign_key_idx;       /* which key was requested */
static uint8_t g_sign_data[2048];    /* data to sign */
static uint32_t g_sign_data_len;
static uint32_t g_sign_req_time;     /* uptime when request arrived */

/* Serial RX accumulator */
static uint8_t  g_rx_buf[RX_BUF_SIZE];
static uint32_t g_rx_len;

/* TX_BUF_SIZE kept for clarity even though tx uses local arrays */

/* Scratch for fingerprint display */
static char g_fp_str[52];   /* "SHA256:" + 43 chars base64 + NUL */

/* ═══════════════════════════════════════════════════════════════════════
 * String helpers (no stdlib)
 * ═══════════════════════════════════════════════════════════════════════ */

static int slen(const char *s)
{
    int n = 0; while (s[n]) n++; return n;
}
static void sncopy(char *d, const char *s, int max)
{
    int i = 0; while (i < max-1 && s[i]) { d[i]=s[i]; i++; } d[i]='\0';
}
static int str_to_int(const char *s)
{
    int v=0; while (*s>='0'&&*s<='9') v=v*10+(*s++)-'0'; return v;
}
static void int_to_str(char *b, int v)
{
    if (!v) { b[0]='0'; b[1]='\0'; return; }
    char t[12]; int n=0;
    while (v>0){t[n++]='0'+v%10; v/=10;}
    int i; for (i=0;i<n;i++) b[i]=t[n-1-i]; b[n]='\0';
}

/* ── Hex encode / decode ─────────────────────────────────────────────── */
static const char HEX[] = "0123456789abcdef";

static void bin_to_hex(char *hex, const uint8_t *bin, int len)
{
    for (int i=0;i<len;i++){hex[i*2]=HEX[bin[i]>>4];hex[i*2+1]=HEX[bin[i]&0xF];}
    hex[len*2]='\0';
}
static int hex_digit(char c)
{
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return 0;
}
static void hex_to_bin(uint8_t *bin, const char *hex, int n_bytes)
{
    for (int i=0;i<n_bytes;i++)
        bin[i]=(uint8_t)((hex_digit(hex[i*2])<<4)|hex_digit(hex[i*2+1]));
}

/* ── Base64 (for SSH fingerprint display) ────────────────────────────── */
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode_nopad(char *out, const uint8_t *in, int len)
{
    int i=0,j=0;
    while (i<len) {
        uint32_t a=in[i++];
        uint32_t b=(i<len)?in[i++]:0;
        uint32_t c=(i<len)?in[i++]:0;
        out[j++]=B64[(a>>2)&0x3F];
        out[j++]=B64[((a<<4)|(b>>4))&0x3F];
        out[j++]=B64[((b<<2)|(c>>6))&0x3F];
        out[j++]=B64[c&0x3F];
    }
    /* strip trailing '=' padding */
    while (j>0 && out[j-1]=='A' && (len%3)!=0) j--;
    /* actually: trailing padding based on len%3 */
    if (len%3==1) j-=2;
    else if (len%3==2) j-=1;
    out[j]='\0';
}

/* ── NVS settings key builder ────────────────────────────────────────── */
static void build_ssh_key(char *buf, int idx, const char *field)
{
    /* "vault/ssh/N/field" */
    buf[0]='v';buf[1]='a';buf[2]='u';buf[3]='l';buf[4]='t';
    buf[5]='/';buf[6]='s';buf[7]='s';buf[8]='h';buf[9]='/';
    buf[10]='0'+idx; buf[11]='/';
    int i=12; while (*field) buf[i++]=*field++;
    buf[i]='\0';
}

/* ═══════════════════════════════════════════════════════════════════════
 * Key storage  (NVS encrypted via AES-256-CBC wrap key)
 *
 * Security layering:
 *   Flash encryption (hardware, ESP32-S3)  ← outer: NVS partition AES-256-XTS
 *     AES-256-CBC wrap key (per-key, hardware RNG) ← inner: software envelope
 *       Ed25519 private seed (32 bytes, encrypted at rest)
 *
 * The wrap key itself is also in NVS — the defence-in-depth rationale is
 * that if the flash encryption efuse is burned the inner layer is
 * redundant. If efuse is not burned (dev boards) the inner layer still
 * requires an attacker to know which NVS key is the wrap key.
 * ═══════════════════════════════════════════════════════════════════════ */

static void save_key(int idx)
{
    char key[24]; char hex[65];

    /* count */
    char cnt[4]; int_to_str(cnt, g_key_count);
    settings_set(SSH_COUNT_KEY, cnt);

    /* name */
    build_ssh_key(key, idx, "name");
    settings_set(key, g_names[idx]);

    /* wrap key (32 bytes → 64 hex) */
    build_ssh_key(key, idx, "wk");
    bin_to_hex(hex, g_wrap_key[idx], AES_KEY);
    settings_set(key, hex);

    /* IV (16 bytes → 32 hex) */
    build_ssh_key(key, idx, "iv");
    bin_to_hex(hex, g_wrap_iv[idx], AES_BLOCK);
    hex[32]='\0';
    settings_set(key, hex);

    /* encrypted seed (32 bytes → 64 hex) */
    build_ssh_key(key, idx, "enc");
    bin_to_hex(hex, g_enc_seed[idx], 32);
    settings_set(key, hex);

    /* public key (32 bytes → 64 hex) */
    build_ssh_key(key, idx, "pub");
    bin_to_hex(hex, g_pubkeys[idx], 32);
    settings_set(key, hex);
}

static int load_keys(void)
{
    char buf[65]; char key[24];

    if (settings_get(SSH_COUNT_KEY, buf, (int32_t)sizeof(buf)) != 0) return 0;
    int n = str_to_int(buf);
    if (n<0||n>MAX_KEYS) return 0;

    int loaded=0;
    for (int i=0;i<n;i++) {
        build_ssh_key(key, i, "name");
        if (settings_get(key, g_names[i], KEY_NAME_MAX) != 0) continue;

        build_ssh_key(key, i, "wk");
        if (settings_get(key, buf, (int32_t)sizeof(buf)) != 0) continue;
        hex_to_bin(g_wrap_key[i], buf, AES_KEY);

        build_ssh_key(key, i, "iv");
        if (settings_get(key, buf, (int32_t)sizeof(buf)) != 0) continue;
        hex_to_bin(g_wrap_iv[i], buf, AES_BLOCK);

        build_ssh_key(key, i, "enc");
        if (settings_get(key, buf, (int32_t)sizeof(buf)) != 0) continue;
        hex_to_bin(g_enc_seed[i], buf, 32);

        build_ssh_key(key, i, "pub");
        if (settings_get(key, buf, (int32_t)sizeof(buf)) != 0) continue;
        hex_to_bin(g_pubkeys[i], buf, 32);

        loaded++;
    }
    g_key_count = loaded;
    return loaded;
}

/* Generate a new Ed25519 key and persist it */
static int generate_key(int idx, const char *name)
{
    uint8_t seed[32];

    /* Generate random seed + derive public key */
    if (crypto_ed25519_keygen(seed, g_pubkeys[idx]) != 0) return -1;

    /* Generate wrap key and IV from hardware RNG */
    crypto_random(g_wrap_key[idx], AES_KEY);
    crypto_random(g_wrap_iv[idx],  AES_BLOCK);

    /* Encrypt seed with wrap key (32 bytes → 32 bytes, exact multiple of 16) */
    if (crypto_aes256_encrypt(g_wrap_key[idx], g_wrap_iv[idx],
                               seed, 32, g_enc_seed[idx]) != 0) {
        /* Zero seed before failure return */
        for (int i=0;i<32;i++) seed[i]=0;
        return -1;
    }

    /* Zero plaintext seed — it only ever lives on WASM stack */
    for (int i=0;i<32;i++) seed[i]=0;

    sncopy(g_names[idx], name, KEY_NAME_MAX);
    if (idx >= g_key_count) g_key_count = idx + 1;

    save_key(idx);
    return 0;
}

/* Decrypt seed into a stack buffer, sign, then zero immediately */
static int sign_with_key(int idx,
                          const uint8_t *msg, uint32_t msg_len,
                          uint8_t *sig_out)
{
    uint8_t seed[32];

    /* Decrypt seed */
    if (crypto_aes256_decrypt(g_wrap_key[idx], g_wrap_iv[idx],
                               g_enc_seed[idx], 32, seed) != 0) {
        return -1;
    }

    int ret = crypto_ed25519_sign(seed, msg, msg_len, sig_out);

    /* Zero seed immediately after signing */
    for (int i=0;i<32;i++) seed[i]=0;

    return ret;
}

/* ═══════════════════════════════════════════════════════════════════════
 * SSH fingerprint
 * Format: "SHA256:" + base64_nopad(sha256(key_blob))
 * key_blob = "\x00\x00\x00\x0Bssh-ed25519\x00\x00\x00\x20" + pubkey (51B)
 * ═══════════════════════════════════════════════════════════════════════ */

static void compute_fingerprint(int idx)
{
    uint8_t blob[51];
    /* 4-byte length of "ssh-ed25519" = 11 */
    blob[0]=0; blob[1]=0; blob[2]=0; blob[3]=11;
    blob[4]='s';blob[5]='s';blob[6]='h';blob[7]='-';
    blob[8]='e';blob[9]='d';blob[10]='2';blob[11]='5';
    blob[12]='5';blob[13]='1';blob[14]='9';
    /* 4-byte length of pubkey = 32 */
    blob[15]=0; blob[16]=0; blob[17]=0; blob[18]=32;
    for (int i=0;i<32;i++) blob[19+i] = g_pubkeys[idx][i];

    uint8_t digest[32];
    crypto_sha256(blob, 51, digest);

    g_fp_str[0]='S';g_fp_str[1]='H';g_fp_str[2]='A';g_fp_str[3]='2';
    g_fp_str[4]='5';g_fp_str[5]='6';g_fp_str[6]=':';
    base64_encode_nopad(g_fp_str + 7, digest, 32);
}

/* ═══════════════════════════════════════════════════════════════════════
 * SSH agent wire helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static uint32_t read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|p[3];
}
static void write_u32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8);  p[3]=(uint8_t)v;
}
static int write_str(uint8_t *p, const uint8_t *data, uint32_t len)
{
    write_u32(p, len);
    for (uint32_t i=0;i<len;i++) p[4+i]=data[i];
    return (int)(4+len);
}

/* Build key_blob for one ssh-ed25519 key (51 bytes) into buf */
static int build_key_blob(uint8_t *buf, const uint8_t *pubkey)
{
    static const uint8_t ED25519_TAG[] = {'s','s','h','-','e','d','2','5','5','1','9'};
    int n=0;
    n += write_str(buf+n, ED25519_TAG, 11);
    n += write_str(buf+n, pubkey, 32);
    return n; /* = 51 */
}

/* Build sig_blob for an Ed25519 signature (83 bytes) into buf */
static int build_sig_blob(uint8_t *buf, const uint8_t *sig)
{
    static const uint8_t ED25519_TAG[] = {'s','s','h','-','e','d','2','5','5','1','9'};
    int n=0;
    n += write_str(buf+n, ED25519_TAG, 11);
    n += write_str(buf+n, sig, 64);
    return n; /* = 83 */
}

/* ── Send SSH agent failure ───────────────────────────────────────────── */
static void send_failure(void)
{
    uint8_t pkt[5];
    write_u32(pkt, 1);
    pkt[4] = SSH_AGENT_FAILURE;
    uart_write(g_uart, pkt, 5);
}

/* ── Handle SSH2_AGENTC_REQUEST_IDENTITIES ───────────────────────────── */
static void handle_identities(void)
{
    /*
     * SSH2_AGENT_IDENTITIES_ANSWER:
     *   1 byte  msg type (12)
     *   4 bytes nkeys
     *   for each:
     *     string key_blob (51 bytes)
     *     string comment  (4 + name_len)
     */
    uint8_t  msg[TX_BUF_SIZE];
    uint32_t pos = 1;  /* skip 4-byte length prefix, fill later */

    msg[pos++] = SSH2_AGENT_IDENTITIES_ANSWER;
    write_u32(msg + pos, (uint32_t)g_key_count); pos += 4;

    for (int i=0;i<g_key_count;i++) {
        uint8_t blob[51];
        int blen = build_key_blob(blob, g_pubkeys[i]);
        if (pos + 4 + (uint32_t)blen + 4 + (uint32_t)slen(g_names[i])
            >= TX_BUF_SIZE - 4) break;

        pos += (uint32_t)write_str(msg+pos, blob, (uint32_t)blen);
        pos += (uint32_t)write_str(msg+pos,
                                   (const uint8_t *)g_names[i],
                                   (uint32_t)slen(g_names[i]));
    }

    /* Fill in total length (everything after the 4-byte prefix) */
    write_u32(msg, pos - 4);
    uart_write(g_uart, msg, pos);
}

/* ── Handle SSH2_AGENTC_SIGN_REQUEST ────────────────────────────────── */
static void handle_sign_request(const uint8_t *payload, uint32_t payload_len)
{
    /*
     * payload layout (after message type byte):
     *   string key_blob    → we use first 4 bytes (blob len) to skip it
     *   string data        → what we must sign
     *   uint32 flags       → ignored (Ed25519 has no flag variants)
     */
    if (payload_len < 8) { send_failure(); return; }

    uint32_t blob_len = read_u32(payload);
    if (blob_len + 8 > payload_len) { send_failure(); return; }

    /* Identify which key by matching the pubkey embedded in the blob */
    const uint8_t *blob = payload + 4;
    int matched = -1;
    for (int i=0;i<g_key_count;i++) {
        uint8_t expected[51];
        build_key_blob(expected, g_pubkeys[i]);
        int eq=1;
        for (uint32_t j=0;j<blob_len&&j<51;j++) if (blob[j]!=expected[j]){eq=0;break;}
        if (eq && blob_len==51) { matched=i; break; }
    }
    if (matched < 0) { send_failure(); return; }

    /* Parse data_to_sign */
    const uint8_t *dp = payload + 4 + blob_len;
    uint32_t remaining = payload_len - 4 - blob_len;
    if (remaining < 4) { send_failure(); return; }
    uint32_t data_len = read_u32(dp); dp += 4; remaining -= 4;
    if (data_len > remaining || data_len > sizeof(g_sign_data)) {
        send_failure(); return;
    }

    /* Store request for user approval */
    g_sign_key_idx  = matched;
    for (uint32_t i=0;i<data_len;i++) g_sign_data[i]=dp[i];
    g_sign_data_len = data_len;
    g_sign_req_time = (uint32_t)rtc_get_uptime_ms();
    g_state         = STATE_APPROVE;
}

/* ── Dispatch incoming SSH agent message ─────────────────────────────── */
static void dispatch_ssh_message(const uint8_t *msg, uint32_t len)
{
    if (len == 0) { send_failure(); return; }

    uint8_t type = msg[0];
    const uint8_t *payload = msg + 1;
    uint32_t payload_len = len - 1;

    switch (type) {
    case SSH2_AGENTC_REQUEST_IDENTITIES:
        handle_identities();
        break;
    case SSH2_AGENTC_SIGN_REQUEST:
        if (g_key_count == 0) { send_failure(); return; }
        handle_sign_request(payload, payload_len);
        break;
    default:
        send_failure();
        break;
    }
}

/* ── Execute approved sign request ───────────────────────────────────── */
static void execute_sign(void)
{
    uint8_t sig[64];
    if (sign_with_key(g_sign_key_idx,
                      g_sign_data, g_sign_data_len, sig) != 0) {
        send_failure();
        return;
    }

    /*
     * SSH2_AGENT_SIGN_RESPONSE:
     *   1 byte  msg type (14)
     *   string  sig_blob (83 bytes)
     */
    uint8_t msg[TX_BUF_SIZE];
    uint32_t pos = 4; /* leave room for length prefix */
    msg[pos++] = SSH2_AGENT_SIGN_RESPONSE;

    uint8_t sig_blob[83];
    int slen_blob = build_sig_blob(sig_blob, sig);
    pos += (uint32_t)write_str(msg+pos, sig_blob, (uint32_t)slen_blob);

    write_u32(msg, pos - 4);
    uart_write(g_uart, msg, pos);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Serial RX pump — accumulate bytes and dispatch complete messages
 * ═══════════════════════════════════════════════════════════════════════ */

static void poll_uart(void)
{
    uint8_t tmp[64];
    int n = uart_read(g_uart, tmp, (uint32_t)sizeof(tmp));
    if (n <= 0) return;

    /* Append to buffer */
    for (int i=0;i<n;i++) {
        if (g_rx_len < RX_BUF_SIZE) g_rx_buf[g_rx_len++] = tmp[i];
    }

    /* Process complete messages */
    while (g_rx_len >= 4) {
        uint32_t msg_len = read_u32(g_rx_buf);
        if (msg_len == 0 || msg_len > RX_BUF_SIZE - 4) {
            g_rx_len = 0;
            break;
        }
        if (g_rx_len < 4 + msg_len) break; /* wait for more bytes */

        /* Only handle new sign requests when idle (don't queue) */
        if (g_state != STATE_APPROVE) {
            dispatch_ssh_message(g_rx_buf + 4, msg_len);
        } else {
            /* One pending at a time — reject additional sign requests */
            uint8_t type = (msg_len>0) ? g_rx_buf[4] : 0;
            if (type == SSH2_AGENTC_SIGN_REQUEST) send_failure();
            else if (type == SSH2_AGENTC_REQUEST_IDENTITIES) handle_identities();
            else send_failure();
        }

        /* Consume message from buffer */
        uint32_t consumed = 4 + msg_len;
        g_rx_len -= consumed;
        for (uint32_t i=0;i<g_rx_len;i++) g_rx_buf[i]=g_rx_buf[consumed+i];
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Display
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_header(const char *left, const char *right)
{
    display_rect(0, 0, SCR_W, HDR_H, COL_HDR);
    display_text(6, 6, left, COL_TEXT);
    if (right) {
        int rx = SCR_W - slen(right)*7 - 4;
        display_text(rx, 6, right, COL_ACCENT);
    }
    display_hline(0, HDR_H, SCR_W, COL_SEP);
}
static void draw_footer(const char *hint)
{
    display_hline(0, FOOT_Y-1, SCR_W, COL_SEP);
    display_rect(0, FOOT_Y, SCR_W, SCR_H-FOOT_Y, COL_HDR);
    display_text(4, FOOT_Y+6, hint, COL_DIM);
}

/* ── STATE_SETUP ────────────────────────────────────────────────────── */
static void render_setup(void)
{
    display_clear(COL_BG);
    draw_header("SSH KEY AGENT", NULL);

    /* Padlock (open) icon */
    display_circle(160, 75, 22, COL_ACCENT);
    display_circle_fill(160, 75, 14, COL_BG);
    display_circle_fill(160, 75, 5, COL_ACCENT);
    /* shackle opening */
    display_rect(148, 53, 12, 16, COL_BG);  /* gap in shackle */

    display_text(SCR_W/2 - 63, 104, "No SSH keys on device.", COL_DIM);
    display_text(SCR_W/2 - 70, 120, "Press A to generate a new key.", COL_TEXT);
    display_text(SCR_W/2 - 84, 140, "Private seed never leaves this device.", COL_DIM);
    display_text(SCR_W/2 - 77, 156, "Key type: ssh-ed25519  (Ed25519 PSA)", COL_DIM);

    draw_footer("A:generate new key");
    display_flush();
}

/* ── STATE_KEYGEN ───────────────────────────────────────────────────── */
static void render_keygen(void)
{
    display_clear(COL_BG);
    draw_header("SSH KEY AGENT", NULL);
    display_text(SCR_W/2 - 63, 110, "Generating key pair...", COL_ACCENT);
    display_text(SCR_W/2 - 63, 128, "Collecting hardware entropy", COL_DIM);
    display_flush();
}

/* ── STATE_LIST ─────────────────────────────────────────────────────── */
static void render_list(void)
{
    display_clear(COL_BG);

    char hdr_r[8];
    hdr_r[0]='0'+g_key_count; hdr_r[1]=' '; hdr_r[2]='k';
    hdr_r[3]='e'; hdr_r[4]='y'; hdr_r[5]='s'; hdr_r[6]='\0';
    draw_header("SSH KEY AGENT", hdr_r);

    /* Agent status indicator */
    display_rect(6, HDR_H+3, 7, 7,
        (g_uart >= 0) ? COL_OK : COL_ERR);
    display_text(17, HDR_H+3,
        (g_uart >= 0) ? "agent ready" : "UART error", COL_DIM);

    for (int v=0;v<VROWS;v++) {
        int idx = g_scroll + v;
        if (idx >= g_key_count) break;
        int y = LIST_Y + 14 + v*ROW_H;
        int sel = (idx == g_cursor);

        display_rect(0, y-2, SCR_W, ROW_H, sel ? COL_SEL : COL_BG);

        /* Key icon: small lock rect */
        display_rect_outline(6, y+2, 9, 9, sel ? COL_ACCENT : COL_SEP);
        display_circle_fill(10, y+1, 3, sel ? COL_ACCENT : COL_SEP);

        /* Name */
        char name_buf[22];
        sncopy(name_buf, g_names[idx], 21);
        display_text(22, y+4, name_buf, sel ? COL_TEXT : COL_DIM);

        /* Fingerprint short (first 20 chars after "SHA256:") */
        compute_fingerprint(idx);
        char fp_short[22];
        sncopy(fp_short, g_fp_str + 7, 21); /* skip "SHA256:" */
        display_text(200, y+4, fp_short, COL_SEP);

        if (sel) display_rect_outline(0, y-2, SCR_W-1, ROW_H-1, COL_ACCENT);
    }

    if (g_key_count > VROWS) {
        int th = VROWS*ROW_H*VROWS/g_key_count; if (th<4) th=4;
        int ty = LIST_Y+14 + VROWS*ROW_H*g_scroll/g_key_count;
        display_rect(SCR_W-3, LIST_Y+14, 3, VROWS*ROW_H, COL_SEP);
        display_rect(SCR_W-3, ty, 3, th, COL_ACCENT);
    }

    if (g_key_count < MAX_KEYS) {
        int add_y = LIST_Y + 14 + g_key_count*ROW_H + 4;
        if (add_y < FOOT_Y - 10)
            display_text(22, add_y, "+ press A to generate new key", COL_SEP);
    }

    draw_footer("A:new key  Y:detail  X:delete  \x18\x19:scroll");
    display_flush();
}

/* ── STATE_APPROVE ──────────────────────────────────────────────────── */
static void render_approve(void)
{
    uint32_t elapsed = (uint32_t)rtc_get_uptime_ms() - g_sign_req_time;
    int secs_left = (int)((SIGN_TIMEOUT_MS - elapsed) / 1000);
    if (secs_left < 0) secs_left = 0;

    display_clear(COL_BG);
    display_rect(0, 0, SCR_W, HDR_H, COL_WARN);
    display_text(6, 6, "SSH SIGN REQUEST", COL_BG);
    display_hline(0, HDR_H, SCR_W, COL_SEP);

    display_text(6, 26, "Key:", COL_DIM);
    display_text(50, 26, g_names[g_sign_key_idx], COL_TEXT);

    display_hline(6, 40, SCR_W-12, COL_SEP);
    display_text(6, 44, "Data (SHA-256 of message):", COL_DIM);

    /* Show SHA-256 of the data-to-sign as hex, 16 bytes per row */
    uint8_t digest[32];
    crypto_sha256(g_sign_data, g_sign_data_len, digest);
    char hex[65]; bin_to_hex(hex, digest, 32);
    display_text(6, 58, hex,      COL_ACCENT); /* first 32 hex chars (16B) */
    display_text(6, 70, hex + 32, COL_ACCENT); /* last  32 hex chars (16B) */

    display_hline(6, 86, SCR_W-12, COL_SEP);

    /* Data length */
    display_text(6, 92, "Size:", COL_DIM);
    char sz[16]; int_to_str(sz, (int)g_sign_data_len);
    int sn = slen(sz);
    sz[sn]=' '; sz[sn+1]='b'; sz[sn+2]='y'; sz[sn+3]='t'; sz[sn+4]='e'; sz[sn+5]='s';
    sz[sn+6]='\0';
    display_text(46, 92, sz, COL_TEXT);

    /* Countdown bar */
    display_text(6, 108, "Auto-deny in:", COL_DIM);
    char cs[4]; int_to_str(cs, secs_left); int cn=slen(cs);
    cs[cn]='s'; cs[cn+1]='\0';
    display_text(SCR_W-30, 108, cs,
                 secs_left>10 ? COL_OK : secs_left>5 ? COL_WARN : COL_ERR);
    display_progress_bar(6, 120, SCR_W-12, 6,
                         secs_left, 30,
                         secs_left>10 ? COL_OK : secs_left>5 ? COL_WARN : COL_ERR,
                         COL_SEP);

    /* Approve / deny buttons */
    display_rect_outline(20, 140, 120, 32, COL_OK);
    display_text(32, 154, "[A] APPROVE", COL_OK);

    display_rect_outline(180, 140, 120, 32, COL_ERR);
    display_text(192, 154, "[B] DENY", COL_ERR);

    draw_footer("A:approve and sign  B:deny");
    display_flush();
}

/* ── STATE_DETAIL ───────────────────────────────────────────────────── */
static void render_detail(int idx)
{
    display_clear(COL_BG);
    draw_header("KEY DETAIL", "B:back");

    int y = 28;
    display_text(6, y, "Name",     COL_DIM);
    display_text(80, y, g_names[idx], COL_TEXT); y+=16;

    display_text(6, y, "Type",     COL_DIM);
    display_text(80, y, "ssh-ed25519", COL_ACCENT); y+=16;

    compute_fingerprint(idx);
    display_text(6, y, "Fingerprint", COL_DIM); y+=14;
    /* Show full fingerprint: "SHA256:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" */
    /* 50 chars total — split across two rows at 25 chars each */
    char row1[28], row2[28];
    sncopy(row1, g_fp_str, 26);
    sncopy(row2, g_fp_str+25, 26);
    display_text(6, y, row1, COL_ACCENT); y+=12;
    display_text(6, y, row2, COL_ACCENT); y+=16;

    display_hline(6, y, SCR_W-12, COL_SEP); y+=6;

    /* Public key hex, 16 bytes per row = 32 hex chars per row × 2 rows */
    display_text(6, y, "Public key (hex)", COL_DIM); y+=14;
    char hex[65]; bin_to_hex(hex, g_pubkeys[idx], 32);
    display_text(6, y, hex,      COL_DIM); y+=12;
    display_text(6, y, hex + 32, COL_DIM); y+=16;

    display_hline(6, y, SCR_W-12, COL_SEP); y+=6;
    display_text(6, y, "Private key: encrypted on-device", COL_SEP); y+=14;
    display_text(6, y, "AES-256-CBC (wrap key) + flash enc.", COL_SEP);

    draw_footer("B:back  X:delete key");
    display_flush();
}

/* ── STATE_CONFIRM_DELETE ───────────────────────────────────────────── */
static void render_confirm_delete(int idx)
{
    display_clear(COL_BG);
    display_rect(0, 0, SCR_W, HDR_H, COL_ERR);
    display_text(6, 6, "DELETE KEY?", COL_TEXT);
    display_hline(0, HDR_H, SCR_W, COL_SEP);

    display_text(SCR_W/2-91, 80, "This will permanently delete:", COL_DIM);
    display_text(SCR_W/2-42, 96, g_names[idx], COL_TEXT);
    display_text(SCR_W/2-91,112, "The private key cannot be recovered.", COL_WARN);

    display_rect_outline(20, 148, 120, 32, COL_ERR);
    display_text(25, 162, "[A] DELETE", COL_ERR);
    display_rect_outline(180, 148, 120, 32, COL_OK);
    display_text(192, 162, "[B] CANCEL", COL_OK);

    draw_footer("A:delete  B:cancel");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════
 * Key deletion
 * ═══════════════════════════════════════════════════════════════════════ */

static void delete_key(int idx)
{
    char key[24];

    /* Zero in-memory key material */
    for (int i=0;i<32;i++) g_pubkeys  [idx][i]=0;
    for (int i=0;i<32;i++) g_enc_seed [idx][i]=0;
    for (int i=0;i<32;i++) g_wrap_key [idx][i]=0;
    for (int i=0;i<16;i++) g_wrap_iv  [idx][i]=0;

    /* Delete NVS entries */
    build_ssh_key(key, idx, "name"); settings_delete(key);
    build_ssh_key(key, idx, "wk");   settings_delete(key);
    build_ssh_key(key, idx, "iv");   settings_delete(key);
    build_ssh_key(key, idx, "enc");  settings_delete(key);
    build_ssh_key(key, idx, "pub");  settings_delete(key);

    /* Compact remaining keys */
    for (int i=idx;i<g_key_count-1;i++) {
        sncopy(g_names[i], g_names[i+1], KEY_NAME_MAX);
        for (int j=0;j<32;j++) g_pubkeys [i][j]=g_pubkeys [i+1][j];
        for (int j=0;j<32;j++) g_enc_seed[i][j]=g_enc_seed[i+1][j];
        for (int j=0;j<32;j++) g_wrap_key[i][j]=g_wrap_key[i+1][j];
        for (int j=0;j<16;j++) g_wrap_iv [i][j]=g_wrap_iv [i+1][j];
        save_key(i);
    }
    g_key_count--;
    char cnt[4]; int_to_str(cnt, g_key_count);
    settings_set(SSH_COUNT_KEY, cnt);

    /* Delete the now-dangling last NVS slot */
    build_ssh_key(key, g_key_count, "name"); settings_delete(key);
    build_ssh_key(key, g_key_count, "wk");   settings_delete(key);
    build_ssh_key(key, g_key_count, "iv");   settings_delete(key);
    build_ssh_key(key, g_key_count, "enc");  settings_delete(key);
    build_ssh_key(key, g_key_count, "pub");  settings_delete(key);

    if (g_cursor >= g_key_count && g_cursor > 0) g_cursor--;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Input handling
 * ═══════════════════════════════════════════════════════════════════════ */

static void handle_input(void)
{
    uint32_t btns = (uint32_t)input_get_buttons();
    uint32_t edge = btns & ~g_prev_btns;
    g_prev_btns   = btns;

    switch (g_state) {

    case STATE_SETUP:
        if (edge & AKIRA_BTN_A) {
            if (g_key_count < MAX_KEYS) {
                g_state = STATE_KEYGEN;
                render_keygen();
                display_flush();
                /* Generate key #0 with a default name */
                if (generate_key(g_key_count, "AkiraOS SSH Key") == 0) {
                    g_state = STATE_LIST;
                } else {
                    g_state = STATE_SETUP; /* generation failed */
                }
            }
        }
        break;

    case STATE_LIST:
        if (edge & AKIRA_BTN_UP) {
            if (g_cursor > 0) {
                g_cursor--;
                if (g_cursor < g_scroll) g_scroll = g_cursor;
            }
        }
        if (edge & AKIRA_BTN_DOWN) {
            if (g_cursor < g_key_count) {
                g_cursor++;
                if (g_cursor >= g_scroll + VROWS) g_scroll = g_cursor - VROWS + 1;
            }
        }
        if (edge & AKIRA_BTN_A) {
            /* Generate new key (cursor past last) or act on selected */
            if (g_cursor == g_key_count && g_key_count < MAX_KEYS) {
                g_state = STATE_KEYGEN;
                render_keygen();
                display_flush();
                if (generate_key(g_key_count, "AkiraOS SSH Key") == 0) {
                    g_cursor = g_key_count - 1;
                }
                g_state = (g_key_count > 0) ? STATE_LIST : STATE_SETUP;
            }
        }
        if ((edge & AKIRA_BTN_Y) && g_cursor < g_key_count) {
            g_state = STATE_DETAIL;
        }
        if ((edge & AKIRA_BTN_X) && g_cursor < g_key_count) {
            g_state = STATE_CONFIRM_DELETE;
        }
        break;

    case STATE_APPROVE: {
        uint32_t elapsed = (uint32_t)rtc_get_uptime_ms() - g_sign_req_time;
        if ((edge & AKIRA_BTN_A) || elapsed >= SIGN_TIMEOUT_MS) {
            if ((edge & AKIRA_BTN_A) && elapsed < SIGN_TIMEOUT_MS) {
                execute_sign();
            } else {
                send_failure();
            }
            g_state = STATE_LIST;
        }
        if (edge & AKIRA_BTN_B) {
            send_failure();
            g_state = STATE_LIST;
        }
        break;
    }

    case STATE_DETAIL:
        if (edge & AKIRA_BTN_B) { g_state = STATE_LIST; }
        if ((edge & AKIRA_BTN_X) && g_cursor < g_key_count) {
            g_state = STATE_CONFIRM_DELETE;
        }
        break;

    case STATE_CONFIRM_DELETE:
        if (edge & AKIRA_BTN_A) {
            delete_key(g_cursor);
            g_state = (g_key_count > 0) ? STATE_LIST : STATE_SETUP;
        }
        if (edge & AKIRA_BTN_B) {
            g_state = (g_cursor < g_key_count) ? STATE_LIST : STATE_LIST;
        }
        break;
    }
}

/* ── Sign request timeout check (called from main loop) ──────────────── */
static void check_sign_timeout(void)
{
    if (g_state != STATE_APPROVE) return;
    uint32_t elapsed = (uint32_t)rtc_get_uptime_ms() - g_sign_req_time;
    if (elapsed >= SIGN_TIMEOUT_MS) {
        send_failure();
        g_state = STATE_LIST;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════ */

int main(void)
{
    printf("vault.ssh-key v1.0 — Ed25519 SSH agent");

    g_tmr = timer_create();
    g_prev_btns = 0;
    g_cursor = 0; g_scroll = 0;
    g_rx_len = 0;

    /* Open UART for SSH agent protocol */
    g_uart = uart_open(UART_PORT, UART_BAUD);
    if (g_uart < 0) {
        printf("vault.ssh-key: UART open failed (%d)", g_uart);
    }

    /* Load persisted keys */
    load_keys();
    g_state = (g_key_count > 0) ? STATE_LIST : STATE_SETUP;

    while (1) {
        timer_start(g_tmr);

        poll_uart();
        check_sign_timeout();
        handle_input();

        switch (g_state) {
        case STATE_SETUP:   render_setup();                       break;
        case STATE_LIST:    render_list();                        break;
        case STATE_APPROVE: render_approve();                     break;
        case STATE_DETAIL:  render_detail(g_cursor);              break;
        case STATE_CONFIRM_DELETE: render_confirm_delete(g_cursor); break;
        default: break;
        }

        int used = timer_elapsed(g_tmr);
        if (used < 100) delay((uint32_t)(100 - used) * 1000);
    }
    return 0;
}
