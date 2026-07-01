/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * ui_demo — exercises the shared akira_ui kit and serves as the migration
 * template: status bar + list + meter + tag + guarded dialog.
 */

#include "akira_api.h"
#include "akira_ui.h"

int main(void)
{
    display_clear(AKIRA_UI_INK);

    akira_ui_status_t sb = {
        .title = "wifi.scan",
        .clock = "12:00:00",
        .battery_pct = 82,
        .show_wifi = false,
        .show_bt = false,
    };
    akira_ui_status_bar(&sb);

    static const char *ssids[] = {"HomeNet_5G", "CafeGuest", "IoT_Bridge"};
    static const char *chans[] = {"ch6", "ch1", "ch11"};
    static const int   rssi[]  = {4, 2, 1};
    int sel = 0;

    for (int i = 0; i < 3; i++) {
        akira_ui_list_row(AKIRA_UI_STATUSBAR_H + 4 + i * 22, 20,
                          ssids[i], chans[i], rssi[i], i == sel);
    }

    akira_ui_tag(6, 96, "ROLLING CODE", /*dithered=*/true);
    akira_ui_meter(6, 116, 200, 8, 60, 100);
    display_flush();

    if (akira_ui_confirm_dialog("RF_RAW_TX", "confirm TX at 433.92 MHz?")) {
        akira_ui_alert("transmitting", "433.92 MHz");
    } else {
        akira_ui_alert("cancelled", "no RF emitted");
    }

    while (1) {
        delay(1000000);
    }
    return 0;
}
