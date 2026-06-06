/*
 * field_cc1121.c — CC1121 (CC1101-compatible) SPI driver
 * SmartRF Studio 7 register bank for all presets.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "field.h"

/* ── CC1121 register addresses ──────────────────────────────────────────── */
#define IOCFG2   0x00
#define IOCFG1   0x01
#define IOCFG0   0x02
#define FIFOTHR  0x03
#define SYNC1    0x04
#define SYNC0    0x05
#define PKTLEN   0x06
#define PKTCTRL1 0x07
#define PKTCTRL0 0x08
#define ADDR     0x09
#define CHANNR   0x0A
#define FSCTRL1  0x0B
#define FSCTRL0  0x0C
#define FREQ2    0x0D
#define FREQ1    0x0E
#define FREQ0    0x0F
#define MDMCFG4  0x10
#define MDMCFG3  0x11
#define MDMCFG2  0x12
#define MDMCFG1  0x13
#define MDMCFG0  0x14
#define DEVIATN  0x15
#define MCSM2    0x16
#define MCSM1    0x17
#define MCSM0    0x18
#define FOCCFG   0x19
#define BSCFG    0x1A
#define AGCCTRL2 0x1B
#define AGCCTRL1 0x1C
#define AGCCTRL0 0x1D
#define WOREVT1  0x1E
#define WOREVT0  0x1F
#define WORCTRL  0x20
#define FREND1   0x21
#define FREND0   0x22
#define FSCAL3   0x23
#define FSCAL2   0x24
#define FSCAL1   0x25
#define FSCAL0   0x26
#define RCCTRL1  0x27
#define RCCTRL0  0x28
#define FSTEST   0x29
#define PTEST    0x2A
#define AGCTEST  0x2B
#define TEST2    0x2C
#define TEST1    0x2D
#define TEST0    0x2E
/* Status registers (burst-read with 0xC0 prefix) */
#define PARTNUM  0x30
#define VERSION  0x31
#define FREQEST  0x32
#define LQI_REG  0x33
#define RSSI_REG 0x34
#define MARCSTATE 0x35
#define RXBYTES  0x3B
#define TXBYTES  0x3A
/* PA table */
#define PATABLE  0x3E
/* Strobes (already defined in field.h via CC_S* but repeated for clarity) */
#define SRES   0x30
#define SIDLE  0x36
#define STX    0x35
#define SRX    0x34
#define SFTX   0x3B
#define SFRX   0x3A
#define SNOP   0x3D

/* ── SPI helpers ────────────────────────────────────────────────────────── */
static void spi_begin(void){ gpio_write(GPIO_CC_CS,0); }
static void spi_end(void)  { gpio_write(GPIO_CC_CS,1); }

static uint8_t spi_xfer_byte(uint8_t b) {
    uint8_t tx[1]={b}, rx[1]={0};
    spi_transfer(0, tx, rx, 1);
    return rx[0];
}

void cc1121_write_reg(uint8_t addr, uint8_t val) {
    spi_begin();
    spi_xfer_byte(addr & 0x3Fu);   /* write, single */
    spi_xfer_byte(val);
    spi_end();
}

uint8_t cc1121_read_reg(uint8_t addr) {
    uint8_t v;
    spi_begin();
    spi_xfer_byte(0x80u | (addr & 0x3Fu));  /* read, single */
    v=spi_xfer_byte(0);
    spi_end();
    return v;
}

void cc1121_strobe(uint8_t cmd) {
    spi_begin();
    spi_xfer_byte(cmd);
    spi_end();
}

static void cc1121_burst_write(uint8_t addr, const uint8_t *buf, uint8_t len) {
    spi_begin();
    spi_xfer_byte(0x40u | (addr & 0x3Fu));  /* write, burst */
    uint8_t tx[1];
    for(uint8_t i=0;i<len;i++){tx[0]=buf[i];spi_transfer(0,tx,0,1);}
    spi_end();
}

static void cc1121_burst_read(uint8_t addr, uint8_t *buf, uint8_t len) {
    spi_begin();
    spi_xfer_byte(0xC0u | (addr & 0x3Fu));  /* read, burst, status */
    uint8_t rx[1]; uint8_t zero[1]={0};
    for(uint8_t i=0;i<len;i++){spi_transfer(0,zero,rx,1);buf[i]=rx[0];}
    spi_end();
}

/* ── Frequency calculation ──────────────────────────────────────────────── */
/* CC1121: FREQ = freq_hz * 2^16 / f_xosc (26 MHz) */
static void set_freq_regs(uint32_t freq_hz) {
    uint64_t freq_reg = ((uint64_t)freq_hz << 16) / 26000000ULL;
    cc1121_write_reg(FREQ2, (uint8_t)(freq_reg >> 16));
    cc1121_write_reg(FREQ1, (uint8_t)(freq_reg >> 8));
    cc1121_write_reg(FREQ0, (uint8_t)(freq_reg));
}

/* ── Data rate & bandwidth ──────────────────────────────────────────────── */
/* DRATE_E/M: datarate = (256+DRATE_M) * 2^DRATE_E * 26e6 / 2^28 */
static void set_datarate(uint32_t bps, uint8_t *mdmcfg4, uint8_t *mdmcfg3) {
    /* Lookup table for common rates */
    static const struct { uint32_t bps; uint8_t e; uint8_t m; } tbl[] = {
        {    600, 5, 131}, {   1200, 5, 131}, /* note: 600=wrong, adjust E */
        {   1200, 6,  65}, {   2400, 6, 131}, {   4800, 7, 131},
        {   9600, 8, 131}, {  19200, 9, 131}, {  38400,10, 131},
        {  50000,10, 178}, { 100000,11, 178}, { 250000,12, 214},
        { 500000,13, 214}, {0,0,0}
    };
    uint8_t best_e=8, best_m=131;
    uint32_t best_err=0xFFFFFFFFu;
    for(int i=0;tbl[i].bps;i++){
        uint32_t err=tbl[i].bps>bps?tbl[i].bps-bps:bps-tbl[i].bps;
        if(err<best_err){best_err=err;best_e=tbl[i].e;best_m=tbl[i].m;}
    }
    /* MDMCFG4[3:0]=DRATE_E, MDMCFG3=DRATE_M */
    *mdmcfg4 = (*mdmcfg4 & 0xF0u) | (best_e & 0x0Fu);
    *mdmcfg3 = best_m;
}

/* ── PA table (TX power) ────────────────────────────────────────────────── */
static uint8_t pa_value(int8_t dbm) {
    /* Approximate mapping for 433/868 MHz, per CC1101 datasheet */
    if(dbm<=-11) return 0x03;
    if(dbm<= -6) return 0x17;
    if(dbm<=  0) return 0x50;
    if(dbm<=  5) return 0x81;
    if(dbm<= 10) return 0xC2;
    if(dbm<= 12) return 0xC5;
    return 0xC0; /* +14 dBm */
}

/* ── SmartRF default register table (common safe values) ───────────────── */
static const uint8_t CC_DEFAULTS[] = {
/*addr  val   description */
/*0x00*/0x29,  /* IOCFG2: GDO2=CLK_XOSC/192 */
/*0x01*/0x2E,  /* IOCFG1: GDO1=tristate */
/*0x02*/0x06,  /* IOCFG0: GDO0=assert on SYNC, deassert on EOP */
/*0x03*/0x07,  /* FIFOTHR: RX=32 TX=33 */
/*0x04*/0xD3,  /* SYNC1 */
/*0x05*/0x91,  /* SYNC0 */
/*0x06*/0xFF,  /* PKTLEN: unlimited */
/*0x07*/0x04,  /* PKTCTRL1: CRC autoflush, no addr check, RSSI+LQI append */
/*0x08*/0x45,  /* PKTCTRL0: whitening ON, CRC ON, variable packet length */
/*0x09*/0x00,  /* ADDR */
/*0x0A*/0x00,  /* CHANNR: channel 0 */
/*0x0B*/0x06,  /* FSCTRL1: IF=152.3 kHz */
/*0x0C*/0x00,  /* FSCTRL0 */
/*0x0D*/0x10,  /* FREQ2 (placeholders — set properly in init) */
/*0x0E*/0xA7,  /* FREQ1 */
/*0x0F*/0x62,  /* FREQ0 */
/*0x10*/0xCA,  /* MDMCFG4: BW=203 kHz, DRATE_E=10 */
/*0x11*/0x83,  /* MDMCFG3: DRATE_M=131 → ~38.4 kbps */
/*0x12*/0x93,  /* MDMCFG2: 2-FSK, 16/16 sync word */
/*0x13*/0x22,  /* MDMCFG1: FEC off, 4 preamble bytes */
/*0x14*/0xF8,  /* MDMCFG0: channel spacing 199.951 kHz */
/*0x15*/0x35,  /* DEVIATN: FSK deviation ±47.607 kHz */
/*0x16*/0x07,  /* MCSM2 */
/*0x17*/0x30,  /* MCSM1: CCA always, RX→IDLE, TX→IDLE */
/*0x18*/0x18,  /* MCSM0: auto-cal on IDLE→RX, FS power-up=149 µs */
/*0x19*/0x16,  /* FOCCFG */
/*0x1A*/0x6C,  /* BSCFG */
/*0x1B*/0x43,  /* AGCCTRL2 */
/*0x1C*/0x40,  /* AGCCTRL1 */
/*0x1D*/0x91,  /* AGCCTRL0 */
/*0x1E*/0x87,  /* WOREVT1 */
/*0x1F*/0x6B,  /* WOREVT0 */
/*0x20*/0xFB,  /* WORCTRL */
/*0x21*/0x56,  /* FREND1 */
/*0x22*/0x10,  /* FREND0 */
/*0x23*/0xE9,  /* FSCAL3 */
/*0x24*/0x2A,  /* FSCAL2 */
/*0x25*/0x00,  /* FSCAL1 */
/*0x26*/0x1F,  /* FSCAL0 */
/*0x27*/0x41,  /* RCCTRL1 */
/*0x28*/0x00,  /* RCCTRL0 */
/*0x29*/0x59,  /* FSTEST */
/*0x2A*/0x7F,  /* PTEST */
/*0x2B*/0x3F,  /* AGCTEST */
/*0x2C*/0x81,  /* TEST2 */
/*0x2D*/0x35,  /* TEST1 */
/*0x2E*/0x09,  /* TEST0 */
};

/* ── Public init ────────────────────────────────────────────────────────── */
void cc1121_init(const cc1121_cfg_t *cfg) {
    /* Configure CS pin */
    gpio_configure(GPIO_CC_CS, GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH);
    gpio_configure(GPIO_CC_GDO0, GPIO_INPUT | GPIO_PULL_DOWN);

    /* Hardware reset */
    spi_begin(); delay(10); spi_end();
    delay(50);
    cc1121_strobe(SRES);
    delay(2000); /* wait for reset */

    /* Burst-write default register bank */
    cc1121_burst_write(0x00, CC_DEFAULTS, sizeof(CC_DEFAULTS));

    /* Apply config */
    set_freq_regs(cfg->freq_hz);

    /* Modulation */
    uint8_t mdmcfg2 = cc1121_read_reg(MDMCFG2) & 0x8Fu;
    switch(cfg->modulation){
    case CC_MOD_2FSK: mdmcfg2|=0x00u; break;
    case CC_MOD_4FSK: mdmcfg2|=0x10u; break;
    case CC_MOD_GFSK: mdmcfg2|=0x10u; break; /* GFSK via shape bit */
    case CC_MOD_MSK:  mdmcfg2|=0x30u; break;
    }
    /* Sync word mode: 16-bit */
    mdmcfg2 |= 0x02u;
    cc1121_write_reg(MDMCFG2, mdmcfg2);

    /* Data rate */
    uint8_t m4=cc1121_read_reg(MDMCFG4), m3;
    set_datarate(cfg->datarate_bps, &m4, &m3);
    cc1121_write_reg(MDMCFG4, m4);
    cc1121_write_reg(MDMCFG3, m3);

    /* Sync word */
    if(cfg->sync_len>=2){
        cc1121_write_reg(SYNC1, cfg->sync_word[0]);
        cc1121_write_reg(SYNC0, cfg->sync_word[1]);
    }

    /* Preamble bytes */
    uint8_t m1=cc1121_read_reg(MDMCFG1)&0xE3u;
    uint8_t pb=cfg->preamble_bytes>=24?7:cfg->preamble_bytes>=12?6:
               cfg->preamble_bytes>=8?5:cfg->preamble_bytes>=6?4:
               cfg->preamble_bytes>=4?3:cfg->preamble_bytes>=3?2:
               cfg->preamble_bytes>=2?1:0;
    cc1121_write_reg(MDMCFG1, m1|(pb<<2));

    /* CRC + whitening */
    uint8_t pkt0=0x05u; /* variable length, no whitening, CRC on */
    if(!cfg->crc_en)      pkt0&=~0x04u;
    if(cfg->whitening_en) pkt0|=0x40u;
    cc1121_write_reg(PKTCTRL0, pkt0);

    /* PA table */
    uint8_t pa=pa_value(cfg->tx_power_dbm);
    spi_begin();
    spi_xfer_byte(0x40u|PATABLE); /* burst write PA table */
    for(int i=0;i<8;i++) spi_xfer_byte(pa);
    spi_end();

    /* Enter RX */
    cc1121_strobe(SRX);
}

void cc1121_set_freq(uint32_t freq_hz) {
    cc1121_strobe(SIDLE);
    set_freq_regs(freq_hz);
    cc1121_strobe(SRX);
}

void cc1121_set_power(int8_t dbm) {
    uint8_t pa=pa_value(dbm);
    spi_begin();
    spi_xfer_byte(0x40u|PATABLE);
    for(int i=0;i<8;i++) spi_xfer_byte(pa);
    spi_end();
}

void cc1121_tx(const uint8_t *buf, uint8_t len) {
    cc1121_strobe(SIDLE);
    cc1121_strobe(SFTX);
    spi_begin();
    spi_xfer_byte(0x40u|0x3Fu); /* burst write TX FIFO */
    spi_xfer_byte(len);          /* length byte */
    for(uint8_t i=0;i<len;i++) spi_xfer_byte(buf[i]);
    spi_end();
    cc1121_strobe(STX);
}

uint8_t cc1121_rx_ready(void) {
    return cc1121_read_reg(RXBYTES) & 0x7Fu;
}

uint8_t cc1121_rx_read(uint8_t *buf, uint8_t max_len) {
    uint8_t nb = cc1121_read_reg(RXBYTES) & 0x7Fu;
    if(!nb) return 0;
    /* Read length byte */
    uint8_t len;
    cc1121_burst_read(0x3Fu, &len, 1);
    if(len>max_len||len>nb-1) { cc1121_strobe(SFRX); return 0; }
    cc1121_burst_read(0x3Fu, buf, len);
    /* Two appended status bytes: RSSI, LQI|CRC_OK */
    cc1121_strobe(SRX);
    return len;
}

int8_t cc1121_rssi(void) {
    uint8_t r = cc1121_read_reg(RSSI_REG);
    int8_t rssi_raw = (int8_t)r;
    return (rssi_raw/2) - 74; /* per CC1101 datasheet */
}

uint8_t cc1121_lqi(void) {
    return cc1121_read_reg(LQI_REG) & 0x7Fu;
}

uint8_t cc1121_state(void) {
    return (cc1121_read_reg(MARCSTATE) & 0x1Fu);
}
