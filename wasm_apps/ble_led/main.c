/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file ble_led.c
 * @brief BLE LED control demo — Arduino-style BLE API for AkiraOS WASM.
 *
 * Sets up a custom BLE GATT service with a single writable characteristic.
 * A connected peer (e.g. nRF Connect) can write 0x01 to turn the LED on
 * and 0x00 to turn it off.
 *
 * Matches the Arduino BLE example pattern:
 *
 *   BLEService ledService("19B10000-E8F2-537E-4F6C-D104768A1214");
 *   BLEByteCharacteristic switchChar("19B10001-...", BLERead | BLEWrite);
 *
 * Required capabilities: "ble", "gpio.write", "gpio.read"
 *
 * Compile with:
 *   cd wasm_sample && ./build_wasm_apps.sh ble_led
 */

#include "include/akira_api.h"

/* ---- Service / Characteristic UUIDs (custom 128-bit) ---- */
#define LED_SERVICE_UUID  "19B10000-E8F2-537E-4F6C-D104768A1214"
#define SWITCH_CHAR_UUID  "19B10001-E8F2-537E-4F6C-D104768A1214"

/* ---- GPIO pin for the LED (akiraconsole onboard LED) ---- */
#define LED_PIN   48

/* ---- BLE event buffer: 4 header + up to 64 data bytes ---- */
#define EVT_BUF_LEN 68

int main(void)
{
    /* ---- 1. Create service and characteristic ---- */
    int svc = ble_service_create(LED_SERVICE_UUID);
    if (svc < 0) {
        printf("ble: service_create failed");
        return svc;
    }

    /* One-byte characteristic: Read + Write */
    int sw_char = ble_char_create(SWITCH_CHAR_UUID,
                                  BLE_PROP_READ | BLE_PROP_WRITE,
                                  1);
    if (sw_char < 0) {
        printf("ble: char_create failed");
        return sw_char;
    }

    /* ---- 2. Assemble and register the service ---- */
    ble_service_add_char(svc, sw_char);
    ble_add_service(svc);

    /* ---- 3. Configure advertising ---- */
    ble_set_local_name("AkiraOS_LED");
    ble_set_advertised_service(svc);

    /* ---- 4. Start BLE (lazy-inits the BT stack in BLE_APP mode) ---- */
    int ret = ble_init();
    if (ret < 0) {
        printf("ble: init failed (%d) — HID mode active?");
        return ret;
    }

    ble_advertise();
    printf("ble_led: advertising, waiting for connection...");

    /* ---- 5. Initialise LED pin as output, default off ---- */
    gpio_configure(LED_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
    uint8_t led_state = 0;

    /* ---- 6. Main event loop ---- */
    uint8_t evt_buf[EVT_BUF_LEN];

    while (1) {
        int evt = ble_event_pop(evt_buf, sizeof(evt_buf));

        if (evt == BLE_EVT_CONNECTED) {
            printf("ble_led: peer connected");

        } else if (evt == BLE_EVT_DISCONNECTED) {
            printf("ble_led: peer disconnected — re-advertising");
            ble_advertise();

        } else if (evt == BLE_EVT_CHAR_WRITTEN) {
            /* evt_buf layout:
             *   [0] type
             *   [1] char_handle
             *   [2-3] data_len LE
             *   [4+]  data bytes
             */
            uint16_t data_len = (uint16_t)(evt_buf[2] | (evt_buf[3] << 8));
            if (data_len >= 1) {
                led_state = evt_buf[4];
                gpio_write(LED_PIN, led_state ? 1 : 0);
                printf("ble_led: LED ");
                if(led_state)
                    printf("ON");
                else
                    printf("OFF");
            }
        }

        /* Yield 10 ms to avoid spinning the CPU at 100% */
        delay(10000); /* 10 000 µs = 10 ms */
    }

    return 0;
}
