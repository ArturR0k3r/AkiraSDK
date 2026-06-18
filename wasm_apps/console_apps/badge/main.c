/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief akira.badge — conference badge identity display
 *
 * Reads identity from NVS settings and shows a static badge screen:
 *   badge/handle  — callsign / hacker name  (max 16 chars)
 *   badge/name    — real name               (max 32 chars)
 *   badge/org     — organization            (max 16 chars)
 *   badge/role    — role label              (max 16 chars)
 *
 * Controls:
 *   Y   refresh identity from settings
 *   B   exit to launcher
 */

#include "akira_api.h"

/* ── Display constants ────────────────────────────────────────────────── */
#define SCR_W  320
#define SCR_H  240

/* ── Colors ───────────────────────────────────────────────────────────── */
#define COL_BG      0x0000
#define COL_BORDER  0x2945
#define COL_ACCENT  0x07FF   /* cyan */
#define COL_WHITE   0xFFFF
#define COL_GRAY    0x7BEF
#define COL_DIM     0x39E7
#define COL_YELLOW  0xFFE0

/* ── String helpers ───────────────────────────────────────────────────── */
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* ── Draw badge ───────────────────────────────────────────────────────── */
static void render(const char *handle, const char *name,
                   const char *org,    const char *role)
{
    display_clear(COL_BG);

    /* Outer border */
    display_rect_outline(4, 4, SCR_W - 8, SCR_H - 8, COL_ACCENT);
    display_rect_outline(7, 7, SCR_W - 14, SCR_H - 14, COL_BORDER);

    /* Corner accents */
    display_rect(4,  4,  12, 2,  COL_ACCENT);
    display_rect(4,  4,  2,  12, COL_ACCENT);
    display_rect(SCR_W - 16, 4,  12, 2,  COL_ACCENT);
    display_rect(SCR_W - 6,  4,  2,  12, COL_ACCENT);
    display_rect(4,  SCR_H - 6,  12, 2,  COL_ACCENT);
    display_rect(4,  SCR_H - 16, 2,  12, COL_ACCENT);
    display_rect(SCR_W - 16, SCR_H - 6,  12, 2,  COL_ACCENT);
    display_rect(SCR_W - 6,  SCR_H - 16, 2,  12, COL_ACCENT);

    /* "AKIRA BADGE" header text */
    display_text(SCR_W / 2 - 42, 14, "AKIRA BADGE", COL_DIM);
    display_rect(16, 26, SCR_W - 32, 1, COL_BORDER);

    /* Handle — large, centered */
    int handle_x = SCR_W / 2 - str_len(handle) * 11 / 2;
    display_text_large(handle_x, 50, handle, COL_ACCENT);

    /* Separator under handle */
    display_rect(40, 76, SCR_W - 80, 1, COL_BORDER);

    /* Name */
    if (str_len(name) > 0) {
        int nx = SCR_W / 2 - str_len(name) * 7 / 2;
        display_text(nx, 90, name, COL_WHITE);
    }

    /* Org */
    if (str_len(org) > 0) {
        int ox = SCR_W / 2 - str_len(org) * 7 / 2;
        display_text(ox, 110, org, COL_GRAY);
    }

    /* Role pill */
    if (str_len(role) > 0) {
        int rw = str_len(role) * 7 + 16;
        int rx = SCR_W / 2 - rw / 2;
        display_rect(rx, 132, rw, 16, COL_ACCENT);
        display_text(rx + 8, 134, role, COL_BG);
    }

    /* Bottom hint */
    display_rect(16, SCR_H - 28, SCR_W - 32, 1, COL_BORDER);
    display_text(SCR_W / 2 - 63, SCR_H - 20, "Y:REFRESH  B:EXIT", COL_DIM);

    display_flush();
}

/* ── Entry point ──────────────────────────────────────────────────────── */
int main(void)
{
    char handle[17] = "UNKNOWN";
    char name[33]   = "";
    char org[17]    = "AKIRA";
    char role[17]   = "HACKER";

    settings_get("badge/handle", handle, sizeof(handle));
    settings_get("badge/name",   name,   sizeof(name));
    settings_get("badge/org",    org,    sizeof(org));
    settings_get("badge/role",   role,   sizeof(role));

    render(handle, name, org, role);

    uint32_t prev_btns = 0;

    while (1) {
        uint32_t btns    = (uint32_t)input_get_buttons();
        uint32_t pressed = btns & ~prev_btns;
        prev_btns = btns;

        if (pressed & AKIRA_BTN_Y) {
            settings_get("badge/handle", handle, sizeof(handle));
            settings_get("badge/name",   name,   sizeof(name));
            settings_get("badge/org",    org,    sizeof(org));
            settings_get("badge/role",   role,   sizeof(role));
            render(handle, name, org, role);
        }

        if (pressed & AKIRA_BTN_B) {
            app_switch("supervisor");
            return 0;
        }

        delay(20000);
    }

    return 0;
}
