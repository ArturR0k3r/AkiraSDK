/*
 * field_ui.c — AkiraField display renderer, all 5 modes
 * 320×240 RGB565, 8×8 monospace content / 8×13 status headers
 * SPDX-License-Identifier: Apache-2.0
 */
#include "field.h"

/* ── Rendering primitives ───────────────────────────────────────────────── */
#define TX(x) (x)
#define TY(y) (y)

static void tx8(int x, int y, const char *s, uint16_t c) {
    display_text(x, y, s, c);
}
static void txl(int x, int y, const char *s, uint16_t c) {
    display_text_large(x, y, s, c);
}
static void row_bg(int y, int h, uint16_t c) {
    display_rect(0, y, SCR_W, h, c);
}
static void pane_bg(int x, int y, int w, int h, uint16_t c) {
    display_rect(x, y, w, h, c);
}

/* ── Helpers ────────────────────────────────────────────────────────────── */
static char _sb[48];
static const char *fmt_mhz(uint32_t hz) {
    uint32_t mhz = hz / 1000000u;
    uint32_t khz = (hz % 1000000u) / 1000u;
    /* "868.1" */
    int i=0;
    const char *ms=u32_str(mhz); while(*ms)_sb[i++]=*ms++;
    _sb[i++]='.';
    const char *ks=u32_str(khz/100); while(*ks)_sb[i++]=*ks++;
    _sb[i]='\0'; return _sb;
}
static const char *fmt_kbps(uint32_t bps) {
    uint32_t k=bps/1000; uint32_t r=(bps%1000)/100;
    int i=0; const char *s=u32_str(k); while(*s)_sb[i++]=*s++;
    _sb[i++]='.'; _sb[i++]='0'+r; _sb[i++]='k'; _sb[i]='\0';
    return _sb;
}
static const char *mod_str(uint8_t m) {
    switch(m){case CC_MOD_2FSK:return "2-FSK";case CC_MOD_4FSK:return "4-FSK";
              case CC_MOD_GFSK:return "GFSK"; case CC_MOD_MSK: return "MSK";}
    return "???";
}

/* ── Status bar ─────────────────────────────────────────────────────────── */
static void draw_status(void) {
    row_bg(0, STATUS_H, C_HDR);
    /* Left: CC1121 status */
    char buf[64]; int pos=0;
    buf[pos++]='C'; buf[pos++]='C'; buf[pos++]=':';
    const char *mhz=fmt_mhz(g_cfg.cc.freq_hz);
    while(*mhz)buf[pos++]=*mhz++;
    buf[pos++]=' ';
    const char *md=mod_str(g_cfg.cc.modulation);
    while(*md)buf[pos++]=*md++;
    buf[pos]='\0';
    tx8(2, 2, buf, C_FSK);

    /* Right: LR1121 status */
    pos=0;
    char lr_buf[48];
    lr_buf[pos++]='L'; lr_buf[pos++]='R'; lr_buf[pos++]=':';
    const char *lmhz=fmt_mhz(g_cfg.lr.freq_hz);
    while(*lmhz)lr_buf[pos++]=*lmhz++;
    lr_buf[pos++]=' ';
    lr_buf[pos++]='S'; lr_buf[pos++]='F'; lr_buf[pos++]='0'+g_cfg.lr.sf;
    lr_buf[pos]='\0';
    tx8(SCR_W/2+2, 2, lr_buf, C_LORA);

    /* Vertical divider */
    display_vline(SCR_W/2, 0, STATUS_H, C_DIM);
    display_hline(0, STATUS_H-1, SCR_W, C_DIM);
}

/* ── Mode tab bar ───────────────────────────────────────────────────────── */
static const char *MODE_NAMES[MODE_COUNT]={"MONITOR","LINK TEST","SCAN","TX BENCH","CONFIG"};

static void draw_tabs(void) {
    row_bg(STATUS_H, TAB_H, C_BG);
    int tab_w = SCR_W / MODE_COUNT; /* 64 px each */
    for(int i=0;i<MODE_COUNT;i++){
        int x = i * tab_w;
        if((field_mode_t)i == g_mode) {
            display_rect(x, STATUS_H, tab_w-1, TAB_H, C_OK);
            tx8(x+2, STATUS_H+2, MODE_NAMES[i], C_BG);
        } else {
            tx8(x+2, STATUS_H+2, MODE_NAMES[i], C_DIM);
        }
    }
    display_hline(0, STATUS_H+TAB_H-1, SCR_W, C_DIM);
}

/* ── Action bar ─────────────────────────────────────────────────────────── */
static void draw_action(const char *left, const char *right) {
    row_bg(ACTION_Y, SCR_H-ACTION_Y, C_HDR);
    display_hline(0, ACTION_Y, SCR_W, C_DIM);
    if(left)  tx8(2, ACTION_Y+3, left,  C_FG);
    if(right) tx8(SCR_W-str_len(right)*8-2, ACTION_Y+3, right, C_OK);
}

/* ── Pane divider ───────────────────────────────────────────────────────── */
static void draw_pane_divider(void) {
    display_vline(PANE_W+1, CONTENT_Y, CONTENT_H, C_DIM);
    display_vline(PANE_W+2, CONTENT_Y, CONTENT_H, C_DIM);
}

/* ── MONITOR mode ───────────────────────────────────────────────────────── */
static void draw_rx_ring(rx_ring_t *ring, int px, int py, int pw,
                         uint16_t hdr_color, const char *title) {
    /* Header */
    display_rect(px, py, pw, 13, C_HDR);
    tx8(px+2, py+1, title, hdr_color);
    /* Separator */
    display_hline(px, py+13, pw, C_DIM);

    /* Show up to 4 packets */
    int row_y = py + 16;
    uint8_t t = ring->tail;
    uint8_t h = ring->head;
    uint8_t count = 0;
    while(t != h && count < 4) {
        rx_pkt_t *p = &ring->pkt[t];
        /* Line 1: seq + rssi + len */
        char line1[32]; int li=0;
        line1[li++]='#';
        const char *sq=u32_str(p->seq);
        while(*sq&&li<5)line1[li++]=*sq++;
        while(li<5)line1[li++]=' ';
        const char *rs=i8_str(p->rssi_dbm);
        while(*rs&&li<12)line1[li++]=*rs++;
        line1[li++]='d'; line1[li++]='B'; line1[li++]=' ';
        const char *ln=u32_str(p->len);
        while(*ln&&li<20)line1[li++]=*ln++;
        line1[li++]='B'; line1[li]='\0';
        tx8(px+2, row_y, line1, C_FG);
        row_y += 10;

        /* Line 2: hex preview */
        if(row_y < ACTION_Y-10 && p->len > 0) {
            char hex[32]; int hi=0;
            for(int i=0;i<4&&i<p->len;i++){
                hex[hi++]=hex_c(p->data[i]>>4);
                hex[hi++]=hex_c(p->data[i]&0xFu);
                hex[hi++]=' ';
            }
            if(p->len>4){hex[hi++]='.';hex[hi++]='.';hex[hi++]='.';}
            hex[hi]='\0';
            tx8(px+4, row_y, hex, C_DIM);
            row_y += 10;
        }

        t = (t+1) % RX_RING_SIZE;
        count++;
        if(row_y >= ACTION_Y-20) break;
    }

    /* Footer: counters */
    char footer[32]; int fi=0;
    footer[fi++]='R'; footer[fi++]='X'; footer[fi++]=':';
    const char *rc=u32_str(ring->rx_count); while(*rc)footer[fi++]=*rc++;
    footer[fi++]=' '; footer[fi++]='E'; footer[fi++]='R'; footer[fi++]='R';
    footer[fi++]=':';
    const char *ec=u32_str(ring->err_count); while(*ec)footer[fi++]=*ec++;
    footer[fi]='\0';
    display_hline(px, ACTION_Y-14, pw, C_DIM);
    tx8(px+2, ACTION_Y-12, footer, C_DIM);
}

static void draw_monitor(void) {
    char cc_title[32], lr_title[32];
    int i=0; const char *m=fmt_mhz(g_cfg.cc.freq_hz);
    cc_title[i++]='C'; cc_title[i++]='C';
    while(*m&&i<14)cc_title[i++]=*m++;
    cc_title[i++]=' ';
    const char *md=mod_str(g_cfg.cc.modulation);
    while(*md&&i<28)cc_title[i++]=*md++;
    cc_title[i]='\0';

    i=0; m=fmt_mhz(g_cfg.lr.freq_hz);
    lr_title[i++]='L'; lr_title[i++]='R';
    while(*m&&i<14)lr_title[i++]=*m++;
    lr_title[i++]=' '; lr_title[i++]='S'; lr_title[i++]='F';
    lr_title[i++]='0'+g_cfg.lr.sf; lr_title[i]='\0';

    draw_rx_ring(&g_rx_cc, 0, CONTENT_Y, PANE_W, C_FSK, cc_title);
    draw_pane_divider();
    draw_rx_ring(&g_rx_lr, PANE_SEP, CONTENT_Y, SCR_W-PANE_SEP, C_LORA, lr_title);

    draw_action("B:clear", g_paused?"A:resume":"A:freeze");
}

/* ── LINK TEST mode ─────────────────────────────────────────────────────── */
static void draw_stat_pane(link_stats_t *s, int px, uint16_t col,
                           const char *header, const char *params) {
    display_rect(px, CONTENT_Y, PANE_W, CONTENT_H, C_BG);
    /* Border */
    display_rect(px, CONTENT_Y, PANE_W, 1, col);
    display_rect(px, CONTENT_Y+CONTENT_H-1, PANE_W, 1, col);
    display_vline(px, CONTENT_Y, CONTENT_H, col);
    display_vline(px+PANE_W-1, CONTENT_Y, CONTENT_H, col);

    int y = CONTENT_Y + 3;
    tx8(px+4, y, header, col); y+=12;
    tx8(px+4, y, params, C_DIM); y+=14;

    /* RTT */
    tx8(px+4, y, "RTT:", C_DIM); y+=10;
    if(s->ok) {
        uint32_t rtt_avg = s->rtt_sum_us / s->ok;
        char rv[32]; int ri=0;
        const char *r=u32_str(rtt_avg/1000); while(*r)rv[ri++]=*r++;
        rv[ri++]=' '; rv[ri++]='m'; rv[ri++]='s'; rv[ri]='\0';
        tx8(px+8, y, rv, C_FG);
    } else tx8(px+8, y, "---", C_DIM);
    y+=10;

    tx8(px+4, y, "RSSI:", C_DIM);
    char rsstr[8]; int rsi=0;
    const char *rs=i8_str(s->last_rssi); while(*rs)rsstr[rsi++]=*rs++;
    rsstr[rsi++]='d'; rsstr[rsi++]='B'; rsstr[rsi]='\0';
    tx8(px+40, y, rsstr, s->last_rssi>-90?C_OK:s->last_rssi>-100?C_WARN:C_ERR);
    y+=10;

    tx8(px+4, y, "LQI:", C_DIM);
    tx8(px+36, y, u32_str(s->last_lqi), C_FG); y+=10;

    /* Packet loss */
    uint32_t loss_pct = s->total ? (s->total-s->ok)*100/s->total : 0;
    tx8(px+4, y, "OK:", C_DIM);
    char ok_str[24]; int oi=0;
    const char *ok=u32_str(s->ok); while(*ok)ok_str[oi++]=*ok++;
    ok_str[oi++]='/';
    const char *tot=u32_str(s->total); while(*tot)ok_str[oi++]=*tot++;
    ok_str[oi]='\0';
    tx8(px+28, y, ok_str, C_FG); y+=10;

    tx8(px+4, y, "LOSS:", C_DIM);
    char loss_str[8]; int li2=0;
    const char *lp=u32_str(loss_pct); while(*lp)loss_str[li2++]=*lp++;
    loss_str[li2++]='%'; loss_str[li2]='\0';
    tx8(px+44, y, loss_str, loss_pct<5?C_OK:loss_pct<20?C_WARN:C_ERR);
}

static void draw_link(void) {
    /* Header */
    pane_bg(0, CONTENT_Y, SCR_W, 14, C_HDR);
    char hdr[48]; int hi=0;
    const char *ms=g_cfg.role?"[SLAVE]":"[MASTER]"; while(*ms)hdr[hi++]=*ms++;
    hdr[hi++]=' '; hdr[hi++]='s'; hdr[hi++]='e'; hdr[hi++]='q'; hdr[hi++]=':';
    hdr[hi]='\0';
    tx8(2, CONTENT_Y+2, hdr, C_FG);

    char cc_params[32]; int ci=0;
    const char *cf=fmt_mhz(g_cfg.cc.freq_hz); while(*cf)cc_params[ci++]=*cf++;
    cc_params[ci++]=' ';
    const char *cm=mod_str(g_cfg.cc.modulation); while(*cm)cc_params[ci++]=*cm++;
    cc_params[ci]='\0';

    char lr_params[32]; int lpi=0;
    const char *lf=fmt_mhz(g_cfg.lr.freq_hz); while(*lf)lr_params[lpi++]=*lf++;
    lr_params[lpi++]=' '; lr_params[lpi++]='S'; lr_params[lpi++]='F';
    lr_params[lpi++]='0'+g_cfg.lr.sf; lr_params[lpi]='\0';

    draw_stat_pane(&g_stat_cc, 0,       C_FSK,  "CC1121", cc_params);
    draw_stat_pane(&g_stat_lr, PANE_SEP, C_LORA, "LR1121", lr_params);
    draw_action("B:role", "A:reset");
}

/* ── SCAN mode ──────────────────────────────────────────────────────────── */
static void draw_waterfall(scan_state_t *sc, int px, int py, int pw, int ph,
                           uint16_t col, const char *label) {
    display_rect(px, py, pw, ph, C_BG);
    tx8(px+2, py+1, label, col);
    int bar_y = py + 11;
    int bar_h = ph - 22;
    /* Draw bars */
    for(int i=0;i<pw&&i<WFALL_W;i++){
        uint8_t h = sc->buf[i];
        if(h > (uint8_t)bar_h) h = (uint8_t)bar_h;
        /* Background */
        display_vline(px+i, bar_y, bar_h, C_HDR);
        /* Bar */
        if(h > 0) {
            uint16_t bc = (uint16_t)((i==(int)sc->cur_ch) ? C_OK : col);
            display_vline(px+i, bar_y + bar_h - h, h, bc);
        }
    }
    /* Peak label */
    char peak_buf[32]; int pbi=0;
    const char *pf=fmt_mhz(sc->peak_freq_hz); while(*pf)peak_buf[pbi++]=*pf++;
    peak_buf[pbi++]=' ';
    const char *pr=i8_str(sc->peak_rssi); while(*pr)peak_buf[pbi++]=*pr++;
    peak_buf[pbi++]='d'; peak_buf[pbi++]='B'; peak_buf[pbi]='\0';
    tx8(px+2, bar_y+bar_h+2, peak_buf, C_WARN);
}

static void draw_scan(void) {
    int half_h = CONTENT_H / 2;
    draw_waterfall(&g_scan_cc, 0,       CONTENT_Y,         SCR_W, half_h,
                   C_FSK,  "CC1121 SCAN");
    display_hline(0, CONTENT_Y+half_h, SCR_W, C_DIM);
    draw_waterfall(&g_scan_lr, 0,       CONTENT_Y+half_h,  SCR_W, half_h,
                   C_LORA, "LR1121 CAD");
    draw_action("B:reset", "A:config");
}

/* ── TX BENCH mode ──────────────────────────────────────────────────────── */
static const char *PAT_NAMES[]={"CW","PRN","CUSTOM","PREAMBLE"};

static void draw_bench_pane(tx_bench_t *t, int px, uint16_t col,
                            uint32_t freq_hz, const char *mod) {
    display_rect(px, CONTENT_Y, PANE_W, CONTENT_H, C_BG);
    int y = CONTENT_Y + 2;
    const char *mhz = fmt_mhz(freq_hz);
    tx8(px+2, y, mhz, col); y+=12;
    tx8(px+2, y, mod, C_DIM); y+=12;

    tx8(px+2, y, "Pat:", C_DIM);
    tx8(px+34, y, PAT_NAMES[t->pattern], C_FG); y+=10;

    tx8(px+2, y, "Burst:", C_DIM);
    tx8(px+50, y, u32_str(t->burst_count), C_FG); y+=10;

    tx8(px+2, y, "Iv:", C_DIM);
    char iv_str[12]; int ivi=0;
    const char *iv=u32_str(t->interval_ms); while(*iv)iv_str[ivi++]=*iv++;
    iv_str[ivi++]='m'; iv_str[ivi++]='s'; iv_str[ivi]='\0';
    tx8(px+26, y, iv_str, C_FG); y+=14;

    /* Progress bar */
    uint32_t pct = t->burst_count ? t->sent * PANE_W / t->burst_count : 0;
    display_rect(px+2, y, PANE_W-4, 8, C_HDR);
    if(pct > 0) display_rect(px+2, y, (int)pct, 8, t->running?col:C_DIM);
    y+=12;

    char cnt[16]; int ci=0;
    const char *s=u32_str(t->sent); while(*s)cnt[ci++]=*s++;
    cnt[ci++]='/';
    const char *t2=u32_str(t->burst_count); while(*t2)cnt[ci++]=*t2++;
    cnt[ci]='\0';
    tx8(px+2, y, cnt, t->running?col:C_DIM);
}

static void draw_txbench(void) {
    draw_bench_pane(&g_txb_cc, 0,       C_FSK,
                    g_cfg.cc.freq_hz, mod_str(g_cfg.cc.modulation));
    draw_pane_divider();
    draw_bench_pane(&g_txb_lr, PANE_SEP, C_LORA,
                    g_cfg.lr.freq_hz, "LoRa");
    draw_action("B:stop", "A:start");
}

/* ── CONFIG mode ─────────────────────────────────────────────────────────── */
static const char *CC_FIELD_NAMES[]={"Freq MHz","Modulation","DataRate","RX BW",
    "TX Power","Sync Word","CRC","Whitening","Preamble"};
#define CC_FIELDS 9
static const char *LR_FIELD_NAMES[]={"Freq MHz","Pkt Type","SF","BW","CR",
    "TX Power","PA","GFSK BR","GFSK Fdev","Preamble","CRC","IQ Inv","LDRO"};
#define LR_FIELDS 13

static void draw_config(void) {
    int is_lr = g_config_radio;
    int nf = is_lr ? LR_FIELDS : CC_FIELDS;
    const char **names = is_lr ? LR_FIELD_NAMES : CC_FIELD_NAMES;

    display_rect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);
    display_hline(0, CONTENT_Y, SCR_W, is_lr?C_LORA:C_FSK);

    char hdr[32]; int hi=0;
    const char *rn=is_lr?"LR1121 CONFIG":"CC1121 CONFIG";
    while(*rn)hdr[hi++]=*rn++; hdr[hi]='\0';
    tx8(2, CONTENT_Y+3, hdr, is_lr?C_LORA:C_FSK);

    int start=g_config_field>6?g_config_field-6:0;
    for(int i=start;i<nf&&(i-start)<8;i++){
        int y = CONTENT_Y + 18 + (i-start)*20;
        int sel=(i==g_config_field);
        if(sel) display_rect(0, y-1, SCR_W, 18, C_SEL);
        tx8(4, y+2, names[i], sel?C_OK:C_DIM);

        /* Value on right */
        const char *val="---";
        char vbuf[32]; int vi=0;
        if(!is_lr) {
            switch(i){
            case 0: val=fmt_mhz(g_cfg.cc.freq_hz); break;
            case 1: val=mod_str(g_cfg.cc.modulation); break;
            case 2: val=fmt_kbps(g_cfg.cc.datarate_bps); break;
            case 3: { const char *bw=u32_str(g_cfg.cc.rx_bw_hz/1000);
                      while(*bw)vbuf[vi++]=*bw++;
                      vbuf[vi++]='k'; vbuf[vi]='\0'; val=vbuf; break; }
            case 4: { val=i8_str(g_cfg.cc.tx_power_dbm); vi=0;
                      const char *s=val; while(*s)vbuf[vi++]=*s++;
                      vbuf[vi++]='d'; vbuf[vi++]='B'; vbuf[vi++]='m';
                      vbuf[vi]='\0'; val=vbuf; break; }
            case 6: val=g_cfg.cc.crc_en?"ON":"OFF"; break;
            case 7: val=g_cfg.cc.whitening_en?"ON":"OFF"; break;
            case 8: val=u32_str(g_cfg.cc.preamble_bytes); break;
            }
        } else {
            switch(i){
            case 0: val=fmt_mhz(g_cfg.lr.freq_hz); break;
            case 1: val=g_cfg.lr.pkt_type==LR_PKT_LORA?"LoRa":"GFSK"; break;
            case 2: { vbuf[0]='S'; vbuf[1]='F'; const char *s=u32_str(g_cfg.lr.sf);
                      int k=2; while(*s)vbuf[k++]=*s++; vbuf[k]='\0'; val=vbuf; break; }
            case 3: val=g_cfg.lr.bw==LR_BW_125?"125k":
                        g_cfg.lr.bw==LR_BW_250?"250k":"500k"; break;
            case 4: val=g_cfg.lr.cr==LR_CR_4_5?"4/5":
                        g_cfg.lr.cr==LR_CR_4_6?"4/6":
                        g_cfg.lr.cr==LR_CR_4_7?"4/7":"4/8"; break;
            case 5: { val=i8_str(g_cfg.lr.tx_power_dbm); vi=0;
                      const char *s=val; while(*s)vbuf[vi++]=*s++;
                      vbuf[vi++]='d'; vbuf[vi++]='B'; vbuf[vi++]='m';
                      vbuf[vi]='\0'; val=vbuf; break; }
            case 6: val=g_cfg.lr.pa_sel?"LP":"HP"; break;
            case 11: val=g_cfg.lr.iq_invert?"ON":"OFF"; break;
            case 12: val=g_cfg.lr.ldro?"Auto":"OFF"; break;
            }
        }
        tx8(SCR_W-str_len(val)*8-4, y+2, val, sel?C_FG:C_DIM);
    }
    draw_action("B:save", "A:edit");
}

/* ── Top-level draw dispatch ─────────────────────────────────────────────── */
void ui_draw(void) {
    display_clear(C_BG);
    draw_status();
    draw_tabs();
    switch(g_mode){
    case MODE_MONITOR: draw_monitor();  break;
    case MODE_LINK:    draw_link();     break;
    case MODE_SCAN:    draw_scan();     break;
    case MODE_TXBENCH: draw_txbench();  break;
    case MODE_CONFIG:  draw_config();   break;
    }
    display_flush();
}

/* ── Key handler ─────────────────────────────────────────────────────────── */
void ui_key(int key, int long_press) {
    if(long_press && key==KEY_LEFT) {
        /* Force both radios IDLE */
        cc1121_strobe(0x36u); /* SIDLE */
        lr1121_set_rx(0);     /* single + no preamble = idle */
        return;
    }
    if(long_press && key==KEY_SET) {
        /* Reboot radios */
        cc1121_strobe(0x30u); /* SRES */
        delay(2000);
        lr1121_hard_reset();
        cc1121_init(&g_cfg.cc);
        lr1121_init();
        return;
    }

    switch(g_mode){
    case MODE_MONITOR:
        if(key==KEY_A) g_paused^=1;
        if(key==KEY_B) {
            mem_set(&g_rx_cc,0,sizeof(g_rx_cc));
            mem_set(&g_rx_lr,0,sizeof(g_rx_lr));
        }
        if(key==KEY_RIGHT) { g_mode=MODE_LINK; link_init(); }
        if(key==KEY_LEFT)  g_mode=MODE_CONFIG;
        break;

    case MODE_LINK:
        if(key==KEY_A) link_reset_stats();
        if(key==KEY_B) g_cfg.role^=1;
        if(key==KEY_LEFT)  g_mode=MODE_MONITOR;
        if(key==KEY_RIGHT) g_mode=MODE_SCAN;
        break;

    case MODE_SCAN:
        if(key==KEY_A) g_mode=MODE_CONFIG;
        if(key==KEY_B) { scan_init(); }
        if(key==KEY_LEFT)  g_mode=MODE_LINK;
        if(key==KEY_RIGHT) g_mode=MODE_TXBENCH;
        break;

    case MODE_TXBENCH:
        if(key==KEY_A) { txbench_start(0); txbench_start(1); }
        if(key==KEY_B) { txbench_stop(0);  txbench_stop(1);  }
        if(key==KEY_LEFT)  g_mode=MODE_SCAN;
        if(key==KEY_RIGHT) g_mode=MODE_CONFIG;
        break;

    case MODE_CONFIG:
        if(key==KEY_LEFT)  {
            if(g_config_radio==1) g_config_radio=0;
            else g_mode=MODE_TXBENCH;
        }
        if(key==KEY_RIGHT) g_config_radio^=1;
        if(key==KEY_UP)    g_config_field=(g_config_field>0)?g_config_field-1:0;
        if(key==KEY_DOWN) {
            int mx=g_config_radio?LR_FIELDS-1:CC_FIELDS-1;
            g_config_field=(g_config_field<mx)?g_config_field+1:mx;
        }
        if(key==KEY_A) {
            /* Cycle config value */
            if(!g_config_radio) {
                switch(g_config_field){
                case 1: g_cfg.cc.modulation=(g_cfg.cc.modulation+1)%4; break;
                case 2: {
                    static const uint32_t rates[]={600,1200,2400,4800,9600,38400,50000,250000,500000};
                    int n=9; uint32_t c=g_cfg.cc.datarate_bps;
                    int idx=0; for(int i=0;i<n;i++) if(rates[i]==c){idx=(i+1)%n;break;}
                    g_cfg.cc.datarate_bps=rates[idx]; break; }
                case 4: g_cfg.cc.tx_power_dbm=(g_cfg.cc.tx_power_dbm>=14)?-11:g_cfg.cc.tx_power_dbm+3; break;
                case 6: g_cfg.cc.crc_en^=1; break;
                case 7: g_cfg.cc.whitening_en^=1; break;
                }
            } else {
                switch(g_config_field){
                case 1: g_cfg.lr.pkt_type=(g_cfg.lr.pkt_type==LR_PKT_LORA)?LR_PKT_GFSK:LR_PKT_LORA; break;
                case 2: g_cfg.lr.sf=(g_cfg.lr.sf>=12)?5:g_cfg.lr.sf+1; break;
                case 3: g_cfg.lr.bw=(g_cfg.lr.bw==LR_BW_125)?LR_BW_250:
                                     g_cfg.lr.bw==LR_BW_250?LR_BW_500:LR_BW_125; break;
                case 4: g_cfg.lr.cr=(g_cfg.lr.cr>=LR_CR_4_8)?LR_CR_4_5:g_cfg.lr.cr+1; break;
                case 6: g_cfg.lr.pa_sel^=1; break;
                case 11:g_cfg.lr.iq_invert^=1; break;
                case 12:g_cfg.lr.ldro^=1; break;
                }
            }
        }
        if(key==KEY_B) field_cfg_save(&g_cfg);
        break;
    }
    /* Global tab switches */
    if(key==KEY_SET && !long_press) g_paused^=1;
}
