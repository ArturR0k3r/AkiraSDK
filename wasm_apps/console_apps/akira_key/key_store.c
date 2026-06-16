/*
 * key_store.c — AkiraKey persistent storage with PIN + ChaCha20 encryption
 * SPDX-License-Identifier: Apache-2.0
 *
 * File layout (key.bin):
 *   [0..71]  Plaintext header  (magic, pin_hash, pin_salt, enc_nonce, pad)
 *   [72..]   ChaCha20-encrypted vault body
 *
 * PIN verification : PBKDF2-SHA256(pin, pin_salt,          1024) == pin_hash
 * Encryption key  : PBKDF2-SHA256(pin, pin_salt XOR 0xAA, 1024)
 */
#include "key.h"

/* ── Global state ────────────────────────────────────────────────────── */
key_vault_t g_key_vault;
int         g_dirty            = 0;
screen_t    g_screen           = SCR_BOOT;
screen_t    g_prev_screen      = SCR_BOOT;
int         g_locked           = 1;
int         g_exit             = 0;
int         g_boot_usb_status  = 0;
int32_t     GW = 320, GH = 240;

/* In-memory derived encryption key — zeroed on lock */
static uint8_t g_vault_key[32];

#define KEY_FILE "key.bin"

/* ── Memory helpers ──────────────────────────────────────────────────── */
static void mem_zero(void *p, int n) {
    volatile uint8_t *q = (volatile uint8_t *)p; while (n--) *q++ = 0;
}
static void mem_cpy(void *d, const void *s, int n) {
    uint8_t *dd = (uint8_t *)d; const uint8_t *ss = (const uint8_t *)s;
    while (n--) *dd++ = *ss++;
}
static int mem_eq(const void *a, const void *b, int n) {
    const uint8_t *aa = (const uint8_t *)a, *bb = (const uint8_t *)b;
    int r = 0; while (n--) r |= (*aa++) ^ (*bb++); return r == 0;
}

/* ── ChaCha20 PRNG (for key/nonce generation) ────────────────────────── */
static uint32_t rng_st[16], rng_buf[16];
static int rng_pos = 64;

static void rng_init(void) {
    int32_t t = timer_elapsed(timer_create());
    uint8_t seed[64]; uint32_t x = (uint32_t)t ^ 0xDEADBEEFu;
    for (int i = 0; i < 64; i++) { x^=x<<13; x^=x>>17; x^=x<<5; seed[i]=(uint8_t)x; }
    rng_st[0]=0x61707865u; rng_st[1]=0x3320646eu;
    rng_st[2]=0x79622d32u; rng_st[3]=0x6b206574u;
    for (int i = 0; i < 8; i++)
        rng_st[4+i] = (uint32_t)seed[i*4]   | ((uint32_t)seed[i*4+1]<<8) |
                      ((uint32_t)seed[i*4+2]<<16) | ((uint32_t)seed[i*4+3]<<24);
    rng_st[12] = 0;
    rng_st[13] = (uint32_t)seed[32]|((uint32_t)seed[33]<<8)|
                 ((uint32_t)seed[34]<<16)|((uint32_t)seed[35]<<24);
    rng_st[14] = (uint32_t)seed[36]; rng_st[15] = (uint32_t)seed[40];
}

static uint8_t rng_byte(void) {
    if (rng_pos >= 64) {
        uint32_t tmp[16]; int i;
        for (i = 0; i < 16; i++) tmp[i] = rng_st[i];
        for (i = 0; i < 10; i++) {
            #define QR(a,b,c,d) \
                tmp[a]+=tmp[b]; tmp[d]^=tmp[a]; tmp[d]=(tmp[d]<<16)|(tmp[d]>>16); \
                tmp[c]+=tmp[d]; tmp[b]^=tmp[c]; tmp[b]=(tmp[b]<<12)|(tmp[b]>>20); \
                tmp[a]+=tmp[b]; tmp[d]^=tmp[a]; tmp[d]=(tmp[d]<<8) |(tmp[d]>>24); \
                tmp[c]+=tmp[d]; tmp[b]^=tmp[c]; tmp[b]=(tmp[b]<<7) |(tmp[b]>>25);
            QR(0,4,8,12) QR(1,5,9,13) QR(2,6,10,14) QR(3,7,11,15)
            QR(0,5,10,15) QR(1,6,11,12) QR(2,7,8,13) QR(3,4,9,14)
            #undef QR
        }
        for (i = 0; i < 16; i++) rng_buf[i] = tmp[i] + rng_st[i];
        rng_st[12]++; rng_pos = 0;
    }
    return ((uint8_t *)rng_buf)[rng_pos++];
}
static void rng_fill(uint8_t *buf, int len) { for (int i = 0; i < len; i++) buf[i] = rng_byte(); }

/* ── Key derivation ──────────────────────────────────────────────────── */
static void derive_pin_hash(const char *pin, const uint8_t *pin_salt, uint8_t *out) {
    pbkdf2_sha256((const uint8_t *)pin, sv_len(pin), pin_salt, 16, 1024, out);
}

static void derive_vault_key(const char *pin, const uint8_t *pin_salt, uint8_t *out) {
    uint8_t enc_salt[16];
    for (int i = 0; i < 16; i++) enc_salt[i] = pin_salt[i] ^ 0xAAu;
    pbkdf2_sha256((const uint8_t *)pin, sv_len(pin), enc_salt, 16, 1024, out);
}

/* ── Encrypt/decrypt vault body in-place ─────────────────────────────── */
static void crypt_body(void) {
    uint8_t *body = (uint8_t *)&g_key_vault + VAULT_HDR_SIZE;
    int      blen = (int)sizeof(g_key_vault) - VAULT_HDR_SIZE;
    chacha20_xor(g_vault_key, g_key_vault.enc_nonce, 0, body, blen);
}

/* ── Public API ──────────────────────────────────────────────────────── */

int kstore_exists(void) {
    int fd = storage_open(KEY_FILE, STORAGE_O_READ);
    if (fd < 0) return 0;
    storage_close(fd); return 1;
}

/* Read entire file into g_key_vault (body stays encrypted until kstore_unlock). */
int kstore_load_hdr(void) {
    int fd = storage_open(KEY_FILE, STORAGE_O_READ);
    if (fd < 0) return -1;
    int r = storage_read(fd, &g_key_vault, sizeof(g_key_vault));
    storage_close(fd);
    if (r != (int)sizeof(g_key_vault) || !mem_eq(g_key_vault.magic, KEY_MAGIC, 4))
        return -2;
    return 0;
}

/*
 * Verify PIN, derive vault key, decrypt body.
 * Returns 0 on success, -1 on wrong PIN.
 */
int kstore_unlock(const char *pin) {
    uint8_t test_hash[32];
    derive_pin_hash(pin, g_key_vault.pin_salt, test_hash);
    if (!mem_eq(test_hash, g_key_vault.pin_hash, 32)) return -1;

    derive_vault_key(pin, g_key_vault.pin_salt, g_vault_key);
    crypt_body();   /* decrypt */
    g_locked = 0;
    return 0;
}

/*
 * Create a fresh vault, set PIN, auto-unlock.
 * Wipes any prior vault in memory.
 */
int kstore_create(const char *pin) {
    rng_init();
    mem_zero(&g_key_vault, sizeof(g_key_vault));
    mem_cpy(g_key_vault.magic, KEY_MAGIC, 4);

    rng_fill(g_key_vault.pin_salt, 16);
    rng_fill(g_key_vault.enc_nonce, 12);

    derive_pin_hash(pin, g_key_vault.pin_salt, g_key_vault.pin_hash);
    derive_vault_key(pin, g_key_vault.pin_salt, g_vault_key);

    g_key_vault.ble_enabled = 0;
    uint8_t seed[32]; rng_fill(seed, 32);
    ed25519_generate_keypair(seed, g_key_vault.ssh_pub, g_key_vault.ssh_key);

    g_locked = 0;
    g_dirty  = 1;
    kstore_save();
    return 0;
}

/* Change PIN while vault is already unlocked. */
void kstore_set_pin(const char *pin) {
    rng_fill(g_key_vault.pin_salt, 16);  /* fresh salt */
    derive_pin_hash(pin, g_key_vault.pin_salt, g_key_vault.pin_hash);
    derive_vault_key(pin, g_key_vault.pin_salt, g_vault_key);
    g_dirty = 1;
}

/* Encrypt vault body into a scratch buffer, write to disk. */
void kstore_save(void) {
    if (g_locked) return;

    /* Work on a copy so we don't corrupt the in-memory plaintext */
    static key_vault_t tmp;
    mem_cpy(&tmp, &g_key_vault, sizeof(tmp));

    /* Fresh nonce each save — prevents keystream reuse */
    rng_fill(tmp.enc_nonce, 12);
    mem_cpy(g_key_vault.enc_nonce, tmp.enc_nonce, 12);

    uint8_t *body = (uint8_t *)&tmp + VAULT_HDR_SIZE;
    int      blen = (int)sizeof(tmp) - VAULT_HDR_SIZE;
    chacha20_xor(g_vault_key, tmp.enc_nonce, 0, body, blen);

    int fd = storage_open(KEY_FILE, STORAGE_O_WRITE);
    if (fd < 0) return;
    storage_write(fd, &tmp, sizeof(tmp));
    storage_close(fd);
    g_dirty = 0;
}

/* Zero derived key and mark locked. Caller must redirect to PIN screen. */
void kstore_lock(void) {
    mem_zero(g_vault_key, 32);
    g_locked = 1;
}

/* Delete vault file and zero in-memory state (factory reset / wipe). */
void kstore_wipe(void) {
    storage_delete(KEY_FILE);
    mem_zero(&g_key_vault, sizeof(g_key_vault));
    mem_zero(g_vault_key, 32);
    g_locked = 1;
    g_dirty  = 0;
}
