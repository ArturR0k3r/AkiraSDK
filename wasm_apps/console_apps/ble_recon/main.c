#include "akira_api.h"

#define MAX_DEVICES      32
#define SCAN_POLL_BATCH  4    /* reports drained from the queue per frame */
#define HEADER_H_DIVISOR 12   /* header height = SCR_H / HEADER_H_DIVISOR */
#define ROW_H_DIVISOR    10   /* row height    = SCR_H / ROW_H_DIVISOR */
#define NAME_DISPLAY_MAX 24   /* truncate device names for the row width */
#define RSSI_COL_OFFSET  40   /* RSSI column x = SCR_W - RSSI_COL_OFFSET */
#define FOOTER_Y_OFFSET  14   /* footer y = SCR_H - FOOTER_Y_OFFSET */
#define FRAME_DELAY_US   20000 /* ~50 fps */

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

typedef struct {
    uint8_t addr[AKIRA_BLE_SCAN_ADDR_LEN];
    int8_t  rssi;
    char    name[AKIRA_BLE_SCAN_NAME_LEN];
} device_t;

static device_t devices[MAX_DEVICES];
static int device_count = 0;

static const char *preset_names[BLE_SPAM_PRESET_COUNT] = {
    "Apple Continuity",
    "Google Fast Pair",
    "MS Swift Pair",
    "Random Flood",
};

typedef enum { SCREEN_SCAN, SCREEN_SPAM } screen_t;
static screen_t screen = SCREEN_SCAN;

static int scan_active = 0;
static int scan_sel = 0;

static int spam_active = 0;
static int spam_preset = 0;

static int addr_eq(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < AKIRA_BLE_SCAN_ADDR_LEN; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void upsert_device(const akira_ble_scan_report_t *rep)
{
    for (int i = 0; i < device_count; i++) {
        if (addr_eq(devices[i].addr, rep->addr)) {
            devices[i].rssi = rep->rssi;
            if (rep->name[0] != '\0') {
                for (int j = 0; j < AKIRA_BLE_SCAN_NAME_LEN; j++) {
                    devices[i].name[j] = rep->name[j];
                }
            }
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
        int r = ble_scan_pop(&rep, sizeof(rep));

        if (r != 1) break;

        upsert_device(&rep);
    }
    sort_devices_by_rssi();
}

static void draw_scan_screen(void)
{
    int32_t hdr_h = SCR_H / HEADER_H_DIVISOR;

    display_rect(0, 0, SCR_W, hdr_h, 0xFFFF);
    display_text(4, hdr_h / 4, scan_active ? "BLE SCAN [ON]" : "BLE SCAN [OFF]", 0x0000);

    int32_t row_h = SCR_H / ROW_H_DIVISOR;
    int32_t list_top = hdr_h;
    int32_t visible_rows = (SCR_H - list_top) / row_h;

    for (int i = 0; i < visible_rows && i < device_count; i++) {
        int32_t y = list_top + i * row_h;
        int selected = (i == scan_sel);

        if (selected) {
            display_rect(0, y, SCR_W, row_h, 0xFFFF);
        }

        char line[NAME_DISPLAY_MAX + 2];
        const char *name = (devices[i].name[0] != '\0') ? devices[i].name : "(unnamed)";
        int n = 0;

        for (int c = 0; name[c] != '\0' && n < NAME_DISPLAY_MAX; c++) line[n++] = name[c];
        line[n] = '\0';

        display_text(4, y + row_h / 4, line, selected ? 0x0000 : 0xFFFF);

        char rssi_str[8];
        int r = devices[i].rssi;
        int neg = r < 0;
        if (neg) r = -r;

        int p = 0;
        char tmp[8];

        if (r == 0) tmp[p++] = '0';
        while (r > 0) { tmp[p++] = '0' + (r % 10); r /= 10; }

        int q = 0;
        if (neg) rssi_str[q++] = '-';
        while (p > 0) rssi_str[q++] = tmp[--p];
        rssi_str[q] = '\0';

        display_text(SCR_W - RSSI_COL_OFFSET, y + row_h / 4, rssi_str, selected ? 0x0000 : 0xFFFF);
    }

    display_text(4, SCR_H - FOOTER_Y_OFFSET, "A:toggle scan  X:spam  B:exit", 0xFFFF);
}

static void draw_spam_screen(void)
{
    int32_t hdr_h = SCR_H / HEADER_H_DIVISOR;

    display_rect(0, 0, SCR_W, hdr_h, 0xFFFF);
    display_text(4, hdr_h / 4, "BLE SPAM", 0x0000);

    int32_t row_h = SCR_H / ROW_H_DIVISOR;
    int32_t y = hdr_h + row_h;

    for (int i = 0; i < BLE_SPAM_PRESET_COUNT; i++) {
        int selected = (i == spam_preset);

        if (selected) {
            display_rect(0, y, SCR_W, row_h, 0xFFFF);
        }
        display_text(4, y + 4, preset_names[i], selected ? 0x0000 : 0xFFFF);
        y += row_h;
    }

    if (spam_active) {
        display_text(4, y + 10, "ACTIVE", 0xFFFF);

        int count = ble_spam_packet_count();
        char cbuf[16];
        int p = 0, n = 0;
        char tmp[12];

        if (count == 0) tmp[p++] = '0';
        while (count > 0) { tmp[p++] = '0' + (count % 10); count /= 10; }
        while (p > 0) cbuf[n++] = tmp[--p];
        cbuf[n] = '\0';

        display_text(RSSI_COL_OFFSET * 2, y + 10, cbuf, 0xFFFF);
    }

    display_text(4, SCR_H - FOOTER_Y_OFFSET, "UP/DN:preset  A:start/stop  X:scan  B:exit", 0xFFFF);
}

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    int prev_mask = 0;

    while (1) {
        int mask = input_get_buttons();
        int pressed = mask & ~prev_mask;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_X)) {
            screen = (screen == SCREEN_SCAN) ? SCREEN_SPAM : SCREEN_SCAN;
        }

        if (screen == SCREEN_SCAN) {
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
                if (scan_active) {
                    ble_scan_stop();
                    scan_active = 0;
                } else {
                    /* scan and spam share one radio mode-lock and cannot
                     * run at once — stop spam first if it's active. */
                    if (spam_active) {
                        ble_spam_stop();
                        spam_active = 0;
                    }
                    if (ble_scan_start(1) == 0) {
                        scan_active = 1;
                    }
                }
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP) && scan_sel > 0) {
                scan_sel--;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN) && scan_sel < device_count - 1) {
                scan_sel++;
            }
            if (scan_active) {
                poll_scan();
            }
        } else {
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP) && spam_preset > 0) {
                spam_preset--;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN) && spam_preset < BLE_SPAM_PRESET_COUNT - 1) {
                spam_preset++;
            }
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
                if (spam_active) {
                    ble_spam_stop();
                    spam_active = 0;
                } else {
                    /* scan and spam share one radio mode-lock and cannot
                     * run at once — stop scan first if it's active. */
                    if (scan_active) {
                        ble_scan_stop();
                        scan_active = 0;
                    }
                    if (ble_spam_start(spam_preset) == 0) {
                        spam_active = 1;
                    }
                }
            }
        }

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            if (scan_active) ble_scan_stop();
            if (spam_active) ble_spam_stop();
            break;
        }

        display_rect(0, 0, SCR_W, SCR_H, 0x0000);
        if (screen == SCREEN_SCAN) {
            draw_scan_screen();
        } else {
            draw_spam_screen();
        }
        display_flush();

        prev_mask = mask;
        delay(FRAME_DELAY_US);
    }

    return 0;
}
