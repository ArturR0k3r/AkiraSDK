/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ble_beacon.c
 * @brief BLE GATT sensor peripheral.
 *
 * Advertises a custom GATT service and streams live sensor data: an
 * accelerometer characteristic (3x int16, milli-g) and a temperature
 * characteristic (int16, centi-degC). Connect from a phone (e.g. nRF Connect)
 * to read/subscribe and watch the values change as you move the console.
 *
 *   B — exit
 *
 * Capabilities: display.write, input.read, sensor.read, ble
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

/* Custom 128-bit UUIDs (random base, distinct last field per attribute). */
#define UUID_SVC  "a1c30000-6b6f-4a69-b2a0-1f5e9c8d7a01"
#define UUID_ACC  "a1c30000-6b6f-4a69-b2a0-1f5e9c8d7a02"
#define UUID_TEMP "a1c30000-6b6f-4a69-b2a0-1f5e9c8d7a03"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* pack a signed 16-bit value little-endian */
static void put_i16(uint8_t *p, int v)
{
    v = clampi(v, -32768, 32767);
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    int ok = 0;
    int c_acc = -1, c_temp = -1;
    if (ble_init() == 0) {
        ble_set_local_name("Akira Sensor");
        int svc = ble_service_create(UUID_SVC);
        c_acc   = ble_char_create(UUID_ACC,  BLE_PROP_READ | BLE_PROP_NOTIFY, 6);
        c_temp  = ble_char_create(UUID_TEMP, BLE_PROP_READ | BLE_PROP_NOTIFY, 2);
        if (svc >= 0 && c_acc >= 0 && c_temp >= 0) {
            ble_service_add_char(svc, c_acc);
            ble_service_add_char(svc, c_temp);
            ble_add_service(svc);
            ble_set_advertised_service(svc);
            ok = (ble_advertise() == 0);
        }
    }

    int connected = 0;
    int prev = input_get_buttons();

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            ble_stop_advertise();
            ble_deinit();
            return 0;
        }

        /* Drain connection events. */
        uint8_t ev[8];
        int type;
        while ((type = ble_event_pop(ev, sizeof(ev))) > 0) {
            if (type == BLE_EVT_CONNECTED)    connected = 1;
            if (type == BLE_EVT_DISCONNECTED) connected = 0;
        }

        /* Read sensors and publish. */
        int ax = sensor_read(SENSOR_CHAN_ACCEL_X);
        int ay = sensor_read(SENSOR_CHAN_ACCEL_Y);
        int az = sensor_read(SENSOR_CHAN_ACCEL_Z);
        int tc = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
        int have_acc  = (ax != AKIRA_SENSOR_ERROR);
        int have_temp = (tc != AKIRA_SENSOR_ERROR);

        if (ok && have_acc) {
            uint8_t b[6];
            put_i16(&b[0], ax); /* milli-g-ish: m/s^2 x1000 */
            put_i16(&b[2], ay);
            put_i16(&b[4], az);
            ble_char_write(c_acc, b, sizeof(b));
        }
        if (ok && have_temp) {
            uint8_t b[2];
            put_i16(b, tc / 10); /* centi-degC */
            ble_char_write(c_temp, b, sizeof(b));
        }

        /* ---------- draw ---------- */
        display_clear(COLOR_BLACK);

        int32_t hdr_h = SCR_H / 8;
        display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
        display_text(6, hdr_h / 2 - 4, "BLE SENSOR BEACON", COLOR_BLACK);

        int32_t y = hdr_h + 12;
        if (!ok) {
            display_text(8, y, "BLE init failed", COLOR_WHITE);
        } else {
            display_text(8, y, connected ? "STATE: CONNECTED" : "STATE: ADVERTISING",
                         COLOR_WHITE);
            y += 18;
            display_text(8, y, "Name: Akira Sensor", COLOR_WHITE);
            y += 22;

            display_text(8, y, "ACC x/y/z (mg):", COLOR_WHITE);
            y += 16;
            if (have_acc) {
                display_number(16, y, ax, COLOR_WHITE);
                display_number(120, y, ay, COLOR_WHITE);
                display_number(224, y, az, COLOR_WHITE);
            } else {
                display_text(16, y, "no accel", COLOR_WHITE);
            }
            y += 22;

            display_text(8, y, "TEMP (cC):", COLOR_WHITE);
            if (have_temp) {
                display_number(120, y, tc / 10, COLOR_WHITE);
            } else {
                display_text(120, y, "no temp", COLOR_WHITE);
            }
            y += 24;

            /* live pulse so it's obvious the beacon is running */
            display_circle(SCR_W - 18, hdr_h + 6, 5, COLOR_WHITE);
            if (connected) {
                display_circle_fill(SCR_W - 18, hdr_h + 6, 3, COLOR_WHITE);
            }
        }

        int32_t ftr_h = SCR_H / 10;
        display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
        display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                     "Connect via BLE app   B:exit", COLOR_BLACK);

        display_flush();
        delay(200000); /* 5 Hz update is plenty for a sensor feed */
    }
    return 0;
}
