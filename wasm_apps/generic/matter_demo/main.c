/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief matter_demo — headless Matter accessory demo (no GPIO/PWM).
 *
 * Exercises the whole device-as-endpoint path over the Matter co-processor
 * (or the in-firmware mock) and logs everything, so it can be tried on any
 * board without wiring an LED — ideal for bring-up on the ESP32-C6.
 *
 * It registers an On/Off + Level endpoint, opens a pairing window, prints the
 * onboarding QR / manual code, then logs every command it receives from the
 * fabric (or from `matter inject` when using the mock).
 *
 * Required capability: "matter"
 *
 * Build:  cd AkiraSDK/wasm_apps/generic/matter_demo && make
 */

#include "akira_api.h"

int main(void)
{
    unsigned int clusters[] = {
        MATTER_CLUSTER_ONOFF,
        MATTER_CLUSTER_LEVEL_CONTROL,
    };
    int ep = matter_endpoint_add(MATTER_DEVTYPE_DIMMABLE_LIGHT, clusters, 2);
    if (ep < 0) {
        printf("matter_demo: endpoint_add failed (%d) — co-processor up?", ep);
        return ep;
    }
    printf("matter_demo: endpoint %d registered", ep);

    if (matter_open_pairing(300) == 0) {
        char qr[48];
        char manual[16];
        if (matter_get_pairing(qr, sizeof(qr), manual, sizeof(manual)) == 0) {
            printf("matter_demo: pair via QR '%s' or manual code '%s'", qr, manual);
        }
    }

    int on = 0;
    int level = 254;

    while (1) {
        int e, cluster, cmd;
        unsigned char buf[16];
        int n = matter_cmd_poll(&e, &cluster, &cmd, buf, sizeof(buf), 2000);
        if (n < 0) {
            continue;   /* timeout */
        }

        if (cluster == MATTER_CLUSTER_ONOFF) {
            if (cmd == MATTER_CMD_ON) {
                on = 1;
            } else if (cmd == MATTER_CMD_OFF) {
                on = 0;
            } else if (cmd == MATTER_CMD_TOGGLE) {
                on = !on;
            }
            unsigned char v = on ? 1 : 0;
            matter_report_attr(e, MATTER_CLUSTER_ONOFF, MATTER_ATTR_ONOFF, &v, 1);
            printf("matter_demo: endpoint %d -> %s", e, on ? "ON" : "OFF");

        } else if (cluster == MATTER_CLUSTER_LEVEL_CONTROL && n >= 1) {
            level = buf[0];
            matter_report_attr(e, MATTER_CLUSTER_LEVEL_CONTROL,
                               MATTER_ATTR_CURRENT_LEVEL, buf, 1);
            printf("matter_demo: endpoint %d level -> %d", e, level);
        }
    }

    return 0;
}
