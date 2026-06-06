/*
 * field_link.c — Ping-pong link test state machine + RTT stats
 * SPDX-License-Identifier: Apache-2.0
 */
#include "field.h"

/* ── CRC-16/CCITT ───────────────────────────────────────────────────────── */
uint16_t crc16_ccitt(const uint8_t *buf, int len) {
    uint16_t crc = 0xFFFFu;
    while(len--) {
        crc ^= ((uint16_t)*buf++) << 8;
        for(int i=0;i<8;i++)
            crc = (crc & 0x8000u) ? (crc<<1)^0x1021u : crc<<1;
    }
    return crc;
}

/* ── Link state ─────────────────────────────────────────────────────────── */
typedef enum {
    LS_IDLE=0, LS_SEND_CC, LS_WAIT_CC, LS_SEND_LR, LS_WAIT_LR
} link_state_t;

static link_state_t ls       = LS_IDLE;
static uint16_t     seq_cc   = 0;
static uint16_t     seq_lr   = 0;
static uint32_t     ts_ping  = 0;   /* uptime_us when last ping was sent */
static uint32_t     ts_next  = 0;   /* uptime_us when next ping is due   */
static uint32_t     timeout_us = 0;

#define CC_TIMEOUT_US  500000u   /* 500 ms */
#define LR_TIMEOUT_US 2000000u   /* 2 s    */

static void stats_update(link_stats_t *s, int ok, uint32_t rtt_us,
                         int8_t rssi, int8_t snr, uint8_t lqi) {
    s->total++;
    if(ok) {
        s->ok++;
        if(!s->rtt_min_us || rtt_us < s->rtt_min_us) s->rtt_min_us = rtt_us;
        if(rtt_us > s->rtt_max_us) s->rtt_max_us = rtt_us;
        s->rtt_sum_us += rtt_us;
        /* Histogram: 16 bins, each bin = 480 ms → 0..7680 ms */
        uint32_t bin = rtt_us / 480000u;
        if(bin >= RTT_HIST_BINS) bin = RTT_HIST_BINS-1;
        s->hist[bin]++;
    }
    s->last_rssi    = rssi;
    s->last_snr_qdb = snr;
    s->last_lqi     = lqi;
}

static void build_ping(field_pkt_t *p, uint8_t radio, uint16_t seq) {
    p->magic[0]=0xAC; p->magic[1]=0xFD;
    p->role = 0x01; /* PING */
    p->radio = radio;
    p->seq  = seq;
    p->tx_ts = g_uptime_us;
    for(int i=0;i<8;i++) p->payload[i]=0xAA;
    p->crc16 = crc16_ccitt((uint8_t*)p, sizeof(*p)-2);
}

static int check_pong(const uint8_t *buf, uint8_t len, uint8_t radio, uint16_t seq) {
    if(len < sizeof(field_pkt_t)) return 0;
    const field_pkt_t *p = (const field_pkt_t*)buf;
    if(p->magic[0]!=0xAC||p->magic[1]!=0xFD) return 0;
    if(p->role!=0x02) return 0;
    if(p->radio!=radio) return 0;
    if(p->seq!=seq) return 0;
    uint16_t calc = crc16_ccitt((const uint8_t*)p, sizeof(*p)-2);
    return (calc == p->crc16) ? 1 : 0;
}

void link_init(void) {
    ls = LS_IDLE;
    seq_cc = seq_lr = 0;
    ts_next = g_uptime_us + 500000u;
}

void link_reset_stats(void) {
    mem_set(&g_stat_cc, 0, sizeof(g_stat_cc));
    mem_set(&g_stat_lr, 0, sizeof(g_stat_lr));
}

void link_tick(void) {
    if(g_cfg.role != 0) {
        /* SLAVE mode: check for incoming PING and echo back PONG */
        uint8_t buf[PKT_MAX_LEN];
        uint8_t len;

        len = cc1121_rx_read(buf, sizeof(buf));
        if(len >= (uint8_t)sizeof(field_pkt_t)) {
            field_pkt_t *p = (field_pkt_t*)buf;
            if(p->magic[0]==0xAC&&p->magic[1]==0xFD&&p->role==0x01&&p->radio==0x01){
                p->role=0x02; p->tx_ts=g_uptime_us;
                p->crc16=crc16_ccitt((uint8_t*)p,sizeof(*p)-2);
                cc1121_tx((uint8_t*)p,sizeof(*p));
            }
        }
        len = lr1121_rx_read(buf, sizeof(buf));
        if(len >= (uint8_t)sizeof(field_pkt_t)) {
            field_pkt_t *p = (field_pkt_t*)buf;
            if(p->magic[0]==0xAC&&p->magic[1]==0xFD&&p->role==0x01&&p->radio==0x02){
                p->role=0x02; p->tx_ts=g_uptime_us;
                p->crc16=crc16_ccitt((uint8_t*)p,sizeof(*p)-2);
                lr1121_set_tx((uint8_t*)p,sizeof(*p),2000);
            }
        }
        return;
    }

    /* MASTER state machine */
    uint8_t buf[PKT_MAX_LEN];
    uint8_t len;
    uint32_t now = g_uptime_us;
    field_pkt_t ping;

    switch(ls) {
    case LS_IDLE:
        if((int32_t)(now - ts_next) >= 0) {
            ls = LS_SEND_CC;
        }
        break;

    case LS_SEND_CC:
        build_ping(&ping, 0x01, seq_cc);
        cc1121_tx((uint8_t*)&ping, sizeof(ping));
        ts_ping  = now;
        timeout_us = CC_TIMEOUT_US;
        ls = LS_WAIT_CC;
        break;

    case LS_WAIT_CC:
        len = cc1121_rx_read(buf, sizeof(buf));
        if(len) {
            if(check_pong(buf, len, 0x01, seq_cc)) {
                uint32_t rtt = now - ts_ping;
                stats_update(&g_stat_cc, 1, rtt,
                             cc1121_rssi(), 0, cc1121_lqi());
            } else {
                stats_update(&g_stat_cc, 0, 0, cc1121_rssi(), 0, 0);
            }
            seq_cc++; ls = LS_SEND_LR;
        } else if((now - ts_ping) > timeout_us) {
            stats_update(&g_stat_cc, 0, 0, 0, 0, 0);
            seq_cc++; ls = LS_SEND_LR;
        }
        break;

    case LS_SEND_LR:
        build_ping(&ping, 0x02, seq_lr);
        lr1121_set_tx((uint8_t*)&ping, sizeof(ping), 2000);
        ts_ping  = now;
        timeout_us = LR_TIMEOUT_US;
        ls = LS_WAIT_LR;
        break;

    case LS_WAIT_LR: {
        uint32_t irq = lr1121_get_irq();
        if(irq & LR_IRQ_RX_DONE) {
            lr1121_clear_irq(LR_IRQ_RX_DONE);
            len = lr1121_rx_read(buf, sizeof(buf));
            if(check_pong(buf, len, 0x02, seq_lr)) {
                uint32_t rtt = now - ts_ping;
                int16_t rssi = lr1121_get_rssi();
                int8_t snr   = lr1121_get_snr();
                stats_update(&g_stat_lr, 1, rtt, (int8_t)(rssi/-10), snr, 0);
            } else {
                stats_update(&g_stat_lr, 0, 0, 0, 0, 0);
            }
            seq_lr++; ls = LS_IDLE;
            ts_next = now + g_cfg.link_interval_ms * 1000u;
        } else if(irq & LR_IRQ_CRC_ERROR) {
            lr1121_clear_irq(LR_IRQ_CRC_ERROR);
            stats_update(&g_stat_lr, 0, 0, 0, 0, 0);
            seq_lr++; ls = LS_IDLE;
            ts_next = now + g_cfg.link_interval_ms * 1000u;
        } else if((now - ts_ping) > timeout_us) {
            stats_update(&g_stat_lr, 0, 0, 0, 0, 0);
            seq_lr++; ls = LS_IDLE;
            ts_next = now + g_cfg.link_interval_ms * 1000u;
        }
        break;
    }
    }
}
