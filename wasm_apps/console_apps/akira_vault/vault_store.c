/*
 * vault_store.c — Vault persistence via AkiraOS storage API
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vault.h"
/* AkiraOS APIs — declared in vault.h via extern; akira_api.h only in main.c */

/* ── Global state ──────────────────────────────────────────────────────── */
vault_body_t   g_vault;
vault_file_hdr_t g_hdr;
uint8_t        g_key[32];
int            g_locked       = 1;
int            g_vault_dirty  = 0;
screen_t       g_screen       = SCR_BOOT;
screen_t       g_prev_screen  = SCR_BOOT;
int32_t        GW = 320, GH = 240;

#define VAULT_FILE "vault.bin"

/* ── Helpers ────────────────────────────────────────────────────────────── */
static void mem_zero(void *p, int n) { uint8_t *q=(uint8_t*)p; while(n--)*q++=0; }
static void mem_cpy(void *d, const void *s, int n) {
    uint8_t *dd=(uint8_t*)d; const uint8_t *ss=(const uint8_t*)s;
    while(n--)*dd++=*ss++;
}
static int mem_eq(const void *a, const void *b, int n) {
    const uint8_t *aa=(const uint8_t*)a, *bb=(const uint8_t*)b;
    int r=0; while(n--) r|=(*aa++)^(*bb++); return r==0;
}

/* ── PRNG (ChaCha20-based, seeded from AkiraOS crypto_random) ──────────── */
static uint32_t rng_st[16];
static uint32_t rng_buf[16];
static int      rng_pos = 64;

static void rng_init(void) {
    /* Seed from AkiraOS hardware TRNG via crypto_random syscall */
    uint8_t seed[64];
    /* AkiraOS exports crypto_random — fills with hardware entropy */
    crypto_random(seed, 64);
    rng_st[0]=0x61707865u;rng_st[1]=0x3320646eu;
    rng_st[2]=0x79622d32u;rng_st[3]=0x6b206574u;
    for(int i=0;i<8;i++)
        rng_st[4+i]=(uint32_t)seed[i*4]|((uint32_t)seed[i*4+1]<<8)|
                    ((uint32_t)seed[i*4+2]<<16)|((uint32_t)seed[i*4+3]<<24);
    rng_st[12]=0; rng_st[13]=(uint32_t)seed[32]|(uint32_t)seed[33]<<8|
               (uint32_t)seed[34]<<16|(uint32_t)seed[35]<<24;
    rng_st[14]=(uint32_t)seed[36]; rng_st[15]=(uint32_t)seed[40];
}

static uint8_t rng_byte(void) {
    if(rng_pos>=64){
        uint32_t tmp[16]; int i;
        /* inline ChaCha20 block */
        for(i=0;i<16;i++) tmp[i]=rng_st[i];
        for(i=0;i<10;i++){
            #define QQR(a,b,c,d) tmp[a]+=tmp[b];tmp[d]^=tmp[a];tmp[d]=(tmp[d]<<16)|(tmp[d]>>16); \
                                  tmp[c]+=tmp[d];tmp[b]^=tmp[c];tmp[b]=(tmp[b]<<12)|(tmp[b]>>20); \
                                  tmp[a]+=tmp[b];tmp[d]^=tmp[a];tmp[d]=(tmp[d]<<8)|(tmp[d]>>24); \
                                  tmp[c]+=tmp[d];tmp[b]^=tmp[c];tmp[b]=(tmp[b]<<7)|(tmp[b]>>25);
            QQR(0,4,8,12) QQR(1,5,9,13) QQR(2,6,10,14) QQR(3,7,11,15)
            QQR(0,5,10,15) QQR(1,6,11,12) QQR(2,7,8,13) QQR(3,4,9,14)
        }
        for(i=0;i<16;i++) rng_buf[i]=tmp[i]+rng_st[i];
        rng_st[12]++; rng_pos=0;
    }
    return ((uint8_t*)rng_buf)[rng_pos++];
}

static void rng_fill(uint8_t *buf, int len) {
    for(int i=0;i<len;i++) buf[i]=rng_byte();
}

/* ── PIN fast-verify hash (stored in header plaintext) ─────────────────── */
static void pin_make_hash(const char *pin6, const uint8_t *salt, uint8_t *out32) {
    /* HMAC-SHA256(pin, salt) — lightweight but sufficient for quick reject */
    hmac_sha256((const uint8_t*)pin6, PIN_LEN, salt, 32, out32);
}

/* ── Key derivation from PIN ────────────────────────────────────────────── */
static void derive_key(const char *pin6, const uint8_t *salt, uint8_t *key32) {
    pbkdf2_sha256((const uint8_t*)pin6, PIN_LEN, salt, 32, PIN_KDF_ROUNDS, key32);
}

/* ── Vault exists? ──────────────────────────────────────────────────────── */
int vault_exists(void) {
    int fd = storage_open(VAULT_FILE, STORAGE_O_READ);
    if(fd<0) return 0;
    storage_close(fd); return 1;
}

/* ── Create new vault ───────────────────────────────────────────────────── */
int vault_create(const char *pin6) {
    rng_init();
    /* Init header */
    mem_cpy(g_hdr.magic, "AKF3", 4);
    rng_fill(g_hdr.salt, 32);
    rng_fill(g_hdr.nonce, 12);
    pin_make_hash(pin6, g_hdr.salt, g_hdr.pin_hash);
    g_hdr.body_len = sizeof(vault_body_t);
    g_hdr.wrong_attempts = 0;
    /* Init body */
    mem_zero(&g_vault, sizeof(g_vault));
    mem_cpy(g_vault.magic, "AKV3", 4);
    g_vault.autolock_s = 60;
    /* Generate master entropy */
    rng_fill(g_vault.entropy, ENTROPY_BYTES_24);
    g_vault.entropy_len = ENTROPY_BYTES_24;
    g_vault.phrase_shown = 0;
    /* Derive key and save */
    derive_key(pin6, g_hdr.salt, g_key);
    g_vault_dirty = 1;
    vault_save();
    g_locked = 0;
    return 0;
}

/* ── Load vault ─────────────────────────────────────────────────────────── */
int vault_load(const char *pin6) {
    int fd = storage_open(VAULT_FILE, STORAGE_O_READ);
    if(fd<0) return -1;
    int r = storage_read(fd, &g_hdr, sizeof(g_hdr));
    if(r!=(int)sizeof(g_hdr)||!mem_eq(g_hdr.magic,"AKF3",4)){
        storage_close(fd); return -2;
    }
    /* Quick PIN reject */
    uint8_t ph[32]; pin_make_hash(pin6, g_hdr.salt, ph);
    if(!mem_eq(ph, g_hdr.pin_hash, 32)){
        storage_close(fd);
        g_hdr.wrong_attempts++;
        /* Save updated attempt count */
        int fw=storage_open(VAULT_FILE,STORAGE_O_RDWR);
        if(fw>=0){storage_write(fw,&g_hdr,sizeof(g_hdr));storage_close(fw);}
        return -3; /* wrong PIN */
    }
    /* Read encrypted body */
    uint8_t enc[sizeof(vault_body_t)];
    r = storage_read(fd, enc, sizeof(enc));
    storage_close(fd);
    if(r!=(int)sizeof(vault_body_t)) return -4;
    /* Derive key and decrypt */
    derive_key(pin6, g_hdr.salt, g_key);
    mem_cpy(&g_vault, enc, sizeof(vault_body_t));
    chacha20_xor(g_key, g_hdr.nonce, 0, (uint8_t*)&g_vault, sizeof(vault_body_t));
    /* Verify body magic */
    if(!mem_eq(g_vault.magic,"AKV3",4)){
        mem_zero(&g_vault,sizeof(g_vault));
        mem_zero(g_key,32);
        return -5; /* corrupt / wrong key */
    }
    /* Reset wrong attempts on success */
    g_hdr.wrong_attempts = 0;
    g_locked = 0;
    return 0;
}

/* ── Save vault (encrypt + write) ───────────────────────────────────────── */
void vault_save(void) {
    /* Fresh nonce each save */
    rng_fill(g_hdr.nonce, 12);
    /* Encrypt a copy */
    uint8_t enc[sizeof(vault_body_t)];
    mem_cpy(enc, &g_vault, sizeof(vault_body_t));
    chacha20_xor(g_key, g_hdr.nonce, 0, enc, sizeof(vault_body_t));
    int fd = storage_open(VAULT_FILE, STORAGE_O_WRITE);
    if(fd<0) return;
    storage_write(fd, &g_hdr, sizeof(g_hdr));
    storage_write(fd, enc, sizeof(vault_body_t));
    storage_close(fd);
    g_vault_dirty = 0;
}

/* ── Lock vault ─────────────────────────────────────────────────────────── */
void vault_lock(void) {
    if(g_vault_dirty) vault_save();
    /* Securely zero secrets */
    mem_zero(&g_vault, sizeof(g_vault));
    mem_zero(g_key, 32);
    g_locked = 1;
    g_screen = SCR_UNLOCK;
}

/* ── PIN verify for screen-level checks ────────────────────────────────── */
int pin_verify(const char *pin6) {
    uint8_t ph[32]; pin_make_hash(pin6, g_hdr.salt, ph);
    return mem_eq(ph, g_hdr.pin_hash, 32) ? 0 : -1;
}
