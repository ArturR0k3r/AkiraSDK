/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief RGB LED controller — BLE-controlled PWM RGB for AkiraOS WASM.
 *
 * Stands up a custom BLE GATT service with a single 3-byte writable
 * characteristic. A connected peer (AkiraApp, nRF Connect, ...) writes
 * [R, G, B] (0-255 each) and the app drives three PWM channels:
 *
 *   channel 0 -> GPIO4 (Red)
 *   channel 1 -> GPIO5 (Green)
 *   channel 2 -> GPIO6 (Blue)
 *
 * The board wires a common-anode RGB LED with low-side N-MOSFETs on each
 * color, so PWM is NON-inverted: duty 100% = fully on. If your LED is
 * driven the other way (common-cathode via high-side / direct-inverted),
 * set RGB_INVERT to 1.
 *
 * Required capabilities: "ble", "pwm"
 *
 * Build:  cd AkiraSDK/wasm_apps/generic/rgb_controller && make
 */

#include "akira_api.h"

/* ---- Service / Characteristic UUIDs (custom 128-bit, Akira namespace) ---- */
#define RGB_SERVICE_UUID "414b4952-0002-0001-0001-000000000001"
#define RGB_CHAR_UUID    "414b4952-0002-0001-0001-000000000002"

/* ---- PWM configuration ---- */
#define PWM_FREQ_HZ 1000 /* 1 kHz — flicker-free for an LED */
#define CH_RED      0     /* GPIO4 */
#define CH_GREEN    1     /* GPIO5 */
#define CH_BLUE     2     /* GPIO6 */
#define RGB_INVERT  0     /* 0 = low-side MOSFET (duty high = bright) */

/* ---- BLE event buffer: 4 header + up to 64 data bytes ---- */
#define EVT_BUF_LEN 68

/* Map an 8-bit color component (0-255) to a PWM duty percent (0-100). */
static int duty_from_u8(uint8_t v)
{
    int duty = (v * 100) / 255;
#if RGB_INVERT
    duty = 100 - duty;
#endif
    return duty;
}

static void apply_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    pwm_set(CH_RED,   PWM_FREQ_HZ, duty_from_u8(r));
    pwm_set(CH_GREEN, PWM_FREQ_HZ, duty_from_u8(g));
    pwm_set(CH_BLUE,  PWM_FREQ_HZ, duty_from_u8(b));
}

int main(void)
{
    /* ---- 1. Create service + 3-byte RGB characteristic ---- */
    int svc = ble_service_create(RGB_SERVICE_UUID);
    if (svc < 0) {
        printf("rgb: service_create failed");
        return svc;
    }

    int rgb_char = ble_char_create(RGB_CHAR_UUID,
                                   BLE_PROP_READ | BLE_PROP_WRITE,
                                   3);
    if (rgb_char < 0) {
        printf("rgb: char_create failed");
        return rgb_char;
    }

    ble_service_add_char(svc, rgb_char);
    ble_add_service(svc);

    /* ---- 2. Advertise ---- */
    ble_set_local_name("AkiraOS_RGB");
    ble_set_advertised_service(svc);

    int ret = ble_init();
    if (ret < 0) {
        printf("rgb: ble_init failed (%d) — HID mode active?");
        return ret;
    }
    ble_advertise();
    printf("rgb_controller: advertising, waiting for connection...");

    /* ---- 3. Start with the LED off ---- */
    apply_rgb(0, 0, 0);

    /* ---- 4. Event loop ---- */
    uint8_t evt_buf[EVT_BUF_LEN];

    while (1) {
        int evt = ble_event_pop(evt_buf, sizeof(evt_buf));

        if (evt == BLE_EVT_CONNECTED) {
            printf("rgb_controller: peer connected");

        } else if (evt == BLE_EVT_DISCONNECTED) {
            printf("rgb_controller: peer disconnected — re-advertising");
            ble_advertise();

        } else if (evt == BLE_EVT_CHAR_WRITTEN) {
            /* evt_buf: [0]type [1]char_handle [2-3]data_len LE [4+]data */
            uint16_t data_len = (uint16_t)(evt_buf[2] | (evt_buf[3] << 8));
            if (data_len >= 3) {
                apply_rgb(evt_buf[4], evt_buf[5], evt_buf[6]);
                printf("rgb_controller: set color");
            }
        }

        /* Yield 10 ms to avoid spinning the CPU at 100% */
        delay(10000);
    }

    return 0;
}
