/*
 * field_lr1121.c — LR1121 SPI opcode driver
 * 4-wire SPI: opcode (2 bytes) + payload; poll BUSY before each command.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "field.h"

/* ── LR1121 command opcodes ─────────────────────────────────────────────── */
#define LR_CMD_GET_STATUS        0x0100u
#define LR_CMD_WRITE_REG_MEM8   0x0105u
#define LR_CMD_READ_REG_MEM8    0x0106u
#define LR_CMD_WRITE_BUFFER8    0x0109u
#define LR_CMD_READ_BUFFER8     0x010Au
#define LR_CMD_CLEAR_RX_BUFFER  0x010Bu
#define LR_CMD_SET_STDBY        0x0208u
#define LR_CMD_SET_TX           0x020Du
#define LR_CMD_SET_RX           0x020Eu
#define LR_CMD_SET_RF_FREQ      0x020Au
#define LR_CMD_SET_PKT_TYPE     0x020Cu
#define LR_CMD_SET_TX_PARAMS    0x0211u
#define LR_CMD_SET_LORA_MOD     0x0315u
#define LR_CMD_SET_GFSK_MOD     0x0316u
#define LR_CMD_GET_IRQ_STATUS   0x0113u
#define LR_CMD_CLEAR_IRQ        0x0114u
#define LR_CMD_SET_DIO_IRQ_PARAMS 0x0116u
#define LR_CMD_GET_PKT_STATUS   0x011Du
#define LR_CMD_GET_RX_BUFFER    0x011Eu
#define LR_CMD_REBOOT           0x0218u

/* ── SPI helpers ────────────────────────────────────────────────────────── */
static void lr_wait_busy(void) {
    uint32_t t0 = 0; /* simplified: loop-wait up to 5ms */
    for(int i=0;i<5000;i++){
        if(!gpio_read(GPIO_LR_BUSY)) return;
        delay(1);
    }
}

static void lr_cmd_begin(uint16_t opcode) {
    lr_wait_busy();
    gpio_write(GPIO_LR_CS, 0);
    uint8_t tx[2]={(uint8_t)(opcode>>8),(uint8_t)opcode};
    spi_transfer(0,tx,0,2);
}
static void lr_cmd_end(void) { gpio_write(GPIO_LR_CS, 1); }

static void lr_write8(uint8_t b) {
    uint8_t tx[1]={b};
    spi_transfer(0,tx,0,1);
}
static uint8_t lr_read8(void) {
    uint8_t tx[1]={0},rx[1]={0};
    spi_transfer(0,tx,rx,1);
    return rx[0];
}

/* ── Public API ─────────────────────────────────────────────────────────── */
void lr1121_init(void) {
    gpio_configure(GPIO_LR_CS,   GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH);
    gpio_configure(GPIO_LR_BUSY, GPIO_INPUT);
    gpio_configure(GPIO_LR_DIO9, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(GPIO_LR_NRST, GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH);

    /* Hard reset */
    gpio_write(GPIO_LR_NRST, 0);
    delay(10000);
    gpio_write(GPIO_LR_NRST, 1);
    delay(50000);

    /* Enter standby */
    lr_cmd_begin(LR_CMD_SET_STDBY);
    lr_write8(0x00); /* RC oscillator */
    lr_cmd_end();
}

void lr1121_hard_reset(void) {
    gpio_write(GPIO_LR_NRST, 0); delay(5000);
    gpio_write(GPIO_LR_NRST, 1); delay(50000);
}

uint8_t lr1121_busy(void) {
    return gpio_read(GPIO_LR_BUSY) ? 1u : 0u;
}

void lr1121_set_rf_freq(uint32_t freq_hz) {
    lr_cmd_begin(LR_CMD_SET_RF_FREQ);
    lr_write8((uint8_t)(freq_hz >> 24));
    lr_write8((uint8_t)(freq_hz >> 16));
    lr_write8((uint8_t)(freq_hz >> 8));
    lr_write8((uint8_t)(freq_hz));
    lr_cmd_end();
}

void lr1121_set_pkt_type(lr_pkt_type_t t) {
    lr_cmd_begin(LR_CMD_SET_PKT_TYPE);
    lr_write8((uint8_t)t);
    lr_cmd_end();
}

void lr1121_set_lora_params(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro) {
    lr_cmd_begin(LR_CMD_SET_LORA_MOD);
    lr_write8(sf);
    lr_write8(bw);
    lr_write8(cr);
    lr_write8(ldro ? 1 : 0);
    lr_cmd_end();
}

void lr1121_set_gfsk_params(uint32_t br_bps, uint32_t fdev_hz, uint8_t pulse_shape) {
    /* Bitrate register = 32e6 * 4096 / br_bps */
    uint64_t br_reg = (uint64_t)32000000 * 4096 / br_bps;
    /* Fdev register = fdev_hz * 2^24 / 32e6 */
    uint32_t fdev_reg = (uint32_t)((uint64_t)fdev_hz * (1u<<24) / 32000000u);
    lr_cmd_begin(LR_CMD_SET_GFSK_MOD);
    lr_write8((uint8_t)(br_reg >> 16));
    lr_write8((uint8_t)(br_reg >> 8));
    lr_write8((uint8_t)(br_reg));
    lr_write8(pulse_shape & 0x07u);
    lr_write8((uint8_t)(fdev_reg >> 16));
    lr_write8((uint8_t)(fdev_reg >> 8));
    lr_write8((uint8_t)(fdev_reg));
    lr_cmd_end();
}

void lr1121_set_tx_power(int8_t dbm, uint8_t pa_sel) {
    lr_cmd_begin(LR_CMD_SET_TX_PARAMS);
    lr_write8((uint8_t)(int8_t)dbm);
    lr_write8(0x07u); /* ramp time 40 µs */
    lr_cmd_end();
    (void)pa_sel; /* PA config done via radio set_pa_config — omit for brevity */
}

void lr1121_set_irq_mask(uint32_t mask) {
    lr_cmd_begin(LR_CMD_SET_DIO_IRQ_PARAMS);
    lr_write8((uint8_t)(mask >> 24));
    lr_write8((uint8_t)(mask >> 16));
    lr_write8((uint8_t)(mask >> 8));
    lr_write8((uint8_t)(mask));
    lr_write8(0); lr_write8(0); lr_write8(0); lr_write8(0); /* DIO mask */
    lr_cmd_end();
}

void lr1121_set_rx(uint32_t timeout_ms) {
    uint32_t to_sym = (timeout_ms == 0xFFFFFFFFu) ? 0xFFFFFFu :
                      (timeout_ms == 0) ? 0u :
                      (timeout_ms * 64u); /* 64 ticks/ms at 64 kHz */
    lr_cmd_begin(LR_CMD_SET_RX);
    lr_write8((uint8_t)(to_sym >> 16));
    lr_write8((uint8_t)(to_sym >> 8));
    lr_write8((uint8_t)(to_sym));
    lr_cmd_end();
}

void lr1121_set_tx(uint8_t *buf, uint8_t len, uint32_t timeout_ms) {
    /* Write payload to buffer */
    lr_cmd_begin(LR_CMD_WRITE_BUFFER8);
    lr_write8(0x00); /* offset */
    for(uint8_t i=0;i<len;i++) lr_write8(buf[i]);
    lr_cmd_end();
    /* Issue TX */
    uint32_t to_sym = timeout_ms ? timeout_ms * 64u : 0u;
    lr_cmd_begin(LR_CMD_SET_TX);
    lr_write8((uint8_t)(to_sym >> 16));
    lr_write8((uint8_t)(to_sym >> 8));
    lr_write8((uint8_t)(to_sym));
    lr_cmd_end();
}

uint32_t lr1121_get_irq(void) {
    lr_cmd_begin(LR_CMD_GET_IRQ_STATUS);
    uint32_t irq = 0;
    irq |= ((uint32_t)lr_read8()) << 24;
    irq |= ((uint32_t)lr_read8()) << 16;
    irq |= ((uint32_t)lr_read8()) << 8;
    irq |= lr_read8();
    lr_cmd_end();
    return irq;
}

void lr1121_clear_irq(uint32_t mask) {
    lr_cmd_begin(LR_CMD_CLEAR_IRQ);
    lr_write8((uint8_t)(mask >> 24));
    lr_write8((uint8_t)(mask >> 16));
    lr_write8((uint8_t)(mask >> 8));
    lr_write8((uint8_t)(mask));
    lr_cmd_end();
}

int16_t lr1121_get_rssi(void) {
    /* GET_PKT_STATUS returns: rssi_pkt, snr_pkt, signal_rssi_pkt */
    lr_cmd_begin(LR_CMD_GET_PKT_STATUS);
    uint8_t rssi_raw = lr_read8();
    lr_cmd_end();
    /* rssi_pkt is in -dBm/2 units → convert to tenths */
    return -(int16_t)rssi_raw * 5; /* -raw/2 * 10 = -raw*5 tenths */
}

int8_t lr1121_get_snr(void) {
    lr_cmd_begin(LR_CMD_GET_PKT_STATUS);
    lr_read8(); /* skip rssi */
    int8_t snr = (int8_t)lr_read8();
    lr_cmd_end();
    return snr; /* quarter-dB units */
}

uint8_t lr1121_rx_read(uint8_t *buf, uint8_t max_len) {
    /* GET_RX_BUFFER_STATUS → payload_len, rx_start_pointer */
    lr_cmd_begin(LR_CMD_GET_RX_BUFFER);
    uint8_t len = lr_read8();
    uint8_t off = lr_read8();
    lr_cmd_end();
    if(!len || len > max_len) return 0;
    lr_cmd_begin(LR_CMD_READ_BUFFER8);
    lr_write8(off);
    for(uint8_t i=0;i<len;i++) buf[i]=lr_read8();
    lr_cmd_end();
    lr_cmd_begin(LR_CMD_CLEAR_RX_BUFFER);
    lr_cmd_end();
    return len;
}
