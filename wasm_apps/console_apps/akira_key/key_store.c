/*
 * key_store.c — AkiraKey persistent storage (no PIN, no encryption)
 * SPDX-License-Identifier: Apache-2.0
 */
#include "key.h"

key_vault_t g_key_vault;
int         g_dirty       = 0;
screen_t    g_screen      = SCR_BOOT;
screen_t    g_prev_screen = SCR_BOOT;
int32_t     GW = 320, GH = 240;

#define KEY_FILE  "key.bin"
#define KEY_MAGIC "AKK2"

static void mem_zero(void *p,int n){uint8_t *q=(uint8_t*)p;while(n--)*q++=0;}
static void mem_cpy(void *d,const void *s,int n){
    uint8_t *dd=(uint8_t*)d;const uint8_t *ss=(const uint8_t*)s;while(n--)*dd++=*ss++;}
static int mem_eq(const void *a,const void *b,int n){
    const uint8_t *aa=(const uint8_t*)a,*bb=(const uint8_t*)b;
    int r=0;while(n--)r|=(*aa++)^(*bb++);return r==0;}

/* ChaCha20 core PRNG — used only for SSH keypair seed */
static uint32_t rng_st[16], rng_buf[16];
static int rng_pos=64;
static void rng_init(void){
    int32_t t=timer_elapsed(timer_create());
    uint8_t seed[64]; uint32_t x=(uint32_t)t^0xDEADBEEFu;
    for(int i=0;i<64;i++){x^=x<<13;x^=x>>17;x^=x<<5;seed[i]=(uint8_t)x;}
    rng_st[0]=0x61707865u;rng_st[1]=0x3320646eu;
    rng_st[2]=0x79622d32u;rng_st[3]=0x6b206574u;
    for(int i=0;i<8;i++)
        rng_st[4+i]=(uint32_t)seed[i*4]|((uint32_t)seed[i*4+1]<<8)|
                    ((uint32_t)seed[i*4+2]<<16)|((uint32_t)seed[i*4+3]<<24);
    rng_st[12]=0;
    rng_st[13]=(uint32_t)seed[32]|(uint32_t)seed[33]<<8|(uint32_t)seed[34]<<16|(uint32_t)seed[35]<<24;
    rng_st[14]=(uint32_t)seed[36];rng_st[15]=(uint32_t)seed[40];
}
static uint8_t rng_byte(void){
    if(rng_pos>=64){
        uint32_t tmp[16];int i;
        for(i=0;i<16;i++)tmp[i]=rng_st[i];
        for(i=0;i<10;i++){
            #define QR(a,b,c,d) tmp[a]+=tmp[b];tmp[d]^=tmp[a];tmp[d]=(tmp[d]<<16)|(tmp[d]>>16); \
                                 tmp[c]+=tmp[d];tmp[b]^=tmp[c];tmp[b]=(tmp[b]<<12)|(tmp[b]>>20); \
                                 tmp[a]+=tmp[b];tmp[d]^=tmp[a];tmp[d]=(tmp[d]<<8)|(tmp[d]>>24); \
                                 tmp[c]+=tmp[d];tmp[b]^=tmp[c];tmp[b]=(tmp[b]<<7)|(tmp[b]>>25);
            QR(0,4,8,12)QR(1,5,9,13)QR(2,6,10,14)QR(3,7,11,15)
            QR(0,5,10,15)QR(1,6,11,12)QR(2,7,8,13)QR(3,4,9,14)
        }
        for(i=0;i<16;i++)rng_buf[i]=tmp[i]+rng_st[i];
        rng_st[12]++;rng_pos=0;
    }
    return((uint8_t*)rng_buf)[rng_pos++];
}
static void rng_fill(uint8_t *buf,int len){for(int i=0;i<len;i++)buf[i]=rng_byte();}

int kstore_exists(void){
    int fd=storage_open(KEY_FILE,STORAGE_O_READ);
    if(fd<0)return 0;storage_close(fd);return 1;}

int kstore_create(void){
    rng_init();
    mem_zero(&g_key_vault,sizeof(g_key_vault));
    mem_cpy(g_key_vault.magic,KEY_MAGIC,4);
    g_key_vault.ble_enabled=1;
    uint8_t seed[32];rng_fill(seed,32);
    ed25519_generate_keypair(seed,g_key_vault.ssh_pub,g_key_vault.ssh_key);
    g_dirty=1;kstore_save();return 0;}

int kstore_load(void){
    int fd=storage_open(KEY_FILE,STORAGE_O_READ);
    if(fd<0)return -1;
    int r=storage_read(fd,&g_key_vault,sizeof(g_key_vault));
    storage_close(fd);
    if(r!=(int)sizeof(g_key_vault)||!mem_eq(g_key_vault.magic,KEY_MAGIC,4)){
        mem_zero(&g_key_vault,sizeof(g_key_vault));return -2;}
    return 0;}

void kstore_save(void){
    int fd=storage_open(KEY_FILE,STORAGE_O_WRITE);
    if(fd<0)return;
    storage_write(fd,&g_key_vault,sizeof(g_key_vault));
    storage_close(fd);g_dirty=0;}
