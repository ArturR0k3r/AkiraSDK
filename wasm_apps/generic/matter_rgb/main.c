/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief matter_rgb — expose the board's PWM RGB LED as a Matter accessory.
 *
 * The Matter equivalent of rgb_controller: instead of a custom BLE GATT
 * service, it registers an Extended Color Light endpoint (On/Off + Level +
 * Color Control) with the Matter co-processor, opens a pairing window, and
 * prints the onboarding QR / manual code. It then services inbound commands
 * from the fabric (Home Assistant, Google Home, Alexa, Apple Home) by driving
 * three PWM channels, and reports state changes back out.
 *
 *   channel 0 -> GPIO4 (Red)
 *   channel 1 -> GPIO5 (Green)
 *   channel 2 -> GPIO6 (Blue)
 *
 * Required capabilities: "matter", "pwm"
 *
 * Build:  cd AkiraSDK/wasm_apps/generic/matter_rgb && make
 */

#include "akira_api.h"

/* ---- PWM configuration (mirrors rgb_controller) ---- */
#define PWM_FREQ_HZ 1000
#define CH_RED      0
#define CH_GREEN    1
#define CH_BLUE     2
#define RGB_INVERT  0

/* ---- Local device state ---- */
static int      s_endpoint = -1;
static uint8_t  s_on = 0;
static uint8_t  s_level = 254;              /* Matter level range 1..254 */
static uint8_t  s_rgb[3] = { 255, 255, 255 }; /* default warm white */

static int duty_from_u8(uint8_t v)
{
    int duty = (v * 100) / 255;
#if RGB_INVERT
    duty = 100 - duty;
#endif
    return duty;
}

/* Push the effective color (on/off * level * rgb) to the LED. */
static void apply_output(void)
{
    if (!s_on) {
        pwm_set(CH_RED, PWM_FREQ_HZ, duty_from_u8(0));
        pwm_set(CH_GREEN, PWM_FREQ_HZ, duty_from_u8(0));
        pwm_set(CH_BLUE, PWM_FREQ_HZ, duty_from_u8(0));
        return;
    }
    /* Scale each channel by the current level (1..254 -> 0..255). */
    uint8_t r = (uint8_t)((s_rgb[0] * s_level) / 254);
    uint8_t g = (uint8_t)((s_rgb[1] * s_level) / 254);
    uint8_t b = (uint8_t)((s_rgb[2] * s_level) / 254);
    pwm_set(CH_RED, PWM_FREQ_HZ, duty_from_u8(r));
    pwm_set(CH_GREEN, PWM_FREQ_HZ, duty_from_u8(g));
    pwm_set(CH_BLUE, PWM_FREQ_HZ, duty_from_u8(b));
}

/* Report the current On/Off state back to the fabric. */
static void report_onoff(void)
{
    uint8_t v = s_on ? 1 : 0;
    matter_report_attr(s_endpoint, MATTER_CLUSTER_ONOFF,
                       MATTER_ATTR_ONOFF, &v, 1);
}

/* Report the current level back to the fabric. */
static void report_level(void)
{
    matter_report_attr(s_endpoint, MATTER_CLUSTER_LEVEL_CONTROL,
                       MATTER_ATTR_CURRENT_LEVEL, &s_level, 1);
}

int main(void)
{
    /* ---- 1. Register the endpoint ---- */
    unsigned int clusters[] = {
        MATTER_CLUSTER_ONOFF,
        MATTER_CLUSTER_LEVEL_CONTROL,
        MATTER_CLUSTER_COLOR_CONTROL,
    };
    s_endpoint = matter_endpoint_add(MATTER_DEVTYPE_COLOR_LIGHT, clusters, 3);
    if (s_endpoint < 0) {
        printf("matter_rgb: endpoint_add failed (%d) — co-processor up?",
               s_endpoint);
        return s_endpoint;
    }
    printf("matter_rgb: endpoint %d registered", s_endpoint);

    /* ---- 2. Open pairing and show the onboarding codes ---- */
    if (matter_open_pairing(300) == 0) {
        char qr[48];
        char manual[16];
        if (matter_get_pairing(qr, sizeof(qr), manual, sizeof(manual)) == 0) {
            printf("matter_rgb: pair via QR '%s' or manual code '%s'",
                   qr, manual);
        }
    }

    /* ---- 3. Start off ---- */
    apply_output();

    /* ---- 4. Command loop ---- */
    uint8_t buf[32];
    while (1) {
        int ep, cluster, cmd;
        int n = matter_cmd_poll(&ep, &cluster, &cmd, buf, sizeof(buf), 1000);
        if (n < 0) {
            /* Timeout / no command — just loop. */
            continue;
        }

        if (cluster == MATTER_CLUSTER_ONOFF) {
            if (cmd == MATTER_CMD_ON) {
                s_on = 1;
            } else if (cmd == MATTER_CMD_OFF) {
                s_on = 0;
            } else if (cmd == MATTER_CMD_TOGGLE) {
                s_on = !s_on;
            }
            apply_output();
            report_onoff();
            printf("matter_rgb: on=%d", s_on);

        } else if (cluster == MATTER_CLUSTER_LEVEL_CONTROL) {
            if (cmd == MATTER_CMD_MOVE_TO_LEVEL && n >= 1) {
                s_level = buf[0] ? buf[0] : 1;
                s_on = 1;
                apply_output();
                report_level();
                printf("matter_rgb: level=%d", s_level);
            }

        } else if (cluster == MATTER_CLUSTER_COLOR_CONTROL) {
            /* Demo: accept a raw 3-byte [R,G,B] payload. */
            if (n >= 3) {
                s_rgb[0] = buf[0];
                s_rgb[1] = buf[1];
                s_rgb[2] = buf[2];
                s_on = 1;
                apply_output();
                printf("matter_rgb: color set");
            }
        }
    }

    return 0;
}
