/*
 * field.h — AkiraField common types, constants, shared state
 * Dual-Radio Field Tool: CC1121 (sub-GHz FSK) + LR1121 (LoRa/GFSK multi-band)
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef FIELD_H
#define FIELD_H

#include <stdint.h>

/* ── Display ────────────────────────────────────────────────────────────── */
#define SCR_W    320
#define SCR_H    240
#define STATUS_H  16   /* y = 0..15  */
#define TAB_H     16   /* y = 16..31 */
#define CONTENT_Y 32   /* y = 32..223 */
#define CONTENT_H 192
#define ACTION_Y  224  /* y = 224..239 */
#define PANE_W    158  /* left/right half width */
#define PANE_SEP  162  /* right pane x start */

/* ── Colours (RGB565) ───────────────────────────────────────────────────── */
#define C_BG      0x0000u
#define C_FG      0xFFFFu
#define C_OK      0x07F8u   /* green  */
#define C_WARN    0xFFE0u   /* amber  */
#define C_ERR     0xF800u   /* red    */
#define C_DIM     0x8410u
#define C_HDR     0x0020u   /* very dark blue */
#define C_SEL     0x0210u   /* dark green highlight */
#define C_LORA    0x07FFu   /* cyan — LR1121 */
#define C_FSK     0xFD20u   /* orange — CC1121 */

/* ── GPIO pins ──────────────────────────────────────────────────────────── */
#define PIN_UP       4
#define PIN_DOWN     5
#define PIN_LEFT     6
#define PIN_RIGHT    7
#define PIN_A        15
#define PIN_B        16
#define PIN_SET      0   /* active-LOW */

/* Radio control pins (SPI is managed by AkiraOS SPI driver) */
#define GPIO_CC_CS   10  /* CC1121 chip select  */
#define GPIO_CC_GDO0 34  /* CC1121 sync detect  */
#define GPIO_LR_CS   38  /* LR1121 chip select  */
#define GPIO_LR_BUSY 39  /* LR1121 BUSY         */
#define GPIO_LR_DIO9 40  /* LR1121 IRQ          */
#define GPIO_LR_NRST 41  /* LR1121 NRESET       */
#define GPIO_SPI_SCK 12
#define GPIO_SPI_MOSI 11
#define GPIO_SPI_MISO 13

/* ── Modes ──────────────────────────────────────────────────────────────── */
typedef enum {
    MODE_MONITOR  = 0,
    MODE_LINK     = 1,
    MODE_SCAN     = 2,
    MODE_TXBENCH  = 3,
    MODE_CONFIG   = 4,
    MODE_COUNT    = 5
} field_mode_t;

/* ── CC1121 enums ───────────────────────────────────────────────────────── */
#define CC_MOD_2FSK  0
#define CC_MOD_4FSK  1
#define CC_MOD_GFSK  2
#define CC_MOD_MSK   3

typedef struct {
    uint32_t freq_hz;
    uint8_t  modulation;
    uint32_t datarate_bps;
    int8_t   tx_power_dbm;
    uint8_t  preamble_bytes;
    uint32_t rx_bw_hz;
    uint8_t  sync_word[4];
    uint8_t  sync_len;
    uint8_t  crc_en;
    uint8_t  whitening_en;
} cc1121_cfg_t;

/* ── LR1121 enums ───────────────────────────────────────────────────────── */
typedef enum { LR_PKT_GFSK=0x00, LR_PKT_LORA=0x01 } lr_pkt_type_t;
#define LR_BW_125  0x06
#define LR_BW_250  0x05
#define LR_BW_500  0x04
#define LR_BW_200  0x16
#define LR_BW_400  0x15
#define LR_BW_800  0x14
#define LR_CR_4_5  0x01
#define LR_CR_4_6  0x02
#define LR_CR_4_7  0x03
#define LR_CR_4_8  0x04
#define LR_IRQ_TX_DONE    (1u<<2)
#define LR_IRQ_RX_DONE    (1u<<3)
#define LR_IRQ_CRC_ERROR  (1u<<10)
#define LR_IRQ_HDR_ERROR  (1u<<9)
#define LR_IRQ_PREAMBLE   (1u<<7)

typedef struct {
    uint32_t     freq_hz;
    lr_pkt_type_t pkt_type;
    uint8_t      sf;          /* 5-12 */
    uint8_t      bw;          /* LR_BW_* */
    uint8_t      cr;          /* LR_CR_* */
    uint8_t      ldro;
    int8_t       tx_power_dbm;
    uint8_t      pa_sel;      /* 0=HP 1=LP */
    uint32_t     gfsk_br_bps;
    uint32_t     gfsk_fdev_hz;
    uint8_t      preamble_sym;
    uint8_t      header_explicit;
    uint8_t      crc_type;
    uint8_t      iq_invert;
} lr1121_cfg_t;

/* ── Packet ring buffer ─────────────────────────────────────────────────── */
#define RX_RING_SIZE  8
#define PKT_MAX_LEN   127

typedef struct {
    uint8_t  data[PKT_MAX_LEN];
    uint8_t  len;
    int8_t   rssi_dbm;
    uint8_t  lqi;        /* CC1121 */
    int8_t   snr_qdb;    /* LR1121: quarter-dB */
    uint16_t seq;
    uint32_t ts_us;
} rx_pkt_t;

typedef struct {
    rx_pkt_t pkt[RX_RING_SIZE];
    volatile uint8_t head, tail;
    uint32_t rx_count;
    uint32_t err_count;
    uint8_t  overflow;
} rx_ring_t;

/* ── Link-test packet ───────────────────────────────────────────────────── */
typedef struct {
    uint8_t  magic[2];   /* 0xAC 0xFD */
    uint8_t  role;       /* 1=PING 2=PONG */
    uint8_t  radio;      /* 1=CC1121 2=LR1121 */
    uint16_t seq;
    uint32_t tx_ts;
    uint8_t  payload[8];
    uint16_t crc16;
} __attribute__((packed)) field_pkt_t;

/* ── RTT statistics ─────────────────────────────────────────────────────── */
#define RTT_HIST_BINS 16

typedef struct {
    uint32_t ok, total;
    uint32_t rtt_min_us, rtt_max_us, rtt_sum_us;
    uint16_t hist[RTT_HIST_BINS]; /* 0..15 bins covering 0..7680 ms */
    int8_t   last_rssi;
    int8_t   last_snr_qdb;
    uint8_t  last_lqi;
} link_stats_t;

/* ── Scan waterfall ─────────────────────────────────────────────────────── */
#define WFALL_W  158
#define WFALL_H   96
#define RSSI_FLOOR  (-120)
#define RSSI_CEIL   (-40)

typedef struct {
    uint8_t  buf[WFALL_W];   /* RSSI height per channel, 0..WFALL_H */
    uint32_t freq_start_hz;
    uint32_t freq_stop_hz;
    uint32_t step_hz;
    uint32_t cur_ch;
    uint32_t n_ch;
    int8_t   peak_rssi;
    uint32_t peak_freq_hz;
    uint8_t  dirty;
} scan_state_t;

/* ── TX bench ───────────────────────────────────────────────────────────── */
typedef enum { TX_PAT_CW=0, TX_PAT_PRN, TX_PAT_CUSTOM, TX_PAT_PREAMBLE } tx_pattern_t;

typedef struct {
    uint8_t      active;
    tx_pattern_t pattern;
    uint8_t      custom_payload[8];
    uint32_t     burst_count;
    uint32_t     interval_ms;
    volatile uint32_t sent;
    volatile uint8_t  running;
} tx_bench_t;

/* ── Config (NVS-backed) ────────────────────────────────────────────────── */
#define FIELD_CFG_MAGIC 0xAF1E0002u

typedef struct {
    uint32_t     magic;
    cc1121_cfg_t cc;
    lr1121_cfg_t lr;
    uint8_t      role;     /* 0=MASTER 1=SLAVE */
    uint32_t     link_interval_ms;
    uint32_t     crc32;    /* CRC32 of bytes [0..sizeof-4] */
} field_cfg_t;

/* ── Global state ───────────────────────────────────────────────────────── */
extern field_mode_t  g_mode;
extern field_cfg_t   g_cfg;
extern rx_ring_t     g_rx_cc;
extern rx_ring_t     g_rx_lr;
extern link_stats_t  g_stat_cc;
extern link_stats_t  g_stat_lr;
extern scan_state_t  g_scan_cc;
extern scan_state_t  g_scan_lr;
extern tx_bench_t    g_txb_cc;
extern tx_bench_t    g_txb_lr;
extern volatile uint32_t g_irq_cc;  /* pending CC1121 events */
extern volatile uint32_t g_irq_lr;  /* pending LR1121 IRQ flags */
extern int           g_paused;
extern int           g_config_radio; /* 0=CC 1=LR */
extern int           g_config_field; /* selected field in CONFIG mode */
extern uint32_t      g_uptime_us;    /* updated each loop */

/* ── AkiraOS API forwards ───────────────────────────────────────────────── */
extern int display_clear(uint32_t c);
extern int display_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t c);
extern int display_text(int32_t x,int32_t y,const char *t,uint32_t c);
extern int display_text_large(int32_t x,int32_t y,const char *t,uint32_t c);
extern int display_hline(int32_t x,int32_t y,int32_t l,uint32_t c);
extern int display_vline(int32_t x,int32_t y,int32_t l,uint32_t c);
extern int display_flush(void);
extern int gpio_configure(uint32_t p,uint32_t f);
extern int gpio_read(uint32_t p);
extern int gpio_write(uint32_t p,uint32_t v);
extern int delay(uint32_t us);
extern int settings_get(const char *k,char *b,int32_t l);
extern int settings_set(const char *k,const char *v);
extern int timer_create(void);
extern int timer_start(int32_t h);
extern int timer_elapsed(int32_t h);
/* AkiraOS SPI API (native, exposed via CAP_SPI) */
extern int spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, int len);

/* ── GPIO flags ─────────────────────────────────────────────────────────── */
#ifndef GPIO_INPUT
#define GPIO_INPUT            (1u<<0)
#define GPIO_OUTPUT           (1u<<1)
#define GPIO_OUTPUT_INIT_LOW  (1u<<2)
#define GPIO_OUTPUT_INIT_HIGH (1u<<3)
#define GPIO_PULL_UP          (1u<<4)
#define GPIO_PULL_DOWN        (1u<<5)
#define GPIO_ACTIVE_LOW       (1u<<6)
#endif

/* ── Subsystem APIs ─────────────────────────────────────────────────────── */
/* field_cc1121.c */
void     cc1121_init(const cc1121_cfg_t *cfg);
void     cc1121_write_reg(uint8_t addr, uint8_t val);
uint8_t  cc1121_read_reg(uint8_t addr);
void     cc1121_strobe(uint8_t cmd);
void     cc1121_tx(const uint8_t *buf, uint8_t len);
uint8_t  cc1121_rx_ready(void);
uint8_t  cc1121_rx_read(uint8_t *buf, uint8_t max_len);
int8_t   cc1121_rssi(void);
uint8_t  cc1121_lqi(void);
uint8_t  cc1121_state(void);
void     cc1121_set_freq(uint32_t freq_hz);
void     cc1121_set_power(int8_t dbm);

/* field_lr1121.c */
void     lr1121_init(void);
void     lr1121_set_rf_freq(uint32_t freq_hz);
void     lr1121_set_pkt_type(lr_pkt_type_t t);
void     lr1121_set_lora_params(uint8_t sf,uint8_t bw,uint8_t cr,uint8_t ldro);
void     lr1121_set_gfsk_params(uint32_t br,uint32_t fdev,uint8_t ps);
void     lr1121_set_tx_power(int8_t dbm,uint8_t pa_sel);
void     lr1121_set_rx(uint32_t timeout_ms);
void     lr1121_set_tx(uint8_t *buf,uint8_t len,uint32_t timeout_ms);
uint32_t lr1121_get_irq(void);
void     lr1121_clear_irq(uint32_t mask);
int16_t  lr1121_get_rssi(void);
int8_t   lr1121_get_snr(void);
uint8_t  lr1121_rx_read(uint8_t *buf,uint8_t max_len);
void     lr1121_set_irq_mask(uint32_t mask);
uint8_t  lr1121_busy(void);
void     lr1121_hard_reset(void);

/* field_config.c */
void     field_cfg_defaults(field_cfg_t *cfg);
int      field_cfg_load(field_cfg_t *cfg);
void     field_cfg_save(const field_cfg_t *cfg);

/* field_link.c */
void     link_init(void);
void     link_tick(void);
void     link_reset_stats(void);
uint16_t crc16_ccitt(const uint8_t *buf, int len);

/* field_scan.c */
void     scan_init(void);
void     scan_tick_cc(void);
void     scan_tick_lr(void);

/* field_txbench.c */
void     txbench_init(void);
void     txbench_tick(void);
void     txbench_start(int radio);
void     txbench_stop(int radio);

/* field_ui.c */
void     ui_draw(void);
void     ui_key(int key, int long_press);
/* key codes */
#define KEY_UP     0
#define KEY_DOWN   1
#define KEY_LEFT   2
#define KEY_RIGHT  3
#define KEY_A      4
#define KEY_B      5
#define KEY_SET    6
#define KEY_NONE  -1

/* ── Helpers ────────────────────────────────────────────────────────────── */
static inline uint32_t u32_min(uint32_t a,uint32_t b){return a<b?a:b;}
static inline uint32_t u32_max(uint32_t a,uint32_t b){return a>b?a:b;}
static inline void mem_set(void*p,int c,int n){uint8_t*q=(uint8_t*)p;while(n--)*q++=c;}
static inline void mem_cpy(void*d,const void*s,int n){
    uint8_t*dd=(uint8_t*)d;const uint8_t*ss=(const uint8_t*)s;while(n--)*dd++=*ss++;}

static inline int str_len(const char*s){int n=0;while(s[n])n++;return n;}
static inline void str_cpy(char*d,const char*s,int m){
    int i=0;while(i<m-1&&s[i]){d[i]=s[i];i++;}d[i]='\0';}

/* Integer to decimal string, returns pointer to static buf */
static char _itoa_buf[16];
static inline const char *u32_str(uint32_t v){
    if(!v){_itoa_buf[0]='0';_itoa_buf[1]='\0';return _itoa_buf;}
    int i=0;uint32_t t=v;
    while(t){_itoa_buf[i++]='0'+t%10;t/=10;}
    for(int a=0,b=i-1;a<b;a++,b--){char x=_itoa_buf[a];_itoa_buf[a]=_itoa_buf[b];_itoa_buf[b]=x;}
    _itoa_buf[i]='\0';return _itoa_buf;
}
static inline const char *i8_str(int8_t v){
    if(v>=0) return u32_str((uint32_t)v);
    _itoa_buf[0]='-'; uint32_t a=(uint32_t)(v<-127?128:(-v));
    const char *s=u32_str(a); int l=str_len(s);
    for(int i=0;i<=l;i++) _itoa_buf[i+1]=s[i];
    return _itoa_buf;
}

/* CRC32 for config validation */
static inline uint32_t crc32_byte(uint32_t crc, uint8_t b){
    crc^=b;
    for(int i=0;i<8;i++) crc=(crc>>1)^(crc&1?0xEDB88320u:0u);
    return crc;
}
static inline uint32_t crc32(const uint8_t *buf, int len){
    uint32_t c=0xFFFFFFFFu;
    while(len--) c=crc32_byte(c,*buf++);
    return c^0xFFFFFFFFu;
}

/* Hex nibble */
static inline char hex_c(uint8_t n){return n<10?'0'+n:'A'+(n-10);}

#endif /* FIELD_H */
