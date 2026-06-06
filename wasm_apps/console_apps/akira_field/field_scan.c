/*
 * field_scan.c — RSSI sweep + 158-sample waterfall buffer
 * SPDX-License-Identifier: Apache-2.0
 */
#include "field.h"

#define CC_DWELL_US  2000u    /* 2 ms per CC1121 channel (calibrate+settle) */
#define LR_DWELL_US  5000u    /* 5 ms per LR1121 CAD at SF7 */

void scan_init(void) {
    /* CC1121 scan: 820–960 MHz, 1 MHz step → 140 channels → wrap at WFALL_W */
    g_scan_cc.freq_start_hz = 820000000u;
    g_scan_cc.freq_stop_hz  = 960000000u;
    g_scan_cc.step_hz       = (g_scan_cc.freq_stop_hz - g_scan_cc.freq_start_hz) / WFALL_W;
    g_scan_cc.n_ch          = WFALL_W;
    g_scan_cc.cur_ch        = 0;
    g_scan_cc.peak_rssi     = RSSI_FLOOR;
    g_scan_cc.dirty         = 1;
    mem_set(g_scan_cc.buf, 0, WFALL_W);

    /* LR1121 scan: 863–870 MHz (LoRa EU868 band) */
    g_scan_lr.freq_start_hz = 863000000u;
    g_scan_lr.freq_stop_hz  = 870000000u;
    g_scan_lr.step_hz       = (g_scan_lr.freq_stop_hz - g_scan_lr.freq_start_hz) / WFALL_W;
    g_scan_lr.n_ch          = WFALL_W;
    g_scan_lr.cur_ch        = 0;
    g_scan_lr.peak_rssi     = RSSI_FLOOR;
    g_scan_lr.dirty         = 1;
    mem_set(g_scan_lr.buf, 0, WFALL_W);
}

static uint8_t rssi_to_height(int8_t rssi_dbm) {
    if(rssi_dbm <= RSSI_FLOOR) return 0;
    if(rssi_dbm >= RSSI_CEIL)  return WFALL_H;
    return (uint8_t)((rssi_dbm - RSSI_FLOOR) * WFALL_H / (RSSI_CEIL - RSSI_FLOOR));
}

/* Called periodically from main loop; advances one channel per call */
void scan_tick_cc(void) {
    uint32_t freq = g_scan_cc.freq_start_hz +
                    g_scan_cc.cur_ch * g_scan_cc.step_hz;
    cc1121_set_freq(freq);
    delay(CC_DWELL_US);
    int8_t rssi = cc1121_rssi();
    g_scan_cc.buf[g_scan_cc.cur_ch] = rssi_to_height(rssi);
    if(rssi > g_scan_cc.peak_rssi) {
        g_scan_cc.peak_rssi     = rssi;
        g_scan_cc.peak_freq_hz  = freq;
    }
    g_scan_cc.cur_ch = (g_scan_cc.cur_ch + 1) % g_scan_cc.n_ch;
    if(g_scan_cc.cur_ch == 0) g_scan_cc.peak_rssi = RSSI_FLOOR; /* reset each sweep */
    g_scan_cc.dirty = 1;
}

void scan_tick_lr(void) {
    uint32_t freq = g_scan_lr.freq_start_hz +
                    g_scan_lr.cur_ch * g_scan_lr.step_hz;
    lr1121_set_rf_freq(freq);
    lr1121_set_rx(0); /* single shot */
    delay(LR_DWELL_US);
    uint32_t irq = lr1121_get_irq();
    lr1121_clear_irq(irq);
    int16_t rssi_t = lr1121_get_rssi(); /* tenths of dBm */
    int8_t  rssi   = (int8_t)(rssi_t / 10);
    uint8_t cad    = (irq & LR_IRQ_PREAMBLE) ? 1 : 0;
    /* blend RSSI with CAD flag */
    uint8_t h = rssi_to_height(rssi);
    if(cad && h < 32) h = 32; /* ensure visible if CAD detected */
    g_scan_lr.buf[g_scan_lr.cur_ch] = h;
    if(rssi > g_scan_lr.peak_rssi) {
        g_scan_lr.peak_rssi    = rssi;
        g_scan_lr.peak_freq_hz = freq;
    }
    g_scan_lr.cur_ch = (g_scan_lr.cur_ch + 1) % g_scan_lr.n_ch;
    if(g_scan_lr.cur_ch == 0) g_scan_lr.peak_rssi = RSSI_FLOOR;
    g_scan_lr.dirty = 1;
}
