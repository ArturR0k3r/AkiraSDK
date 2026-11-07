/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief ha_rgb — expose the board's PWM RGB LED to Home Assistant over MQTT.
 *
 * The MQTT twin of rgb_controller / matter_rgb: it announces a Home Assistant
 * light entity via MQTT discovery (so it auto-appears in HA), then services
 * on/off/brightness/color commands from HA by driving three PWM channels and
 * reports state back. No co-processor needed — the S3's WiFi talks to your
 * MQTT broker directly.
 *
 *   channel 0 -> GPIO4 (Red)
 *   channel 1 -> GPIO5 (Green)
 *   channel 2 -> GPIO6 (Blue)
 *
 * Required capabilities: "mqtt", "pwm"
 *
 * Build:  cd AkiraSDK/wasm_apps/generic/ha_rgb && make
 */

#include "akira_api.h"

#define PWM_FREQ_HZ 1000
#define CH_RED      0
#define CH_GREEN    1
#define CH_BLUE     2
#define RGB_INVERT  0

#define OBJECT_ID   "rgb"

/* ---- Local device state (HA ranges: brightness 0..255, rgb 0..255) ---- */
static int s_on = 0;
static int s_bri = 255;
static int s_r = 255, s_g = 255, s_b = 255;

static int duty_from_u8(int v)
{
    int duty = (v * 100) / 255;
#if RGB_INVERT
    duty = 100 - duty;
#endif
    return duty;
}

static void apply_output(void)
{
    if (!s_on) {
        pwm_set(CH_RED, PWM_FREQ_HZ, duty_from_u8(0));
        pwm_set(CH_GREEN, PWM_FREQ_HZ, duty_from_u8(0));
        pwm_set(CH_BLUE, PWM_FREQ_HZ, duty_from_u8(0));
        return;
    }
    int r = (s_r * s_bri) / 255;
    int g = (s_g * s_bri) / 255;
    int b = (s_b * s_bri) / 255;
    pwm_set(CH_RED, PWM_FREQ_HZ, duty_from_u8(r));
    pwm_set(CH_GREEN, PWM_FREQ_HZ, duty_from_u8(g));
    pwm_set(CH_BLUE, PWM_FREQ_HZ, duty_from_u8(b));
}

int main(void)
{
    /* ---- 1. Wait for the MQTT broker connection ---- */
    while (!mqtt_connected()) {
        printf("ha_rgb: waiting for MQTT broker...");
        delay(1000000);  /* 1s */
    }

    /* ---- 2. Announce the HA light entity ---- */
    int rc = ha_light_register(OBJECT_ID, "Akira RGB");
    if (rc != 0) {
        printf("ha_rgb: ha_light_register failed (%d)", rc);
        return rc;
    }
    printf("ha_rgb: registered — check Home Assistant for 'Akira RGB'");

    /* ---- 3. Publish initial (off) state ---- */
    apply_output();
    ha_light_report(OBJECT_ID, s_on, s_bri, s_r, s_g, s_b);

    /* ---- 4. Command loop ---- */
    while (1) {
        int fields = ha_light_poll(OBJECT_ID, &s_on, &s_bri, &s_r, &s_g, &s_b, 2000);
        if (fields > 0) {
            apply_output();
            ha_light_report(OBJECT_ID, s_on, s_bri, s_r, s_g, s_b);
            printf("ha_rgb: on=%d bri=%d rgb=%d,%d,%d", s_on, s_bri, s_r, s_g, s_b);
        }
    }

    return 0;
}
