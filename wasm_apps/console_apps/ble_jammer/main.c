
#include "akira_api.h"

#define HEADER_H_DIV  10
#define FOOTER_H_DIV  10
#define ROW_H_DIV     14
#define CH_BAR_W_DIV   6

#define MAX_DEVICES      16
#define SCAN_POLL_BATCH   4
#define FRAME_DELAY_US 5000
#define HOPS_PER_FRAME    1

#define TX_POWER_MIN_DBM  0
#define TX_POWER_MAX_DBM 12
#define TX_POWER_STEP_DBM 2

static const uint32_t ble_advert_freqs[] = {
    2402000000,  
    2426000000,  
    2480000000,  
};
#define BLE_ADVERT_COUNT 3
#define BLE_FULL_COUNT  40

typedef enum {
    JAM_MODE_ADVERT = 0,
    JAM_MODE_FULL,
    JAM_MODE_SINGLE,
    JAM_MODE_COUNT,
} jam_mode_t;

static const char *mode_names[JAM_MODE_COUNT] = {
    "ADVERT", "FULL", "SINGLE",
};

typedef struct {
    uint8_t addr[AKIRA_BLE_SCAN_ADDR_LEN];
    int8_t  rssi;
    char    name[AKIRA_BLE_SCAN_NAME_LEN];
} device_t;

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

static device_t    devices[MAX_DEVICES];
static int         device_count;

static int         jam_active;
static jam_mode_t  jam_mode       = JAM_MODE_ADVERT;
static int         jam_ch_idx;
static int         jam_single_ch;
static int8_t      tx_power_dbm  = 10;
static uint32_t    jam_pkt_count;
static int         scan_active;
static uint32_t    last_tx_freq_hz;


static int addr_eq(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < AKIRA_BLE_SCAN_ADDR_LEN; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static void upsert_device(const akira_ble_scan_report_t *rep)
{
    for (int i = 0; i < device_count; i++) {
        if (addr_eq(devices[i].addr, rep->addr)) {
            devices[i].rssi = rep->rssi;
            if (rep->name[0])
                for (int j = 0; j < AKIRA_BLE_SCAN_NAME_LEN; j++)
                    devices[i].name[j] = rep->name[j];
            return;
        }
    }
    if (device_count < MAX_DEVICES) {
        device_t *d = &devices[device_count++];
        for (int i = 0; i < AKIRA_BLE_SCAN_ADDR_LEN; i++) d->addr[i] = rep->addr[i];
        d->rssi = rep->rssi;
        for (int j = 0; j < AKIRA_BLE_SCAN_NAME_LEN; j++) d->name[j] = rep->name[j];
    }
}

static void sort_devices_by_rssi(void)
{
    for (int i = 1; i < device_count; i++) {
        device_t key = devices[i];
        int j = i - 1;
        while (j >= 0 && devices[j].rssi < key.rssi) {
            devices[j + 1] = devices[j];
            j--;
        }
        devices[j + 1] = key;
    }
}

static void poll_scan(void)
{
    for (int i = 0; i < SCAN_POLL_BATCH; i++) {
        akira_ble_scan_report_t rep;
        if (ble_scan_pop(&rep, sizeof(rep)) != 1) break;
        upsert_device(&rep);
    }
    sort_devices_by_rssi();
}

static uint32_t ble_phys_freq(int idx)
{
    return 2402000000U + (uint32_t)idx * 2000000U;
}

static int jam_channel_count(void)
{
    if (jam_mode == JAM_MODE_ADVERT) return BLE_ADVERT_COUNT;
    if (jam_mode == JAM_MODE_SINGLE) return 1;
    return BLE_FULL_COUNT;
}

static uint32_t jam_curr_freq(void)
{
    if (jam_mode == JAM_MODE_ADVERT) return ble_advert_freqs[jam_ch_idx];
    if (jam_mode == JAM_MODE_SINGLE) return ble_phys_freq(jam_single_ch);
    return ble_phys_freq(jam_ch_idx);
}

static void jam_burst(void)
{
    int count = jam_channel_count();
    for (int hop = 0; hop < HOPS_PER_FRAME; hop++) {
        uint32_t freq = jam_curr_freq();
        last_tx_freq_hz = freq;
        rf_tx_cw_stop();
        rf_set_frequency(freq);
        rf_tx_cw_start();
        jam_ch_idx++;
        if (jam_ch_idx >= count) jam_ch_idx = 0;
    }
    jam_pkt_count += HOPS_PER_FRAME;
}

static void jam_start(void)
{
    if (jam_active) return;
    rf_select(AKIRA_RF_CHIP_LR2021);
    rf_set_power(tx_power_dbm);
    jam_ch_idx = 0;
    jam_pkt_count = 0;
    last_tx_freq_hz = 0;
    jam_active = 1;
}

static void jam_stop(void)
{
    rf_tx_cw_stop();
    jam_active = 0;
}

static void scan_start(void)
{
    if (scan_active) return;
    ble_scan_start(1);
    scan_active = 1;
}

static void scan_stop(void)
{
    if (!scan_active) return;
    ble_scan_stop();
    scan_active = 0;
}


static int u32_to_str(char *buf, int bufsz, uint32_t n)
{
    if (bufsz < 1) return 0;
    if (n == 0) { buf[0] = '0'; return 1; }
    char tmp[12]; int p = 0;
    while (n > 0 && p < (int)sizeof(tmp)) { tmp[p++] = '0' + (n % 10); n /= 10; }
    int q = 0;
    while (p > 0 && q < bufsz - 1) buf[q++] = tmp[--p];
    buf[q] = '\0';
    return q;
}

static int i32_to_str(char *buf, int bufsz, int32_t n)
{
    if (bufsz < 1) return 0;
    int neg = (n < 0); if (neg) n = -n;
    int q = 0;
    if (neg && q < bufsz - 1) buf[q++] = '-';
    q += u32_to_str(buf + q, bufsz - q, (uint32_t)n);
    return q;
}


static void draw_header(void)
{
    int32_t h = SCR_H / HEADER_H_DIV;
    display_rect(0, 0, SCR_W, h, 0xFFFF);
    display_text(4, h / 4, "BLE JAMMER", 0x0000);

    const char *status = jam_active ? " ON " : "OFF";
    int32_t badge_w = 36, badge_x = SCR_W - badge_w - 4, badge_y = h / 4;
    if (jam_active) {
        display_rect(badge_x, 2, badge_w, h - 4, 0xFFFF);
        display_rect_outline(badge_x, 2, badge_w, h - 4, 0x0000);
        display_text(badge_x + 4, badge_y, status, 0x0000);
    } else {
        display_rect(badge_x, 2, badge_w, h - 4, 0x0000);
        display_text(badge_x + 4, badge_y, status, 0xFFFF);
    }
}

static void draw_channel_bars(int32_t y)
{
    int32_t bar_w = SCR_W / CH_BAR_W_DIV, bar_h = SCR_H / 24, gap = 4;

    if (jam_mode == JAM_MODE_ADVERT) {
        int32_t total_w = BLE_ADVERT_COUNT * bar_w + (BLE_ADVERT_COUNT - 1) * gap;
        int32_t x0 = (SCR_W - total_w) / 2;
        for (int i = 0; i < BLE_ADVERT_COUNT; i++) {
            int32_t x = x0 + i * (bar_w + gap);
            int active = jam_active && (i == jam_ch_idx);
            if (active) display_rect(x, y, bar_w, bar_h, 0xFFFF);
            else display_rect_outline(x, y, bar_w, bar_h, 0xFFFF);
            char lbl[6];
            u32_to_str(lbl, sizeof(lbl), ble_advert_freqs[i] / 1000000U);
            display_text(x + 2, y + 1, lbl, active ? 0x0000 : 0xFFFF);
        }
    } else if (jam_mode == JAM_MODE_SINGLE) {
        int32_t bw = bar_w * 3, x0 = (SCR_W - bw) / 2;
        display_rect_outline(x0, y, bw, bar_h, 0xFFFF);
        char lbl[16];
        int q = u32_to_str(lbl, sizeof(lbl), ble_phys_freq(jam_single_ch) / 1000000U);
        lbl[q++] = 'M'; lbl[q] = '\0';
        display_text(x0 + 4, y + 1, lbl, 0xFFFF);
    } else {
        int bars = 20;
        int32_t bw = (SCR_W - (bars + 1) * 2) / bars;
        if (bw < 2) bw = 2;
        int32_t x0 = (SCR_W - (bars * bw + (bars - 1) * 2)) / 2;
        for (int i = 0; i < bars; i++) {
            int32_t x = x0 + i * (bw + 2);
            int active = jam_active && (i == jam_ch_idx / 2);
            if (active) display_rect(x, y, bw, bar_h, 0xFFFF);
            else display_rect_outline(x, y, bw, bar_h, 0xFFFF);
        }
    }
}

static void draw_info(int32_t y)
{
    int32_t row = SCR_H / ROW_H_DIV;

    display_text(4, y, "Mode:", 0xFFFF);
    display_text(44, y, mode_names[jam_mode], 0xFFFF);
    const char *hop = (jam_mode == JAM_MODE_SINGLE) ? "FIXED" : "HOP >>";
    display_text(SCR_W - 72, y, hop, 0xFFFF);
    y += row;

    display_text(4, y, "Freq:", 0xFFFF);
    uint32_t fh = last_tx_freq_hz ? last_tx_freq_hz : jam_curr_freq();
    char fbuf[14]; int q = u32_to_str(fbuf, sizeof(fbuf), fh / 1000000U);
    fbuf[q++] = ' '; fbuf[q++] = 'M'; fbuf[q++] = 'H'; fbuf[q++] = 'z'; fbuf[q] = '\0';
    display_text(44, y, fbuf, 0xFFFF);
    y += row;

    display_text(4, y, "Pwr:", 0xFFFF);
    char pbuf[14]; q = 0;
    if (tx_power_dbm >= 0) pbuf[q++] = '+';
    q += i32_to_str(pbuf + q, (int)sizeof(pbuf) - q, tx_power_dbm);
    pbuf[q++] = ' '; pbuf[q++] = 'd'; pbuf[q++] = 'B'; pbuf[q++] = 'm'; pbuf[q] = '\0';
    display_text(44, y, pbuf, 0xFFFF);
    y += row;

    display_text(4, y, "Pkts:", 0xFFFF);
    char cbuf[14];
    u32_to_str(cbuf, sizeof(cbuf), jam_pkt_count);
    display_text(44, y, cbuf, 0xFFFF);
}

static void draw_device_list(int32_t y)
{
    int32_t row = SCR_H / ROW_H_DIV;
    display_text(4, y, "-- Nearby Devices --", 0xFFFF);
    y += row;

    int32_t ftr_h = SCR_H / FOOTER_H_DIV;
    int32_t avail = SCR_H - ftr_h - y;
    int32_t visible = avail / row;
    if (visible < 1) visible = 1;
    if (visible > MAX_DEVICES) visible = MAX_DEVICES;

    for (int i = 0; i < visible && i < device_count; i++) {
        int32_t rssi_w = (devices[i].rssi + 100) * (SCR_W - 4) / 100;
        if (rssi_w < 2) rssi_w = 2;
        if (rssi_w > SCR_W - 4) rssi_w = SCR_W - 4;
        display_rect(2, y + row - 6, rssi_w, 4, 0xFFFF);

        const char *name = devices[i].name[0] ? devices[i].name : "??";
        display_text(4, y, name, 0xFFFF);

        char rbuf[12];
        int rq = i32_to_str(rbuf, sizeof(rbuf), devices[i].rssi);
        rbuf[rq++] = ' '; rbuf[rq++] = 'd'; rbuf[rq++] = 'B'; rbuf[rq] = '\0';
        display_text(SCR_W - 48, y, rbuf, 0xFFFF);
        y += row;
    }
    if (device_count == 0)
        display_text(4, y, "(no devices seen)", 0xFFFF);
}

static void draw_footer(void)
{
    int32_t h = SCR_H / FOOTER_H_DIV, y = SCR_H - h;
    display_rect(0, y, SCR_W, h, 0xFFFF);
    const char *hint = (jam_mode == JAM_MODE_SINGLE)
        ? "A:toggle U/D:mode L/R:ch B:exit"
        : "A:toggle U/D:mode L/R:pwr B:exit";
    display_text(4, y + h / 4, hint, 0x0000);
}

static void draw_all(void)
{
    display_clear(0x0000);
    draw_header();
    int32_t y = SCR_H / HEADER_H_DIV + 4;
    draw_channel_bars(y);
    y += SCR_H / 18;
    draw_info(y);
    y += (SCR_H / ROW_H_DIV) * 4 + 4;
    draw_device_list(y);
    draw_footer();
    display_flush();
}


int main(void)
{
    display_get_size(&SCR_W, &SCR_H);
    scan_start();

    int prev_mask = 0;
    while (1) {
        int mask  = input_get_buttons();
        int press = mask & ~prev_mask;
        prev_mask = mask;

        if (AKIRA_BTN_PRESSED(press, AKIRA_BTN_B)) {
            jam_stop(); scan_stop(); return 0;
        }
        if (AKIRA_BTN_PRESSED(press, AKIRA_BTN_A)) {
            if (jam_active) jam_stop(); else jam_start();
        }
        if (AKIRA_BTN_PRESSED(press, AKIRA_BTN_UP)) {
            jam_mode = (jam_mode_t)((jam_mode + 1) % JAM_MODE_COUNT);
            jam_ch_idx = 0;
        }
        if (AKIRA_BTN_PRESSED(press, AKIRA_BTN_DOWN)) {
            jam_mode = (jam_mode_t)((jam_mode - 1 + (int)JAM_MODE_COUNT) % (int)JAM_MODE_COUNT);
            jam_ch_idx = 0;
        }
        if (AKIRA_BTN_PRESSED(press, AKIRA_BTN_LEFT)) {
            if (jam_mode == JAM_MODE_SINGLE) {
                if (--jam_single_ch < 0) jam_single_ch = 39;
            } else {
                tx_power_dbm -= TX_POWER_STEP_DBM;
                if (tx_power_dbm < TX_POWER_MIN_DBM) tx_power_dbm = TX_POWER_MIN_DBM;
                if (jam_active) rf_set_power(tx_power_dbm);
            }
        }
        if (AKIRA_BTN_PRESSED(press, AKIRA_BTN_RIGHT)) {
            if (jam_mode == JAM_MODE_SINGLE) {
                if (++jam_single_ch > 39) jam_single_ch = 0;
            } else {
                tx_power_dbm += TX_POWER_STEP_DBM;
                if (tx_power_dbm > TX_POWER_MAX_DBM) tx_power_dbm = TX_POWER_MAX_DBM;
                if (jam_active) rf_set_power(tx_power_dbm);
            }
        }

        if (jam_active) jam_burst();
        poll_scan();
        draw_all();
        delay(FRAME_DELAY_US);
    }
    return 0;
}
