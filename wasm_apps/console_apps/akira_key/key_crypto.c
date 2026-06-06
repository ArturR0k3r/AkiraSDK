/*
 * key_crypto.c — SHA-256, HMAC, PBKDF2, ChaCha20, TOTP, base32, ed25519 stub
 * SPDX-License-Identifier: Apache-2.0
 *
 * SHA-256: FIPS 180-4
 * ChaCha20: RFC 7539
 * PBKDF2: RFC 2898
 * TOTP: RFC 6238 / HOTP RFC 4226
 * ed25519: stub (full impl needs ~4KB)
 */
#include "key.h"

/* ── SHA-256 ────────────────────────────────────────────────────────────── */
#define RR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static const uint32_t K[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_block(uint32_t h[8], const uint8_t *blk) {
    uint32_t w[64]; int i;
    for(i=0;i<16;i++) w[i]=(uint32_t)blk[i*4]<<24|(uint32_t)blk[i*4+1]<<16|
                            (uint32_t)blk[i*4+2]<<8|(uint32_t)blk[i*4+3];
    for(i=16;i<64;i++){
        uint32_t s0=RR(w[i-15],7)^RR(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1=RR(w[i-2],17)^RR(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for(i=0;i<64;i++){
        uint32_t S1=RR(e,6)^RR(e,11)^RR(e,25);
        uint32_t ch=(e&f)^(~e&g);
        uint32_t t1=hh+S1+ch+K[i]+w[i];
        uint32_t S0=RR(a,2)^RR(a,13)^RR(a,22);
        uint32_t maj=(a&b)^(a&c)^(b&c);
        uint32_t t2=S0+maj;
        hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}

void sha256(const uint8_t *d, int l, uint8_t *out) {
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t blk[64]; int i, rem=l%64, pad=rem<56?55-rem:119-rem;
    uint64_t bits=(uint64_t)l*8;
    int pos=0;
    while(pos+64<=l){ sha256_block(h,d+pos); pos+=64; }
    for(i=0;i<rem;i++) blk[i]=d[pos+i];
    blk[rem]=0x80;
    for(i=rem+1;i<64&&i<=pad+rem+1;i++) blk[i]=0;
    if(pad+rem+1<64){
        for(i=0;i<8;i++) blk[56+i]=(uint8_t)(bits>>((7-i)*8));
        sha256_block(h,blk);
    } else {
        sha256_block(h,blk);
        for(i=0;i<56;i++) blk[i]=0;
        for(i=0;i<8;i++) blk[56+i]=(uint8_t)(bits>>((7-i)*8));
        sha256_block(h,blk);
    }
    for(i=0;i<8;i++){out[i*4]=(uint8_t)(h[i]>>24);out[i*4+1]=(uint8_t)(h[i]>>16);
                      out[i*4+2]=(uint8_t)(h[i]>>8);out[i*4+3]=(uint8_t)h[i];}
}

void hmac_sha256(const uint8_t *k,int kl,const uint8_t *m,int ml,uint8_t *out){
    uint8_t ki[64],ko[64]; int i;
    if(kl>64){sha256(k,kl,ki);kl=32;} else for(i=0;i<kl;i++)ki[i]=k[i];
    for(i=kl;i<64;i++)ki[i]=0;
    for(i=0;i<64;i++){ko[i]=ki[i]^0x5cu;ki[i]^=0x36u;}
    uint8_t inner[32];
    /* Process ki block then m */
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    sha256_block(h,ki); /* ki is already 64 bytes */
    uint8_t blk2[64]; int pos=0,rem=ml%64,pad=rem<56?55-rem:119-rem;
    uint64_t bits=(uint64_t)(64+ml)*8;
    while(pos+64<=ml){sha256_block(h,m+pos);pos+=64;}
    int r2=ml-pos;
    for(i=0;i<r2;i++)blk2[i]=m[pos+i];
    blk2[r2]=0x80;
    for(i=r2+1;i<=r2+pad&&i<64;i++)blk2[i]=0;
    if(r2+1+pad<64){
        for(i=0;i<8;i++)blk2[56+i]=(uint8_t)(bits>>((7-i)*8));
        sha256_block(h,blk2);
    } else {
        sha256_block(h,blk2);
        for(i=0;i<56;i++)blk2[i]=0;
        for(i=0;i<8;i++)blk2[56+i]=(uint8_t)(bits>>((7-i)*8));
        sha256_block(h,blk2);
    }
    for(i=0;i<8;i++){inner[i*4]=(uint8_t)(h[i]>>24);inner[i*4+1]=(uint8_t)(h[i]>>16);
                      inner[i*4+2]=(uint8_t)(h[i]>>8);inner[i*4+3]=(uint8_t)h[i];}
    /* outer = sha256(ko || inner) */
    uint8_t outer_in[96];
    for(i=0;i<64;i++)outer_in[i]=ko[i];
    for(i=0;i<32;i++)outer_in[64+i]=inner[i];
    sha256(outer_in,96,out);
}

void pbkdf2_sha256(const uint8_t *pw,int pwl,const uint8_t *salt,int sl,
                   int iters,uint8_t *out){
    /* 1 block = 32 bytes (PBKDF2 block 1) */
    uint8_t u[32], f[32], s2[sl+4];
    for(int i=0;i<sl;i++) s2[i]=salt[i];
    s2[sl]=0;s2[sl+1]=0;s2[sl+2]=0;s2[sl+3]=1; /* block counter = 1 */
    hmac_sha256(pw,pwl,s2,sl+4,u);
    for(int i=0;i<32;i++) f[i]=u[i];
    for(int j=1;j<iters;j++){
        hmac_sha256(pw,pwl,u,32,u);
        for(int i=0;i<32;i++) f[i]^=u[i];
    }
    for(int i=0;i<32;i++) out[i]=f[i];
}

/* ── ChaCha20 ────────────────────────────────────────────────────────────── */
#define ROTL(x,n) (((x)<<(n))|((x)>>(32-(n))))
static void chacha20_block(const uint32_t in[16], uint32_t out[16]){
    int i; for(i=0;i<16;i++) out[i]=in[i];
    for(i=0;i<10;i++){
        #define QRR(a,b,c,d) out[a]+=out[b];out[d]^=out[a];out[d]=ROTL(out[d],16); \
                              out[c]+=out[d];out[b]^=out[c];out[b]=ROTL(out[b],12); \
                              out[a]+=out[b];out[d]^=out[a];out[d]=ROTL(out[d],8); \
                              out[c]+=out[d];out[b]^=out[c];out[b]=ROTL(out[b],7);
        QRR(0,4,8,12)QRR(1,5,9,13)QRR(2,6,10,14)QRR(3,7,11,15)
        QRR(0,5,10,15)QRR(1,6,11,12)QRR(2,7,8,13)QRR(3,4,9,14)
    }
    for(i=0;i<16;i++) out[i]+=in[i];
}

void chacha20_xor(const uint8_t *key,const uint8_t *nonce,uint32_t ctr,
                  uint8_t *data,int len){
    uint32_t st[16],ks[16];
    st[0]=0x61707865u;st[1]=0x3320646eu;st[2]=0x79622d32u;st[3]=0x6b206574u;
    for(int i=0;i<8;i++) st[4+i]=(uint32_t)key[i*4]|((uint32_t)key[i*4+1]<<8)|
                                   ((uint32_t)key[i*4+2]<<16)|((uint32_t)key[i*4+3]<<24);
    st[12]=ctr;
    st[13]=(uint32_t)nonce[0]|((uint32_t)nonce[1]<<8)|((uint32_t)nonce[2]<<16)|((uint32_t)nonce[3]<<24);
    st[14]=(uint32_t)nonce[4]|((uint32_t)nonce[5]<<8)|((uint32_t)nonce[6]<<16)|((uint32_t)nonce[7]<<24);
    st[15]=(uint32_t)nonce[8]|((uint32_t)nonce[9]<<8)|((uint32_t)nonce[10]<<16)|((uint32_t)nonce[11]<<24);
    int pos=0;
    while(pos<len){
        chacha20_block(st,ks); st[12]++;
        int blk=len-pos>64?64:len-pos;
        for(int i=0;i<blk;i++) data[pos+i]^=((uint8_t*)ks)[i];
        pos+=blk;
    }
}

/* ── TOTP ────────────────────────────────────────────────────────────────── */
uint32_t totp_generate(const uint8_t *secret,int slen,
                       uint64_t unix_sec,int period,int digits){
    uint64_t T=unix_sec/period;
    uint8_t msg[8];
    for(int i=7;i>=0;i--){msg[i]=(uint8_t)(T&0xFF);T>>=8;}
    uint8_t mac[32];
    hmac_sha256(secret,slen,msg,8,mac);
    int off=mac[31]&0x0F;
    uint32_t code=((uint32_t)(mac[off]&0x7F)<<24)|((uint32_t)mac[off+1]<<16)|
                  ((uint32_t)mac[off+2]<<8)|(uint32_t)mac[off+3];
    uint32_t mod=1; for(int i=0;i<digits;i++) mod*=10;
    return code%mod;
}

/* ── Base32 decode ──────────────────────────────────────────────────────── */
int base32_decode(const char *b32,uint8_t *out,int max){
    static const int8_t t[256]={
        [' ']=0,['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,
        ['H']=7,['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,
        ['P']=15,['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,
        ['X']=23,['Y']=24,['Z']=25,['2']=26,['3']=27,['4']=28,['5']=29,['6']=30,['7']=31,
    };
    int n=0; uint32_t acc=0; int bits=0;
    for(int i=0;b32[i]&&b32[i]!='='&&n<max;i++){
        unsigned char c=(unsigned char)b32[i];
        if(c>='a'&&c<='z') c=c-'a'+'A';
        int v=t[c]; acc=(acc<<5)|v; bits+=5;
        if(bits>=8){bits-=8;out[n++]=(uint8_t)(acc>>bits);}
    }
    return n;
}

/* ── ed25519 keypair stub ────────────────────────────────────────────────── */
void ed25519_generate_keypair(const uint8_t *seed32, uint8_t pub[32], uint8_t priv[64]) {
    /* Store seed in first 32 bytes of "private" key — full ed25519 would expand here */
    for(int i=0;i<32;i++) priv[i]=seed32[i];
    /* SHA-256 of seed as public stub (real impl uses curve25519 scalar mult) */
    sha256(seed32, 32, pub);
    for(int i=0;i<32;i++) priv[32+i]=pub[i];
}
