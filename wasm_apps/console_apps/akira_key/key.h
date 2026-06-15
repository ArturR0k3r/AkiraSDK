/*
 * key.h — AkiraKey electronic key: types, constants, shared state
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef KEY_H
#define KEY_H

#include <stdint.h>

/* ── Display ────────────────────────────────────────────────────────────── */
extern int32_t GW, GH;
#define GLYPH_W   8
#define GLYPH_H   13
#define COLS      (GW / GLYPH_W)
#define ROWS      (GH / GLYPH_H)

/* ── Colours (RGB565) ───────────────────────────────────────────────────── */
#define C_BG      0x0000u
#define C_FG      0xFFFFu
#define C_ACCENT  0x07E0u   /* green */
#define C_WARN    0xFFE0u
#define C_DANGER  0xF800u
#define C_DIM     0x8410u
#define C_HEADER  0x0010u
#define C_BLUE    0x001Fu

/* ── GPIO pins ───────────────────────────────────────────────────────────── */
#define PIN_UP       4
#define PIN_DOWN     5
#define PIN_LEFT     6
#define PIN_RIGHT    7
#define PIN_A        15
#define PIN_B        16
#define PIN_SET      0   /* active-low */

/* ── Limits ──────────────────────────────────────────────────────────────── */
#define PIN_LEN         6
#define PIN_KDF_ROUNDS  10000
#define WIPE_AFTER      3
#define MAX_TOTP_SLOTS  16
#define MAX_FIDO2_SLOTS 8
#define MAX_PASS_SLOTS  8

/* ── Screen IDs ──────────────────────────────────────────────────────────── */
typedef enum {
    SCR_BOOT = 0,
    SCR_UNLOCK,
    SCR_HOME,
    SCR_TOTP_LIST,
    SCR_TOTP_VIEW,
    SCR_FIDO2_LIST,
    SCR_FIDO2_VIEW,
    SCR_PASS_LIST,
    SCR_PASS_VIEW,
    SCR_SSH_VIEW,
    SCR_SETTINGS,
    SCR_CHANGE_PIN,
    SCR_FACTORY_RESET,
    SCR_ABOUT,
    SCR_COUNT
} screen_t;

/* ── TOTP/HOTP slot ──────────────────────────────────────────────────────── */
typedef struct {
    char     label[32];
    uint8_t  secret[32];
    uint8_t  secret_len;
    uint8_t  digits;    /* 6 or 8 */
    uint8_t  period;    /* 30 or 60 */
    uint8_t  active;
} totp_slot_t;

/* ── FIDO2 resident credential ───────────────────────────────────────────── */
typedef struct {
    char     rp_id[48];
    char     user_name[32];
    uint8_t  private_key[32];   /* ed25519 */
    uint32_t sign_count;
    uint8_t  active;
} fido2_cred_t;

/* ── Password slot ───────────────────────────────────────────────────────── */
typedef struct {
    char    label[24];
    char    username[32];
    char    password[48];   /* stored encrypted in vault body */
    uint8_t active;
} pass_slot_t;

/* ── Key vault body (ChaCha20-encrypted) ─────────────────────────────────── */
typedef struct {
    uint8_t      magic[4];     /* "AKK1" */
    uint8_t      ssh_key[32];  /* ed25519 private key for SSH agent */
    uint8_t      ssh_pub[32];  /* ed25519 public key */
    uint32_t     autolock_s;
    uint8_t      ble_enabled;
    uint8_t      pad[3];
    totp_slot_t  totp[MAX_TOTP_SLOTS];
    fido2_cred_t fido2[MAX_FIDO2_SLOTS];
    pass_slot_t  pass[MAX_PASS_SLOTS];
} key_vault_t;

/* ── File header (plaintext) ─────────────────────────────────────────────── */
typedef struct {
    uint8_t  magic[4];     /* "AKK1" */
    uint8_t  salt[32];
    uint8_t  nonce[12];
    uint8_t  pin_hash[32];
    uint32_t body_len;
    uint8_t  wrong_attempts;
} key_hdr_t;

/* ── Global state ────────────────────────────────────────────────────────── */
extern key_vault_t g_key_vault;
extern key_hdr_t   g_key_hdr;
extern uint8_t     g_enc_key[32];
extern int         g_locked;
extern int         g_dirty;
extern screen_t    g_screen;
extern screen_t    g_prev_screen;

/* ── Crypto (key_crypto.c) ───────────────────────────────────────────────── */
void     sha256(const uint8_t *d, int l, uint8_t *out);
void     hmac_sha256(const uint8_t *k, int kl, const uint8_t *m, int ml, uint8_t *out);
void     pbkdf2_sha256(const uint8_t *pw, int pwl, const uint8_t *salt, int sl,
                       int iters, uint8_t *out);
void     chacha20_xor(const uint8_t *key, const uint8_t *nonce,
                      uint32_t ctr, uint8_t *data, int len);
uint32_t totp_generate(const uint8_t *secret, int slen,
                       uint64_t unix_sec, int period, int digits);
int      base32_decode(const char *b32, uint8_t *out, int max);
void     ed25519_generate_keypair(const uint8_t *seed32,
                                  uint8_t pub[32], uint8_t priv[64]);

/* ── Store (key_store.c) ─────────────────────────────────────────────────── */
int  kstore_exists(void);
int  kstore_create(const char *pin6);
int  kstore_load(const char *pin6);
void kstore_save(void);
void kstore_lock(void);

/* ── UI (key_ui.c) ───────────────────────────────────────────────────────── */
void ui_init(void);
void ui_draw(void);
void ui_handle_key(int key, int long_press);
void ui_tick(uint64_t unix_sec);

#define KEY_UP      0
#define KEY_DOWN    1
#define KEY_LEFT    2
#define KEY_RIGHT   3
#define KEY_A       4
#define KEY_B       5
#define KEY_NONE   -1

/* ── AkiraOS API forwards ───────────────────────────────────────────────── */
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
extern int hid_disable(void);
extern int hid_fido_recv(void *buf,int len);
extern int hid_fido_send(const void *buf,int len);
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

#ifndef GPIO_INPUT
#define GPIO_INPUT            (1U<<0)
#define GPIO_PULL_UP          (1U<<4)
#define GPIO_PULL_DOWN        (1U<<5)
#define GPIO_ACTIVE_LOW       (1U<<6)
#endif
#define STORAGE_O_READ  0
#define STORAGE_O_WRITE 1
#define STORAGE_O_RDWR  3
#define HID_TRANSPORT_BLE   1
#define HID_TRANSPORT_USB   2
#define HID_DEVICE_KEYBOARD 0x01

/* ── String helpers ─────────────────────────────────────────────────────── */
static inline int sv_len(const char *s){int n=0;while(s[n])n++;return n;}
static inline void sv_cpy(char *d,const char *s,int m){int i=0;while(i<m-1&&s[i]){d[i]=s[i];i++;}d[i]='\0';}
static inline int sv_cmp(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return(unsigned char)*a-(unsigned char)*b;}
static inline void sv_ncpy(char *d,const char *s,int n){int i=0;while(i<n&&s[i]){d[i]=s[i];i++;}while(i<n)d[i++]='\0';}

#endif /* KEY_H */
