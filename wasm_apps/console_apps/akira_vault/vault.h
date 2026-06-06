/*
 * vault.h — AkiraVault types, constants, shared state
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef VAULT_H
#define VAULT_H

#include <stdint.h>

/* ── Display ────────────────────────────────────────────────────────────── */
/* Actual resolution read at runtime via display_get_size() */
extern int32_t GW, GH;   /* screen width / height in pixels */
/* Terminal grid: 8×13 glyph */
#define GLYPH_W   8
#define GLYPH_H   13
#define COLS      (GW / GLYPH_W)   /* ~40 on 320px */
#define ROWS      (GH / GLYPH_H)   /* ~18 on 240px */

/* ── Colours (RGB565) ───────────────────────────────────────────────────── */
#define C_BG      0x0000u   /* #000000 */
#define C_FG      0xFFFFu   /* #FFFFFF */
#define C_ACCENT  0x07F8u   /* #00FF00 ≈ green → white on mono */
#define C_WARN    0xFFE0u   /* #FFFF00 ≈ amber */
#define C_DANGER  0xF800u   /* #FF0000 → dark-gray on mono */
#define C_DIM     0x8410u   /* dim gray */
#define C_HEADER  0x0020u   /* very dark blue row bg */

/* ── GPIO pins ───────────────────────────────────────────────────────────── */
#define PIN_UP       4
#define PIN_DOWN     5
#define PIN_LEFT     7   /* BACK */
#define PIN_RIGHT    6   /* ENTER */
#define PIN_CENTER   15  /* SELECT */
#define PIN_SETTINGS 0   /* active-low emergency lock */

/* ── Sizes / limits ──────────────────────────────────────────────────────── */
#define PIN_LEN          6
#define PIN_KDF_ROUNDS   10000
#define WIPE_AFTER       3
#define MAX_OTP_SLOTS    16
#define MAX_ACCOUNTS     8
#define MAX_FIDO2_SLOTS  8

#define SEED_WORDS_12    12
#define SEED_WORDS_24    24
#define ENTROPY_BYTES_12 16   /* 128-bit for 12 words */
#define ENTROPY_BYTES_24 32   /* 256-bit for 24 words */

/* ── Screen IDs ──────────────────────────────────────────────────────────── */
typedef enum {
    SCR_BOOT = 0,
    SCR_SETUP_WELCOME,
    SCR_SETUP_GENERATE,
    SCR_SETUP_VERIFY,
    SCR_SETUP_RESTORE,
    SCR_UNLOCK,
    SCR_HOME,
    SCR_WALLET,
    SCR_ACCOUNTS,
    SCR_ACCOUNT_DETAIL,
    SCR_SIGN_MESSAGE,
    SCR_SHOW_XPUB,
    SCR_KEYS,
    SCR_TOTP_LIST,
    SCR_TOTP_CODE,
    SCR_TOTP_ADD,
    SCR_FIDO2_LIST,
    SCR_FIDO2_APPROVE,
    SCR_SSH_AGENT,
    SCR_SETTINGS,
    SCR_PIN_CHANGE,
    SCR_AUTO_LOCK,
    SCR_BLE_TOGGLE,
    SCR_FACTORY_RESET,
    SCR_ABOUT,
    SCR_COUNT
} screen_t;

/* ── TOTP slot ───────────────────────────────────────────────────────────── */
typedef struct {
    char     label[32];
    uint8_t  secret[32];   /* base32-decoded */
    uint8_t  secret_len;
    uint8_t  digits;       /* 6 or 8 */
    uint8_t  period;       /* 30 or 60 */
    uint8_t  algo;         /* 0=SHA1-compat HMAC-SHA256, 1=SHA256 */
    uint8_t  active;
} otp_slot_t;

/* ── FIDO2 resident credential ───────────────────────────────────────────── */
typedef struct {
    char     rp_id[64];
    char     user_name[48];
    uint8_t  private_key[32];   /* ed25519 */
    uint8_t  credential_id[32];
    uint32_t sign_count;
    uint8_t  active;
} fido2_cred_t;

/* ── HD Account ──────────────────────────────────────────────────────────── */
typedef struct {
    char     name[24];
    uint32_t index;         /* BIP44 account index */
    uint8_t  coin_type;     /* 0=BTC 60=ETH 501=SOL */
    uint8_t  active;
} hd_account_t;

/* ── Vault body (encrypted with ChaCha20) ────────────────────────────────── */
typedef struct {
    uint8_t      magic[4];      /* "AKV2" */
    uint8_t      seed[64];      /* BIP39 512-bit stretched seed */
    uint8_t      entropy[32];   /* raw entropy (for phrase recovery display) */
    uint8_t      entropy_len;   /* 16 or 32 */
    uint8_t      phrase_shown;  /* 1 = user has seen seed phrase */
    uint32_t     autolock_s;    /* 0=never, 30/60/300 */
    uint8_t      ble_enabled;
    uint8_t      rf_enabled;
    uint8_t      pad[2];
    otp_slot_t   otp[MAX_OTP_SLOTS];
    fido2_cred_t fido2[MAX_FIDO2_SLOTS];
    hd_account_t accounts[MAX_ACCOUNTS];
} vault_body_t;

/* ── File header (plaintext) ─────────────────────────────────────────────── */
typedef struct {
    uint8_t  magic[4];       /* "AKF2" */
    uint8_t  salt[32];       /* PBKDF2 salt */
    uint8_t  nonce[12];      /* ChaCha20 nonce */
    uint8_t  pin_hash[32];   /* HMAC-SHA256(PIN, salt) for fast reject */
    uint32_t body_len;
    uint8_t  wrong_attempts;
} vault_file_hdr_t;

/* ── Global vault state ──────────────────────────────────────────────────── */
extern vault_body_t  g_vault;
extern vault_file_hdr_t g_hdr;
extern uint8_t       g_key[32];     /* session encryption key */
extern int           g_locked;
extern int           g_vault_dirty;
extern screen_t      g_screen;
extern screen_t      g_prev_screen;

/* ── Crypto API (vault_crypto.c) ─────────────────────────────────────────── */
void     sha256(const uint8_t *d, int l, uint8_t *out);
void     hmac_sha256(const uint8_t *k, int kl, const uint8_t *m, int ml, uint8_t *out);
void     pbkdf2_sha256(const uint8_t *pw, int pwl, const uint8_t *salt, int sl,
                       int iters, uint8_t *out);
void     chacha20_xor(const uint8_t *key, const uint8_t *nonce,
                      uint32_t ctr, uint8_t *data, int len);
uint32_t totp_generate(const uint8_t *secret, int slen,
                       uint64_t unix_sec, int period, int digits);
int      base32_decode(const char *b32, uint8_t *out, int max);
void     bip39_entropy_to_mnemonic(const uint8_t *entropy, int elen,
                                   char words_out[24][12]);
int      bip39_mnemonic_to_seed(const char words[24][12], int word_count,
                                uint8_t seed_out[64]);

/* ── Store API (vault_store.c) ───────────────────────────────────────────── */
int  vault_exists(void);
int  vault_create(const char *pin6);
int  vault_load(const char *pin6);
void vault_save(void);
void vault_lock(void);

/* ── PIN API (vault_pin.c implemented inline in store) ───────────────────── */
int  pin_verify(const char *pin6);

/* ── UI API (vault_ui.c) ─────────────────────────────────────────────────── */
void ui_init(void);
void ui_draw(void);
void ui_handle_key(int key, int long_press);
void ui_tick(uint64_t unix_sec);
void uint32_zero_pad_6(uint32_t v, int digits, char *out);
/* key values */
#define KEY_UP      0
#define KEY_DOWN    1
#define KEY_LEFT    2
#define KEY_RIGHT   3
#define KEY_CENTER  4
#define KEY_NONE   -1

/* ── AkiraOS API forwards (only main.c includes akira_api.h) ────────────── */
extern int display_clear(uint32_t color);
extern int display_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t color);
extern int display_text(int32_t x,int32_t y,const char *t,uint32_t color);
extern int display_text_large(int32_t x,int32_t y,const char *t,uint32_t color);
extern int display_hline(int32_t x,int32_t y,int32_t len,uint32_t color);
extern int display_flush(void);
extern int display_get_size(int32_t *w,int32_t *h);
extern int gpio_configure(uint32_t pin,uint32_t flags);
extern int gpio_read(uint32_t pin);
extern int hid_init(int transport,int types);
extern int hid_type_string(const char *s);
extern int storage_open(const char *path,int flags);
extern int storage_read(int fd,void *buf,int len);
extern int storage_write(int fd,const void *buf,int len);
extern void storage_close(int fd);
extern int storage_delete(const char *path);
extern int settings_get(const char *key,char *buf,int32_t len);
extern int settings_set(const char *key,const char *val);
extern int timer_create(void);
extern int timer_start(int32_t h);
extern int timer_elapsed(int32_t h);
extern int delay(uint32_t us);
extern int crypto_random(void *buf,int len);
/* GPIO flags */
#ifndef GPIO_INPUT
#define GPIO_INPUT            (1U<<0)
#define GPIO_PULL_UP          (1U<<4)
#define GPIO_PULL_DOWN        (1U<<5)
#define GPIO_ACTIVE_LOW       (1U<<6)
#endif
/* Storage flags */
#define STORAGE_O_READ  0
#define STORAGE_O_WRITE 1
#define STORAGE_O_RDWR  3
/* HID */
#define HID_TRANSPORT_BLE 1
#define HID_TRANSPORT_USB 2
#define HID_DEVICE_KEYBOARD 0x01

/* ── String helpers (no stdlib) ─────────────────────────────────────────── */
static inline int sv_len(const char *s) {
    int n=0; while(s[n]) n++; return n;
}
static inline void sv_cpy(char *d, const char *s, int max) {
    int i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]='\0';
}
static inline int sv_cmp(const char *a, const char *b) {
    while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}
static inline void sv_ncpy(char *d, const char *s, int n) {
    int i=0; while(i<n&&s[i]){d[i]=s[i];i++;} while(i<n) d[i++]='\0';
}

#endif /* VAULT_H */
