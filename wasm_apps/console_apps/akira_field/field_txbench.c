/*
 * field_txbench.c — TX burst engine with pattern generator
 * SPDX-License-Identifier: Apache-2.0
 */
#include "field.h"

/* ── PN9 pseudo-random sequence generator ───────────────────────────────── */
static uint16_t pn9_state = 0x01FFu;
static uint8_t pn9_next(void) {
    uint8_t out = 0;
    for(int i=7;i>=0;i--){
        int bit = ((pn9_state>>8)^(pn9_state>>4))&1;
        out |= (uint8_t)(bit<<i);
        pn9_state = ((pn9_state<<1)|bit)&0x1FFu;
    }
    return out;
}

static uint32_t last_tx_cc_us = 0;
static uint32_t last_tx_lr_us = 0;

void txbench_init(void) {
    mem_set(&g_txb_cc, 0, sizeof(g_txb_cc));
    mem_set(&g_txb_lr, 0, sizeof(g_txb_lr));
    g_txb_cc.burst_count = 100;
    g_txb_cc.interval_ms = 500;
    g_txb_cc.pattern = TX_PAT_CUSTOM;
    g_txb_cc.custom_payload[0]=0xDE; g_txb_cc.custom_payload[1]=0xAD;
    g_txb_cc.custom_payload[2]=0xBE; g_txb_cc.custom_payload[3]=0xEF;

    g_txb_lr.burst_count = 100;
    g_txb_lr.interval_ms = 500;
    g_txb_lr.pattern = TX_PAT_CUSTOM;
    g_txb_lr.custom_payload[0]=0xAC; g_txb_lr.custom_payload[1]=0xCE;
    g_txb_lr.custom_payload[2]=0x55; g_txb_lr.custom_payload[3]=0xAA;
}

void txbench_start(int radio) {
    if(radio==0) { g_txb_cc.running=1; g_txb_cc.sent=0; }
    else         { g_txb_lr.running=1; g_txb_lr.sent=0; }
}

void txbench_stop(int radio) {
    if(radio==0) g_txb_cc.running=0;
    else         g_txb_lr.running=0;
}

static void fill_pattern(tx_bench_t *t, uint8_t *buf, uint8_t *len) {
    *len = 20;
    switch(t->pattern) {
    case TX_PAT_CW:
        /* CW: fill with 0x55 (alternating for FSK) */
        for(int i=0;i<20;i++) buf[i]=0x55u;
        break;
    case TX_PAT_PRN:
        for(int i=0;i<20;i++) buf[i]=pn9_next();
        break;
    case TX_PAT_CUSTOM:
        for(int i=0;i<20;i++) buf[i]=t->custom_payload[i%8];
        break;
    case TX_PAT_PREAMBLE:
        for(int i=0;i<20;i++) buf[i]=0xAAu;
        break;
    }
}

void txbench_tick(void) {
    uint32_t now = g_uptime_us;
    uint8_t buf[32]; uint8_t len;

    /* CC1121 bench */
    if(g_txb_cc.running && g_txb_cc.sent < g_txb_cc.burst_count) {
        uint32_t iv = g_txb_cc.interval_ms * 1000u;
        if((now - last_tx_cc_us) >= iv) {
            fill_pattern(&g_txb_cc, buf, &len);
            cc1121_tx(buf, len);
            g_txb_cc.sent++;
            last_tx_cc_us = now;
        }
    } else if(g_txb_cc.running && g_txb_cc.sent >= g_txb_cc.burst_count) {
        g_txb_cc.running = 0;
    }

    /* LR1121 bench */
    if(g_txb_lr.running && g_txb_lr.sent < g_txb_lr.burst_count) {
        uint32_t iv = g_txb_lr.interval_ms * 1000u;
        if((now - last_tx_lr_us) >= iv) {
            fill_pattern(&g_txb_lr, buf, &len);
            lr1121_set_tx(buf, len, 2000);
            g_txb_lr.sent++;
            last_tx_lr_us = now;
        }
    } else if(g_txb_lr.running && g_txb_lr.sent >= g_txb_lr.burst_count) {
        g_txb_lr.running = 0;
    }
}
