/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * akira_vault — Hardware Crypto Wallet + Password Manager
 *
 * Features:
 *   - PIN-protected encrypted vault (ChaCha20 + PBKDF2-HMAC-SHA256)
 *   - Password manager with USB/BLE HID autotype
 *   - TOTP authenticator (RFC 6238, HMAC-SHA256 variant)
 *   - 256-bit entropy wallet seed generator
 *
 * Navigation:
 *   UP / DOWN    — scroll list / navigate fields
 *   LEFT / RIGHT — char picker / toggle values
 *   A            — confirm / select / autotype password
 *   B            — back / cancel / delete char
 *   X            — delete entry (with confirmation)
 *   Y            — add new entry / secondary action
 */

#include "../include/akira_api.h"
#include <stdint.h>

/* GPIO button pins (prod board, ACTIVE_HIGH, PULL_DOWN) */
#define VPIN_UP    4
#define VPIN_DOWN  5
#define VPIN_LEFT  6
#define VPIN_RIGHT 7
#define VPIN_A     15
#define VPIN_B     16
#define VPIN_X     17
#define VPIN_Y     41

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * MEMORY HELPERS (no stdlib)
 * ═══════════════════════════════════════════════════════════════════════ */

static void mem_set(void *s, int c, int n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
}

static void mem_cpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static int mem_cmp(const void *a, const void *b, int n) {
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

static void str_ncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_nlen(const char *s, int max) {
    int n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static int int_to_str(int v, char *buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12]; int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    if (neg) tmp[i++] = '-';
    int len = i;
    for (int j = 0; j < len; j++) buf[j] = tmp[len - 1 - j];
    buf[len] = '\0';
    return len;
}

static void uint32_zero_pad(uint32_t v, int digits, char *buf) {
    buf[digits] = '\0';
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (v % 10);
        v /= 10;
    }
}

static void to_hex(const uint8_t *src, int len, char *dst) {
    const char *h = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        dst[i * 2]     = h[src[i] >> 4];
        dst[i * 2 + 1] = h[src[i] & 0xF];
    }
    dst[len * 2] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════════
 * SHA-256
 * ═══════════════════════════════════════════════════════════════════════ */

static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define S0(x) (ROTR32(x,2)^ROTR32(x,13)^ROTR32(x,22))
#define S1(x) (ROTR32(x,6)^ROTR32(x,11)^ROTR32(x,25))
#define G0(x) (ROTR32(x,7)^ROTR32(x,18)^((x)>>3))
#define G1(x) (ROTR32(x,17)^ROTR32(x,19)^((x)>>10))
#define CH(x,y,z) (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))

typedef struct { uint32_t s[8]; uint8_t buf[64]; uint32_t blo, bhi, len; } sha256_t;

static void sha256_transform(sha256_t *c, const uint8_t *d) {
    uint32_t a,b,cc,e,f,g,h,t1,t2,w[64]; int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)d[i*4]<<24)|((uint32_t)d[i*4+1]<<16)|((uint32_t)d[i*4+2]<<8)|d[i*4+3];
    for (; i < 64; i++) w[i] = G1(w[i-2])+w[i-7]+G0(w[i-15])+w[i-16];
    a=c->s[0];b=c->s[1];cc=c->s[2];uint32_t d2=c->s[3];
    e=c->s[4];f=c->s[5];g=c->s[6];h=c->s[7];
    for (i = 0; i < 64; i++) {
        t1=h+S1(e)+CH(e,f,g)+SHA256_K[i]+w[i]; t2=S0(a)+MAJ(a,b,cc);
        h=g;g=f;f=e;e=d2+t1;d2=cc;cc=b;b=a;a=t1+t2;
    }
    c->s[0]+=a;c->s[1]+=b;c->s[2]+=cc;c->s[3]+=d2;
    c->s[4]+=e;c->s[5]+=f;c->s[6]+=g;c->s[7]+=h;
}

static void sha256_init(sha256_t *c) {
    c->s[0]=0x6a09e667;c->s[1]=0xbb67ae85;c->s[2]=0x3c6ef372;c->s[3]=0xa54ff53a;
    c->s[4]=0x510e527f;c->s[5]=0x9b05688c;c->s[6]=0x1f83d9ab;c->s[7]=0x5be0cd19;
    c->blo=c->bhi=c->len=0;
}

static void sha256_update(sha256_t *c, const uint8_t *data, int len) {
    while (len--) {
        c->buf[c->len++] = *data++;
        if (c->len == 64) {
            sha256_transform(c, c->buf);
            c->blo += 512; if (!c->blo) c->bhi++;
            c->len = 0;
        }
    }
}

static void sha256_final(sha256_t *c, uint8_t *h) {
    uint32_t i = c->len;
    c->buf[i++] = 0x80;
    if (c->len < 56) { while (i < 56) c->buf[i++] = 0; }
    else { while (i < 64) c->buf[i++] = 0; sha256_transform(c, c->buf); mem_set(c->buf, 0, 56); }
    c->blo += c->len * 8;
    c->buf[63]=c->blo;c->buf[62]=c->blo>>8;c->buf[61]=c->blo>>16;c->buf[60]=c->blo>>24;
    c->buf[59]=c->bhi;c->buf[58]=c->bhi>>8;c->buf[57]=c->bhi>>16;c->buf[56]=c->bhi>>24;
    sha256_transform(c, c->buf);
    for (i = 0; i < 4; i++) {
        h[i]=c->s[0]>>(24-i*8);h[i+4]=c->s[1]>>(24-i*8);h[i+8]=c->s[2]>>(24-i*8);h[i+12]=c->s[3]>>(24-i*8);
        h[i+16]=c->s[4]>>(24-i*8);h[i+20]=c->s[5]>>(24-i*8);h[i+24]=c->s[6]>>(24-i*8);h[i+28]=c->s[7]>>(24-i*8);
    }
}

static void sha256_hash(const uint8_t *d, int l, uint8_t *out) {
    sha256_t c; sha256_init(&c); sha256_update(&c,d,l); sha256_final(&c,out);
}

/* ═══════════════════════════════════════════════════════════════════════
 * HMAC-SHA256
 * ═══════════════════════════════════════════════════════════════════════ */

static void hmac_sha256(const uint8_t *key, int klen,
                         const uint8_t *msg, int mlen, uint8_t *out) {
    uint8_t k[64], ipad[64], opad[64], tmp[32];
    mem_set(k, 0, 64);
    if (klen > 64) sha256_hash(key, klen, k);
    else mem_cpy(k, key, klen);
    for (int i = 0; i < 64; i++) { ipad[i] = k[i]^0x36; opad[i] = k[i]^0x5C; }
    sha256_t c;
    sha256_init(&c); sha256_update(&c,ipad,64); sha256_update(&c,msg,mlen); sha256_final(&c,tmp);
    sha256_init(&c); sha256_update(&c,opad,64); sha256_update(&c,tmp,32);  sha256_final(&c,out);
}

/* ═══════════════════════════════════════════════════════════════════════
 * PBKDF2-HMAC-SHA256 (single 32-byte block, N iterations)
 * ═══════════════════════════════════════════════════════════════════════ */

static void pbkdf2(const uint8_t *pass, int plen,
                   const uint8_t *salt, int slen,
                   int iters, uint8_t *out) {
    uint8_t s1[132], u[32], t[32];
    int s1len = slen + 4;
    mem_cpy(s1, salt, slen);
    s1[slen]=0; s1[slen+1]=0; s1[slen+2]=0; s1[slen+3]=1;
    hmac_sha256(pass, plen, s1, s1len, u);
    mem_cpy(t, u, 32);
    for (int i = 1; i < iters; i++) {
        hmac_sha256(pass, plen, u, 32, u);
        for (int j = 0; j < 32; j++) t[j] ^= u[j];
    }
    mem_cpy(out, t, 32);
}

/* ═══════════════════════════════════════════════════════════════════════
 * CHACHA20 STREAM CIPHER
 * ═══════════════════════════════════════════════════════════════════════ */

#define ROTL32(v,n) (((v)<<(n))|((v)>>(32-(n))))
#define QR(a,b,c,d) a+=b;d^=a;d=ROTL32(d,16);c+=d;b^=c;b=ROTL32(b,12);a+=b;d^=a;d=ROTL32(d,8);c+=d;b^=c;b=ROTL32(b,7);

static void chacha20_block(const uint32_t *in, uint32_t *out) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = in[i];
    for (int i = 0; i < 10; i++) {
        QR(x[0],x[4],x[8], x[12]) QR(x[1],x[5],x[9], x[13])
        QR(x[2],x[6],x[10],x[14]) QR(x[3],x[7],x[11],x[15])
        QR(x[0],x[5],x[10],x[15]) QR(x[1],x[6],x[11],x[12])
        QR(x[2],x[7],x[8], x[13]) QR(x[3],x[4],x[9], x[14])
    }
    for (int i = 0; i < 16; i++) out[i] = x[i] + in[i];
}

static void chacha20_xor(const uint8_t *key, const uint8_t *nonce,
                          uint32_t ctr, uint8_t *data, int len) {
    uint32_t st[16];
    st[0]=0x61707865; st[1]=0x3320646e; st[2]=0x79622d32; st[3]=0x6b206574;
    for (int i = 0; i < 8; i++)
        st[4+i] = (uint32_t)key[i*4]|((uint32_t)key[i*4+1]<<8)|
                  ((uint32_t)key[i*4+2]<<16)|((uint32_t)key[i*4+3]<<24);
    st[12] = ctr;
    st[13] = (uint32_t)nonce[0]|((uint32_t)nonce[1]<<8)|((uint32_t)nonce[2]<<16)|((uint32_t)nonce[3]<<24);
    st[14] = (uint32_t)nonce[4]|((uint32_t)nonce[5]<<8)|((uint32_t)nonce[6]<<16)|((uint32_t)nonce[7]<<24);
    st[15] = (uint32_t)nonce[8]|((uint32_t)nonce[9]<<8)|((uint32_t)nonce[10]<<16)|((uint32_t)nonce[11]<<24);
    int pos = 0;
    while (len > 0) {
        uint32_t blk[16]; chacha20_block(st, blk);
        uint8_t *kb = (uint8_t *)blk;
        int n = len < 64 ? len : 64;
        for (int i = 0; i < n; i++) data[pos++] ^= kb[i];
        len -= n; st[12]++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * BASE32 DECODER (for TOTP secrets)
 * ═══════════════════════════════════════════════════════════════════════ */

static int b32_decode(const char *b32, uint8_t *out, int out_max) {
    int bits = 0, acc = 0, n = 0;
    while (*b32 && n < out_max) {
        char c = *b32++;
        int v;
        if (c>='A'&&c<='Z') v=c-'A';
        else if (c>='a'&&c<='z') v=c-'a';
        else if (c>='2'&&c<='7') v=c-'2'+26;
        else if (c=='=') break;
        else continue;
        acc = (acc<<5)|v; bits += 5;
        if (bits >= 8) { out[n++] = (acc>>(bits-8))&0xFF; bits -= 8; }
    }
    return n;
}

/* ═══════════════════════════════════════════════════════════════════════
 * TOTP (RFC 6238, HMAC-SHA256 variant)
 * ═══════════════════════════════════════════════════════════════════════ */

static uint32_t totp_code(const uint8_t *secret, int slen,
                           uint64_t unix_sec, int period, int digits) {
    uint64_t T = unix_sec / (uint32_t)period;
    uint8_t msg[8] = {
        (uint8_t)(T>>56),(uint8_t)(T>>48),(uint8_t)(T>>40),(uint8_t)(T>>32),
        (uint8_t)(T>>24),(uint8_t)(T>>16),(uint8_t)(T>>8),(uint8_t)(T)
    };
    uint8_t mac[32];
    hmac_sha256(secret, slen, msg, 8, mac);
    int off = mac[31] & 0x0F;
    uint32_t code = ((uint32_t)(mac[off]&0x7F)<<24)|((uint32_t)mac[off+1]<<16)|
                    ((uint32_t)mac[off+2]<<8)|(uint32_t)mac[off+3];
    uint32_t mod = 1;
    for (int i = 0; i < digits; i++) mod *= 10;
    return code % mod;
}

/* Timer handle — declared here so PRNG can use it; created in main() */
static int g_uptimer = -1;

/* ═══════════════════════════════════════════════════════════════════════
 * PRNG (ChaCha20-based, seeded from uptime)
 * ═══════════════════════════════════════════════════════════════════════ */

static uint32_t g_rng_st[16];
static uint32_t g_rng_buf[16];
static int g_rng_pos = 64;

static void rng_init(void) {
    /* g_uptimer must be created before this call; timer_elapsed gives entropy */
    uint64_t t = (g_uptimer >= 0) ? (uint64_t)(uint32_t)timer_elapsed(g_uptimer) : 0ULL;
    g_rng_st[0]=0x61707865; g_rng_st[1]=0x3320646e;
    g_rng_st[2]=0x79622d32; g_rng_st[3]=0x6b206574;
    g_rng_st[4]=(uint32_t)t; g_rng_st[5]=(uint32_t)(t>>32);
    g_rng_st[6]=0xDEADBEEF; g_rng_st[7]=0xCAFEBABE;
    g_rng_st[8]=0xFEEDFACE; g_rng_st[9]=0x01234567;
    g_rng_st[10]=0x89ABCDEF; g_rng_st[11]=0x76543210;
    g_rng_st[12]=0; g_rng_st[13]=0xACEF1234;
    g_rng_st[14]=0x5678CDEF; g_rng_st[15]=0xABCD9876;
}

static uint8_t rng_byte(void) {
    if (g_rng_pos >= 64) {
        chacha20_block(g_rng_st, g_rng_buf);
        g_rng_st[12]++;
        g_rng_pos = 0;
    }
    return ((uint8_t *)g_rng_buf)[g_rng_pos++];
}

static void rng_fill(uint8_t *buf, int len) {
    for (int i = 0; i < len; i++) buf[i] = rng_byte();
}

/* Mix more entropy into the RNG state (called on button presses) */
static void rng_stir(uint32_t v) {
    g_rng_st[5] ^= v;
    g_rng_st[6] ^= (g_uptimer >= 0) ? (uint32_t)timer_elapsed(g_uptimer) : v;
    g_rng_pos = 64;
}

/* ═══════════════════════════════════════════════════════════════════════
 * DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════════════ */

#define MAX_PW        10
#define MAX_TOTP       8
#define MAX_WALLET     4
#define PIN_LEN        4

typedef struct {
    char name[24];
    char username[48];
    char password[64];
    char url[32];
} pw_entry_t;          /* 168 bytes */

typedef struct {
    char name[24];
    char secret_b32[48]; /* base32 as entered by user */
    uint8_t digits;      /* 6 or 8 */
    uint8_t period;      /* 30 or 60 */
    uint8_t _pad[6];
} totp_entry_t;        /* 80 bytes */

typedef struct {
    char label[24];
    uint8_t entropy[32]; /* 256-bit entropy seed */
} wallet_entry_t;      /* 56 bytes */

typedef struct {
    uint8_t  magic[4];          /* "AKVO" — checked after decryption to verify PIN */
    uint32_t pw_count;
    uint32_t totp_count;
    uint32_t wallet_count;
    uint8_t  _pad[16];
    pw_entry_t     pw[MAX_PW];
    totp_entry_t   totp[MAX_TOTP];
    wallet_entry_t wallet[MAX_WALLET];
} vault_body_t;

typedef struct {
    uint8_t  magic[4];   /* "AKV1" */
    uint8_t  salt[16];   /* PBKDF2 salt */
    uint8_t  nonce[12];  /* ChaCha20 nonce */
    uint32_t body_len;
} vault_hdr_t;           /* 36 bytes */

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN IDs
 * ═══════════════════════════════════════════════════════════════════════ */

#define SCR_BOOT         0
#define SCR_LOCK         1
#define SCR_NEW_VAULT    2
#define SCR_MENU         3
#define SCR_PW_LIST      4
#define SCR_PW_VIEW      5
#define SCR_PW_ADD       6
#define SCR_TOTP_LIST    7
#define SCR_TOTP_VIEW    8
#define SCR_TOTP_ADD     9
#define SCR_WALLET_LIST  10
#define SCR_WALLET_VIEW  11
#define SCR_SETTINGS     12
#define SCR_PIN_CHANGE   13
#define SCR_TIME_SETUP   14
#define SCR_CHAR_PICKER  15
#define SCR_CONFIRM      16

/* ═══════════════════════════════════════════════════════════════════════
 * APPLICATION STATE
 * ═══════════════════════════════════════════════════════════════════════ */

#define AUTO_LOCK_MS  120000ULL  /* 2 minutes */
#define PBKDF2_ITERS  500

static vault_body_t g_vault;
static vault_hdr_t  g_fhdr;
static uint8_t      g_vkey[32];   /* ChaCha20 key derived from PIN */
static int          g_vault_dirty;

static int      g_screen;
static int      g_cursor;
static int      g_scroll;
static int      g_dirty = 1;
static int      g_selected;       /* item index for view/delete screens */

/* PIN entry */
static char g_pin[PIN_LEN + 2];
static int  g_pin_len;
static int  g_pin_digit;          /* current digit 0–9 being selected */
static char g_pin2[PIN_LEN + 2];  /* for confirm step in new vault / PIN change */
static int  g_pin_step;           /* 0=enter, 1=confirm */
static int  g_pin_err;            /* 1=wrong PIN, 2=mismatch */

/* Time */
static uint64_t g_unix_base;  /* Unix timestamp when time was manually set */
static uint64_t g_ms_base;   /* g_frame value when time was set */
static int      g_time_set;
static int      g_tset_field;     /* 0=year 1=mon 2=day 3=hour 4=min */
static int      g_tset_vals[5];   /* year, mon, day, hour, min */

/* Generic char picker */
static const char *g_pck_charset;
static int         g_pck_cslen;
static char       *g_pck_dst;
static int         g_pck_dst_max;
static int         g_pck_ret_scr; /* screen to return to */
static char        g_pck_title[32];
static char        g_pck_buf[96]; /* edit buffer */
static int         g_pck_len;
static int         g_pck_cidx;    /* char index in charset */

/* Add/edit entry temps */
typedef struct {
    char name[24]; char username[48]; char password[64]; char url[32];
    int  field;    /* 0=name 1=user 2=pass 3=url 4=save */
} pw_add_t;

typedef struct {
    char name[24]; char secret[48];
    int digits; int period;
    int field;  /* 0=name 1=secret 2=digits 3=period 4=save */
} totp_add_t;

static pw_add_t   g_pwa;
static totp_add_t g_tota;
static char       g_wlt_label[24]; /* new wallet label */

/* Confirmation dialog */
static const char *g_conf_msg;
static int         g_conf_action;
static int         g_conf_cursor; /* 0=No 1=Yes */
#define CONF_DEL_PW     1
#define CONF_DEL_TOTP   2
#define CONF_DEL_WALLET 3
#define CONF_WIPE       4

/* Wallet view */
static char g_wlt_hex[65]; /* hex of entropy for display */

/* HID */
static int g_hid_transport; /* HID_TRANSPORT_BLE or HID_TRANSPORT_USB */
static int g_hid_on;

/* Input — GPIO-based, ACTIVE_HIGH PULL_DOWN */
static uint32_t g_prev_btns_gpio;
static int g_hold_frames;
static int g_last_repeat_frames;
static int g_last_activity_frames;
static int g_frame; /* monotonic frame counter (16 ms each) */

/* g_uptimer declared near PRNG section above */

#define HOLD_DELAY_FRAMES   22  /* 22 × 16 ms ≈ 350 ms */
#define REPEAT_FRAMES        8  /* 8 × 16 ms ≈ 130 ms */
#define AUTO_LOCK_FRAMES  7500  /* 7500 × 16 ms = 120 s */
#define FRAME_MS            16  /* ms per main-loop iteration */

/* ═══════════════════════════════════════════════════════════════════════
 * UNIX TIME HELPERS  (purely manual — no RTC API required)
 * ═══════════════════════════════════════════════════════════════════════ */

static uint64_t get_unix(void) {
    if (!g_time_set) return 0;
    /* g_ms_base = frame count when time was set;
       g_unix_base = unix timestamp at that frame */
    int elapsed_frames = g_frame - (int)(g_ms_base & 0x7FFFFFFF);
    if (elapsed_frames < 0) elapsed_frames = 0;
    return g_unix_base + (uint64_t)elapsed_frames * FRAME_MS / 1000;
}

/* Days in month (non-leap) */
static const int DAYS_PER_MON[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

static uint64_t ymd_hm_to_unix(int y, int mo, int d, int hr, int mn) {
    /* Simplified: days since 1970-01-01 */
    long days = 0;
    for (int yr = 1970; yr < y; yr++) {
        int leap = ((yr%4==0&&yr%100!=0)||(yr%400==0));
        days += leap ? 366 : 365;
    }
    int leap = ((y%4==0&&y%100!=0)||(y%400==0));
    for (int m = 1; m < mo; m++) {
        days += DAYS_PER_MON[m];
        if (m == 2 && leap) days++;
    }
    days += d - 1;
    return (uint64_t)days * 86400 + (uint64_t)hr * 3600 + (uint64_t)mn * 60;
}

static void save_time_to_settings(void) {
    char buf[24];
    /* Store current unix time so TOTP survives power cycles */
    uint64_t now = get_unix();
    int_to_str((int)(now & 0x7FFFFFFF), buf);
    settings_set("vault/tunix", buf);
}

static void load_time_from_settings(void) {
    char b1[24];
    if (settings_get("vault/tunix", b1, sizeof(b1)) == 0) {
        uint32_t stored_unix = 0;
        for (int i = 0; b1[i] >= '0' && b1[i] <= '9'; i++)
            stored_unix = stored_unix * 10 + (b1[i] - '0');
        if (stored_unix > 1700000000U) { /* sanity: after 2023 */
            g_unix_base = stored_unix;
            g_ms_base   = 0; /* relative to frame 0 at startup */
            g_time_set  = 1;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * VAULT CRYPTO — LOAD / SAVE
 * ═══════════════════════════════════════════════════════════════════════ */

static int vault_exists(void) {
    int fd = storage_open("vault.bin", STORAGE_O_READ);
    if (fd < 0) return 0;
    storage_close(fd);
    return 1;
}

static int vault_create(const char *pin) {
    mem_set(&g_vault, 0, sizeof(g_vault));
    g_vault.magic[0]='A'; g_vault.magic[1]='K';
    g_vault.magic[2]='V'; g_vault.magic[3]='O';
    rng_fill(g_fhdr.salt, 16);
    rng_fill(g_fhdr.nonce, 12);
    g_fhdr.magic[0]='A'; g_fhdr.magic[1]='K';
    g_fhdr.magic[2]='V'; g_fhdr.magic[3]='1';
    g_fhdr.body_len = sizeof(vault_body_t);
    pbkdf2((uint8_t *)pin, str_nlen(pin, 10), g_fhdr.salt, 16, PBKDF2_ITERS, g_vkey);
    return 0;
}

static void vault_save(void) {
    uint8_t body[sizeof(vault_body_t)];
    mem_cpy(body, &g_vault, sizeof(vault_body_t));
    rng_fill(g_fhdr.nonce, 12); /* fresh nonce each write */
    chacha20_xor(g_vkey, g_fhdr.nonce, 0, body, sizeof(body));
    int fd = storage_open("vault.bin", STORAGE_O_WRITE);
    if (fd < 0) return;
    storage_write(fd, &g_fhdr, sizeof(g_fhdr));
    storage_write(fd, body, sizeof(body));
    storage_close(fd);
    g_vault_dirty = 0;
}

/* Returns 0 on success, -1 on wrong PIN or corrupt */
static int vault_load(const char *pin) {
    int fd = storage_open("vault.bin", STORAGE_O_READ);
    if (fd < 0) return -1;
    int r = storage_read(fd, &g_fhdr, sizeof(g_fhdr));
    if (r != sizeof(g_fhdr) || mem_cmp(g_fhdr.magic, "AKV1", 4)) {
        storage_close(fd); return -1;
    }
    uint8_t body[sizeof(vault_body_t)];
    r = storage_read(fd, body, sizeof(body));
    storage_close(fd);
    if (r < (int)sizeof(vault_body_t)) return -1;
    pbkdf2((uint8_t *)pin, str_nlen(pin, 10), g_fhdr.salt, 16, PBKDF2_ITERS, g_vkey);
    chacha20_xor(g_vkey, g_fhdr.nonce, 0, body, sizeof(body));
    if (mem_cmp(body, "AKVO", 4)) return -1; /* wrong PIN */
    mem_cpy(&g_vault, body, sizeof(vault_body_t));
    return 0;
}

static void vault_lock(void) {
    if (g_vault_dirty) vault_save();
    if (g_time_set) save_time_to_settings();
    mem_set(&g_vault, 0, sizeof(g_vault));
    mem_set(g_vkey,   0, sizeof(g_vkey));
}

/* ═══════════════════════════════════════════════════════════════════════
 * INPUT — GPIO edge + repeat (no input_get_buttons needed)
 * ═══════════════════════════════════════════════════════════════════════ */

static void vault_btns_init(void) {
    uint32_t f = GPIO_INPUT | GPIO_PULL_DOWN;
    gpio_configure(VPIN_UP,    f); gpio_configure(VPIN_DOWN,  f);
    gpio_configure(VPIN_LEFT,  f); gpio_configure(VPIN_RIGHT, f);
    gpio_configure(VPIN_A,     f); gpio_configure(VPIN_B,     f);
    gpio_configure(VPIN_X,     f); gpio_configure(VPIN_Y,     f);
}

static const int VPINS[8] = {
    VPIN_UP, VPIN_DOWN, VPIN_LEFT, VPIN_RIGHT, VPIN_A, VPIN_B, VPIN_X, VPIN_Y
};
static const uint32_t VMASKS[8] = {
    AKIRA_BTN_UP, AKIRA_BTN_DOWN, AKIRA_BTN_LEFT, AKIRA_BTN_RIGHT,
    AKIRA_BTN_A,  AKIRA_BTN_B,   AKIRA_BTN_X,    AKIRA_BTN_Y
};

static uint32_t poll_btns(void) {
    /* Build current bitmask from GPIO */
    uint32_t cur = 0;
    for (int i = 0; i < 8; i++)
        if (gpio_read(VPINS[i]) == 1) cur |= VMASKS[i];

    uint32_t edges = cur & ~g_prev_btns_gpio;
    g_prev_btns_gpio = cur;

    if (edges) {
        g_hold_frames       = g_frame;
        g_last_repeat_frames = g_frame;
        g_last_activity_frames = g_frame;
        rng_stir(cur ^ (uint32_t)g_frame);
        return edges;
    }
    /* Held nav-key repeat */
    uint32_t nav = AKIRA_BTN_UP|AKIRA_BTN_DOWN|AKIRA_BTN_LEFT|AKIRA_BTN_RIGHT;
    if ((cur & nav) &&
        (g_frame - g_hold_frames) >= HOLD_DELAY_FRAMES &&
        (g_frame - g_last_repeat_frames) >= REPEAT_FRAMES) {
        g_last_repeat_frames = g_frame;
        g_last_activity_frames = g_frame;
        return cur & nav;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * CHARSETS
 * ═══════════════════════════════════════════════════════════════════════ */

static const char CS_FULL[]  = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
static const char CS_B32[]   = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static const char CS_ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_";

/* ═══════════════════════════════════════════════════════════════════════
 * UI HELPERS
 * ═══════════════════════════════════════════════════════════════════════ */

/* Runtime screen dimensions — set by display_get_size() in main() */
static int32_t g_sw = CONSOLE_WIDTH;
static int32_t g_sh = CONSOLE_HEIGHT;
static int g_mono = 0; /* 1 = monochrome display (Sharp LS027B7DH01) */

#define W    g_sw
#define H    g_sh
#define HH   CONSOLE_HEADER_H
#define FY   (g_sh - CONSOLE_FOOTER_H)
#define FH   CONSOLE_FOOTER_H
#define CY   CONSOLE_CONTENT_Y
#define CTNH (FY - CY)
#define RH   CONSOLE_ROW_H

/* Color aliases — monochrome-safe: non-black = white on Sharp */
#define CB  0x0000U                /* background   black  */
#define CHD 0x0000U                /* header/footer black (pure black works on both) */
#define CS  0xFFFFU                /* separator    white  */
#define CA  (g_mono ? 0xFFFFU : CONSOLE_COLOR_ACCENT) /* cyan / white */
#define CT  0xFFFFU                /* primary text white  */
#define CD  (g_mono ? 0xC618U : CONSOLE_COLOR_DIM)    /* dim text     */
#define CSL (g_mono ? 0xFFFFU : CONSOLE_COLOR_SEL_BG) /* selection bg */
#define COK (g_mono ? 0xFFFFU : CONSOLE_COLOR_OK)
#define CER (g_mono ? 0xFFFFU : CONSOLE_COLOR_ERR)
#define CWN (g_mono ? 0xFFFFU : CONSOLE_COLOR_WARN)

/* Visible rows in content area */
#define VROWS  (CTNH / RH)

/* Text color for selected row — inverted on monochrome */
#define CT_SEL  (g_mono ? 0x0000U : 0xFFFFU)

static void ui_header(const char *title) {
    display_rect(0, 0, W, HH, CB);
    display_triangle_fill(6, 5, 14, 1, 22, 5, CA);
    display_rect(8, 5, 8, 11, CA);
    display_text(28, 5, title, CT);
    display_hline(0, HH, W, CS);
}

static void ui_footer(const char *left, const char *right) {
    display_rect(0, FY, W, FH, CB);
    display_hline(0, FY, W, CS);
    if (left)  display_text(4,                          FY + 5, left,  CD);
    if (right) display_text(W - 4 - strlen(right)*7,    FY + 5, right, CD);
}

static void ui_row(int vi, const char *label, const char *sub, int sel, uint16_t badge) {
    int y = CY + vi * RH;
    uint16_t row_bg  = sel ? CSL : CB;
    uint16_t row_txt = sel ? CT_SEL : CT;
    uint16_t row_sub = sel ? CT_SEL : CD;
    display_rect(0, y, W, RH, row_bg);
    display_hline(0, y + RH - 1, W, CS);
    if (badge) {
        /* On monochrome, skip colored badge dot to avoid confusion */
        uint16_t bdg = g_mono ? row_txt : badge;
        display_rect(4, y + 9, 7, 7, bdg);
        display_text(15, y + 6,  label, row_txt);
        if (sub) display_text(15, y + 15, sub, row_sub);
    } else {
        display_text(8, y + 6,  label, row_txt);
        if (sub) display_text(8, y + 15, sub, row_sub);
    }
}

/* Draw a progress bar (0..max) */
static void ui_bar(int x, int y, int w, int h, int val, int max, uint16_t fg, uint16_t bg) {
    display_rect(x, y, w, h, bg);
    if (max > 0) {
        int fw = (val * w) / max;
        if (fw > 0) display_rect(x, y, fw, h, fg);
    }
    display_rect_outline(x, y, w, h, fg);
}

/* Mask a string: show first 2 chars then *** */
static void mask_str(const char *src, char *dst, int max) {
    int l = str_nlen(src, max - 1);
    if (l == 0) { dst[0] = '\0'; return; }
    if (l <= 2) { str_ncpy(dst, src, max); return; }
    dst[0] = src[0]; dst[1] = src[1];
    int n = l - 2;
    if (n > max - 3) n = max - 3;
    for (int i = 0; i < n; i++) dst[2 + i] = '*';
    dst[2 + n] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: BOOT
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_boot(void) {
    display_clear(CB);
    display_text_large(W/2 - 66, 80, "AKIRA VAULT", CA);
    display_text(W/2 - 56, 110, "Hardware Security Device", CD);
    display_hline(W/2 - 80, 130, 160, CS);
    display_text(W/2 - 42, 145, "Initialising...", CD);
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: LOCK (PIN ENTRY)
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_lock(void) {
    display_clear(CB);
    ui_header("AKIRA VAULT");
    if (g_pin_step == 0)
        display_text_large(W/2 - 44, 40, "ENTER PIN", CT);
    else if (g_pin_step == 1)
        display_text_large(W/2 - 55, 40, "CONFIRM PIN", CT);

    /* PIN dots */
    for (int i = 0; i < PIN_LEN; i++) {
        int x = W/2 - (PIN_LEN*18)/2 + i*18 + 4;
        if (i < g_pin_len) display_circle_fill(x, 90, 6, CA);
        else               display_circle(x, 90, 6, CD);
    }

    /* Digit selector */
    display_rect(W/2 - 20, 108, 40, 24, CHD);
    display_rect_outline(W/2 - 20, 108, 40, 24, CA);
    char dc[2] = {'0' + g_pin_digit, 0};
    display_text_large(W/2 - 5, 110, dc, CT);
    display_text(W/2 - 56, 108, "<", CD);
    display_text(W/2 + 46, 108, ">", CD);

    /* Error message */
    if (g_pin_err == 1) display_text(W/2 - 42, 145, "Wrong PIN!", CER);
    if (g_pin_err == 2) display_text(W/2 - 49, 145, "PINs don't match", CER);

    ui_footer("[A] add digit  [B] del  [Y] confirm", 0);
    display_flush();
}

static void handle_lock(uint32_t btns) {
    if (!btns) return;
    g_pin_err = 0;

    if (btns & AKIRA_BTN_RIGHT) { g_pin_digit = (g_pin_digit + 1) % 10; g_dirty = 1; }
    if (btns & AKIRA_BTN_LEFT)  { g_pin_digit = (g_pin_digit + 9) % 10; g_dirty = 1; }
    if (btns & AKIRA_BTN_UP)    { g_pin_digit = (g_pin_digit + 5) % 10; g_dirty = 1; }
    if (btns & AKIRA_BTN_DOWN)  { g_pin_digit = (g_pin_digit + 5) % 10; g_dirty = 1; }

    if (btns & AKIRA_BTN_A) {
        if (g_pin_len < PIN_LEN) {
            if (g_pin_step == 0) g_pin[g_pin_len]  = '0' + g_pin_digit;
            else                 g_pin2[g_pin_len] = '0' + g_pin_digit;
            g_pin_len++;
            /* Auto-confirm when all digits entered */
            if (g_pin_len == PIN_LEN) {
                if (g_screen == SCR_LOCK) {
                    g_pin[PIN_LEN] = '\0';
                    if (vault_load(g_pin) == 0) {
                        g_screen = SCR_MENU; g_cursor = 0;
                    } else {
                        g_pin_err = 1;
                        g_pin_len = 0;
                    }
                } else if (g_screen == SCR_NEW_VAULT) {
                    if (g_pin_step == 0) {
                        g_pin[PIN_LEN] = '\0';
                        g_pin_len = 0;
                        g_pin_step = 1;
                    } else {
                        g_pin2[PIN_LEN] = '\0';
                        if (mem_cmp(g_pin, g_pin2, PIN_LEN) == 0) {
                            vault_create(g_pin);
                            vault_save();
                            g_screen = SCR_MENU; g_cursor = 0;
                        } else {
                            g_pin_err = 2;
                            g_pin_len = 0; g_pin_step = 0;
                        }
                    }
                } else if (g_screen == SCR_PIN_CHANGE) {
                    if (g_pin_step == 0) {
                        g_pin[PIN_LEN] = '\0';
                        g_pin_len = 0; g_pin_step = 1;
                    } else {
                        g_pin2[PIN_LEN] = '\0';
                        if (mem_cmp(g_pin, g_pin2, PIN_LEN) == 0) {
                            /* Re-derive key with new PIN */
                            pbkdf2((uint8_t *)g_pin, PIN_LEN,
                                   g_fhdr.salt, 16, PBKDF2_ITERS, g_vkey);
                            g_vault_dirty = 1;
                            vault_save();
                            g_screen = SCR_SETTINGS; g_cursor = 0;
                        } else {
                            g_pin_err = 2;
                            g_pin_len = 0; g_pin_step = 0;
                        }
                    }
                }
            }
        }
        g_dirty = 1;
    }
    if (btns & AKIRA_BTN_B) {
        if (g_pin_len > 0) { g_pin_len--; g_dirty = 1; }
        else if (g_pin_step == 1) { g_pin_step = 0; g_dirty = 1; }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: MAIN MENU
 * ═══════════════════════════════════════════════════════════════════════ */

static const char *MENU_ITEMS[] = {
    "Passwords", "Authenticator", "Wallet", "Settings", "Lock"
};
#define MENU_COUNT 5

static void draw_menu(void) {
    display_clear(CB);
    ui_header("AKIRA VAULT");
    /* Show entry counts next to menu items */
    char sub0[32], sub1[32], sub2[32];
    int_to_str(g_vault.pw_count,   sub0); sub0[str_nlen(sub0,30)] = ' ';
    /* Build sub labels with counts */
    char tmp[8];
    int_to_str((int)g_vault.pw_count, tmp);
    str_ncpy(sub0, tmp, 4); str_ncpy(sub0 + str_nlen(sub0,30), " stored", 8);
    int_to_str((int)g_vault.totp_count, tmp);
    str_ncpy(sub1, tmp, 4); str_ncpy(sub1 + str_nlen(sub1,30), " accounts", 10);
    int_to_str((int)g_vault.wallet_count, tmp);
    str_ncpy(sub2, tmp, 4); str_ncpy(sub2 + str_nlen(sub2,30), " seeds", 7);

    const char *subs[MENU_COUNT] = { sub0, sub1, sub2, "PIN, time, HID", "Lock vault now" };

    for (int i = 0; i < MENU_COUNT; i++) {
        if (i < VROWS) ui_row(i, MENU_ITEMS[i], subs[i], i == g_cursor, 0);
    }
    ui_footer("[A] open  [UP/DN] nav", NULL);
    display_flush();
}

static void handle_menu(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_UP)   { g_cursor = (g_cursor - 1 + MENU_COUNT) % MENU_COUNT; g_dirty = 1; }
    if (btns & AKIRA_BTN_DOWN) { g_cursor = (g_cursor + 1) % MENU_COUNT;               g_dirty = 1; }
    if (btns & AKIRA_BTN_A) {
        switch (g_cursor) {
        case 0: g_screen = SCR_PW_LIST;    g_cursor = 0; g_scroll = 0; break;
        case 1: g_screen = SCR_TOTP_LIST;  g_cursor = 0; g_scroll = 0; break;
        case 2: g_screen = SCR_WALLET_LIST;g_cursor = 0; g_scroll = 0; break;
        case 3: g_screen = SCR_SETTINGS;   g_cursor = 0;               break;
        case 4:
            vault_lock();
            g_screen = SCR_LOCK; g_pin_len = 0; g_pin_step = 0; g_pin_err = 0;
            break;
        }
        g_dirty = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: PASSWORD LIST
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_pw_list(void) {
    display_clear(CB);
    ui_header("Passwords");
    int n = (int)g_vault.pw_count;
    if (n == 0) {
        display_text(W/2 - 42, H/2 - 10, "No passwords", CD);
        display_text(W/2 - 56, H/2 + 8,  "[Y] Add first entry", CD);
    } else {
        for (int i = 0; i < VROWS && (i + g_scroll) < n; i++) {
            int idx = i + g_scroll;
            char masked[24];
            mask_str(g_vault.pw[idx].password, masked, sizeof(masked));
            ui_row(i, g_vault.pw[idx].name, g_vault.pw[idx].username,
                   (g_cursor - g_scroll) == i, COK);
        }
    }
    ui_footer("[A] view  [Y] add  [X] del  [B] back", NULL);
    display_flush();
}

static void handle_pw_list(uint32_t btns) {
    int n = (int)g_vault.pw_count;
    if (!btns) return;
    if (btns & AKIRA_BTN_UP) {
        if (g_cursor > 0) { g_cursor--;
            if (g_cursor < g_scroll) g_scroll = g_cursor; }
        g_dirty = 1;
    }
    if (btns & AKIRA_BTN_DOWN) {
        if (g_cursor < n - 1) { g_cursor++;
            if (g_cursor >= g_scroll + VROWS) g_scroll = g_cursor - VROWS + 1; }
        g_dirty = 1;
    }
    if (btns & AKIRA_BTN_A) {
        if (n > 0) { g_selected = g_cursor; g_screen = SCR_PW_VIEW; g_dirty = 1; }
    }
    if (btns & AKIRA_BTN_Y) {
        if (n < MAX_PW) {
            mem_set(&g_pwa, 0, sizeof(g_pwa));
            g_screen = SCR_PW_ADD; g_dirty = 1;
        }
    }
    if (btns & AKIRA_BTN_X) {
        if (n > 0) {
            g_selected = g_cursor;
            g_conf_msg = "Delete this password?";
            g_conf_action = CONF_DEL_PW;
            g_conf_cursor = 0;
            g_screen = SCR_CONFIRM; g_dirty = 1;
        }
    }
    if (btns & AKIRA_BTN_B) { g_screen = SCR_MENU; g_cursor = 0; g_dirty = 1; }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: PASSWORD VIEW
 * ═══════════════════════════════════════════════════════════════════════ */

static int g_pw_show = 0; /* toggle password visibility */

static void draw_pw_view(void) {
    display_clear(CB);
    ui_header("Password Entry");
    pw_entry_t *e = &g_vault.pw[g_selected];

    int y = CY + 4;
    display_text(4, y, "Name:", CA);    display_text(50, y, e->name,     CT); y += 20;
    display_text(4, y, "User:", CA);    display_text(50, y, e->username, CT); y += 20;

    char pass_disp[65];
    if (g_pw_show) str_ncpy(pass_disp, e->password, sizeof(pass_disp));
    else           mask_str(e->password, pass_disp, sizeof(pass_disp));
    display_text(4, y, "Pass:", CA);    display_text(50, y, pass_disp,   CT); y += 20;
    display_text(4, y, "URL: ", CA);    display_text(50, y, e->url,      CD); y += 20;

    display_hline(4, y, W - 8, CS); y += 10;

    /* HID status */
    if (g_hid_on) {
        display_text(4, y, "[A] Type password via HID", COK);
        display_text(4, y+14, "[Y] Type username via HID", COK);
    } else {
        display_text(4, y, "HID not active", CWN);
        display_text(4, y+14, "Enable in Settings", CD);
    }

    ui_footer("[A] type pass  [X] show/hide  [B] back", NULL);
    display_flush();
}

static void handle_pw_view(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_B) { g_screen = SCR_PW_LIST; g_dirty = 1; }
    if (btns & AKIRA_BTN_X) { g_pw_show = !g_pw_show; g_dirty = 1; }
    if (btns & AKIRA_BTN_A) {
        if (g_hid_on) hid_type_string(g_vault.pw[g_selected].password);
    }
    if (btns & AKIRA_BTN_Y) {
        if (g_hid_on) hid_type_string(g_vault.pw[g_selected].username);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: PASSWORD ADD
 * ═══════════════════════════════════════════════════════════════════════ */

static const char *PW_FIELDS[] = { "Name", "Username", "Password", "URL", "> Save" };
#define PW_FIELD_COUNT 5

static void draw_pw_add(void) {
    display_clear(CB);
    ui_header("Add Password");
    const char *vals[5] = {
        g_pwa.name, g_pwa.username, g_pwa.password, g_pwa.url, NULL
    };
    for (int i = 0; i < PW_FIELD_COUNT; i++) {
        int y = CY + i * RH;
        display_rect(0, y, W, RH, (g_pwa.field == i) ? CSL : CB);
        display_hline(0, y + RH - 1, W, 0x2104);
        display_text(4, y + 6, PW_FIELDS[i], (g_pwa.field == i) ? CA : CD);
        if (vals[i]) {
            char masked[65];
            if (i == 2 && !g_pw_show) mask_str(vals[i], masked, sizeof(masked));
            else str_ncpy(masked, vals[i], sizeof(masked));
            display_text(72, y + 6, masked, CT);
        } else if (i == 4 && g_pwa.field == 4) {
            display_text(72, y + 6, "(press A)", CA);
        }
    }
    ui_footer("[A] edit/save  [B] cancel  [X] show", NULL);
    display_flush();
}

static void handle_pw_add(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_UP)   { g_pwa.field = (g_pwa.field - 1 + PW_FIELD_COUNT) % PW_FIELD_COUNT; g_dirty = 1; }
    if (btns & AKIRA_BTN_DOWN) { g_pwa.field = (g_pwa.field + 1) % PW_FIELD_COUNT;                  g_dirty = 1; }
    if (btns & AKIRA_BTN_X)    { g_pw_show = !g_pw_show; g_dirty = 1; }
    if (btns & AKIRA_BTN_B)    { g_screen = SCR_PW_LIST; g_dirty = 1; }
    if (btns & AKIRA_BTN_A) {
        switch (g_pwa.field) {
        case 0: /* Name */
            str_ncpy(g_pck_title, "Name", sizeof(g_pck_title));
            g_pck_dst = g_pwa.name; g_pck_dst_max = sizeof(g_pwa.name);
            g_pck_charset = CS_ALPHA; g_pck_cslen = strlen(CS_ALPHA);
            g_pck_ret_scr = SCR_PW_ADD;
            str_ncpy(g_pck_buf, g_pwa.name, sizeof(g_pck_buf));
            g_pck_len = str_nlen(g_pck_buf, sizeof(g_pck_buf)-1);
            g_pck_cidx = 0; g_screen = SCR_CHAR_PICKER; break;
        case 1: /* Username */
            str_ncpy(g_pck_title, "Username", sizeof(g_pck_title));
            g_pck_dst = g_pwa.username; g_pck_dst_max = sizeof(g_pwa.username);
            g_pck_charset = CS_FULL; g_pck_cslen = strlen(CS_FULL);
            g_pck_ret_scr = SCR_PW_ADD;
            str_ncpy(g_pck_buf, g_pwa.username, sizeof(g_pck_buf));
            g_pck_len = str_nlen(g_pck_buf, sizeof(g_pck_buf)-1);
            g_pck_cidx = 0; g_screen = SCR_CHAR_PICKER; break;
        case 2: /* Password */
            str_ncpy(g_pck_title, "Password", sizeof(g_pck_title));
            g_pck_dst = g_pwa.password; g_pck_dst_max = sizeof(g_pwa.password);
            g_pck_charset = CS_FULL; g_pck_cslen = strlen(CS_FULL);
            g_pck_ret_scr = SCR_PW_ADD;
            str_ncpy(g_pck_buf, g_pwa.password, sizeof(g_pck_buf));
            g_pck_len = str_nlen(g_pck_buf, sizeof(g_pck_buf)-1);
            g_pck_cidx = 0; g_screen = SCR_CHAR_PICKER; break;
        case 3: /* URL */
            str_ncpy(g_pck_title, "URL", sizeof(g_pck_title));
            g_pck_dst = g_pwa.url; g_pck_dst_max = sizeof(g_pwa.url);
            g_pck_charset = CS_FULL; g_pck_cslen = strlen(CS_FULL);
            g_pck_ret_scr = SCR_PW_ADD;
            str_ncpy(g_pck_buf, g_pwa.url, sizeof(g_pck_buf));
            g_pck_len = str_nlen(g_pck_buf, sizeof(g_pck_buf)-1);
            g_pck_cidx = 0; g_screen = SCR_CHAR_PICKER; break;
        case 4: /* Save */
            if (str_nlen(g_pwa.name, 1) > 0 && g_vault.pw_count < MAX_PW) {
                pw_entry_t *e = &g_vault.pw[g_vault.pw_count];
                str_ncpy(e->name,     g_pwa.name,     sizeof(e->name));
                str_ncpy(e->username, g_pwa.username, sizeof(e->username));
                str_ncpy(e->password, g_pwa.password, sizeof(e->password));
                str_ncpy(e->url,      g_pwa.url,      sizeof(e->url));
                g_vault.pw_count++;
                g_vault_dirty = 1;
                vault_save();
                g_screen = SCR_PW_LIST; g_cursor = (int)g_vault.pw_count - 1;
            }
            break;
        }
        g_dirty = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: TOTP LIST
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_totp_list(void) {
    display_clear(CB);
    ui_header("Authenticator");
    int n = (int)g_vault.totp_count;

    if (!g_time_set) {
        display_text(4, CY + 8,  "Time not set!", CER);
        display_text(4, CY + 26, "Go to Settings > Set Time", CD);
        display_text(4, CY + 44, "to enable TOTP codes.", CD);
    }

    if (n == 0) {
        int yo = g_time_set ? 0 : 70;
        display_text(W/2 - 42, CY + 60 + yo, "No accounts", CD);
        display_text(W/2 - 56, CY + 78 + yo, "[Y] Add first account", CD);
    } else {
        uint64_t now = get_unix();
        for (int i = 0; i < VROWS && (i + g_scroll) < n; i++) {
            int idx = i + g_scroll;
            totp_entry_t *e = &g_vault.totp[idx];
            /* Decode secret and compute code */
            uint8_t sec[30]; int slen = b32_decode(e->secret_b32, sec, sizeof(sec));
            char code_str[10];
            if (g_time_set && slen > 0) {
                uint32_t code = totp_code(sec, slen, now, e->period, e->digits);
                uint32_zero_pad(code, e->digits, code_str);
            } else {
                str_ncpy(code_str, "------", sizeof(code_str));
            }
            int sel = (g_cursor - g_scroll) == i;
            int y = CY + i * RH;
            display_rect(0, y, W, RH, sel ? CSL : CB);
            display_hline(0, y + RH - 1, W, 0x2104);
            display_text(4,   y + 6,  e->name,    CT);
            display_text(180, y + 6,  code_str,   g_time_set ? CA : CD);
            /* Progress dot */
            if (g_time_set) {
                int pct = (int)(now % e->period);
                int dotx = W/2 - 40 + (pct * 30) / e->period;
                display_rect(dotx, y + 14, 3, 3, (pct > e->period*3/4) ? CER : COK);
            }
        }
    }
    ui_footer("[A] view  [Y] add  [X] del  [B] back", NULL);
    display_flush();
}

static void handle_totp_list(uint32_t btns) {
    int n = (int)g_vault.totp_count;
    if (!btns) return;
    if (btns & AKIRA_BTN_UP) {
        if (g_cursor > 0) { g_cursor--;
            if (g_cursor < g_scroll) g_scroll = g_cursor; } g_dirty = 1;
    }
    if (btns & AKIRA_BTN_DOWN) {
        if (g_cursor < n - 1) { g_cursor++;
            if (g_cursor >= g_scroll + VROWS) g_scroll = g_cursor - VROWS + 1; } g_dirty = 1;
    }
    if (btns & AKIRA_BTN_A) {
        if (n > 0) { g_selected = g_cursor; g_screen = SCR_TOTP_VIEW; g_dirty = 1; }
    }
    if (btns & AKIRA_BTN_Y) {
        if (n < MAX_TOTP) {
            mem_set(&g_tota, 0, sizeof(g_tota));
            g_tota.digits = 6; g_tota.period = 30;
            g_screen = SCR_TOTP_ADD; g_dirty = 1;
        }
    }
    if (btns & AKIRA_BTN_X) {
        if (n > 0) {
            g_selected = g_cursor;
            g_conf_msg = "Delete this account?";
            g_conf_action = CONF_DEL_TOTP;
            g_conf_cursor = 0;
            g_screen = SCR_CONFIRM; g_dirty = 1;
        }
    }
    if (btns & AKIRA_BTN_B) { g_screen = SCR_MENU; g_cursor = 0; g_dirty = 1; }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: TOTP VIEW
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_totp_view(void) {
    display_clear(CB);
    ui_header("Authenticator");
    totp_entry_t *e = &g_vault.totp[g_selected];

    display_text(W/2 - strlen(e->name)*3, CY + 8, e->name, CD);

    if (!g_time_set) {
        display_text(4, CY + 30, "Time not set — set in Settings", CER);
    } else {
        uint64_t now = get_unix();
        uint8_t sec[30]; int slen = b32_decode(e->secret_b32, sec, sizeof(sec));
        uint32_t code = (slen > 0) ? totp_code(sec, slen, now, e->period, e->digits) : 0;

        /* Large code display */
        char code_str[10];
        uint32_zero_pad(code, e->digits, code_str);
        /* Add space in middle for 6-digit */
        if (e->digits == 6) {
            char spaced[10];
            spaced[0]=code_str[0]; spaced[1]=code_str[1]; spaced[2]=code_str[2];
            spaced[3]=' ';
            spaced[4]=code_str[3]; spaced[5]=code_str[4]; spaced[6]=code_str[5];
            spaced[7]='\0';
            display_text_large(W/2 - 50, CY + 36, spaced, CA);
        } else {
            display_text_large(W/2 - 44, CY + 36, code_str, CA);
        }

        /* Countdown bar */
        int rem = (int)(e->period - (now % e->period));
        char rem_str[8]; int_to_str(rem, rem_str);
        display_text(4, CY + 68, rem_str, CD);
        display_text(22, CY + 68, "sec", CD);
        ui_bar(50, CY + 68, W - 80, 12, rem, e->period,
               rem < 10 ? CER : CA, CHD);

        /* Digits/period info */
        display_text(4, CY + 90, "Digits:", CD);
        char ds[4]; int_to_str(e->digits, ds);
        display_text(60, CY + 90, ds, CT);
        display_text(90, CY + 90, "  Period:", CD);
        char ps[8]; int_to_str(e->period, ps);
        display_text(160, CY + 90, ps, CT);
        display_text(185, CY + 90, "s", CT);

        /* HID type button */
        if (g_hid_on) display_text(4, CY + 112, "[A] Type code via HID", COK);
        else          display_text(4, CY + 112, "HID off (enable in Settings)", CD);
    }

    ui_footer("[A] type  [B] back", NULL);
    display_flush();
}

static void handle_totp_view(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_B) { g_screen = SCR_TOTP_LIST; g_dirty = 1; }
    if (btns & AKIRA_BTN_A) {
        if (g_hid_on && g_time_set) {
            totp_entry_t *e = &g_vault.totp[g_selected];
            uint8_t sec[30]; int slen = b32_decode(e->secret_b32, sec, sizeof(sec));
            if (slen > 0) {
                uint32_t code = totp_code(sec, slen, get_unix(), e->period, e->digits);
                char cs[10]; uint32_zero_pad(code, e->digits, cs);
                hid_type_string(cs);
            }
        }
    }
    /* Refresh every second for live countdown */
    g_dirty = 1;
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: TOTP ADD
 * ═══════════════════════════════════════════════════════════════════════ */

static const char *TOTP_FIELDS[] = { "Name", "Secret (B32)", "Digits", "Period", "> Save" };
#define TOTP_FIELD_COUNT 5

static void draw_totp_add(void) {
    display_clear(CB);
    ui_header("Add Authenticator");
    char dstr[4], pstr[8];
    int_to_str(g_tota.digits, dstr);
    int_to_str(g_tota.period, pstr);
    const char *vals[5] = { g_tota.name, g_tota.secret, dstr, pstr, NULL };
    for (int i = 0; i < TOTP_FIELD_COUNT; i++) {
        int y = CY + i * RH;
        display_rect(0, y, W, RH, (g_tota.field == i) ? CSL : CB);
        display_hline(0, y + RH - 1, W, 0x2104);
        display_text(4, y + 6, TOTP_FIELDS[i], (g_tota.field == i) ? CA : CD);
        if (vals[i]) {
            char trunc[32]; str_ncpy(trunc, vals[i], sizeof(trunc));
            display_text(100, y + 6, trunc, CT);
        }
        if (i == 2 || i == 3) display_text(280, y + 6, "<>", CD);
    }
    display_text(4, CY + TOTP_FIELD_COUNT * RH + 4,
                 "Secret: base32 string (A-Z, 2-7)", CD);
    ui_footer("[A] edit  [LR] toggle  [B] cancel", NULL);
    display_flush();
}

static void handle_totp_add(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_UP)   { g_tota.field = (g_tota.field - 1 + TOTP_FIELD_COUNT) % TOTP_FIELD_COUNT; g_dirty = 1; }
    if (btns & AKIRA_BTN_DOWN) { g_tota.field = (g_tota.field + 1) % TOTP_FIELD_COUNT;                    g_dirty = 1; }
    if (btns & AKIRA_BTN_B)    { g_screen = SCR_TOTP_LIST; g_dirty = 1; }
    /* Toggle digits/period with left/right */
    if ((btns & (AKIRA_BTN_LEFT|AKIRA_BTN_RIGHT)) && g_tota.field == 2) {
        g_tota.digits = (g_tota.digits == 6) ? 8 : 6; g_dirty = 1;
    }
    if ((btns & (AKIRA_BTN_LEFT|AKIRA_BTN_RIGHT)) && g_tota.field == 3) {
        g_tota.period = (g_tota.period == 30) ? 60 : 30; g_dirty = 1;
    }
    if (btns & AKIRA_BTN_A) {
        switch (g_tota.field) {
        case 0:
            str_ncpy(g_pck_title, "Account Name", sizeof(g_pck_title));
            g_pck_dst = g_tota.name; g_pck_dst_max = sizeof(g_tota.name);
            g_pck_charset = CS_ALPHA; g_pck_cslen = strlen(CS_ALPHA);
            g_pck_ret_scr = SCR_TOTP_ADD;
            str_ncpy(g_pck_buf, g_tota.name, sizeof(g_pck_buf));
            g_pck_len = str_nlen(g_pck_buf, sizeof(g_pck_buf)-1);
            g_pck_cidx = 0; g_screen = SCR_CHAR_PICKER; break;
        case 1:
            str_ncpy(g_pck_title, "TOTP Secret", sizeof(g_pck_title));
            g_pck_dst = g_tota.secret; g_pck_dst_max = sizeof(g_tota.secret);
            g_pck_charset = CS_B32; g_pck_cslen = strlen(CS_B32);
            g_pck_ret_scr = SCR_TOTP_ADD;
            str_ncpy(g_pck_buf, g_tota.secret, sizeof(g_pck_buf));
            g_pck_len = str_nlen(g_pck_buf, sizeof(g_pck_buf)-1);
            g_pck_cidx = 0; g_screen = SCR_CHAR_PICKER; break;
        case 4: /* Save */
            if (str_nlen(g_tota.name, 1) > 0 &&
                str_nlen(g_tota.secret, 4) >= 4 &&
                g_vault.totp_count < MAX_TOTP) {
                totp_entry_t *e = &g_vault.totp[g_vault.totp_count];
                str_ncpy(e->name,       g_tota.name,   sizeof(e->name));
                str_ncpy(e->secret_b32, g_tota.secret, sizeof(e->secret_b32));
                e->digits = (uint8_t)g_tota.digits;
                e->period = (uint8_t)g_tota.period;
                g_vault.totp_count++;
                g_vault_dirty = 1; vault_save();
                g_screen = SCR_TOTP_LIST; g_cursor = (int)g_vault.totp_count - 1;
            }
            break;
        }
        g_dirty = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: WALLET LIST
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_wallet_list(void) {
    display_clear(CB);
    ui_header("Crypto Wallet");
    int n = (int)g_vault.wallet_count;
    if (n == 0) {
        display_text(W/2 - 42, CY + 60, "No wallet seeds", CD);
        display_text(W/2 - 63, CY + 78, "[Y] Generate new seed", CD);
    } else {
        for (int i = 0; i < VROWS && (i + g_scroll) < n; i++) {
            int idx = i + g_scroll;
            /* Show first 8 bytes of entropy as hex */
            char hex12[13]; to_hex(g_vault.wallet[idx].entropy, 6, hex12);
            char sub[20]; str_ncpy(sub, "0x", 3);
            str_ncpy(sub + 2, hex12, sizeof(sub) - 2);
            ui_row(i, g_vault.wallet[idx].label, sub,
                   (g_cursor - g_scroll) == i, 0xF800); /* red badge = critical */
        }
    }
    ui_footer("[A] view  [Y] new seed  [X] del  [B] back", NULL);
    display_flush();
}

static void handle_wallet_list(uint32_t btns) {
    int n = (int)g_vault.wallet_count;
    if (!btns) return;
    if (btns & AKIRA_BTN_UP) {
        if (g_cursor > 0) { g_cursor--;
            if (g_cursor < g_scroll) g_scroll = g_cursor; } g_dirty = 1;
    }
    if (btns & AKIRA_BTN_DOWN) {
        if (g_cursor < n - 1) { g_cursor++;
            if (g_cursor >= g_scroll + VROWS) g_scroll = g_cursor - VROWS + 1; } g_dirty = 1;
    }
    if (btns & AKIRA_BTN_A) {
        if (n > 0) {
            g_selected = g_cursor;
            to_hex(g_vault.wallet[g_selected].entropy, 32, g_wlt_hex);
            g_screen = SCR_WALLET_VIEW; g_dirty = 1;
        }
    }
    if (btns & AKIRA_BTN_Y) {
        if (n < MAX_WALLET) {
            mem_set(g_wlt_label, 0, sizeof(g_wlt_label));
            g_screen = SCR_WALLET_LIST + 10; /* jump to generation flow */
            /* Use the char picker to get a label */
            str_ncpy(g_pck_title, "Wallet Label", sizeof(g_pck_title));
            g_pck_dst = g_wlt_label; g_pck_dst_max = sizeof(g_wlt_label);
            g_pck_charset = CS_ALPHA; g_pck_cslen = strlen(CS_ALPHA);
            g_pck_ret_scr = SCR_WALLET_LIST + 10; /* special return */
            str_ncpy(g_pck_buf, "", sizeof(g_pck_buf));
            g_pck_len = 0; g_pck_cidx = 0;
            g_screen = SCR_CHAR_PICKER; g_dirty = 1;
        }
    }
    if (btns & AKIRA_BTN_X) {
        if (n > 0) {
            g_selected = g_cursor;
            g_conf_msg = "Delete wallet seed? IRREVERSIBLE!";
            g_conf_action = CONF_DEL_WALLET;
            g_conf_cursor = 0;
            g_screen = SCR_CONFIRM; g_dirty = 1;
        }
    }
    if (btns & AKIRA_BTN_B) { g_screen = SCR_MENU; g_cursor = 0; g_dirty = 1; }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: WALLET VIEW (shows full 256-bit entropy)
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_wallet_view(void) {
    display_clear(CB);
    ui_header("Wallet Seed");
    wallet_entry_t *e = &g_vault.wallet[g_selected];

    display_text(4, CY + 4, e->label, CA);
    display_text(4, CY + 20, "256-bit entropy seed (hex):", CD);
    display_rect_outline(2, CY + 34, W - 4, 68, CS);

    /* Display 32 bytes as 4 lines of 8 bytes each */
    for (int row = 0; row < 4; row++) {
        char line[20];
        to_hex(e->entropy + row * 8, 8, line);
        display_text(8, CY + 38 + row * 16, line, CT);
    }

    display_text(4, CY + 108, "! Write this down and store safely", CWN);
    display_text(4, CY + 122, "  Use as BIP39 seed input offline", CD);

    ui_footer("[B] back", NULL);
    display_flush();
}

static void handle_wallet_view(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_B) { g_screen = SCR_WALLET_LIST; g_dirty = 1; }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: SETTINGS
 * ═══════════════════════════════════════════════════════════════════════ */

static const char *SETTINGS_ITEMS[] = {
    "Change PIN", "Set Time (TOTP)", "HID: BLE Keyboard",
    "HID: USB Keyboard", "Wipe Vault"
};
#define SETTINGS_COUNT 5

static void draw_settings(void) {
    display_clear(CB);
    ui_header("Settings");
    const char *subs[SETTINGS_COUNT] = {
        "4-digit PIN",
        g_time_set ? "Time is set" : "NOT SET - TOTP disabled",
        g_hid_transport == HID_TRANSPORT_BLE ? "Active" : "Inactive",
        g_hid_transport == HID_TRANSPORT_USB ? "Active" : "Inactive",
        "Erase all data"
    };
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        uint16_t badge = 0;
        if (i == 2) badge = (g_hid_transport == HID_TRANSPORT_BLE && g_hid_on) ? COK : CD;
        if (i == 3) badge = (g_hid_transport == HID_TRANSPORT_USB && g_hid_on) ? COK : CD;
        if (i == 1) badge = g_time_set ? COK : CER;
        if (i < VROWS) ui_row(i, SETTINGS_ITEMS[i], subs[i], i == g_cursor, badge);
    }
    ui_footer("[A] select  [UP/DN] nav  [B] back", NULL);
    display_flush();
}

static void handle_settings(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_UP)   { g_cursor = (g_cursor - 1 + SETTINGS_COUNT) % SETTINGS_COUNT; g_dirty = 1; }
    if (btns & AKIRA_BTN_DOWN) { g_cursor = (g_cursor + 1) % SETTINGS_COUNT;                  g_dirty = 1; }
    if (btns & AKIRA_BTN_B)    { g_screen = SCR_MENU; g_cursor = 0; g_dirty = 1; }
    if (btns & AKIRA_BTN_A) {
        switch (g_cursor) {
        case 0: /* Change PIN */
            g_screen = SCR_PIN_CHANGE;
            g_pin_len = 0; g_pin_step = 0; g_pin_err = 0; g_pin_digit = 0;
            break;
        case 1: /* Set Time */
            g_screen = SCR_TIME_SETUP;
            g_tset_field = 0;
            g_tset_vals[0] = 2026; g_tset_vals[1] = 1;
            g_tset_vals[2] = 1;    g_tset_vals[3] = 0; g_tset_vals[4] = 0;
            break;
        case 2: /* BLE HID */
            if (g_hid_on) { hid_disable(); g_hid_on = 0; }
            g_hid_transport = HID_TRANSPORT_BLE;
            hid_init(HID_TRANSPORT_BLE, HID_DEVICE_KEYBOARD);
            g_hid_on = 1;
            settings_set("vault/hid", "ble");
            break;
        case 3: /* USB HID */
            if (g_hid_on) { hid_disable(); g_hid_on = 0; }
            g_hid_transport = HID_TRANSPORT_USB;
            hid_init(HID_TRANSPORT_USB, HID_DEVICE_KEYBOARD);
            g_hid_on = 1;
            settings_set("vault/hid", "usb");
            break;
        case 4: /* Wipe */
            g_conf_msg = "WIPE ALL DATA? Cannot undo!";
            g_conf_action = CONF_WIPE;
            g_conf_cursor = 0;
            g_screen = SCR_CONFIRM;
            break;
        }
        g_dirty = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: TIME SETUP
 * ═══════════════════════════════════════════════════════════════════════ */

static const char *TSET_LABELS[] = { "Year", "Month", "Day", "Hour", "Minute" };
static const int   TSET_MIN[]    = { 2024, 1, 1,  0,  0 };
static const int   TSET_MAX[]    = { 2099,12,31, 23, 59 };

static void draw_time_setup(void) {
    display_clear(CB);
    ui_header("Set Current Time");
    display_text(4, CY + 4, "Set date/time for TOTP codes:", CD);

    for (int i = 0; i < 5; i++) {
        int y = CY + 22 + i * 26;
        int sel = (i == g_tset_field);
        display_rect(0, y, W, 24, sel ? CSL : CB);
        display_text(4, y + 5, TSET_LABELS[i], sel ? CA : CD);
        char vbuf[8]; int_to_str(g_tset_vals[i], vbuf);
        display_text(80, y + 5, vbuf, CT);
        display_text(W - 20, y + 5, "<>", CD);
    }

    /* Preview datetime */
    char preview[32];
    preview[0]='\0';
    char tmp[8];
    int_to_str(g_tset_vals[0], tmp); str_ncpy(preview, tmp, sizeof(preview));
    str_ncpy(preview + strlen(preview), "-", 2);
    int_to_str(g_tset_vals[1], tmp);
    if (g_tset_vals[1] < 10) { preview[strlen(preview)] = '0'; preview[strlen(preview)+1] = '\0'; }
    str_ncpy(preview + strlen(preview), tmp, sizeof(preview) - strlen(preview));
    str_ncpy(preview + strlen(preview), "-", 2);
    int_to_str(g_tset_vals[2], tmp);
    if (g_tset_vals[2] < 10) { preview[strlen(preview)] = '0'; preview[strlen(preview)+1] = '\0'; }
    str_ncpy(preview + strlen(preview), tmp, sizeof(preview) - strlen(preview));
    str_ncpy(preview + strlen(preview), " ", 2);
    int_to_str(g_tset_vals[3], tmp);
    if (g_tset_vals[3] < 10) { preview[strlen(preview)] = '0'; preview[strlen(preview)+1] = '\0'; }
    str_ncpy(preview + strlen(preview), tmp, sizeof(preview) - strlen(preview));
    str_ncpy(preview + strlen(preview), ":", 2);
    int_to_str(g_tset_vals[4], tmp);
    if (g_tset_vals[4] < 10) { preview[strlen(preview)] = '0'; preview[strlen(preview)+1] = '\0'; }
    str_ncpy(preview + strlen(preview), tmp, sizeof(preview) - strlen(preview));
    display_text(4, CY + 157, preview, CA);

    ui_footer("[A/Y] confirm  [LR] change  [UD] field  [B] back", NULL);
    display_flush();
}

static void handle_time_setup(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_UP)   { g_tset_field = (g_tset_field - 1 + 5) % 5; g_dirty = 1; }
    if (btns & AKIRA_BTN_DOWN) { g_tset_field = (g_tset_field + 1) % 5;     g_dirty = 1; }
    if (btns & AKIRA_BTN_RIGHT) {
        int *v = &g_tset_vals[g_tset_field];
        *v = (*v < TSET_MAX[g_tset_field]) ? *v + 1 : TSET_MIN[g_tset_field];
        g_dirty = 1;
    }
    if (btns & AKIRA_BTN_LEFT) {
        int *v = &g_tset_vals[g_tset_field];
        *v = (*v > TSET_MIN[g_tset_field]) ? *v - 1 : TSET_MAX[g_tset_field];
        g_dirty = 1;
    }
    if ((btns & AKIRA_BTN_A) || (btns & AKIRA_BTN_Y)) {
        g_unix_base = ymd_hm_to_unix(g_tset_vals[0], g_tset_vals[1], g_tset_vals[2],
                                      g_tset_vals[3], g_tset_vals[4]);
        g_ms_base   = (uint64_t)(uint32_t)g_frame; /* frame count at time-set */
        g_time_set  = 1;
        save_time_to_settings();
        g_screen = SCR_SETTINGS; g_cursor = 1; g_dirty = 1;
    }
    if (btns & AKIRA_BTN_B) { g_screen = SCR_SETTINGS; g_dirty = 1; }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: CHAR PICKER
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_char_picker(void) {
    display_clear(CB);
    ui_header(g_pck_title);

    /* Current text (last ~42 chars) */
    display_rect(0, CY, W, 26, 0x1082);
    display_text(4, CY + 4, ">", CA);
    int start = g_pck_len > 42 ? g_pck_len - 42 : 0;
    display_text(16, CY + 4, g_pck_buf + start, CT);
    /* Cursor */
    display_rect(16 + (g_pck_len - start) * 7, CY + 3, 2, 12, CA);

    /* Length */
    char lbuf[8]; int_to_str(g_pck_len, lbuf);
    display_text(W - 32, CY + 4, lbuf, CD);

    /* Character strip */
    int sy = CY + 36;
    display_rect(0, sy - 2, W, 28, 0x2104);

    /* Show 15 chars centered: current ± 7 */
    for (int i = -7; i <= 7; i++) {
        int idx = (g_pck_cidx + i + g_pck_cslen) % g_pck_cslen;
        char c = g_pck_charset[idx];
        char cs[2] = {c, 0};
        int x = W/2 + i * 20 - 3;
        if (x < 0 || x > W - 8) continue;
        if (i == 0) {
            display_rect(x - 3, sy, 14, 18, CA);
            display_text(x, sy + 3, cs, CB);
        } else {
            uint16_t col = (i > -3 && i < 3) ? CT : CD;
            display_text(x, sy + 3, cs, col);
        }
    }

    /* Navigation hints under strip */
    display_text(4,     sy + 32, "[</>] char", CD);
    display_text(W/2-24,sy + 32, "[^/v] +8", CD);
    display_text(W-66,  sy + 32, "[A] add", CD);

    /* Instructions */
    ui_footer("[A] add  [B] backspace  [Y] done  [X] cancel", NULL);
    display_flush();
}

static void handle_char_picker(uint32_t btns) {
    if (!btns) return;
    if (btns & AKIRA_BTN_LEFT)  { g_pck_cidx = (g_pck_cidx - 1 + g_pck_cslen) % g_pck_cslen; g_dirty = 1; }
    if (btns & AKIRA_BTN_RIGHT) { g_pck_cidx = (g_pck_cidx + 1) % g_pck_cslen;               g_dirty = 1; }
    if (btns & AKIRA_BTN_UP)    { g_pck_cidx = (g_pck_cidx - 8 + g_pck_cslen) % g_pck_cslen; g_dirty = 1; }
    if (btns & AKIRA_BTN_DOWN)  { g_pck_cidx = (g_pck_cidx + 8) % g_pck_cslen;               g_dirty = 1; }
    if (btns & AKIRA_BTN_A) {
        if (g_pck_len < g_pck_dst_max - 1) {
            g_pck_buf[g_pck_len++] = g_pck_charset[g_pck_cidx];
            g_pck_buf[g_pck_len]   = '\0';
        }
        g_dirty = 1;
    }
    if (btns & AKIRA_BTN_B) {
        if (g_pck_len > 0) { g_pck_len--; g_pck_buf[g_pck_len] = '\0'; }
        g_dirty = 1;
    }
    if (btns & AKIRA_BTN_Y) {
        /* Commit to destination and return */
        str_ncpy(g_pck_dst, g_pck_buf, g_pck_dst_max);
        int ret = g_pck_ret_scr;
        if (ret == SCR_WALLET_LIST + 10) {
            /* Generate wallet seed */
            if (g_vault.wallet_count < MAX_WALLET) {
                wallet_entry_t *we = &g_vault.wallet[g_vault.wallet_count];
                str_ncpy(we->label, g_wlt_label, sizeof(we->label));
                rng_fill(we->entropy, 32);
                /* Extra entropy mix: hash of current state */
                uint8_t ehash[32];
                sha256_hash(we->entropy, 32, ehash);
                for (int i = 0; i < 32; i++) we->entropy[i] ^= ehash[i];
                rng_fill(ehash, 32); /* destroy intermediate */
                g_vault.wallet_count++;
                g_vault_dirty = 1; vault_save();
                /* Show the seed view */
                g_selected = (int)g_vault.wallet_count - 1;
                to_hex(g_vault.wallet[g_selected].entropy, 32, g_wlt_hex);
                g_screen = SCR_WALLET_VIEW;
            } else {
                g_screen = SCR_WALLET_LIST;
            }
        } else {
            g_screen = ret;
        }
        g_dirty = 1;
    }
    if (btns & AKIRA_BTN_X) {
        /* Cancel — discard changes */
        g_screen = g_pck_ret_scr;
        if (g_pck_ret_scr == SCR_WALLET_LIST + 10) g_screen = SCR_WALLET_LIST;
        g_dirty = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN: CONFIRM DIALOG
 * ═══════════════════════════════════════════════════════════════════════ */

static void draw_confirm(void) {
    display_clear(CB);
    ui_header("Confirm Action");
    /* Warning icon area */
    display_triangle_fill(W/2 - 16, CY + 40, W/2 + 16, CY + 40, W/2, CY + 14, CWN);
    display_text(W/2 - 2, CY + 24, "!", CB);

    display_text(W/2 - strlen(g_conf_msg) * 3, CY + 55, g_conf_msg, CT);

    /* Yes / No buttons */
    int nx = W/2 - 60, yx = W/2 + 10;
    display_rect_outline(nx - 4, CY + 80, 52, 28, g_conf_cursor == 0 ? CA : CS);
    display_text_large(nx, CY + 86, "NO",  g_conf_cursor == 0 ? CA : CD);
    display_rect_outline(yx - 4, CY + 80, 52, 28, g_conf_cursor == 1 ? CER : CS);
    display_text_large(yx, CY + 86, "YES", g_conf_cursor == 1 ? CER : CD);

    ui_footer("[LR] choose  [A] confirm  [B] cancel", NULL);
    display_flush();
}

static void handle_confirm(uint32_t btns) {
    if (!btns) return;
    if (btns & (AKIRA_BTN_LEFT|AKIRA_BTN_RIGHT)) { g_conf_cursor ^= 1; g_dirty = 1; }
    if (btns & AKIRA_BTN_B) {
        /* Cancel — return to appropriate list */
        switch (g_conf_action) {
        case CONF_DEL_PW:     g_screen = SCR_PW_LIST;    break;
        case CONF_DEL_TOTP:   g_screen = SCR_TOTP_LIST;  break;
        case CONF_DEL_WALLET: g_screen = SCR_WALLET_LIST; break;
        case CONF_WIPE:       g_screen = SCR_SETTINGS;   break;
        }
        g_dirty = 1;
    }
    if (btns & AKIRA_BTN_A) {
        if (g_conf_cursor == 0) {
            /* No — cancel */
            switch (g_conf_action) {
            case CONF_DEL_PW:     g_screen = SCR_PW_LIST;    break;
            case CONF_DEL_TOTP:   g_screen = SCR_TOTP_LIST;  break;
            case CONF_DEL_WALLET: g_screen = SCR_WALLET_LIST; break;
            case CONF_WIPE:       g_screen = SCR_SETTINGS;   break;
            }
        } else {
            /* Yes — perform action */
            switch (g_conf_action) {
            case CONF_DEL_PW: {
                int n = (int)g_vault.pw_count;
                for (int i = g_selected; i < n - 1; i++)
                    g_vault.pw[i] = g_vault.pw[i + 1];
                mem_set(&g_vault.pw[n - 1], 0, sizeof(pw_entry_t));
                g_vault.pw_count--;
                if (g_cursor >= (int)g_vault.pw_count && g_cursor > 0) g_cursor--;
                g_vault_dirty = 1; vault_save();
                g_screen = SCR_PW_LIST; break;
            }
            case CONF_DEL_TOTP: {
                int n = (int)g_vault.totp_count;
                for (int i = g_selected; i < n - 1; i++)
                    g_vault.totp[i] = g_vault.totp[i + 1];
                mem_set(&g_vault.totp[n - 1], 0, sizeof(totp_entry_t));
                g_vault.totp_count--;
                if (g_cursor >= (int)g_vault.totp_count && g_cursor > 0) g_cursor--;
                g_vault_dirty = 1; vault_save();
                g_screen = SCR_TOTP_LIST; break;
            }
            case CONF_DEL_WALLET: {
                int n = (int)g_vault.wallet_count;
                /* Securely zero the entropy before removing */
                mem_set(g_vault.wallet[g_selected].entropy, 0, 32);
                for (int i = g_selected; i < n - 1; i++)
                    g_vault.wallet[i] = g_vault.wallet[i + 1];
                mem_set(&g_vault.wallet[n - 1], 0, sizeof(wallet_entry_t));
                g_vault.wallet_count--;
                if (g_cursor >= (int)g_vault.wallet_count && g_cursor > 0) g_cursor--;
                g_vault_dirty = 1; vault_save();
                g_screen = SCR_WALLET_LIST; break;
            }
            case CONF_WIPE:
                storage_delete("vault.bin");
                settings_delete("vault/tunix");
                settings_delete("vault/tms");
                settings_delete("vault/hid");
                mem_set(&g_vault, 0, sizeof(g_vault));
                mem_set(g_vkey, 0, sizeof(g_vkey));
                g_hid_on = 0; g_time_set = 0;
                g_screen = SCR_BOOT;
                break;
            }
        }
        g_dirty = 1;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════ */

int main(void) {
    /* Detect actual display — Sharp LS027B7DH01 is 400×240 monochrome */
    display_get_size(&g_sw, &g_sh);
    if (g_sw <= 0) g_sw = CONSOLE_WIDTH;
    if (g_sh <= 0) g_sh = CONSOLE_HEIGHT;
    g_mono = (g_sw >= 400); /* Sharp Memory LCD = 400 px wide */

    /* Init GPIO buttons */
    vault_btns_init();

    /* Monotonic uptime timer */
    g_uptimer = timer_create();
    if (g_uptimer >= 0) timer_start(g_uptimer);

    rng_init();
    load_time_from_settings();

    /* Restore HID transport from settings */
    char hbuf[8];
    if (settings_get("vault/hid", hbuf, sizeof(hbuf)) == 0) {
        if (hbuf[0] == 'u') g_hid_transport = HID_TRANSPORT_USB;
        else                g_hid_transport = HID_TRANSPORT_BLE;
        hid_init(g_hid_transport, HID_DEVICE_KEYBOARD);
        g_hid_on = 1;
    } else {
        g_hid_transport = HID_TRANSPORT_BLE;
        g_hid_on = 0;
    }

    g_screen  = SCR_BOOT;
    g_dirty   = 1;
    g_frame   = 0;
    g_last_activity_frames = 0;

    /* Boot screen — brief display then route based on vault state */
    draw_boot();
    delay(800000); /* 800 ms */

    if (vault_exists()) {
        g_screen = SCR_LOCK;
        g_pin_len = 0; g_pin_step = 0; g_pin_err = 0; g_pin_digit = 0;
    } else {
        g_screen = SCR_NEW_VAULT;
        g_pin_len = 0; g_pin_step = 0; g_pin_err = 0; g_pin_digit = 0;
    }

    /* Main loop */
    while (1) {
        g_frame++;

        uint32_t btns = poll_btns();

        /* Auto-lock after inactivity */
        if (g_screen != SCR_LOCK && g_screen != SCR_NEW_VAULT &&
            g_screen != SCR_BOOT  &&
            (g_frame - g_last_activity_frames) > AUTO_LOCK_FRAMES) {
            vault_lock();
            g_screen = SCR_LOCK;
            g_pin_len = 0; g_pin_step = 0; g_pin_err = 0; g_pin_digit = 0;
            g_dirty = 1;
        }

        /* Handle button input per screen */
        if (btns) {
            switch (g_screen) {
            case SCR_LOCK:       handle_lock(btns);        break;
            case SCR_NEW_VAULT:  handle_lock(btns);        break;
            case SCR_PIN_CHANGE: handle_lock(btns);        break;
            case SCR_MENU:       handle_menu(btns);        break;
            case SCR_PW_LIST:    handle_pw_list(btns);     break;
            case SCR_PW_VIEW:    handle_pw_view(btns);     break;
            case SCR_PW_ADD:     handle_pw_add(btns);      break;
            case SCR_TOTP_LIST:  handle_totp_list(btns);   break;
            case SCR_TOTP_VIEW:  handle_totp_view(btns);   break;
            case SCR_TOTP_ADD:   handle_totp_add(btns);    break;
            case SCR_WALLET_LIST:handle_wallet_list(btns); break;
            case SCR_WALLET_VIEW:handle_wallet_view(btns); break;
            case SCR_SETTINGS:   handle_settings(btns);    break;
            case SCR_TIME_SETUP: handle_time_setup(btns);  break;
            case SCR_CHAR_PICKER:handle_char_picker(btns); break;
            case SCR_CONFIRM:    handle_confirm(btns);     break;
            }
        }

        /* TOTP view refreshes every second even without button input */
        if (g_screen == SCR_TOTP_LIST || g_screen == SCR_TOTP_VIEW) {
            /* refresh ~every second (62 frames × 16 ms ≈ 992 ms) */
            static int last_totp_frame = 0;
            if (g_frame - last_totp_frame >= 62) {
                last_totp_frame = g_frame; g_dirty = 1;
            }
        }

        /* Draw current screen if dirty */
        if (g_dirty) {
            g_dirty = 0;
            switch (g_screen) {
            case SCR_LOCK:
            case SCR_NEW_VAULT:
            case SCR_PIN_CHANGE: draw_lock();         break;
            case SCR_MENU:       draw_menu();         break;
            case SCR_PW_LIST:    draw_pw_list();      break;
            case SCR_PW_VIEW:    draw_pw_view();      break;
            case SCR_PW_ADD:     draw_pw_add();       break;
            case SCR_TOTP_LIST:  draw_totp_list();    break;
            case SCR_TOTP_VIEW:  draw_totp_view();    break;
            case SCR_TOTP_ADD:   draw_totp_add();     break;
            case SCR_WALLET_LIST:draw_wallet_list();  break;
            case SCR_WALLET_VIEW:draw_wallet_view();  break;
            case SCR_SETTINGS:   draw_settings();     break;
            case SCR_TIME_SETUP: draw_time_setup();   break;
            case SCR_CHAR_PICKER:draw_char_picker();  break;
            case SCR_CONFIRM:    draw_confirm();      break;
            default:             draw_boot();         break;
            }
        }

        delay(16000); /* ~60 fps cap, yields CPU */
    }
    return 0;
}
