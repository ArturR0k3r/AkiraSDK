/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * ui_demo — exercises the shared akira_ui kit and serves as the migration
 * template: status bar + list + meter + tag + guarded dialog.
 */

#include "akira_api.h"
#include "akira_ui.h"

/* PoC: the home grid rendered with the Playdate dither-shadow cards. */
static void draw_home(int sel)
{
    int W, H;
    display_get_size(&W, &H);
    display_clear(AKIRA_UI_INK);

    akira_ui_status_t sb = { .title = "AKIRAOS", .clock = "12:00", .battery_pct = 82 };
    akira_ui_status_bar(&sb);

    static const char *labels[] = { "wifi", "subghz", "lora", "ble", "vault", "more" };

    /* 3 cols x 2 rows, grid-snapped with generous padding. */
    const int cols = 3, rows = 2, pad = 10;
    int top = AKIRA_UI_STATUSBAR_H + pad;
    int cw = (W - pad * (cols + 1)) / cols;
    int ch = (H - top - pad * rows) / rows;

    for (int i = 0; i < 6; i++) {
        int r = i / cols, c = i % cols;
        int x = pad + c * (cw + pad);
        int y = top + r * (ch + pad);
        /* icon = NULL here (icons come from akira_icons.h in real apps). */
        akira_ui_grid_tile(x, y, cw, ch, 1, 0, 0, 0, labels[i], i == sel);
    }
    display_flush();
}

int main(void)
{
    int sel = 1; /* "subghz" selected — the inverted card in 01_home.png */
    draw_home(sel);

    /* Guarded action demo — the max-elevation confirm dialog. */
    if (akira_ui_confirm_dialog("RF_RAW_TX", "confirm TX at 433.92 MHz?")) {
        akira_ui_alert("transmitting", "433.92 MHz");
    } else {
        draw_home(sel);
    }

    while (1) {
        delay(1000000);
    }
    return 0;
}
