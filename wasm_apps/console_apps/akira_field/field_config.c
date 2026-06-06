/*
 * field_config.c — NVS-backed config with CRC32 validation
 * SPDX-License-Identifier: Apache-2.0
 */
#include "field.h"

/* Global state */
field_mode_t  g_mode      = MODE_MONITOR;
field_cfg_t   g_cfg;
rx_ring_t     g_rx_cc;
rx_ring_t     g_rx_lr;
link_stats_t  g_stat_cc;
link_stats_t  g_stat_lr;
scan_state_t  g_scan_cc;
scan_state_t  g_scan_lr;
tx_bench_t    g_txb_cc;
tx_bench_t    g_txb_lr;
volatile uint32_t g_irq_cc = 0;
volatile uint32_t g_irq_lr = 0;
int           g_paused     = 0;
int           g_config_radio = 0;
int           g_config_field = 0;
uint32_t      g_uptime_us  = 0;

/* NVS key */
#define CFG_KEY "field_cfg_0"

void field_cfg_defaults(field_cfg_t *c) {
    mem_set(c, 0, sizeof(*c));
    c->magic = FIELD_CFG_MAGIC;

    /* CC1121 — AkiraMesh preset: 433.920 MHz, 2-FSK 4.8 kbps, +14 dBm */
    c->cc.freq_hz       = 433920000u;
    c->cc.modulation    = CC_MOD_2FSK;
    c->cc.datarate_bps  = 4800u;
    c->cc.tx_power_dbm  = 14;
    c->cc.preamble_bytes= 4;
    c->cc.rx_bw_hz      = 203000u;
    c->cc.sync_word[0]  = 0xD3; c->cc.sync_word[1] = 0x91;
    c->cc.sync_word[2]  = 0xD3; c->cc.sync_word[3] = 0x91;
    c->cc.sync_len      = 2;
    c->cc.crc_en        = 1;
    c->cc.whitening_en  = 1;

    /* LR1121 — EU868 LoRa: 868.1 MHz, SF7 BW125 CR4/5, +14 dBm */
    c->lr.freq_hz       = 868100000u;
    c->lr.pkt_type      = LR_PKT_LORA;
    c->lr.sf            = 7;
    c->lr.bw            = LR_BW_125;
    c->lr.cr            = LR_CR_4_5;
    c->lr.ldro          = 1;
    c->lr.tx_power_dbm  = 14;
    c->lr.pa_sel        = 0; /* HP */
    c->lr.gfsk_br_bps   = 9600u;
    c->lr.gfsk_fdev_hz  = 25000u;
    c->lr.preamble_sym  = 8;
    c->lr.header_explicit = 1;
    c->lr.crc_type      = 1;
    c->lr.iq_invert     = 0;

    c->role             = 0; /* MASTER */
    c->link_interval_ms = 500u;

    /* Compute CRC */
    c->crc32 = crc32((const uint8_t*)c, sizeof(*c)-4);
}

int field_cfg_load(field_cfg_t *c) {
    char buf[sizeof(field_cfg_t)*2+4]; /* hex encoded would be bigger, use binary via settings hack */
    /* AkiraOS settings API stores strings — store as raw bytes via storage API */
    /* For simplicity, store as hex string. Max size = sizeof*2 chars */
    int r = settings_get(CFG_KEY, buf, sizeof(buf));
    if(r < 0) return -1;

    /* Parse hex string back to binary */
    int expected = (int)sizeof(field_cfg_t) * 2;
    if(str_len(buf) < expected) return -2;

    uint8_t *raw = (uint8_t*)c;
    for(int i=0;i<(int)sizeof(field_cfg_t);i++){
        uint8_t hi=buf[i*2];
        uint8_t lo=buf[i*2+1];
        hi = hi>='a'?hi-'a'+10:hi>='A'?hi-'A'+10:hi-'0';
        lo = lo>='a'?lo-'a'+10:lo>='A'?lo-'A'+10:lo-'0';
        raw[i]=(hi<<4)|lo;
    }

    /* Validate */
    if(c->magic != FIELD_CFG_MAGIC) return -3;
    uint32_t calc = crc32((const uint8_t*)c, sizeof(*c)-4);
    if(calc != c->crc32) return -4;
    return 0;
}

void field_cfg_save(const field_cfg_t *c) {
    field_cfg_t tmp; mem_cpy(&tmp, c, sizeof(tmp));
    tmp.crc32 = crc32((const uint8_t*)&tmp, sizeof(tmp)-4);

    /* Encode as hex string */
    const uint8_t *raw = (const uint8_t*)&tmp;
    char buf[sizeof(field_cfg_t)*2+2];
    for(int i=0;i<(int)sizeof(field_cfg_t);i++){
        buf[i*2]   = hex_c(raw[i]>>4);
        buf[i*2+1] = hex_c(raw[i]&0xFu);
    }
    buf[sizeof(field_cfg_t)*2] = '\0';
    settings_set(CFG_KEY, buf);
}
