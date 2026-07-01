/**
 * @file main.c
 * @brief VAULT — Unified Hardware Security Suite for AkiraOS
 *
 * Three sections in one app, one home screen:
 *
 *   TOTP      RFC 6238 time-based 2FA codes (like Google Authenticator)
 *   SSH       Ed25519 SSH signing agent over UART (ssh-agent protocol)
 *   PASSKEYS  FIDO2 / WebAuthn hardware authenticator (Ed25519, CTAP2)
 *
 * Navigation
 * ──────────
 *   HOME        LEFT / RIGHT   choose section
 *               A              open section
 *   Any section B              back to HOME
 *   Approve     A              confirm    B  deny/cancel
 *
 * TOTP provisioning (until QR scan lands):
 *   akira-cli settings set vault/count 1
 *   akira-cli settings set vault/0/n  "Gmail"
 *   akira-cli settings set vault/0/s  "JBSWY3DPEHPK3PXP"
 *
 * SSH keys — generated on-device (A button in SSH section).
 * Passkeys  — created by browser via CTAP2; requires AkiraOS ≥ 1.7
 *             (hid.fido2 capability). Credential management UI is fully
 *             functional regardless of kernel version.
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"
#include "../../common/akira_ui.h"
#include <stdint.h>
#include <stddef.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Feature flag — FIDO2 CTAP2 HID transport
 * Set to 1 when AkiraOS kernel 1.7 ships hid_fido_init / hid_fido_recv /
 * hid_fido_send. Until then we only manage stored credentials; no new ones
 * can be created (the browser needs the CTAP channel for that).
 * ═══════════════════════════════════════════════════════════════════════════ */
#define VAULT_FIDO_CTAP_ENABLED 1

/* ═══════════════════════════════════════════════════════════════════════════
 * Display geometry
 * ═══════════════════════════════════════════════════════════════════════════ */
static int32_t SCR_W = 320; /* runtime display size — set in main() */
static int32_t SCR_H = 240;
#define HDR_H    20
#define FOOT_Y  220

/* ═══════════════════════════════════════════════════════════════════════════
 * Monochrome palette — Sharp Memory LCD (1-bit)
 *   All drawing uses only two values.
 *   Hierarchy is expressed through fill vs. outline and inversion,
 *   not through color.
 * ═══════════════════════════════════════════════════════════════════════════ */
#define M_BG  0x0000U   /* black — background, dark regions   */
#define M_FG  0xFFFFU   /* white — text, lines, fills          */

/* Alias every old color slot to one of the two values */
#define C_BG   M_BG
#define C_HDR  M_BG
#define C_SEP  M_FG
#define C_ACC  M_FG
#define C_TXT  M_FG
#define C_DIM  M_FG
#define C_SEL  M_FG   /* selected-row / selected-card fill */
#define C_OK   M_FG
#define C_WARN M_FG
#define C_ERR  M_FG

/* ═══════════════════════════════════════════════════════════════════════════
 * Top-level state machine
 * ═══════════════════════════════════════════════════════════════════════════ */
#define ST_HOME          0
#define ST_TOTP_LIST     1
#define ST_TOTP_VIEW     2
#define ST_SSH_LIST      3
#define ST_SSH_APPROVE   4
#define ST_SSH_DETAIL    5
#define ST_SSH_KEYGEN    6
#define ST_SSH_DEL_DLG   7
#define ST_FIDO_LIST     8
#define ST_FIDO_DETAIL   9
#define ST_FIDO_DEL_DLG 10
#define ST_FIDO_APPROVE 11

/* Home section indices */
#define SEC_TOTP 0
#define SEC_SSH  1
#define SEC_FIDO 2

/* ═══════════════════════════════════════════════════════════════════════════
 * Home card geometry
 *   3 cards × 86px wide + 2 × 21px gap + 2 × 10px margin = 320px
 * ═══════════════════════════════════════════════════════════════════════════ */
#define CARD_W    86
#define CARD_H   150
#define CARD_Y    34
#define CARD_R     8
#define CARD_X0   10
#define CARD_X1  117
#define CARD_X2  224

static const int CARD_X[3] = { CARD_X0, CARD_X1, CARD_X2 };
/* Centre x of each card */
#define CARD_CX0  (CARD_X0 + CARD_W/2)   /* 53  */
#define CARD_CX1  (CARD_X1 + CARD_W/2)   /* 160 */
#define CARD_CX2  (CARD_X2 + CARD_W/2)   /* 267 */
static const int CARD_CX[3] = { CARD_CX0, CARD_CX1, CARD_CX2 };

/* ═══════════════════════════════════════════════════════════════════════════
 * TOTP limits
 * ═══════════════════════════════════════════════════════════════════════════ */
#define TOTP_MAX_AC    16
#define TOTP_NM_MAX    32
#define TOTP_SC_MAX    64
#define TOTP_KEY_MAX   40
#define TOTP_PERIOD    30
#define TOTP_VROWS      8
#define TOTP_ROW_H     20

/* ═══════════════════════════════════════════════════════════════════════════
 * SSH limits
 * ═══════════════════════════════════════════════════════════════════════════ */
#define SSH_MAX_KEYS    4
#define SSH_NM_MAX     32
#define SSH_RX_SZ    4096
#define SSH_SIGN_SZ  2048
#define SSH_TX_SZ     512
#define SSH_VROWS       6
#define SSH_ROW_H      22
#define SSH_SIGN_TIMEOUT_MS 30000

/* SSH agent message types */
#define SSH_AGENT_FAILURE                 5
#define SSH2_AGENT_IDENTITIES_ANSWER     12
#define SSH2_AGENT_SIGN_RESPONSE         14
#define SSH2_AGENTC_REQUEST_IDENTITIES   11
#define SSH2_AGENTC_SIGN_REQUEST         13

/* ═══════════════════════════════════════════════════════════════════════════
 * FIDO2 limits
 * ═══════════════════════════════════════════════════════════════════════════ */
#define FIDO_MAX_CREDS  8
#define FIDO_RPID_MAX  64
#define FIDO_UID_MAX   32
#define FIDO_UNM_MAX   32
#define FIDO_VROWS      6
#define FIDO_ROW_H     22
#define FIDO_APPROVE_TIMEOUT_MS 30000
#define FIDO_NVS_COUNT "vault/fido/count"

/* ── CTAP2 / HID transport ── */
/* AkiraOS kernel exports — registered in akira_export_api.c */
extern int hid_fido_recv(void *buf, uint32_t len);
extern int hid_fido_send(const void *buf, uint32_t len);

/* CTAPHID framing */
#define CTAPHID_PING      0x01u
#define CTAPHID_MSG       0x03u
#define CTAPHID_INIT      0x06u
#define CTAPHID_CANCEL    0x11u
#define CTAPHID_ERROR     0x3Fu
#define CTAPHID_CMD_BIT   0x80u
#define CTAPHID_BROADCAST 0xFFFFFFFFUL

/* CTAPHID error codes */
#define CTAP1_ERR_INVALID_CMD 0x01u
#define CTAP1_ERR_INVALID_PAR 0x02u
#define CTAP1_ERR_INVALID_LEN 0x03u

/* CTAP2 status codes */
#define CTAP2_OK           0x00u
#define CTAP2_ERR_CBOR     0x12u
#define CTAP2_ERR_DENIED   0x27u
#define CTAP2_ERR_NO_CREDS 0x2Eu
#define CTAP2_ERR_CANCEL   0x2Du
#define CTAP2_ERR_NO_ALG   0x26u  /* CTAP2_ERR_UNSUPPORTED_ALGORITHM */

/* CTAP2 authenticator commands */
#define CTAP2_CMD_MAKE_CRED  0x01u
#define CTAP2_CMD_GET_ASSERT 0x02u
#define CTAP2_CMD_GET_INFO   0x04u

/* AkiraOS FIDO2 AAGUID — "AKIRAOS_VAULT001" */
static const uint8_t FIDO_AAGUID[16] = {
    0x41,0x4B,0x49,0x52, 0x41,0x4F,0x53,0x5F,
    0x56,0x41,0x55,0x4C, 0x54,0x30,0x30,0x31
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Ring LUT — sin/cos × 100, 60 segments (6° each), 12-o'clock→clockwise
 * ═══════════════════════════════════════════════════════════════════════════ */
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Global state
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Core */
static int      g_state;
static int      g_home_sel;       /* 0 = TOTP, 1 = SSH, 2 = FIDO */
static int      g_home_prev_sel;
static uint8_t  g_anim;           /* 0 = settled; 1..6 = animating */
static uint8_t  g_enter_anim;     /* enter-section zoom animation */
static uint32_t g_tick;           /* always incrementing frame counter */
static uint32_t g_prev_btns;
static int      g_tmr;

/* TOTP */
static char     g_tn[TOTP_MAX_AC][TOTP_NM_MAX];   /* account names */
static char     g_ts[TOTP_MAX_AC][TOTP_SC_MAX];   /* Base32 secrets */
static char     g_tc[TOTP_MAX_AC][8];              /* "123456\0" codes */
static int      g_t_count;
static int      g_t_cursor;
static int      g_t_scroll;
static uint32_t g_t_last_T;

/* SSH */
static char     g_sn [SSH_MAX_KEYS][SSH_NM_MAX];
static uint8_t  g_spk[SSH_MAX_KEYS][32];           /* Ed25519 public key */
static uint8_t  g_se [SSH_MAX_KEYS][32];           /* encrypted seed */
static uint8_t  g_swk[SSH_MAX_KEYS][32];           /* AES-256 wrap key */
static uint8_t  g_siv[SSH_MAX_KEYS][16];           /* AES IV */
static int      g_s_count;
static int      g_s_cursor;
static int      g_s_scroll;
static int      g_s_uart;
static uint8_t  g_s_rx[SSH_RX_SZ];
static uint32_t g_s_rx_len;
static int      g_s_sign_idx;
static uint8_t  g_s_sign_data[SSH_SIGN_SZ];
static uint32_t g_s_sign_len;
static uint32_t g_s_sign_t;
static char     g_s_fp[52];                        /* "SHA256:..." */

/* FIDO2 */
static char     g_frpid[FIDO_MAX_CREDS][FIDO_RPID_MAX];
static char     g_fuid [FIDO_MAX_CREDS][FIDO_UID_MAX];
static char     g_funm [FIDO_MAX_CREDS][FIDO_UNM_MAX];
static uint8_t  g_fpk  [FIDO_MAX_CREDS][32];
static uint8_t  g_fe   [FIDO_MAX_CREDS][32];
static uint8_t  g_fwk  [FIDO_MAX_CREDS][32];
static uint8_t  g_fiv  [FIDO_MAX_CREDS][16];
static uint32_t g_fsc  [FIDO_MAX_CREDS];           /* sign counter */
static int      g_f_count;
static int      g_f_cursor;
static int      g_f_scroll;
/* pending approval (from CTAP2) */
static uint8_t  g_f_req_type;   /* 1=makeCredential, 2=getAssertion */
static char     g_f_p_rpid[FIDO_RPID_MAX];
static char     g_f_p_unm [FIDO_UNM_MAX];
static char     g_f_p_uid [FIDO_UID_MAX];   /* stored as text until CTAP2 lands */
static uint8_t  g_f_p_hash[32];
static int      g_f_p_key_idx;
static uint32_t g_f_req_t;

/* CTAP2 HID transport state */
static uint32_t g_ctap_cid;           /* allocated channel (0 = none) */
static uint32_t g_ctap_rx_cid;        /* CID of reassembly in progress */
static uint8_t  g_ctap_rx_cmd;
static uint16_t g_ctap_rx_total;
static uint16_t g_ctap_rx_len;
static uint8_t  g_ctap_rx_seq;
static uint8_t  g_ctap_rx_buf[1024];  /* reassembled CTAP2 payload */
static uint8_t  g_ctap_cbor_buf[512]; /* response scratch buffer */
static uint32_t g_ctap_pending_cid;   /* channel awaiting user approval */

/* ═══════════════════════════════════════════════════════════════════════════
 * String helpers  (no stdlib)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int slen(const char *s) { int n=0; while(s[n]) n++; return n; }

static void sncopy(char *d, const char *s, int max)
{ int i=0; while(i<max-1 && s[i]){d[i]=s[i];i++;} d[i]='\0'; }

static int str_to_int(const char *s)
{ int v=0; while(*s>='0'&&*s<='9')v=v*10+(*s++)-'0'; return v; }

static void int_to_str(char *b, int v)
{
    if(!v){b[0]='0';b[1]='\0';return;}
    char t[12]; int n=0;
    while(v>0){t[n++]='0'+v%10;v/=10;}
    int i; for(i=0;i<n;i++) b[i]=t[n-1-i]; b[n]='\0';
}

static const char HEX[]="0123456789abcdef";

static void bin_to_hex(char *hex, const uint8_t *bin, int len)
{ for(int i=0;i<len;i++){hex[i*2]=HEX[bin[i]>>4];hex[i*2+1]=HEX[bin[i]&0xF];}
  hex[len*2]='\0'; }

static int hd(char c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return 0;
}
static void hex_to_bin(uint8_t *bin, const char *hex, int n)
{ for(int i=0;i<n;i++) bin[i]=(uint8_t)((hd(hex[i*2])<<4)|hd(hex[i*2+1])); }

static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64_encode_nopad(char *out, const uint8_t *in, int len)
{
    int i=0,j=0;
    while(i<len){
        uint32_t a=in[i++];
        uint32_t b=(i<len)?in[i++]:0;
        uint32_t c=(i<len)?in[i++]:0;
        out[j++]=B64[(a>>2)&0x3F];
        out[j++]=B64[((a<<4)|(b>>4))&0x3F];
        out[j++]=B64[((b<<2)|(c>>6))&0x3F];
        out[j++]=B64[c&0x3F];
    }
    if(len%3==1) j-=2;
    else if(len%3==2) j-=1;
    out[j]='\0';
}

/* "123456" → 7-char "123 456" for readability */
static void fmt6_spaced(char *dst, const char *src)
{ dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];dst[3]=' ';
  dst[4]=src[3];dst[5]=src[4];dst[6]=src[5];dst[7]='\0'; }

static void fmt6(char *buf, uint32_t v)
{ buf[6]='\0'; for(int i=5;i>=0;i--){buf[i]='0'+(int)(v%10);v/=10;} }

/* Linear interpolation */
static int lerp(int a, int b, int num, int den)
{ return a + (b-a)*num/den; }

/* ═══════════════════════════════════════════════════════════════════════════
 * SHA-1  (RFC 3174) — used by HMAC-SHA1 for TOTP
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct { uint32_t h[5]; uint8_t blk[64]; uint32_t bl; uint32_t tb; } sha1_t;
#define ROL32(n,x) (((x)<<(n))|((x)>>(32-(n))))

static void sha1_compress(sha1_t *c)
{
    uint32_t w[80]; int i;
    for(i=0;i<16;i++) w[i]=((uint32_t)c->blk[i*4]<<24)|((uint32_t)c->blk[i*4+1]<<16)
                           |((uint32_t)c->blk[i*4+2]<<8)|(uint32_t)c->blk[i*4+3];
    for(i=16;i<80;i++) w[i]=ROL32(1,w[i-3]^w[i-8]^w[i-14]^w[i-16]);
    uint32_t a=c->h[0],b=c->h[1],d=c->h[2],e=c->h[3],f=c->h[4];
    for(i=0;i<80;i++){
        uint32_t fn,k;
        if(i<20){fn=(b&d)|(~b&e);k=0x5A827999U;}
        else if(i<40){fn=b^d^e;k=0x6ED9EBA1U;}
        else if(i<60){fn=(b&d)|(b&e)|(d&e);k=0x8F1BBCDCU;}
        else{fn=b^d^e;k=0xCA62C1D6U;}
        uint32_t tmp=ROL32(5,a)+fn+f+k+w[i];
        f=e;e=d;d=ROL32(30,b);b=a;a=tmp;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=d;c->h[3]+=e;c->h[4]+=f;
}
static void sha1_init(sha1_t *c)
{ c->h[0]=0x67452301U;c->h[1]=0xEFCDAB89U;c->h[2]=0x98BADCFEU;
  c->h[3]=0x10325476U;c->h[4]=0xC3D2E1F0U;c->bl=0;c->tb=0; }
static void sha1_update(sha1_t *c, const uint8_t *data, uint32_t len)
{ for(uint32_t i=0;i<len;i++){c->blk[c->bl++]=data[i];c->tb++;
  if(c->bl==64){sha1_compress(c);c->bl=0;}} }
static void sha1_final(sha1_t *c, uint8_t out[20])
{
    uint64_t bits=(uint64_t)c->tb*8U;
    c->blk[c->bl++]=0x80;
    if(c->bl>56){while(c->bl<64)c->blk[c->bl++]=0;sha1_compress(c);c->bl=0;}
    while(c->bl<56)c->blk[c->bl++]=0;
    for(int i=0;i<8;i++) c->blk[56+i]=(uint8_t)(bits>>((7-i)*8));
    sha1_compress(c);
    for(int i=0;i<5;i++){out[i*4]=(uint8_t)(c->h[i]>>24);out[i*4+1]=(uint8_t)(c->h[i]>>16);
        out[i*4+2]=(uint8_t)(c->h[i]>>8);out[i*4+3]=(uint8_t)c->h[i];}
}

static void hmac_sha1(const uint8_t *key, uint32_t kl,
                      const uint8_t *msg, uint32_t ml, uint8_t out[20])
{
    sha1_t ctx; uint8_t k[64],pad[64],inner[20];
    if(kl>64){ sha1_init(&ctx);sha1_update(&ctx,key,kl);sha1_final(&ctx,k);
               for(int i=20;i<64;i++)k[i]=0; }
    else{ uint32_t i; for(i=0;i<kl;i++)k[i]=key[i]; for(;i<64;i++)k[i]=0; }
    for(int i=0;i<64;i++) pad[i]=k[i]^0x36;
    sha1_init(&ctx); sha1_update(&ctx,pad,64); sha1_update(&ctx,msg,ml); sha1_final(&ctx,inner);
    for(int i=0;i<64;i++) pad[i]=k[i]^0x5C;
    sha1_init(&ctx); sha1_update(&ctx,pad,64); sha1_update(&ctx,inner,20); sha1_final(&ctx,out);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Base32 decode  (RFC 4648)
 * ═══════════════════════════════════════════════════════════════════════════ */
static int b32v(char c){
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a';
    if(c>='2'&&c<='7') return 26+(c-'2');
    return -1;
}
static int b32_decode(const char *src, uint8_t *dst, int max)
{
    uint32_t acc=0; int bits=0,out=0;
    for(int i=0;src[i]&&src[i]!='=';i++){
        int v=b32v(src[i]); if(v<0) continue;
        acc=(acc<<5)|(uint32_t)v; bits+=5;
        if(bits>=8){ bits-=8; if(out>=max) return -1;
                     dst[out++]=(uint8_t)(acc>>bits); acc&=(1u<<bits)-1u; }
    }
    return out;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TOTP  (RFC 6238 / RFC 4226)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void totp_compute(const char *b32, uint32_t T, char code[8])
{
    uint8_t key[TOTP_KEY_MAX];
    int kl=b32_decode(b32,key,TOTP_KEY_MAX);
    if(kl<=0){code[0]='?';code[1]='\0';return;}
    uint8_t ctr[8]={0,0,0,0,(uint8_t)(T>>24),(uint8_t)(T>>16),(uint8_t)(T>>8),(uint8_t)T};
    uint8_t hmac[20]; hmac_sha1(key,(uint32_t)kl,ctr,8,hmac);
    int off=hmac[19]&0x0F;
    uint32_t otp=(((uint32_t)(hmac[off]&0x7F)<<24)|((uint32_t)hmac[off+1]<<16)
                 |((uint32_t)hmac[off+2]<<8)|(uint32_t)hmac[off+3])%1000000U;
    fmt6(code,otp);
}
static void totp_refresh(uint32_t T)
{ for(int i=0;i<g_t_count;i++) totp_compute(g_ts[i],T,g_tc[i]); g_t_last_T=T; }

/* ═══════════════════════════════════════════════════════════════════════════
 * NVS helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* TOTP */
static void totp_load(void)
{
    char buf[8]; g_t_count=0;
    if(settings_get("vault/count",buf,(int32_t)sizeof(buf))!=0) return;
    int n=str_to_int(buf); if(n<=0||n>TOTP_MAX_AC) return;
    char key[16];
    for(int i=0;i<n;i++){
        key[0]='v';key[1]='a';key[2]='u';key[3]='l';key[4]='t';key[5]='/';
        key[6]='0'+i/10;key[7]='0'+i%10;key[8]='/';
        key[9]='n';key[10]='\0';
        if(settings_get(key,g_tn[i],TOTP_NM_MAX)!=0) continue;
        key[9]='s';key[10]='\0';
        if(settings_get(key,g_ts[i],TOTP_SC_MAX)!=0) continue;
        g_t_count++;
    }
}

/* SSH NVS key builder: "vault/ssh/N/field" */
static void ssh_nk(char *buf, int idx, const char *f)
{
    buf[0]='v';buf[1]='a';buf[2]='u';buf[3]='l';buf[4]='t';buf[5]='/';
    buf[6]='s';buf[7]='s';buf[8]='h';buf[9]='/';buf[10]='0'+idx;buf[11]='/';
    int i=12; while(*f) buf[i++]=*f++; buf[i]='\0';
}
static void ssh_save_key(int idx)
{
    char key[24],hex[65];
    char cnt[4]; int_to_str(cnt,g_s_count); settings_set("vault/ssh/count",cnt);
    ssh_nk(key,idx,"name"); settings_set(key,g_sn[idx]);
    ssh_nk(key,idx,"wk");  bin_to_hex(hex,g_swk[idx],32); settings_set(key,hex);
    ssh_nk(key,idx,"iv");  bin_to_hex(hex,g_siv[idx],16); hex[32]='\0'; settings_set(key,hex);
    ssh_nk(key,idx,"enc"); bin_to_hex(hex,g_se[idx],32);  settings_set(key,hex);
    ssh_nk(key,idx,"pub"); bin_to_hex(hex,g_spk[idx],32); settings_set(key,hex);
}
static void ssh_load(void)
{
    char buf[65],key[24]; g_s_count=0;
    if(settings_get("vault/ssh/count",buf,(int32_t)sizeof(buf))!=0) return;
    int n=str_to_int(buf); if(n<0||n>SSH_MAX_KEYS) return;
    for(int i=0;i<n;i++){
        ssh_nk(key,i,"name"); if(settings_get(key,g_sn[i],SSH_NM_MAX)!=0) continue;
        ssh_nk(key,i,"wk");   if(settings_get(key,buf,(int32_t)sizeof(buf))!=0) continue;
        hex_to_bin(g_swk[i],buf,32);
        ssh_nk(key,i,"iv");   if(settings_get(key,buf,(int32_t)sizeof(buf))!=0) continue;
        hex_to_bin(g_siv[i],buf,16);
        ssh_nk(key,i,"enc");  if(settings_get(key,buf,(int32_t)sizeof(buf))!=0) continue;
        hex_to_bin(g_se[i],buf,32);
        ssh_nk(key,i,"pub");  if(settings_get(key,buf,(int32_t)sizeof(buf))!=0) continue;
        hex_to_bin(g_spk[i],buf,32);
        g_s_count++;
    }
}

/* FIDO NVS key builder: "vault/fido/N/field" */
static void fido_nk(char *buf, int idx, const char *f)
{
    buf[0]='v';buf[1]='a';buf[2]='u';buf[3]='l';buf[4]='t';buf[5]='/';
    buf[6]='f';buf[7]='i';buf[8]='d';buf[9]='o';buf[10]='/';buf[11]='0'+idx;buf[12]='/';
    int i=13; while(*f) buf[i++]=*f++; buf[i]='\0';
}
static void fido_save_cred(int idx)
{
    char key[28],hex[65];
    char cnt[4]; int_to_str(cnt,g_f_count); settings_set(FIDO_NVS_COUNT,cnt);
    fido_nk(key,idx,"rp");   settings_set(key,g_frpid[idx]);
    fido_nk(key,idx,"uid");  settings_set(key,g_fuid[idx]);
    fido_nk(key,idx,"unm");  settings_set(key,g_funm[idx]);
    fido_nk(key,idx,"wk");   bin_to_hex(hex,g_fwk[idx],32); settings_set(key,hex);
    fido_nk(key,idx,"iv");   bin_to_hex(hex,g_fiv[idx],16); hex[32]='\0'; settings_set(key,hex);
    fido_nk(key,idx,"enc");  bin_to_hex(hex,g_fe[idx],32);  settings_set(key,hex);
    fido_nk(key,idx,"pub");  bin_to_hex(hex,g_fpk[idx],32); settings_set(key,hex);
    char sc[12]; int_to_str(sc,(int)g_fsc[idx]);
    fido_nk(key,idx,"sc");   settings_set(key,sc);
}
static void fido_load(void)
{
    char buf[65],key[28]; g_f_count=0;
    if(settings_get(FIDO_NVS_COUNT,buf,(int32_t)sizeof(buf))!=0) return;
    int n=str_to_int(buf); if(n<0||n>FIDO_MAX_CREDS) return;
    for(int i=0;i<n;i++){
        fido_nk(key,i,"rp");  if(settings_get(key,g_frpid[i],FIDO_RPID_MAX)!=0) continue;
        fido_nk(key,i,"uid"); if(settings_get(key,g_fuid[i],FIDO_UID_MAX)!=0) continue;
        fido_nk(key,i,"unm"); if(settings_get(key,g_funm[i],FIDO_UNM_MAX)!=0) continue;
        fido_nk(key,i,"wk");  if(settings_get(key,buf,(int32_t)sizeof(buf))!=0) continue;
        hex_to_bin(g_fwk[i],buf,32);
        fido_nk(key,i,"iv");  if(settings_get(key,buf,(int32_t)sizeof(buf))!=0) continue;
        hex_to_bin(g_fiv[i],buf,16);
        fido_nk(key,i,"enc"); if(settings_get(key,buf,(int32_t)sizeof(buf))!=0) continue;
        hex_to_bin(g_fe[i],buf,32);
        fido_nk(key,i,"pub"); if(settings_get(key,buf,(int32_t)sizeof(buf))!=0) continue;
        hex_to_bin(g_fpk[i],buf,32);
        fido_nk(key,i,"sc");  if(settings_get(key,buf,(int32_t)sizeof(buf))!=0){g_fsc[i]=0;}
        else g_fsc[i]=(uint32_t)str_to_int(buf);
        g_f_count++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SSH wire protocol helpers
 * ═══════════════════════════════════════════════════════════════════════════ */
static uint32_t ru32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void wu32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
static int wstr(uint8_t *p, const uint8_t *d, uint32_t l)
{ wu32(p,l); for(uint32_t i=0;i<l;i++) p[4+i]=d[i]; return (int)(4+l); }

static int build_key_blob(uint8_t *buf, const uint8_t *pk)
{
    static const uint8_t TAG[]={'s','s','h','-','e','d','2','5','5','1','9'};
    int n=0; n+=wstr(buf+n,TAG,11); n+=wstr(buf+n,pk,32); return n;
}
static int build_sig_blob(uint8_t *buf, const uint8_t *sig)
{
    static const uint8_t TAG[]={'s','s','h','-','e','d','2','5','5','1','9'};
    int n=0; n+=wstr(buf+n,TAG,11); n+=wstr(buf+n,sig,64); return n;
}
static void ssh_send_failure(void)
{ uint8_t p[5]; wu32(p,1); p[4]=SSH_AGENT_FAILURE; uart_write(g_s_uart,p,5); }

static void ssh_handle_identities(void)
{
    uint8_t msg[SSH_TX_SZ]; uint32_t pos=1;
    msg[pos++]=SSH2_AGENT_IDENTITIES_ANSWER;
    wu32(msg+pos,(uint32_t)g_s_count); pos+=4;
    for(int i=0;i<g_s_count;i++){
        uint8_t blob[51]; int bl=build_key_blob(blob,g_spk[i]);
        if(pos+4+(uint32_t)bl+4+(uint32_t)slen(g_sn[i])>=SSH_TX_SZ-4) break;
        pos+=(uint32_t)wstr(msg+pos,blob,(uint32_t)bl);
        pos+=(uint32_t)wstr(msg+pos,(const uint8_t*)g_sn[i],(uint32_t)slen(g_sn[i]));
    }
    wu32(msg,pos-4); uart_write(g_s_uart,msg,pos);
}

static void ssh_handle_sign_request(const uint8_t *payload, uint32_t pl)
{
    if(pl<8){ssh_send_failure();return;}
    uint32_t blob_len=ru32(payload);
    if(blob_len+8>pl){ssh_send_failure();return;}
    const uint8_t *blob=payload+4;
    int matched=-1;
    for(int i=0;i<g_s_count;i++){
        uint8_t exp[51]; build_key_blob(exp,g_spk[i]);
        int eq=1; for(uint32_t j=0;j<blob_len&&j<51;j++) if(blob[j]!=exp[j]){eq=0;break;}
        if(eq&&blob_len==51){matched=i;break;}
    }
    if(matched<0){ssh_send_failure();return;}
    const uint8_t *dp=payload+4+blob_len; uint32_t rem=pl-4-blob_len;
    if(rem<4){ssh_send_failure();return;}
    uint32_t dl=ru32(dp); dp+=4; rem-=4;
    if(dl>rem||dl>SSH_SIGN_SZ){ssh_send_failure();return;}
    g_s_sign_idx=matched;
    for(uint32_t i=0;i<dl;i++) g_s_sign_data[i]=dp[i];
    g_s_sign_len=dl;
    g_s_sign_t=(uint32_t)rtc_get_uptime_ms();
    g_state=ST_SSH_APPROVE;
}

static void ssh_dispatch(const uint8_t *msg, uint32_t len)
{
    if(!len){ssh_send_failure();return;}
    uint8_t type=msg[0];
    if(type==SSH2_AGENTC_REQUEST_IDENTITIES) { ssh_handle_identities(); return; }
    if(type==SSH2_AGENTC_SIGN_REQUEST){
        if(g_s_count==0){ssh_send_failure();return;}
        ssh_handle_sign_request(msg+1,len-1); return;
    }
    ssh_send_failure();
}

static void ssh_execute_sign(void)
{
    uint8_t seed[32];
    if(crypto_aes256_decrypt(g_swk[g_s_sign_idx],g_siv[g_s_sign_idx],
                              g_se[g_s_sign_idx],32,seed)!=0){ssh_send_failure();return;}
    uint8_t sig[64];
    int r=crypto_ed25519_sign(seed,g_s_sign_data,g_s_sign_len,sig);
    for(int i=0;i<32;i++) seed[i]=0;
    if(r!=0){ssh_send_failure();return;}
    uint8_t msg[SSH_TX_SZ]; uint32_t pos=4;
    msg[pos++]=SSH2_AGENT_SIGN_RESPONSE;
    uint8_t sb[83]; int sl=build_sig_blob(sb,sig);
    pos+=(uint32_t)wstr(msg+pos,sb,(uint32_t)sl);
    wu32(msg,pos-4); uart_write(g_s_uart,msg,pos);
}

static void ssh_poll_uart(void)
{
    if(g_s_uart<0) return;
    uint8_t tmp[64]; int n=uart_read(g_s_uart,tmp,(uint32_t)sizeof(tmp));
    if(n<=0) return;
    for(int i=0;i<n;i++) if(g_s_rx_len<SSH_RX_SZ) g_s_rx[g_s_rx_len++]=tmp[i];
    while(g_s_rx_len>=4){
        uint32_t ml=ru32(g_s_rx);
        if(ml==0||ml>SSH_RX_SZ-4){g_s_rx_len=0;break;}
        if(g_s_rx_len<4+ml) break;
        if(g_state!=ST_SSH_APPROVE) ssh_dispatch(g_s_rx+4,ml);
        else{
            uint8_t t=(ml>0)?g_s_rx[4]:0;
            if(t==SSH2_AGENTC_REQUEST_IDENTITIES) ssh_handle_identities();
            else ssh_send_failure();
        }
        uint32_t consumed=4+ml; g_s_rx_len-=consumed;
        for(uint32_t i=0;i<g_s_rx_len;i++) g_s_rx[i]=g_s_rx[consumed+i];
    }
}

static int ssh_generate_key(int idx, const char *name)
{
    uint8_t seed[32];
    if(crypto_ed25519_keygen(seed,g_spk[idx])!=0) return -1;
    crypto_random(g_swk[idx],32); crypto_random(g_siv[idx],16);
    if(crypto_aes256_encrypt(g_swk[idx],g_siv[idx],seed,32,g_se[idx])!=0){
        for(int i=0;i<32;i++) seed[i]=0; return -1;
    }
    for(int i=0;i<32;i++) seed[i]=0;
    sncopy(g_sn[idx],name,SSH_NM_MAX);
    if(idx>=g_s_count) g_s_count=idx+1;
    ssh_save_key(idx); return 0;
}

static void ssh_delete_key(int idx)
{
    char key[24];
    for(int i=0;i<32;i++){g_spk[idx][i]=0;g_se[idx][i]=0;g_swk[idx][i]=0;}
    for(int i=0;i<16;i++) g_siv[idx][i]=0;
    ssh_nk(key,idx,"name");settings_delete(key); ssh_nk(key,idx,"wk");settings_delete(key);
    ssh_nk(key,idx,"iv");settings_delete(key);  ssh_nk(key,idx,"enc");settings_delete(key);
    ssh_nk(key,idx,"pub");settings_delete(key);
    for(int i=idx;i<g_s_count-1;i++){
        sncopy(g_sn[i],g_sn[i+1],SSH_NM_MAX);
        for(int j=0;j<32;j++){g_spk[i][j]=g_spk[i+1][j];g_se[i][j]=g_se[i+1][j];
                               g_swk[i][j]=g_swk[i+1][j];}
        for(int j=0;j<16;j++) g_siv[i][j]=g_siv[i+1][j];
        ssh_save_key(i);
    }
    g_s_count--;
    char cnt[4]; int_to_str(cnt,g_s_count); settings_set("vault/ssh/count",cnt);
    ssh_nk(key,g_s_count,"name");settings_delete(key); ssh_nk(key,g_s_count,"wk");settings_delete(key);
    ssh_nk(key,g_s_count,"iv");settings_delete(key); ssh_nk(key,g_s_count,"enc");settings_delete(key);
    ssh_nk(key,g_s_count,"pub");settings_delete(key);
    if(g_s_cursor>=g_s_count&&g_s_cursor>0) g_s_cursor--;
}

static void ssh_compute_fingerprint(int idx)
{
    uint8_t blob[51];
    blob[0]=0;blob[1]=0;blob[2]=0;blob[3]=11;
    blob[4]='s';blob[5]='s';blob[6]='h';blob[7]='-';blob[8]='e';
    blob[9]='d';blob[10]='2';blob[11]='5';blob[12]='5';blob[13]='1';blob[14]='9';
    blob[15]=0;blob[16]=0;blob[17]=0;blob[18]=32;
    for(int i=0;i<32;i++) blob[19+i]=g_spk[idx][i];
    uint8_t digest[32]; crypto_sha256(blob,51,digest);
    g_s_fp[0]='S';g_s_fp[1]='H';g_s_fp[2]='A';g_s_fp[3]='2';
    g_s_fp[4]='5';g_s_fp[5]='6';g_s_fp[6]=':';
    b64_encode_nopad(g_s_fp+7,digest,32);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FIDO2 — credential generation & deletion  (CTAP channel is kernel-gated)
 * ═══════════════════════════════════════════════════════════════════════════ */
static int fido_generate_cred(int idx, const char *rpid,
                               const char *uid, const char *unm)
{
    uint8_t seed[32];
    if(crypto_ed25519_keygen(seed,g_fpk[idx])!=0) return -1;
    crypto_random(g_fwk[idx],32); crypto_random(g_fiv[idx],16);
    if(crypto_aes256_encrypt(g_fwk[idx],g_fiv[idx],seed,32,g_fe[idx])!=0){
        for(int i=0;i<32;i++) seed[i]=0; return -1;
    }
    for(int i=0;i<32;i++) seed[i]=0;
    sncopy(g_frpid[idx],rpid,FIDO_RPID_MAX);
    sncopy(g_fuid [idx],uid, FIDO_UID_MAX);
    sncopy(g_funm [idx],unm, FIDO_UNM_MAX);
    g_fsc[idx]=0;
    if(idx>=g_f_count) g_f_count=idx+1;
    fido_save_cred(idx); return 0;
}

static void fido_delete_cred(int idx)
{
    char key[28];
    for(int i=0;i<32;i++){g_fpk[idx][i]=0;g_fe[idx][i]=0;g_fwk[idx][i]=0;}
    for(int i=0;i<16;i++) g_fiv[idx][i]=0;
    fido_nk(key,idx,"rp"); settings_delete(key); fido_nk(key,idx,"uid");settings_delete(key);
    fido_nk(key,idx,"unm");settings_delete(key); fido_nk(key,idx,"wk"); settings_delete(key);
    fido_nk(key,idx,"iv"); settings_delete(key); fido_nk(key,idx,"enc");settings_delete(key);
    fido_nk(key,idx,"pub");settings_delete(key); fido_nk(key,idx,"sc"); settings_delete(key);
    for(int i=idx;i<g_f_count-1;i++){
        sncopy(g_frpid[i],g_frpid[i+1],FIDO_RPID_MAX);
        sncopy(g_fuid [i],g_fuid [i+1],FIDO_UID_MAX);
        sncopy(g_funm [i],g_funm [i+1],FIDO_UNM_MAX);
        for(int j=0;j<32;j++){g_fpk[i][j]=g_fpk[i+1][j];g_fe[i][j]=g_fe[i+1][j];
                               g_fwk[i][j]=g_fwk[i+1][j];}
        for(int j=0;j<16;j++) g_fiv[i][j]=g_fiv[i+1][j];
        g_fsc[i]=g_fsc[i+1];
        fido_save_cred(i);
    }
    g_f_count--;
    char cnt[4]; int_to_str(cnt,g_f_count); settings_set(FIDO_NVS_COUNT,cnt);
    fido_nk(key,g_f_count,"rp"); settings_delete(key);
    fido_nk(key,g_f_count,"uid");settings_delete(key);
    fido_nk(key,g_f_count,"unm");settings_delete(key);
    fido_nk(key,g_f_count,"wk"); settings_delete(key);
    fido_nk(key,g_f_count,"iv"); settings_delete(key);
    fido_nk(key,g_f_count,"enc");settings_delete(key);
    fido_nk(key,g_f_count,"pub");settings_delete(key);
    fido_nk(key,g_f_count,"sc"); settings_delete(key);
    if(g_f_cursor>=g_f_count&&g_f_cursor>0) g_f_cursor--;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — CBOR encoder
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct { uint8_t *buf; uint16_t len; uint16_t cap; } cbor_t;

static void cb_byte(cbor_t *c, uint8_t b)
{ if(c->len < c->cap) c->buf[c->len++] = b; }

static void cb_head(cbor_t *c, uint8_t major, uint32_t val)
{
    uint8_t m = (uint8_t)(major << 5);
    if(val <= 23)          { cb_byte(c, m|(uint8_t)val); }
    else if(val <= 0xFFu)  { cb_byte(c, m|24u); cb_byte(c,(uint8_t)val); }
    else if(val <= 0xFFFFu){ cb_byte(c, m|25u);
                             cb_byte(c,(uint8_t)(val>>8));
                             cb_byte(c,(uint8_t)val); }
    else { cb_byte(c, m|26u);
           cb_byte(c,(uint8_t)(val>>24)); cb_byte(c,(uint8_t)(val>>16));
           cb_byte(c,(uint8_t)(val>>8));  cb_byte(c,(uint8_t)val); }
}
static void cb_uint (cbor_t *c, uint32_t v){ cb_head(c,0,v); }
static void cb_neg  (cbor_t *c, uint32_t v){ cb_head(c,1,v); } /* encodes -(v+1) */
static void cb_bytes(cbor_t *c, const uint8_t *d, uint16_t n)
{ cb_head(c,2,(uint32_t)n); for(uint16_t i=0;i<n;i++) cb_byte(c,d[i]); }
static void cb_text (cbor_t *c, const char *s)
{ uint16_t n=(uint16_t)slen(s); cb_head(c,3,(uint32_t)n);
  for(uint16_t i=0;i<n;i++) cb_byte(c,(uint8_t)s[i]); }
static void cb_array(cbor_t *c, uint16_t n){ cb_head(c,4,(uint32_t)n); }
static void cb_map  (cbor_t *c, uint16_t n){ cb_head(c,5,(uint32_t)n); }
static void cb_bool (cbor_t *c, int v)     { cb_byte(c, v ? 0xF5u : 0xF4u); }

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — CBOR decoder (minimal, targeted to CTAP2 request shapes)
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct { const uint8_t *p; uint16_t rem; } cbd_t;

/* Read one CBOR header. Returns major type (0-7) or -1 on error.
   *val receives the argument integer (count for arrays/maps, length for strings). */
static int cbd_head(cbd_t *d, uint64_t *val)
{
    if(!d->rem) return -1;
    uint8_t b = *d->p++; d->rem--;
    int     major = b >> 5;
    uint8_t info  = b & 0x1Fu;
    uint64_t v = 0;
    if     (info <= 23) { v = info; }
    else if(info == 24) { if(!d->rem) return -1; v = *d->p++; d->rem--; }
    else if(info == 25) { if(d->rem<2) return -1;
                          v=((uint64_t)d->p[0]<<8)|d->p[1]; d->p+=2; d->rem-=2; }
    else if(info == 26) { if(d->rem<4) return -1;
                          v=((uint64_t)d->p[0]<<24)|((uint64_t)d->p[1]<<16)|
                            ((uint64_t)d->p[2]<<8)|(uint64_t)d->p[3];
                          d->p+=4; d->rem-=4; }
    else return -1;
    if(val) *val = v;
    return major;
}

static uint16_t cbd_bytes(cbd_t *d, uint8_t *out, uint16_t max)
{
    uint64_t n; if(cbd_head(d,&n)!=2||n>d->rem) return 0;
    uint16_t copy=(uint16_t)(n>max?max:n);
    if(out) for(uint16_t i=0;i<(uint16_t)n;i++){ if(i<copy) out[i]=d->p[i]; }
    d->p+=(uint16_t)n; d->rem-=(uint16_t)n; return copy;
}
static uint16_t cbd_text(cbd_t *d, char *out, uint16_t max)
{
    uint64_t n; if(cbd_head(d,&n)!=3||n>d->rem) return 0;
    uint16_t copy=(uint16_t)(n>=(uint64_t)max?max-1:(uint16_t)n);
    if(out){ for(uint16_t i=0;i<(uint16_t)n;i++){if(i<copy)out[i]=(char)d->p[i];}
             out[copy]='\0'; }
    d->p+=(uint16_t)n; d->rem-=(uint16_t)n; return copy;
}
/* Skip one complete CBOR item (reads its own header). */
static void cbd_skip(cbd_t *d)
{
    uint64_t n; int m=cbd_head(d,&n);
    if(m<0) return;
    if(m==2||m==3){ uint16_t sz=(uint16_t)(n>d->rem?d->rem:(uint16_t)n);
                    d->p+=sz; d->rem-=sz; }
    else if(m==4)  { for(uint64_t i=0;i<n;i++) cbd_skip(d); }
    else if(m==5)  { for(uint64_t i=0;i<n*2u;i++) cbd_skip(d); }
    /* major 0,1,7 have no payload after the header */
}

/* Search a CBOR map for an unsigned-integer key.
   src is positioned at the MAP item (including its header).
   Returns 1 and sets *val_out to the decoder positioned at the value, or 0. */
static int cbd_map_uint(cbd_t *src, uint64_t key, cbd_t *val_out)
{
    cbd_t d=*src;
    uint64_t n; if(cbd_head(&d,&n)!=5) return 0;
    for(uint64_t i=0;i<n;i++){
        uint64_t k; int km=cbd_head(&d,&k);
        if(km==0&&k==key){ if(val_out)*val_out=d; return 1; }
        /* advance past key payload for byte/text keys, then skip value */
        if((km==2||km==3)&&k<=d.rem){ d.p+=(uint16_t)k; d.rem-=(uint16_t)k; }
        else if(km==4){ for(uint64_t j=0;j<k;j++) cbd_skip(&d); }
        else if(km==5){ for(uint64_t j=0;j<k*2u;j++) cbd_skip(&d); }
        cbd_skip(&d); /* skip value */
    }
    return 0;
}

/* Search a CBOR map for a text key.
   src is positioned at the MAP item (including its header). */
static int cbd_map_text(cbd_t *src, const char *key, cbd_t *val_out)
{
    cbd_t d=*src;
    int kl=slen(key);
    uint64_t n; if(cbd_head(&d,&n)!=5) return 0;
    for(uint64_t i=0;i<n;i++){
        uint64_t ksz; int km=cbd_head(&d,&ksz);
        if(km==3&&(int)ksz==kl&&ksz<=d.rem){
            int match=1;
            for(int j=0;j<kl;j++) if(d.p[j]!=(uint8_t)key[j]){match=0;break;}
            d.p+=(uint16_t)ksz; d.rem-=(uint16_t)ksz;
            if(match){ if(val_out)*val_out=d; return 1; }
        } else {
            if((km==2||km==3)&&ksz<=d.rem){ d.p+=(uint16_t)ksz; d.rem-=(uint16_t)ksz; }
            else if(km==4){ for(uint64_t j=0;j<ksz;j++) cbd_skip(&d); }
            else if(km==5){ for(uint64_t j=0;j<ksz*2u;j++) cbd_skip(&d); }
        }
        cbd_skip(&d); /* skip value */
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — authData builder
 *   flags 0x01 = UP, 0x40 = AT (include attested credential data)
 *   key_idx ignored when AT not set
 * ═══════════════════════════════════════════════════════════════════════════ */
static uint16_t ctap_build_authdata(uint8_t *out, uint16_t outsz,
                                     const char *rpid, uint8_t flags,
                                     uint32_t sign_cnt, int key_idx)
{
    if(outsz < 37u) return 0;
    uint16_t pos = 0;
    crypto_sha256(rpid,(uint32_t)slen(rpid), out+pos); pos+=32;
    out[pos++] = flags;
    out[pos++]=(uint8_t)(sign_cnt>>24); out[pos++]=(uint8_t)(sign_cnt>>16);
    out[pos++]=(uint8_t)(sign_cnt>>8);  out[pos++]=(uint8_t)sign_cnt;
    if(!(flags&0x40u)||key_idx<0) return pos;
    if(outsz < pos+18u+34u) return 0;
    /* AAGUID (16 bytes) */
    for(int i=0;i<16;i++) out[pos++]=FIDO_AAGUID[i];
    /* credentialIdLength (2 bytes BE) = 32 */
    out[pos++]=0; out[pos++]=32;
    /* credentialId = SHA256(pubkey) */
    crypto_sha256(g_fpk[key_idx],32, out+pos); pos+=32;
    /* COSE key: {1:1, 3:-8, -1:6, -2:pubkey} */
    cbor_t c; c.buf=out+pos; c.cap=(uint16_t)(outsz-pos); c.len=0;
    cb_map(&c,4);
    cb_uint(&c,1);  cb_uint(&c,1);               /* kty: OKP */
    cb_uint(&c,3);  cb_neg (&c,7u);              /* alg: -8 (EdDSA) */
    cb_neg (&c,0u); cb_uint(&c,6);               /* crv(-1): Ed25519 */
    cb_neg (&c,1u); cb_bytes(&c,g_fpk[key_idx],32); /* x(-2): pubkey */
    pos+=(uint16_t)c.len;
    return pos;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — HID packet framing (send side)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ctap_hid_send(uint32_t cid, uint8_t cmd,
                           const uint8_t *data, uint16_t len)
{
    uint8_t pkt[64];
    pkt[0]=(uint8_t)(cid>>24); pkt[1]=(uint8_t)(cid>>16);
    pkt[2]=(uint8_t)(cid>>8);  pkt[3]=(uint8_t)cid;
    pkt[4]=(uint8_t)(cmd|CTAPHID_CMD_BIT);
    pkt[5]=(uint8_t)(len>>8);  pkt[6]=(uint8_t)len;
    uint16_t pos=0, chunk=(len<57u)?len:57u;
    for(int i=0;i<57;i++) pkt[7+i]=(i<(int)chunk)?data[pos+i]:0u;
    pos+=chunk;
    hid_fido_send(pkt,64);
    uint8_t seq=0;
    while(pos<len){
        pkt[0]=(uint8_t)(cid>>24); pkt[1]=(uint8_t)(cid>>16);
        pkt[2]=(uint8_t)(cid>>8);  pkt[3]=(uint8_t)cid;
        pkt[4]=seq++;
        uint16_t rem=len-pos; chunk=(rem<59u)?rem:59u;
        for(int i=0;i<59;i++) pkt[5+i]=(i<(int)chunk)?data[pos+i]:0u;
        pos+=chunk;
        hid_fido_send(pkt,64);
    }
}
static void ctap_hid_error(uint32_t cid, uint8_t err)
{ ctap_hid_send(cid, CTAPHID_ERROR, &err, 1); }

/* CTAP2 MSG response: [status_byte][CBOR...] */
static void ctap2_respond(uint32_t cid, uint8_t status,
                           const uint8_t *cbor, uint16_t clen)
{
    uint8_t resp[513]; resp[0]=status;
    uint16_t n=(clen<512u)?clen:512u;
    for(uint16_t i=0;i<n;i++) resp[1+i]=cbor[i];
    ctap_hid_send(cid, CTAPHID_MSG, resp, 1u+n);
}
static void ctap2_error(uint32_t cid, uint8_t err)
{ ctap_hid_send(cid, CTAPHID_MSG, &err, 1); }

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — authenticatorGetInfo
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ctap2_get_info(uint32_t cid)
{
    cbor_t c; c.buf=g_ctap_cbor_buf; c.cap=512; c.len=0;
    /* CTAP 2.0 GetInfo — 4 keys: versions(0x01), aaguid(0x03),
     * options(0x04), maxMsgSize(0x05).
     * NOTE: key 0x06 is pinUvAuthProtocols (array of uint), NOT algorithms.
     *       key 0x0A is algorithms (CTAP 2.1 only).
     * Using incorrect key 0x06 with a map value causes E_INVALIDARG on Windows. */
    cb_map(&c,4);
    cb_uint(&c,1); cb_array(&c,1); cb_text(&c,"FIDO_2_0");  /* versions */
    cb_uint(&c,3); cb_bytes(&c,FIDO_AAGUID,16);             /* aaguid */
    cb_uint(&c,4); cb_map(&c,2);                            /* options */
      cb_text(&c,"rk"); cb_bool(&c,1);
      cb_text(&c,"up"); cb_bool(&c,1);
    cb_uint(&c,5); cb_uint(&c,1200u);                       /* maxMsgSize */
    ctap2_respond(cid, CTAP2_OK, c.buf, c.len);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — authenticatorMakeCredential  (parse → show approval screen)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ctap2_make_credential(uint32_t cid, const uint8_t *req, uint16_t rlen)
{
    if(g_state==ST_FIDO_APPROVE){ ctap2_error(cid,CTAP2_ERR_DENIED); return; }
    if(g_f_count>=FIDO_MAX_CREDS){ ctap2_error(cid,CTAP2_ERR_NO_CREDS); return; }
    cbd_t r; r.p=req; r.rem=rlen;
    cbd_t val;
    /* 0x01: clientDataHash */
    if(!cbd_map_uint(&r,1,&val)) goto bad;
    if(cbd_bytes(&val,g_f_p_hash,32)!=32) goto bad;
    /* 0x02: rp{id:text} */
    if(!cbd_map_uint(&r,2,&val)) goto bad;
    { cbd_t rpv;
      if(!cbd_map_text(&val,"id",&rpv)||!cbd_text(&rpv,g_f_p_rpid,FIDO_RPID_MAX))
          goto bad;
    }
    /* 0x03: user{name?, id?} */
    g_f_p_unm[0]='\0'; g_f_p_uid[0]='\0';
    if(cbd_map_uint(&r,3,&val)){
        cbd_t uv;
        if(cbd_map_text(&val,"name",&uv))        cbd_text(&uv,g_f_p_unm,FIDO_UNM_MAX);
        if(cbd_map_text(&val,"displayName",&uv)) { if(!g_f_p_unm[0]) cbd_text(&uv,g_f_p_unm,FIDO_UNM_MAX); }
        if(cbd_map_text(&val,"id",&uv))          cbd_text(&uv,g_f_p_uid,FIDO_UID_MAX);
    }
    /* 0x04: pubKeyCredParams — must contain EdDSA alg -8 (only alg we support) */
    { int alg_ok=0;
      if(cbd_map_uint(&r,4,&val)){
          cbd_t params=val; uint64_t cnt;
          if(cbd_head(&params,&cnt)==4){
              for(uint64_t i=0;i<cnt&&!alg_ok;i++){
                  cbd_t alg_pos;
                  /* cbd_map_text reads a copy of params, doesn't advance it */
                  if(cbd_map_text(&params,"alg",&alg_pos)){
                      uint64_t v; int m=cbd_head(&alg_pos,&v);
                      int64_t alg=(m==0)?(int64_t)v:-(int64_t)v-1;
                      if(alg==-8) alg_ok=1;
                  }
                  cbd_skip(&params); /* advance past this descriptor */
              }
          }
      }
      if(!alg_ok){ ctap2_error(cid,CTAP2_ERR_NO_ALG); return; }
    }
    g_ctap_pending_cid = cid;
    g_f_req_type = 1;
    g_f_req_t    = (uint32_t)rtc_get_uptime_ms();
    g_state      = ST_FIDO_APPROVE;
    return;
bad:
    ctap2_error(cid, CTAP2_ERR_CBOR);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — authenticatorGetAssertion  (parse → show approval screen)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ctap2_get_assertion(uint32_t cid, const uint8_t *req, uint16_t rlen)
{
    if(g_state==ST_FIDO_APPROVE){ ctap2_error(cid,CTAP2_ERR_DENIED); return; }
    cbd_t r; r.p=req; r.rem=rlen;
    cbd_t val;
    char rpid[FIDO_RPID_MAX];
    /* 0x01: rpId */
    if(!cbd_map_uint(&r,1,&val)||!cbd_text(&val,rpid,FIDO_RPID_MAX)) goto bad;
    /* 0x02: clientDataHash */
    if(!cbd_map_uint(&r,2,&val)||cbd_bytes(&val,g_f_p_hash,32)!=32) goto bad;
    /* 0x03: allowList (optional) */
    g_f_p_key_idx = -1;
    if(cbd_map_uint(&r,3,&val)){
        cbd_t al=val; uint64_t cnt;
        if(cbd_head(&al,&cnt)==4){
            for(uint64_t i=0;i<cnt&&g_f_p_key_idx<0;i++){
                /* al is at start of this descriptor map */
                cbd_t id_val;
                if(cbd_map_text(&al,"id",&id_val)){
                    uint8_t cred_id[32];
                    if(cbd_bytes(&id_val,cred_id,32)==32){
                        uint8_t h[32];
                        for(int k=0;k<g_f_count&&g_f_p_key_idx<0;k++){
                            int rpmatch=(slen(g_frpid[k])==slen(rpid));
                            for(int m=0;rpmatch&&m<slen(rpid);m++)
                                if(g_frpid[k][m]!=rpid[m]) rpmatch=0;
                            if(!rpmatch) continue;
                            crypto_sha256(g_fpk[k],32,h);
                            int match=1;
                            for(int m=0;m<32;m++) if(h[m]!=cred_id[m]){match=0;break;}
                            if(match) g_f_p_key_idx=k;
                        }
                    }
                }
                cbd_skip(&al); /* advance to next descriptor */
            }
        }
    }
    /* Fallback: resident key search by rpId */
    if(g_f_p_key_idx<0){
        for(int k=0;k<g_f_count;k++){
            int ok=(slen(g_frpid[k])==slen(rpid));
            for(int m=0;ok&&m<slen(rpid);m++) if(g_frpid[k][m]!=rpid[m]) ok=0;
            if(ok){ g_f_p_key_idx=k; break; }
        }
    }
    if(g_f_p_key_idx<0){ ctap2_error(cid,CTAP2_ERR_NO_CREDS); return; }
    sncopy(g_f_p_rpid, rpid, FIDO_RPID_MAX);
    sncopy(g_f_p_unm,  g_funm[g_f_p_key_idx], FIDO_UNM_MAX);
    g_ctap_pending_cid = cid;
    g_f_req_type = 2;
    g_f_req_t    = (uint32_t)rtc_get_uptime_ms();
    g_state      = ST_FIDO_APPROVE;
    return;
bad:
    ctap2_error(cid, CTAP2_ERR_CBOR);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — execute after user presses A on the approval screen
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ctap2_execute_approved(void)
{
    uint8_t authdata[300];
    if(g_f_req_type==1){
        /* makeCredential */
        int idx=g_f_count;
        if(fido_generate_cred(idx,g_f_p_rpid,(const char*)g_f_p_uid,g_f_p_unm)!=0){
            ctap2_error(g_ctap_pending_cid, CTAP2_ERR_DENIED);
            g_state=ST_FIDO_LIST; return;
        }
        uint16_t adlen=ctap_build_authdata(authdata,sizeof(authdata),
                                            g_f_p_rpid,0x41u,0u,idx);
        cbor_t c; c.buf=g_ctap_cbor_buf; c.cap=512; c.len=0;
        cb_map(&c,3);
        cb_uint(&c,1); cb_text(&c,"none");           /* fmt */
        cb_uint(&c,2); cb_bytes(&c,authdata,adlen);  /* authData */
        cb_uint(&c,3); cb_map(&c,0);                 /* attStmt: {} */
        ctap2_respond(g_ctap_pending_cid, CTAP2_OK, c.buf, c.len);
    } else {
        /* getAssertion */
        int idx=g_f_p_key_idx;
        g_fsc[idx]++;
        fido_save_cred(idx);
        uint16_t adlen=ctap_build_authdata(authdata,sizeof(authdata),
                                            g_frpid[idx],0x01u,g_fsc[idx],-1);
        /* sign authData || clientDataHash */
        uint8_t to_sign[37+32];
        for(uint16_t i=0;i<adlen&&i<sizeof(to_sign);i++) to_sign[i]=authdata[i];
        for(int i=0;i<32;i++) to_sign[adlen+i]=g_f_p_hash[i];
        uint8_t seed[32];
        if(crypto_aes256_decrypt(g_fwk[idx],g_fiv[idx],g_fe[idx],32,seed)!=0){
            ctap2_error(g_ctap_pending_cid, CTAP2_ERR_DENIED);
            g_state=ST_FIDO_LIST; return;
        }
        uint8_t sig[64];
        int r=crypto_ed25519_sign(seed,to_sign,(uint32_t)(adlen+32u),sig);
        for(int i=0;i<32;i++) seed[i]=0;
        if(r!=0){ ctap2_error(g_ctap_pending_cid,CTAP2_ERR_DENIED);
                  g_state=ST_FIDO_LIST; return; }
        uint8_t cred_id[32]; crypto_sha256(g_fpk[idx],32,cred_id);
        cbor_t c; c.buf=g_ctap_cbor_buf; c.cap=512; c.len=0;
        cb_map(&c,4);
        cb_uint(&c,1); cb_map(&c,2);                       /* credential */
          cb_text(&c,"type"); cb_text(&c,"public-key");
          cb_text(&c,"id");   cb_bytes(&c,cred_id,32);
        cb_uint(&c,2); cb_bytes(&c,authdata,adlen);         /* authData */
        cb_uint(&c,3); cb_bytes(&c,sig,64);                 /* signature */
        cb_uint(&c,4); cb_bytes(&c,                         /* userHandle */
            (const uint8_t*)g_fuid[idx],(uint16_t)slen(g_fuid[idx]));
        ctap2_respond(g_ctap_pending_cid, CTAP2_OK, c.buf, c.len);
    }
    g_state=ST_FIDO_LIST;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — dispatch decoded message
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ctap2_dispatch(uint32_t cid, const uint8_t *msg, uint16_t mlen)
{
    if(!mlen){ ctap_hid_error(cid,CTAP1_ERR_INVALID_LEN); return; }
    switch(msg[0]){
    case CTAP2_CMD_GET_INFO:   ctap2_get_info(cid);                       break;
    case CTAP2_CMD_MAKE_CRED:  ctap2_make_credential(cid,msg+1,mlen-1);  break;
    case CTAP2_CMD_GET_ASSERT: ctap2_get_assertion(cid,msg+1,mlen-1);    break;
    default: ctap2_error(cid, CTAP1_ERR_INVALID_CMD); break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CTAP2 — HID packet receive / reassemble / dispatch  (call each frame)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void poll_ctap_hid(void)
{
    uint8_t pkt[64];
    if(hid_fido_recv(pkt,64)<4) return;

    uint32_t cid=((uint32_t)pkt[0]<<24)|((uint32_t)pkt[1]<<16)|
                 ((uint32_t)pkt[2]<<8)|(uint32_t)pkt[3];

    if(pkt[4]&CTAPHID_CMD_BIT){
        /* ── Init packet (new message) ── */
        uint8_t cmd=(uint8_t)(pkt[4]&~CTAPHID_CMD_BIT);
        uint16_t total=((uint16_t)pkt[5]<<8)|(uint16_t)pkt[6];

        if(cmd==CTAPHID_INIT){
            if(cid==CTAPHID_BROADCAST){
                crypto_random((uint8_t*)&g_ctap_cid,4);
                if(!g_ctap_cid||g_ctap_cid==CTAPHID_BROADCAST) g_ctap_cid=1u;
            }
            uint8_t resp[17];
            for(int i=0;i<8;i++) resp[i]=pkt[7+i];
            resp[8] =(uint8_t)(g_ctap_cid>>24); resp[9] =(uint8_t)(g_ctap_cid>>16);
            resp[10]=(uint8_t)(g_ctap_cid>>8);  resp[11]=(uint8_t)g_ctap_cid;
            resp[12]=2u; resp[13]=1u; resp[14]=0u; resp[15]=0u;
            resp[16]=0x04u; /* CBOR capability */
            ctap_hid_send(cid, CTAPHID_INIT, resp, 17);
            return;
        }
        if(cmd==CTAPHID_PING){
            uint16_t c=(total<57u)?total:57u;
            ctap_hid_send(cid, CTAPHID_PING, pkt+7, c);
            return;
        }
        if(cmd==CTAPHID_CANCEL){
            if(g_state==ST_FIDO_APPROVE&&g_ctap_pending_cid==cid){
                ctap2_error(cid, CTAP2_ERR_CANCEL);
                g_state=ST_FIDO_LIST;
            }
            return;
        }
        if(cid!=g_ctap_cid){ ctap_hid_error(cid,CTAP1_ERR_INVALID_CMD); return; }
        if(cmd!=CTAPHID_MSG){ ctap_hid_error(cid,CTAP1_ERR_INVALID_CMD); return; }
        if(total>sizeof(g_ctap_rx_buf)){ ctap_hid_error(cid,CTAP1_ERR_INVALID_LEN); return; }
        /* start reassembly */
        g_ctap_rx_cid=cid; g_ctap_rx_cmd=cmd;
        g_ctap_rx_total=total; g_ctap_rx_len=0; g_ctap_rx_seq=0;
        uint16_t chunk=(total<57u)?total:57u;
        for(uint16_t i=0;i<chunk;i++) g_ctap_rx_buf[i]=pkt[7+i];
        g_ctap_rx_len=chunk;
        if(g_ctap_rx_len>=g_ctap_rx_total)
            ctap2_dispatch(cid, g_ctap_rx_buf, g_ctap_rx_total);
    } else {
        /* ── Continuation packet ── */
        uint8_t seq=pkt[4];
        if(cid!=g_ctap_rx_cid||seq!=g_ctap_rx_seq){
            ctap_hid_error(cid,CTAP1_ERR_INVALID_PAR); return;
        }
        g_ctap_rx_seq++;
        uint16_t rem=g_ctap_rx_total-g_ctap_rx_len;
        uint16_t chunk=(rem<59u)?rem:59u;
        for(uint16_t i=0;i<chunk&&g_ctap_rx_len<sizeof(g_ctap_rx_buf);i++)
            g_ctap_rx_buf[g_ctap_rx_len++]=pkt[5+i];
        if(g_ctap_rx_len>=g_ctap_rx_total)
            ctap2_dispatch(g_ctap_rx_cid, g_ctap_rx_buf, g_ctap_rx_total);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Drawing primitives — shared chrome
 * ═══════════════════════════════════════════════════════════════════════════ */
static void draw_header(const char *left, const char *right, uint32_t bg)
{
    /* Shared chrome: kit status bar (spans real display via get_size). The
     * kit bar is always the inverted paper-on-ink chrome, so the per-screen
     * bg distinction is no longer needed. */
    (void)bg;
    akira_ui_status_t sb = { .title = left, .clock = right, .battery_pct = -1 };
    akira_ui_status_bar(&sb);
}
static void draw_footer(const char *hint)
{
    display_hline(0,FOOT_Y-1,SCR_W,M_FG);
    display_rect(0,FOOT_Y,SCR_W,SCR_H-FOOT_Y,M_BG);
    display_text(4,FOOT_Y+6,hint,M_FG);
}

/* Countdown progress bar — white fill depleting on black */
static void draw_countdown(int x, int y, int w, int h,
                            int secs_left, int total_secs)
{
    display_progress_bar(x,y,w,h,secs_left,total_secs,M_FG,M_BG);
}

/* Animated ring (depleting clockwise from 12-o'clock) */
static void draw_ring(int cx, int cy, int ro, int ri,
                      int secs_left, uint32_t col)
{
    int elapsed_segs=(TOTP_PERIOD-secs_left)*2;
    display_circle_fill(cx,cy,ro,M_BG);             /* empty base: black */
    for(int i=elapsed_segs;i<60;i++){
        int x0=cx+(int)(ro*SIN60[i]/100); int y0=cy-(int)(ro*COS60[i]/100);
        int j=(i+1)%60;
        int x1=cx+(int)(ro*SIN60[j]/100); int y1=cy-(int)(ro*COS60[j]/100);
        display_triangle_fill(cx,cy,x0,y0,x1,y1,col);
    }
    display_circle_fill(cx,cy,ri,M_BG);             /* hollow centre */
    display_circle(cx,cy,ro,M_FG);
    display_circle(cx,cy,ri,M_FG);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Icons — geometric glyphs drawn with display primitives
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Clock face (TOTP): circle + hour + minute hands */
static void icon_clock(int cx, int cy, int r, uint32_t col)
{
    display_circle(cx,cy,r,col);
    display_circle_fill(cx,cy,2,col);
    /* hour hand: ~10 o'clock */
    display_line(cx,cy,cx-(r*5/10),cy-(r*6/10),col);
    /* minute hand: 12 o'clock */
    display_line(cx,cy,cx,cy-r+3,col);
    /* four tick marks */
    display_line(cx,cy-r,cx,cy-r+3,col);
    display_line(cx,cy+r-3,cx,cy+r,col);
    display_line(cx-r,cy,cx-r+3,cy,col);
    display_line(cx+r-3,cy,cx+r,cy,col);
}

/* Key (SSH): circle head + shank + teeth */
static void icon_key(int cx, int cy, int r, uint32_t col)
{
    int kx=cx-r/2;
    display_circle(kx,cy,r/2,col);
    display_circle_fill(kx,cy,r/4,C_BG);
    /* shank */
    display_rect(cx-r/4,cy-2,r+r/4,4,col);
    /* teeth */
    display_rect(cx+r/4,    cy+2, 3,5,col);
    display_rect(cx+r/4+6,  cy+2, 3,4,col);
}

/* Shield (FIDO2/Passkeys): rounded-top trapezoid + triangle point */
static void icon_shield(int cx, int cy, int r, uint32_t col)
{
    /* upper body */
    display_rounded_rect_fill(cx-r,cy-r,2*r,(int)(1.4*r),r/3,col);
    /* lower triangle point */
    display_triangle_fill(cx-r,cy+(int)(0.4*r),cx+r,cy+(int)(0.4*r),cx,cy+r+2,col);
    /* inner highlight cutout */
    display_rounded_rect_fill(cx-r+4,cy-r+4,2*r-8,(int)(1.4*r)-6,r/4,C_SEP);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HOME screen
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_home_card(int sec, int sel, int secs_left)
{
    int cx = CARD_CX[sec];
    int x  = CARD_X[sec];
    int y  = CARD_Y;

    /*
     * Inversion rule:
     *   selected   → white fill, black content  (prominent, inverted)
     *   unselected → black fill, white outline and content
     */
    uint32_t card_bg   = sel ? M_FG : M_BG;
    uint32_t content   = sel ? M_BG : M_FG;   /* icon, text, separator */

    display_rounded_rect_fill(x, y, CARD_W, CARD_H, CARD_R, card_bg);

    /* Border: selected gets a double outline for extra weight */
    display_rounded_rect(x, y, CARD_W, CARD_H, CARD_R, M_FG);
    if(sel)
        display_rounded_rect(x-2, y-2, CARD_W+4, CARD_H+4, CARD_R+2, M_FG);

    /* Icon centred in the top half of the card */
    int icy = y + 38;
    if(sec==SEC_TOTP)     icon_clock (cx, icy, 16, content);
    else if(sec==SEC_SSH) icon_key   (cx, icy, 14, content);
    else                  icon_shield(cx, icy, 14, content);

    /* Section label */
    const char *labels[3] = {"TOTP", "SSH", "PASSKEYS"};
    int lx = cx - slen(labels[sec])*7/2;
    display_text(lx, y+62, labels[sec], content);

    /* Separator */
    display_hline(x+8, y+78, CARD_W-16, content);

    /* Count line */
    char cnt_str[20];
    if(sec==SEC_TOTP){
        int_to_str(cnt_str, g_t_count);
        int n=slen(cnt_str);
        cnt_str[n]=' ';cnt_str[n+1]='a';cnt_str[n+2]='c';cnt_str[n+3]='c';cnt_str[n+4]='\0';
    } else if(sec==SEC_SSH){
        int_to_str(cnt_str, g_s_count);
        int n=slen(cnt_str);
        cnt_str[n]=' ';cnt_str[n+1]='k';cnt_str[n+2]='e';cnt_str[n+3]='y';cnt_str[n+4]='s';cnt_str[n+5]='\0';
    } else {
        int_to_str(cnt_str, g_f_count);
        int n=slen(cnt_str);
        cnt_str[n]=' ';cnt_str[n+1]='c';cnt_str[n+2]='r';cnt_str[n+3]='d';cnt_str[n+4]='s';cnt_str[n+5]='\0';
    }
    display_text(cx - slen(cnt_str)*7/2, y+88, cnt_str, content);

    /* Status line — filled dot = good, outline dot = bad */
    int dot_x = x + 14;
    int dot_y = y + 122;
    const char *status_txt;
    int status_ok;
    if(sec==SEC_TOTP){
        status_ok  = (g_t_count > 0);
        status_txt = status_ok ? "Active" : "No accs";
    } else if(sec==SEC_SSH){
        status_ok  = (g_s_uart >= 0);
        status_txt = status_ok ? "Ready" : "No UART";
    } else {
        status_ok  = VAULT_FIDO_CTAP_ENABLED;
        status_txt = status_ok ? "CTAP2" : "v1.7 req";
    }
    if(status_ok) display_circle_fill(dot_x, dot_y, 3, content);
    else          display_circle     (dot_x, dot_y, 3, content);
    display_text(dot_x+8, dot_y-4, status_txt, content);

    (void)secs_left;   /* TOTP live code removed from home; keeps home clean */
}

static void render_home(int secs_left)
{
    display_clear(C_BG);
    draw_header("VAULT","v1.0",C_HDR);

    for(int s=0;s<3;s++) draw_home_card(s,(s==g_home_sel),secs_left);

    /* Selection dots with animated slide */
    int dy = CARD_Y + CARD_H + 10;
    int dot_x[3];
    for(int i=0;i<3;i++) dot_x[i]=CARD_CX[i];

    int ind_x;
    if(g_anim>0){
        /* interpolate dot position from prev to current selection */
        int from_x=dot_x[g_home_prev_sel];
        int to_x  =dot_x[g_home_sel];
        ind_x=lerp(to_x,from_x,g_anim,6); /* g_anim counts down 6→0 */
    } else {
        ind_x=dot_x[g_home_sel];
    }

    for(int i=0;i<3;i++){
        if(dot_x[i]==ind_x && g_anim==0)
            display_circle_fill(dot_x[i],dy,4,C_ACC);
        else
            display_circle(dot_x[i],dy,4,C_SEP);
    }
    /* moving accent dot during animation */
    if(g_anim>0) display_circle_fill(ind_x,dy,4,C_ACC);

    /* Hint */
    display_text(SCR_W/2-70,FOOT_Y-16,"\x1B \x1A  select     A  open",C_DIM);
    draw_footer("\x1B\x1A select   A open");
    display_flush();
}

/* Enter-section zoom animation (4 frames) */
static void render_enter_anim(int sec)
{
    /* Frame 4→1: expand card border to full screen */
    int f=g_enter_anim; /* 4..1 */
    int cx=CARD_CX[sec];
    int x=CARD_X[sec], y=CARD_Y, w=CARD_W, h=CARD_H;
    /* expand amount per frame: total 4 steps to fill screen */
    int exp=(5-f)*((SCR_W-w)/4);
    int nx=x-exp; int ny=y-exp*CARD_H/(SCR_W-w);
    int nw=w+exp*2; int nh=h+exp*2*CARD_H/(SCR_W-w);
    if(nx<0) nx=0; if(ny<0) ny=0;
    if(nw>SCR_W) nw=SCR_W; if(nh>SCR_H) nh=SCR_H;

    display_clear(M_BG);
    for(int s=0;s<3;s++){
        if(s!=sec) draw_home_card(s,0,30);
    }
    display_rounded_rect_fill(nx,ny,nw,nh,CARD_R,M_FG);
    display_rounded_rect(nx,ny,nw,nh,CARD_R,M_BG);
    /* icon centred in expanding card */
    int icx=nx+nw/2; int icy=ny+nh/3;
    if(sec==SEC_TOTP)     icon_clock (icx,icy,20,M_BG);
    else if(sec==SEC_SSH) icon_key   (icx,icy,18,M_BG);
    else                  icon_shield(icx,icy,18,M_BG);
    display_flush();

    (void)cx;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TOTP renders
 * ═══════════════════════════════════════════════════════════════════════════ */
static void render_totp_list(int secs_left)
{
    display_clear(C_BG);
    char hr[8]; int_to_str(hr,g_t_count); int n=slen(hr);
    hr[n]=' ';hr[n+1]='A';hr[n+2]='C';hr[n+3]='\0';
    draw_header("VAULT  TOTP",hr,C_HDR);
    draw_countdown(0,HDR_H,SCR_W,3,secs_left,TOTP_PERIOD);

    if(g_t_count==0){
        display_text(SCR_W/2-70,110,"No accounts. Provision via",C_DIM);
        display_text(SCR_W/2-70,124,"akira-cli settings set vault/*",C_SEP);
        draw_footer("B back");
        display_flush(); return;
    }

    for(int v=0;v<TOTP_VROWS;v++){
        int idx=g_t_scroll+v; if(idx>=g_t_count) break;
        int y=HDR_H+8+v*TOTP_ROW_H;
        int sel=(idx==g_t_cursor);
        display_rect(0,y-1,SCR_W,TOTP_ROW_H,sel?M_FG:M_BG);
        char nm[16]; sncopy(nm,g_tn[idx],15);
        display_text(6,y+4,nm,sel?M_BG:M_FG);
        char sp[8]; fmt6_spaced(sp,g_tc[idx]);
        display_text(116,y+4,sp,sel?M_BG:M_FG);
        draw_countdown(248,y+6,60,6,secs_left,TOTP_PERIOD);
        if(sel) display_rect_outline(0,y-1,SCR_W-1,TOTP_ROW_H-1,M_FG);
    }
    if(g_t_count>TOTP_VROWS){
        int th=TOTP_VROWS*TOTP_ROW_H*TOTP_VROWS/g_t_count; if(th<4) th=4;
        int ty=HDR_H+8+TOTP_VROWS*TOTP_ROW_H*g_t_scroll/g_t_count;
        display_rect(SCR_W-3,HDR_H+8,3,TOTP_VROWS*TOTP_ROW_H,C_SEP);
        display_rect(SCR_W-3,ty,3,th,C_ACC);
    }
    /* refresh countdown label */
    char rl[14]; rl[0]='R';rl[1]='e';rl[2]='f';rl[3]='r';rl[4]='e';
    rl[5]='s';rl[6]='h';rl[7]=' ';rl[8]='i';rl[9]='n';rl[10]=' ';
    int ri=11;
    if(secs_left>=10) rl[ri++]='0'+secs_left/10;
    rl[ri++]='0'+secs_left%10; rl[ri++]='s'; rl[ri]='\0';
    display_text(SCR_W-ri*7-4,FOOT_Y-12,rl,(secs_left>5)?C_DIM:C_WARN);
    draw_footer("A view   \x18\x19 scroll   B home");
    display_flush();
}

static void render_totp_view(int idx, int secs_left)
{
    display_clear(C_BG);
    char pos[8]; int pi=0;
    if(g_t_count>=10) pos[pi++]='0'+(idx+1)/10;
    pos[pi++]='0'+(idx+1)%10; pos[pi++]='/';
    if(g_t_count>=10) pos[pi++]='0'+g_t_count/10;
    pos[pi++]='0'+g_t_count%10; pos[pi]='\0';
    draw_header("VAULT  TOTP",pos,C_HDR);

    int nlen=slen(g_tn[idx]);
    display_text(SCR_W/2-nlen*7/2,26,g_tn[idx],C_DIM);

    draw_ring(SCR_W/2,125,60,43,secs_left,M_FG);

    char sp[8]; fmt6_spaced(sp,g_tc[idx]);
    display_text_large(SCR_W/2-38,116,sp,M_FG);

    char ss[5]; int si=0;
    if(secs_left>=10) ss[si++]='0'+secs_left/10;
    ss[si++]='0'+secs_left%10; ss[si++]='s'; ss[si]='\0';
    display_text(SCR_W/2-si*7/2,142,ss,M_FG);

    display_text(6,FOOT_Y-12,"TOTP  SHA-1  6 digits  30s",C_SEP);
    draw_footer("B list   \x1B\x1A account");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SSH renders
 * ═══════════════════════════════════════════════════════════════════════════ */
static void render_ssh_list(void)
{
    display_clear(C_BG);
    char hr[8]; int_to_str(hr,g_s_count); int n=slen(hr);
    hr[n]=' ';hr[n+1]='k';hr[n+2]='e';hr[n+3]='y';hr[n+4]='s';hr[n+5]='\0';
    draw_header("VAULT  SSH",hr,C_HDR);

    /* Agent status — filled dot = ready, outline = error */
    if(g_s_uart>=0) display_circle_fill(10,HDR_H+8,4,M_FG);
    else            display_circle     (10,HDR_H+8,4,M_FG);
    display_text(18,HDR_H+3,(g_s_uart>=0)?"Agent ready":"UART error",M_FG);

    if(g_s_count==0){
        display_text(SCR_W/2-63,100,"No keys on device.",C_DIM);
        display_text(SCR_W/2-63,116,"Press A to generate a new",C_TXT);
        display_text(SCR_W/2-63,130,"ssh-ed25519 key pair.",C_TXT);
        draw_footer("A generate   B home");
        display_flush(); return;
    }

    for(int v=0;v<SSH_VROWS;v++){
        int idx=g_s_scroll+v; if(idx>=g_s_count) break;
        int y=HDR_H+20+v*SSH_ROW_H;
        int sel=(idx==g_s_cursor);
        uint32_t rfg = sel ? M_BG : M_FG;
        display_rect(0,y-2,SCR_W,SSH_ROW_H,sel?M_FG:M_BG);
        display_rect_outline(6,y+3,9,9,rfg);
        display_circle_fill(10,y+2,3,rfg);
        char nm[22]; sncopy(nm,g_sn[idx],21);
        display_text(22,y+5,nm,rfg);
        ssh_compute_fingerprint(idx);
        char fp[22]; sncopy(fp,g_s_fp+7,21);
        display_text(192,y+5,fp,rfg);
        if(sel) display_rect_outline(0,y-2,SCR_W-1,SSH_ROW_H-1,M_FG);
    }

    if(g_s_count<SSH_MAX_KEYS){
        int ay=HDR_H+20+g_s_count*SSH_ROW_H+4;
        if(ay<FOOT_Y-10) display_text(22,ay,"+ A to generate new key",C_SEP);
    }
    if(g_s_count>SSH_VROWS){
        int th=SSH_VROWS*SSH_ROW_H*SSH_VROWS/g_s_count; if(th<4)th=4;
        int ty=HDR_H+20+SSH_VROWS*SSH_ROW_H*g_s_scroll/g_s_count;
        display_rect(SCR_W-3,HDR_H+20,3,SSH_VROWS*SSH_ROW_H,C_SEP);
        display_rect(SCR_W-3,ty,3,th,C_ACC);
    }
    draw_footer("A new key   Y detail   X delete   \x18\x19 scroll");
    display_flush();
}

static void render_ssh_approve(void)
{
    uint32_t elapsed=(uint32_t)rtc_get_uptime_ms()-g_s_sign_t;
    int sl=(int)((SSH_SIGN_TIMEOUT_MS-elapsed)/1000); if(sl<0)sl=0;
    display_clear(M_BG);
    draw_header("SSH  SIGN REQUEST",NULL,M_FG);  /* inverted: white bar, black text */
    display_text(6,26,"Key:",C_DIM);
    display_text(46,26,g_sn[g_s_sign_idx],C_TXT);
    display_hline(6,40,SCR_W-12,C_SEP);
    display_text(6,44,"SHA-256 of data-to-sign:",C_DIM);
    uint8_t dig[32]; crypto_sha256(g_s_sign_data,g_s_sign_len,dig);
    char hex[65]; bin_to_hex(hex,dig,32);
    display_text(6,58,hex,C_ACC);
    display_text(6,70,hex+32,C_ACC);
    display_hline(6,84,SCR_W-12,C_SEP);
    char sz[16]; int_to_str(sz,(int)g_s_sign_len);
    int sn2=slen(sz); sz[sn2]=' ';sz[sn2+1]='b';sz[sn2+2]='y';sz[sn2+3]='t';
    sz[sn2+4]='e';sz[sn2+5]='s';sz[sn2+6]='\0';
    display_text(6,90,"Size:",C_DIM); display_text(46,90,sz,C_TXT);
    display_text(6,106,"Auto-deny in:",C_DIM);
    char cs[5]; int cn2=0;
    if(sl>=10) cs[cn2++]='0'+sl/10; cs[cn2++]='0'+sl%10; cs[cn2++]='s'; cs[cn2]='\0';
    display_text(SCR_W-30,106,cs,(sl>10)?C_OK:(sl>5)?C_WARN:C_ERR);
    draw_countdown(6,118,SCR_W-12,6,sl,30);
    /* APPROVE: inverted (white fill, black text) — visually prominent */
    display_rounded_rect_fill(14,132,120,32,4,M_FG);
    display_text(26,146,"[A] APPROVE",M_BG);
    /* DENY: outline only */
    display_rounded_rect(186,132,120,32,4,M_FG);
    display_text(200,146,"[B] DENY",M_FG);
    draw_footer("A approve and sign   B deny");
    display_flush();
}

static void render_ssh_detail(int idx)
{
    display_clear(C_BG);
    draw_header("KEY DETAIL","B back",C_HDR);
    int y=28;
    display_text(6,y,"Name",C_DIM);  display_text(70,y,g_sn[idx],C_TXT);   y+=16;
    display_text(6,y,"Type",C_DIM);  display_text(70,y,"ssh-ed25519",C_ACC); y+=16;
    ssh_compute_fingerprint(idx);
    display_text(6,y,"Fingerprint",C_DIM); y+=14;
    char r1[28],r2[28]; sncopy(r1,g_s_fp,26); sncopy(r2,g_s_fp+25,26);
    display_text(6,y,r1,C_ACC); y+=12; display_text(6,y,r2,C_ACC); y+=16;
    display_hline(6,y,SCR_W-12,C_SEP); y+=6;
    display_text(6,y,"Public key (hex)",C_DIM); y+=14;
    char hex[65]; bin_to_hex(hex,g_spk[idx],32);
    display_text(6,y,hex,C_DIM); y+=12; display_text(6,y,hex+32,C_DIM); y+=16;
    display_hline(6,y,SCR_W-12,C_SEP); y+=6;
    display_text(6,y,"Private key: AES-256-CBC encrypted",C_SEP);
    draw_footer("B back   X delete");
    display_flush();
}

static void render_ssh_keygen(void)
{
    display_clear(C_BG); draw_header("VAULT  SSH",NULL,C_HDR);
    /* Spinning arc approximation using ring with animated segment */
    int frame=(int)(g_tick&0x0F);
    int seg_start=frame*4; int seg_end=seg_start+20;
    display_circle_fill(SCR_W/2,110,28,C_SEP);
    for(int i=seg_start;i<seg_end;i++){
        int ii=i%60;
        int x0=SCR_W/2+(int)(28*SIN60[ii]/100); int y0=110-(int)(28*COS60[ii]/100);
        int jj=(ii+1)%60;
        int x1=SCR_W/2+(int)(28*SIN60[jj]/100); int y1=110-(int)(28*COS60[jj]/100);
        display_triangle_fill(SCR_W/2,110,x0,y0,x1,y1,C_ACC);
    }
    display_circle_fill(SCR_W/2,110,20,C_BG);
    display_text(SCR_W/2-56,138,"Generating key pair...",C_TXT);
    display_text(SCR_W/2-56,154,"Collecting hardware entropy",C_DIM);
    display_flush();
}

static void render_ssh_del_dlg(int idx)
{
    display_clear(M_BG);
    draw_header("DELETE KEY?",NULL,M_FG);
    display_text(SCR_W/2-77,80,"This will permanently delete:",M_FG);
    display_text(SCR_W/2-slen(g_sn[idx])*7/2,96,g_sn[idx],M_FG);
    display_text(SCR_W/2-77,112,"Private key cannot be recovered.",M_FG);
    /* DELETE: outline (destructive — not given visual prominence) */
    display_rounded_rect(14,140,120,32,4,M_FG);
    display_text(22,154,"[A] DELETE",M_FG);
    /* CANCEL: inverted — safe action gets the prominence */
    display_rounded_rect_fill(186,140,120,32,4,M_FG);
    display_text(194,154,"[B] CANCEL",M_BG);
    draw_footer("A delete   B cancel");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FIDO2 renders
 * ═══════════════════════════════════════════════════════════════════════════ */
static void render_fido_list(void)
{
    display_clear(C_BG);
    char hr[10]; int_to_str(hr,g_f_count); int n=slen(hr);
    hr[n]=' ';hr[n+1]='c';hr[n+2]='r';hr[n+3]='e';hr[n+4]='d';hr[n+5]='\0';
    draw_header("VAULT  PASSKEYS",hr,C_HDR);

    /* CTAP2 status — filled dot = active, outline = unavailable */
#if VAULT_FIDO_CTAP_ENABLED
    display_circle_fill(10,HDR_H+8,4,M_FG);
    display_text(18,HDR_H+3,"CTAP2 USB active",M_FG);
#else
    display_circle     (10,HDR_H+8,4,M_FG);
    display_text(18,HDR_H+3,"CTAP2 USB req. AkiraOS 1.7",M_FG);
#endif

    if(g_f_count==0){
        /* Draw shield icon dimly centred */
        icon_shield(SCR_W/2,90,20,C_SEP);
        display_text(SCR_W/2-63,118,"No passkeys stored.",C_DIM);
#if VAULT_FIDO_CTAP_ENABLED
        display_text(SCR_W/2-84,134,"Use your browser to register a",C_DIM);
        display_text(SCR_W/2-84,148,"passkey on any WebAuthn site.",C_DIM);
#else
        display_text(SCR_W/2-84,134,"Kernel CTAP2 bridge needed to",C_DIM);
        display_text(SCR_W/2-84,148,"create passkeys via browser.",C_DIM);
        display_text(SCR_W/2-84,162,"Coming in AkiraOS v1.7.",C_SEP);
#endif
        draw_footer("B home");
        display_flush(); return;
    }

    for(int v=0;v<FIDO_VROWS;v++){
        int idx=g_f_scroll+v; if(idx>=g_f_count) break;
        int y=HDR_H+20+v*FIDO_ROW_H;
        int sel=(idx==g_f_cursor);
        uint32_t rfg = sel ? M_BG : M_FG;
        display_rect(0,y-2,SCR_W,FIDO_ROW_H,sel?M_FG:M_BG);
        /* shield icon (small) */
        display_rounded_rect_fill(6,y+2,10,10,2,rfg);
        display_triangle_fill(6,y+9,16,y+9,11,y+14,rfg);
        /* RP ID */
        char rp[22]; sncopy(rp,g_frpid[idx],21);
        display_text(22,y+2,rp,rfg);
        /* user name */
        char un[18]; sncopy(un,g_funm[idx],17);
        display_text(22,y+13,un,rfg);
        /* sign count */
        char sc2[8]; int_to_str(sc2,(int)g_fsc[idx]);
        display_text(260,y+6,sc2,rfg);
        if(sel) display_rect_outline(0,y-2,SCR_W-1,FIDO_ROW_H-1,M_FG);
    }
    if(g_f_count>FIDO_VROWS){
        int th=FIDO_VROWS*FIDO_ROW_H*FIDO_VROWS/g_f_count; if(th<4)th=4;
        int ty=HDR_H+20+FIDO_VROWS*FIDO_ROW_H*g_f_scroll/g_f_count;
        display_rect(SCR_W-3,HDR_H+20,3,FIDO_VROWS*FIDO_ROW_H,C_SEP);
        display_rect(SCR_W-3,ty,3,th,C_ACC);
    }
    draw_footer("Y detail   X delete   \x18\x19 scroll   B home");
    display_flush();
}

static void render_fido_approve(void)
{
    uint32_t elapsed=(uint32_t)rtc_get_uptime_ms()-g_f_req_t;
    int sl=(int)((FIDO_APPROVE_TIMEOUT_MS-elapsed)/1000); if(sl<0)sl=0;
    display_clear(M_BG);
    draw_header((g_f_req_type==1)?"CREATE PASSKEY":"SIGN IN WITH PASSKEY",NULL,M_FG);
    /* Shield icon */
    icon_shield(SCR_W/2,48,14,M_FG);
    display_text(6,70,"Site:",C_DIM);  display_text(46,70,g_f_p_rpid,C_TXT);
    display_text(6,86,"User:",C_DIM);  display_text(46,86,g_f_p_unm,C_TXT);
    if(g_f_req_type==2){
        display_hline(6,100,SCR_W-12,C_SEP);
        display_text(6,104,"Challenge hash:",C_DIM);
        char hex[65]; bin_to_hex(hex,g_f_p_hash,32);
        display_text(6,116,hex,C_SEP); display_text(6,126,hex+32,C_SEP);
    }
    display_text(6,140,"Auto-deny in:",C_DIM);
    char cs[5]; int cn2=0;
    if(sl>=10) cs[cn2++]='0'+sl/10; cs[cn2++]='0'+sl%10; cs[cn2++]='s'; cs[cn2]='\0';
    display_text(SCR_W-30,140,cs,(sl>10)?C_OK:(sl>5)?C_WARN:C_ERR);
    draw_countdown(6,152,SCR_W-12,6,sl,30);
    display_rounded_rect_fill(14,162,120,32,4,M_FG);
    display_text(22,176,"[A] APPROVE",M_BG);
    display_rounded_rect(186,162,120,32,4,M_FG);
    display_text(200,176,"[B] DENY",M_FG);
    draw_footer("A approve   B deny");
    display_flush();
}

static void render_fido_detail(int idx)
{
    display_clear(C_BG);
    draw_header("PASSKEY DETAIL","B back",C_HDR);
    int y=28;
    display_text(6,y,"Site (RP)",C_DIM); display_text(80,y,g_frpid[idx],C_TXT); y+=16;
    display_text(6,y,"User",C_DIM);      display_text(80,y,g_funm[idx],C_TXT);  y+=16;
    display_text(6,y,"User ID",C_DIM);   display_text(80,y,g_fuid[idx],C_DIM);  y+=16;
    char sc2[12]; int_to_str(sc2,(int)g_fsc[idx]);
    display_text(6,y,"Sign count",C_DIM); display_text(80,y,sc2,C_TXT); y+=16;
    display_hline(6,y,SCR_W-12,C_SEP); y+=8;
    display_text(6,y,"Algorithm",C_DIM); display_text(80,y,"Ed25519  (alg -8)",C_ACC); y+=16;
    display_text(6,y,"Type",C_DIM);      display_text(80,y,"Discoverable (rk)",C_DIM); y+=16;
    display_hline(6,y,SCR_W-12,C_SEP); y+=8;
    display_text(6,y,"Public key (first 16B)",C_DIM); y+=12;
    char hex[33]; bin_to_hex(hex,g_fpk[idx],16);
    display_text(6,y,hex,C_SEP); y+=12;
    display_text(6,y,"Key: AES-256-CBC encrypted on-device",C_SEP);
    draw_footer("B back   X delete");
    display_flush();
}

static void render_fido_del_dlg(int idx)
{
    display_clear(M_BG);
    draw_header("DELETE PASSKEY?",NULL,M_FG);
    display_text(SCR_W/2-84,78,"This will permanently delete the",M_FG);
    display_text(SCR_W/2-84,94,"passkey for:",M_FG);
    display_text(SCR_W/2-slen(g_frpid[idx])*7/2,110,g_frpid[idx],M_FG);
    display_text(SCR_W/2-84,126,"You will need to re-register on",M_FG);
    display_text(SCR_W/2-84,140,"this site after deletion.",M_FG);
    display_rounded_rect(14,152,120,32,4,M_FG);
    display_text(22,166,"[A] DELETE",M_FG);
    display_rounded_rect_fill(186,152,120,32,4,M_FG);
    display_text(194,166,"[B] CANCEL",M_BG);
    draw_footer("A delete   B cancel");
    display_flush();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Input handler
 * ═══════════════════════════════════════════════════════════════════════════ */
static void handle_input(int secs_left)
{
    uint32_t btns=(uint32_t)input_get_buttons();
    uint32_t edge=btns&~g_prev_btns;
    g_prev_btns=btns;

    /* ── HOME ─────────────────────────────────────────────────────────── */
    if(g_state==ST_HOME){
        if(edge&AKIRA_BTN_LEFT){
            if(g_home_sel>0){
                g_home_prev_sel=g_home_sel;
                g_home_sel--;
                g_anim=6;
            }
        }
        if(edge&AKIRA_BTN_RIGHT){
            if(g_home_sel<2){
                g_home_prev_sel=g_home_sel;
                g_home_sel++;
                g_anim=6;
            }
        }
        if(edge&AKIRA_BTN_A){
            g_enter_anim=4;
            /* target state set after animation completes in render */
        }
        return;
    }

    /* ── TOTP LIST ────────────────────────────────────────────────────── */
    if(g_state==ST_TOTP_LIST){
        if(edge&AKIRA_BTN_B){ g_state=ST_HOME; return; }
        if(edge&AKIRA_BTN_UP){
            if(g_t_cursor>0){ g_t_cursor--;
              if(g_t_cursor<g_t_scroll) g_t_scroll=g_t_cursor; }
        }
        if(edge&AKIRA_BTN_DOWN){
            if(g_t_cursor<g_t_count-1){ g_t_cursor++;
              if(g_t_cursor>=g_t_scroll+TOTP_VROWS) g_t_scroll=g_t_cursor-TOTP_VROWS+1; }
        }
        if((edge&AKIRA_BTN_A)&&g_t_count>0) g_state=ST_TOTP_VIEW;
        return;
    }

    /* ── TOTP VIEW ────────────────────────────────────────────────────── */
    if(g_state==ST_TOTP_VIEW){
        if(edge&AKIRA_BTN_B) g_state=ST_TOTP_LIST;
        if(edge&AKIRA_BTN_LEFT)  { if(g_t_cursor>0) g_t_cursor--; }
        if(edge&AKIRA_BTN_RIGHT) { if(g_t_cursor<g_t_count-1) g_t_cursor++; }
        return;
    }

    /* ── SSH LIST ─────────────────────────────────────────────────────── */
    if(g_state==ST_SSH_LIST){
        if(edge&AKIRA_BTN_B){ g_state=ST_HOME; return; }
        if(edge&AKIRA_BTN_UP){
            if(g_s_cursor>0){ g_s_cursor--;
              if(g_s_cursor<g_s_scroll) g_s_scroll=g_s_cursor; }
        }
        if(edge&AKIRA_BTN_DOWN){
            if(g_s_cursor<g_s_count){ g_s_cursor++;
              if(g_s_cursor>=g_s_scroll+SSH_VROWS) g_s_scroll=g_s_cursor-SSH_VROWS+1; }
        }
        if(edge&AKIRA_BTN_A){
            if(g_s_cursor==g_s_count&&g_s_count<SSH_MAX_KEYS){
                g_state=ST_SSH_KEYGEN;
            }
        }
        if((edge&AKIRA_BTN_Y)&&g_s_cursor<g_s_count) g_state=ST_SSH_DETAIL;
        if((edge&AKIRA_BTN_X)&&g_s_cursor<g_s_count) g_state=ST_SSH_DEL_DLG;
        return;
    }

    /* ── SSH APPROVE ──────────────────────────────────────────────────── */
    if(g_state==ST_SSH_APPROVE){
        uint32_t elapsed=(uint32_t)rtc_get_uptime_ms()-g_s_sign_t;
        if(elapsed>=SSH_SIGN_TIMEOUT_MS){ ssh_send_failure(); g_state=ST_SSH_LIST; return; }
        if(edge&AKIRA_BTN_A){ ssh_execute_sign(); g_state=ST_SSH_LIST; }
        if(edge&AKIRA_BTN_B){ ssh_send_failure(); g_state=ST_SSH_LIST; }
        return;
    }

    /* ── SSH DETAIL ───────────────────────────────────────────────────── */
    if(g_state==ST_SSH_DETAIL){
        if(edge&AKIRA_BTN_B) g_state=ST_SSH_LIST;
        if((edge&AKIRA_BTN_X)&&g_s_cursor<g_s_count) g_state=ST_SSH_DEL_DLG;
        return;
    }

    /* ── SSH DELETE DIALOG ────────────────────────────────────────────── */
    if(g_state==ST_SSH_DEL_DLG){
        if(edge&AKIRA_BTN_A){
            ssh_delete_key(g_s_cursor);
            g_state=ST_SSH_LIST;
        }
        if(edge&AKIRA_BTN_B) g_state=ST_SSH_LIST;
        return;
    }

    /* ── FIDO LIST ────────────────────────────────────────────────────── */
    if(g_state==ST_FIDO_LIST){
        if(edge&AKIRA_BTN_B){ g_state=ST_HOME; return; }
        if(edge&AKIRA_BTN_UP){
            if(g_f_cursor>0){ g_f_cursor--;
              if(g_f_cursor<g_f_scroll) g_f_scroll=g_f_cursor; }
        }
        if(edge&AKIRA_BTN_DOWN){
            if(g_f_cursor<g_f_count-1){ g_f_cursor++;
              if(g_f_cursor>=g_f_scroll+FIDO_VROWS) g_f_scroll=g_f_cursor-FIDO_VROWS+1; }
        }
        if((edge&AKIRA_BTN_Y)&&g_f_cursor<g_f_count) g_state=ST_FIDO_DETAIL;
        if((edge&AKIRA_BTN_X)&&g_f_cursor<g_f_count) g_state=ST_FIDO_DEL_DLG;
        return;
    }

    /* ── FIDO APPROVE ─────────────────────────────────────────────────── */
    if(g_state==ST_FIDO_APPROVE){
        uint32_t elapsed=(uint32_t)rtc_get_uptime_ms()-g_f_req_t;
        if(elapsed>=FIDO_APPROVE_TIMEOUT_MS){
            ctap2_error(g_ctap_pending_cid, CTAP2_ERR_DENIED);
            g_state=ST_FIDO_LIST; return;
        }
        if(edge&AKIRA_BTN_A){ ctap2_execute_approved(); return; }
        if(edge&AKIRA_BTN_B){
            ctap2_error(g_ctap_pending_cid, CTAP2_ERR_DENIED);
            g_state=ST_FIDO_LIST;
        }
        return;
    }

    /* ── FIDO DETAIL ──────────────────────────────────────────────────── */
    if(g_state==ST_FIDO_DETAIL){
        if(edge&AKIRA_BTN_B) g_state=ST_FIDO_LIST;
        if((edge&AKIRA_BTN_X)&&g_f_cursor<g_f_count) g_state=ST_FIDO_DEL_DLG;
        return;
    }

    /* ── FIDO DELETE DIALOG ───────────────────────────────────────────── */
    if(g_state==ST_FIDO_DEL_DLG){
        if(edge&AKIRA_BTN_A){ fido_delete_cred(g_f_cursor); g_state=ST_FIDO_LIST; }
        if(edge&AKIRA_BTN_B) g_state=ST_FIDO_LIST;
        return;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void)
{
    printf("vault v1.0 — TOTP · SSH · Passkeys");

    display_get_size(&SCR_W, &SCR_H); /* adapt layout to the real display width */

    g_tmr       = timer_create();
    g_state     = ST_HOME;
    g_home_sel  = SEC_TOTP;
    g_home_prev_sel = SEC_TOTP;
    g_anim      = 0;
    g_enter_anim= 0;
    g_tick      = 0;
    g_prev_btns = 0;

    /* Load all credential stores */
    totp_load();
    ssh_load();
    fido_load();

    /* Open SSH agent UART (non-fatal if not available) */
    g_s_uart = uart_open(0, 115200);

    /* Seed TOTP codes */
    if(g_t_count>0){
        int ut=rtc_get_unix_time();
        if(ut>0){
            uint32_t T=(uint32_t)(ut/TOTP_PERIOD);
            totp_refresh(T);
        }
    }

    while(1){
        timer_start(g_tmr);
        g_tick++;

        /* ── TOTP window rollover ── */
        int unix_t=rtc_get_unix_time();
        int secs_used=(unix_t>0)?(unix_t%TOTP_PERIOD):0;
        int secs_left=TOTP_PERIOD-secs_used;
        if(unix_t>0){
            uint32_t T=(uint32_t)(unix_t/TOTP_PERIOD);
            if(g_t_count>0&&T!=g_t_last_T) totp_refresh(T);
        }

        /* ── SSH UART poll (always, for sign requests) ── */
        ssh_poll_uart();

        /* ── CTAP2 HID poll (always, for browser passkey requests) ── */
        poll_ctap_hid();

        /* ── SSH sign timeout ── */
        if(g_state==ST_SSH_APPROVE){
            uint32_t el=(uint32_t)rtc_get_uptime_ms()-g_s_sign_t;
            if(el>=SSH_SIGN_TIMEOUT_MS){ ssh_send_failure(); g_state=ST_SSH_LIST; }
        }

        /* ── FIDO timeout ── */
        if(g_state==ST_FIDO_APPROVE){
            uint32_t el=(uint32_t)rtc_get_uptime_ms()-g_f_req_t;
            if(el>=FIDO_APPROVE_TIMEOUT_MS) g_state=ST_FIDO_LIST;
        }

        /* ── Input ── */
        handle_input(secs_left);

        /* ── Enter-section animation ── */
        if(g_enter_anim>0){
            render_enter_anim(g_home_sel);
            g_enter_anim--;
            if(g_enter_anim==0){
                /* Commit to section */
                if(g_home_sel==SEC_TOTP) g_state=ST_TOTP_LIST;
                else if(g_home_sel==SEC_SSH) g_state=ST_SSH_LIST;
                else g_state=ST_FIDO_LIST;
            }
            goto frame_done;
        }

        /* ── SSH KEYGEN (blocking render loop) ── */
        if(g_state==ST_SSH_KEYGEN){
            render_ssh_keygen();
            if(ssh_generate_key(g_s_count,"AkiraOS SSH Key")==0){
                g_s_cursor=g_s_count-1;
            }
            g_state=ST_SSH_LIST;
            goto frame_done;
        }

        /* ── Decrement home slide animation ── */
        if(g_anim>0) g_anim--;

        /* ── Render ── */
        switch(g_state){
        case ST_HOME:       render_home(secs_left);               break;
        case ST_TOTP_LIST:  render_totp_list(secs_left);          break;
        case ST_TOTP_VIEW:  render_totp_view(g_t_cursor,secs_left);break;
        case ST_SSH_LIST:   render_ssh_list();                    break;
        case ST_SSH_APPROVE:render_ssh_approve();                  break;
        case ST_SSH_DETAIL: render_ssh_detail(g_s_cursor);        break;
        case ST_SSH_DEL_DLG:render_ssh_del_dlg(g_s_cursor);       break;
        case ST_FIDO_LIST:  render_fido_list();                   break;
        case ST_FIDO_APPROVE:render_fido_approve();               break;
        case ST_FIDO_DETAIL:render_fido_detail(g_f_cursor);       break;
        case ST_FIDO_DEL_DLG:render_fido_del_dlg(g_f_cursor);    break;
        default: break;
        }

    frame_done:;
        int used=timer_elapsed(g_tmr);
        if(used<100) delay((uint32_t)(100-used)*1000);
    }
    return 0;
}
