/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * ui_demo — exercises the shared akira_ui kit and serves as the migration
 * template: status bar + list + meter + tag + guarded dialog.
 */

#include "akira_api.h"
#include "akira_ui.h"

/* PoC: the home rendered as a VERTICAL carousel of dither-shadow cards —
 * focused app is a large filled card, neighbours are smaller idle cards. */
static void draw_home(int sel)
{
    int W, H;
    display_get_size(&W, &H);
    display_clear(AKIRA_UI_INK);

    akira_ui_status_t sb = { .title = "AKIRAOS", .clock = "12:00", .battery_pct = 82 };
    akira_ui_status_bar(&sb);

    static const char *labels[] = { "wifi", "subghz", "lora", "ble", "vault", "more" };
    const int n = 6;

    int cx = W / 2;
    int cy = (AKIRA_UI_STATUSBAR_H + H) / 2;
    const int sel_h = 58, adj_h = 40, gap = 8, card_w = W - 40;

    for (int slot = -2; slot <= 2; slot++) {
        int idx = sel + slot;
        if (idx < 0 || idx >= n) continue;
        bool foc = (slot == 0);
        int ch = foc ? sel_h : adj_h;
        int cw = foc ? card_w : card_w - 28;
        int x = cx - cw / 2;
        int y = cy - ch / 2 + slot * (adj_h + gap);
        akira_ui_dither_card(x, y, cw, ch, 12, foc, foc ? 5 : 2);
        uint16_t fg = foc ? AKIRA_UI_INK : AKIRA_UI_PAPER;
        display_text(x + 18, y + (ch - 10) / 2, labels[idx], fg);
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
