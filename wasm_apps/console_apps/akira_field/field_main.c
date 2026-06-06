/*
 * field_main.c — AkiraField entry point + event loop
 * SPDX-License-Identifier: Apache-2.0
 */
#include "akira_api.h"
#include "field.h"

/* ── Button debounce (DB_TICKS = 3 frames @ ~16ms = 48ms) ──────────────── */
#define DB_TICKS 3
#define BTN_COUNT 7

static int btn_cnt[BTN_COUNT];
static int btn_state[BTN_COUNT];
static int btn_prev[BTN_COUNT];
static int btn_hold[BTN_COUNT];

static const int BTN_PINS[BTN_COUNT] = {
    PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_A, PIN_B, PIN_SET
};

static void btns_init(void) {
    for(int i=0;i<6;i++)
        gpio_configure(BTN_PINS[i], GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_SET, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
}

static void btns_poll(void) {
    for(int i=0;i<BTN_COUNT;i++){
        int raw = gpio_read(BTN_PINS[i]);
        /* Settings is active-low: invert */
        if(i==6) raw=!raw;
        btn_prev[i]=btn_state[i];
        if(raw) btn_cnt[i]++; else btn_cnt[i]=0;
        btn_state[i]=(btn_cnt[i]>=DB_TICKS)?1:0;
        /* Hold counter (800ms = ~50 frames at 16ms) */
        if(btn_state[i]) btn_hold[i]++; else btn_hold[i]=0;
    }
}

static int btn_rose(int i){ return btn_state[i]&&!btn_prev[i]; }
static int btn_long(int i){ return btn_hold[i]==50; }

/* ── Uptime using AkiraOS timer ─────────────────────────────────────────── */
static int g_timer = -1;
static int g_timer_base_ms = 0;

static void uptime_init(void) {
    g_timer = timer_create();
    if(g_timer>=0) timer_start(g_timer);
}

static void uptime_update(void) {
    if(g_timer<0) return;
    int ms = timer_elapsed(g_timer);
    g_uptime_us = (uint32_t)(ms - g_timer_base_ms) * 1000u;
}

/* ── Startup error display ──────────────────────────────────────────────── */
static void show_error(const char *msg) {
    display_clear(0x0000u);
    display_rect(10, 90, SCR_W-20, 60, C_ERR);
    display_text(20, 100, "RADIO INIT FAILED", C_FG);
    display_text(20, 116, msg, C_WARN);
    display_text(20, 132, "Hold SET 3s to reboot", C_DIM);
    display_flush();
}

/* ── RX polling (ISR-like in single-threaded context) ───────────────────── */
static void poll_rx(void) {
    if(g_paused) return;

    /* CC1121: poll GDO0 (high = SYNC detected / bytes ready) */
    if(gpio_read(GPIO_CC_GDO0) || cc1121_rx_ready()) {
        uint8_t buf[PKT_MAX_LEN];
        uint8_t len = cc1121_rx_read(buf, sizeof(buf));
        if(len && g_rx_cc.rx_count < 0xFFFFFFFFu) {
            rx_pkt_t *p = &g_rx_cc.pkt[g_rx_cc.head];
            mem_cpy(p->data, buf, len<PKT_MAX_LEN?len:PKT_MAX_LEN);
            p->len = len;
            p->rssi_dbm = cc1121_rssi();
            p->lqi = cc1121_lqi();
            p->seq = (uint16_t)g_rx_cc.rx_count;
            p->ts_us = g_uptime_us;
            g_rx_cc.head = (g_rx_cc.head+1) % RX_RING_SIZE;
            if(g_rx_cc.head == g_rx_cc.tail)
                g_rx_cc.tail = (g_rx_cc.tail+1) % RX_RING_SIZE;
            g_rx_cc.rx_count++;
        }
    }

    /* LR1121: poll DIO9 rising edge */
    static int lr_prev_dio = 0;
    int dio = gpio_read(GPIO_LR_DIO9);
    if(dio && !lr_prev_dio) {
        uint32_t irq = lr1121_get_irq();
        lr1121_clear_irq(irq);
        if(irq & LR_IRQ_RX_DONE) {
            uint8_t buf[PKT_MAX_LEN];
            uint8_t len = lr1121_rx_read(buf, sizeof(buf));
            if(len) {
                rx_pkt_t *p = &g_rx_lr.pkt[g_rx_lr.head];
                mem_cpy(p->data, buf, len<PKT_MAX_LEN?len:PKT_MAX_LEN);
                p->len = len;
                p->rssi_dbm = (int8_t)(lr1121_get_rssi()/-10);
                p->snr_qdb  = lr1121_get_snr();
                p->seq = (uint16_t)g_rx_lr.rx_count;
                p->ts_us = g_uptime_us;
                g_rx_lr.head = (g_rx_lr.head+1) % RX_RING_SIZE;
                if(g_rx_lr.head == g_rx_lr.tail)
                    g_rx_lr.tail = (g_rx_lr.tail+1) % RX_RING_SIZE;
                g_rx_lr.rx_count++;
            }
        }
        if(irq & LR_IRQ_CRC_ERROR) g_rx_lr.err_count++;
    }
    lr_prev_dio = dio;
}

/* ── Main ───────────────────────────────────────────────────────────────── */
int main(void) {
    uptime_init();
    btns_init();

    /* Splash */
    display_clear(C_BG);
    display_text_large(40, 90,  "AkiraField", C_OK);
    display_text(80, 115, "Dual-Radio Field Tool", C_DIM);
    display_text(80, 130, "CC1121 + LR1121", C_FG);
    display_flush();
    delay(800000);

    /* Load config */
    if(field_cfg_load(&g_cfg) < 0) {
        field_cfg_defaults(&g_cfg);
    }

    /* Init radios */
    int cc_ok = 1, lr_ok = 1;
    cc1121_init(&g_cfg.cc);
    /* Verify CC1121: read PARTNUM — should be 0x40 */
    uint8_t pn = cc1121_read_reg(0x30u | 0x80u | 0x40u);
    if(pn != 0x40u && pn != 0x00u) { cc_ok = 0; }

    lr1121_init();
    /* Verify LR1121: check BUSY goes low within 50ms */
    lr_ok = !lr1121_busy();

    if(!cc_ok || !lr_ok) {
        show_error(!cc_ok?"CC1121 no response":"LR1121 stuck BUSY");
        /* Wait for hold-SET reboot */
        while(1) {
            btns_poll();
            if(btn_long(6)) { cc1121_strobe(0x30u); lr1121_hard_reset(); }
            delay(16000);
        }
    }

    /* Set LR1121 params from config */
    lr1121_set_pkt_type(g_cfg.lr.pkt_type);
    lr1121_set_rf_freq(g_cfg.lr.freq_hz);
    if(g_cfg.lr.pkt_type==LR_PKT_LORA)
        lr1121_set_lora_params(g_cfg.lr.sf,g_cfg.lr.bw,g_cfg.lr.cr,g_cfg.lr.ldro);
    else
        lr1121_set_gfsk_params(g_cfg.lr.gfsk_br_bps,g_cfg.lr.gfsk_fdev_hz,1);
    lr1121_set_tx_power(g_cfg.lr.tx_power_dbm, g_cfg.lr.pa_sel);
    lr1121_set_irq_mask(LR_IRQ_RX_DONE|LR_IRQ_TX_DONE|LR_IRQ_CRC_ERROR|LR_IRQ_PREAMBLE);
    lr1121_set_rx(0xFFFFFFFFu); /* continuous RX */

    /* Init subsystems */
    link_init();
    scan_init();
    txbench_init();

    g_mode = MODE_MONITOR;

    /* Main loop: ~60 fps, draw every 100ms (10 fps) */
    uint32_t last_draw_ms = 0;
    uint32_t last_scan_ms = 0;

    while(1) {
        uptime_update();
        btns_poll();

        /* Key dispatch */
        for(int i=0;i<BTN_COUNT;i++){
            if(btn_rose(i))  ui_key(i, 0);
            if(btn_long(i))  ui_key(i, 1);
        }

        /* Mode-specific ticks */
        poll_rx();

        int now_ms = g_timer>=0 ? timer_elapsed(g_timer) : 0;

        switch(g_mode){
        case MODE_LINK:
            if(!g_paused) link_tick();
            break;
        case MODE_SCAN:
            if(!g_paused && (now_ms - last_scan_ms) >= 5) {
                scan_tick_cc();
                scan_tick_lr();
                last_scan_ms = now_ms;
            }
            break;
        case MODE_TXBENCH:
            if(!g_paused) txbench_tick();
            break;
        default: break;
        }

        /* Draw at 10 FPS */
        if((now_ms - last_draw_ms) >= 100) {
            ui_draw();
            last_draw_ms = now_ms;
        }

        delay(16000); /* 16ms = ~60Hz poll */
    }
    return 0;
}
