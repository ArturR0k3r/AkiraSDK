/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file stalker_detector.c
 * @brief Anti-stalking BLE tracker scanner.
 *
 * Passively scans BLE for Apple FindMy / AirTag beacons (Apple manufacturer
 * data 0x004C with a FindMy sub-type) and tracks how long each one has been
 * near you. A tracker that stays close over time raises a "TRACKER NEARBY"
 * alert — the tell-tale sign of an AirTag planted to follow someone.
 *
 *   UP/DOWN — scroll the list
 *   A       — clear list
 *   B       — exit
 *
 * Caveat: AirTags rotate their MAC address (~every 15 min), so a single
 * persistent tracker can eventually re-appear as a new row. We track per-MAC
 * dwell time, which catches a tracker within one rotation window — long enough
 * to warn, which is the point of the tool.
 *
 * Capabilities: display.write, input.read, ble.scan
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

#define MAXTRACK   20
#define ALERT_MS   30000  /* dwell time that trips the alert */
#define ALERT_RSSI (-75)  /* ...and only if it's this close or closer */
#define STALE_MS   20000  /* drop entries not seen for this long */

typedef struct {
    uint8_t  addr[6];
    int8_t   rssi;
    uint32_t first_ms;
    uint32_t last_ms;
    uint8_t  findmy;
    uint8_t  used;
} track_t;

static track_t g_tracks[MAXTRACK];

static char hexd(int n) { return (char)(n < 10 ? ('0' + n) : ('A' + n - 10)); }

static int addr_eq(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 6; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* Scan AD structures for Apple (0x004C) FindMy manufacturer data. */
static int is_findmy(const uint8_t *adv, int len)
{
    int i = 0;
    while (i < len) {
        int ad_len = adv[i];
        if (ad_len == 0 || i + ad_len >= len) {
            break;
        }
        int ad_type = adv[i + 1];
        if (ad_type == 0xFF && ad_len >= 4 &&
            adv[i + 2] == 0x4C && adv[i + 3] == 0x00) {
            int sub = adv[i + 4];
            /* 0x12 = FindMy "offline finding" (separated AirTag),
             * 0x07 = proximity pairing, 0x10 = nearby info. */
            if (sub == 0x12 || sub == 0x07 || sub == 0x10) {
                return 1;
            }
        }
        i += ad_len + 1;
    }
    return 0;
}

static void track_update(const akira_ble_scan_report_t *rep, uint32_t now)
{
    if (!is_findmy(rep->adv_data, rep->adv_len)) {
        return; /* only care about trackers */
    }

    for (int i = 0; i < MAXTRACK; i++) {
        if (g_tracks[i].used && addr_eq(g_tracks[i].addr, rep->addr)) {
            g_tracks[i].rssi    = rep->rssi;
            g_tracks[i].last_ms = now;
            g_tracks[i].findmy  = 1;
            return;
        }
    }
    /* new — take a free (or the stalest) slot */
    int slot   = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < MAXTRACK; i++) {
        if (!g_tracks[i].used) {
            slot = i;
            break;
        }
        if (g_tracks[i].last_ms < oldest) {
            oldest = g_tracks[i].last_ms;
            slot   = i;
        }
    }
    for (int k = 0; k < 6; k++) {
        g_tracks[slot].addr[k] = rep->addr[k];
    }
    g_tracks[slot].rssi     = rep->rssi;
    g_tracks[slot].first_ms = now;
    g_tracks[slot].last_ms  = now;
    g_tracks[slot].findmy   = 1;
    g_tracks[slot].used     = 1;
}

static void prune(uint32_t now)
{
    for (int i = 0; i < MAXTRACK; i++) {
        if (g_tracks[i].used && (now - g_tracks[i].last_ms) > STALE_MS) {
            g_tracks[i].used = 0;
        }
    }
}

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    for (int i = 0; i < MAXTRACK; i++) {
        g_tracks[i].used = 0;
    }

    if (ble_scan_start(1) < 0) {
        display_clear(COLOR_BLACK);
        display_text(8, SCR_H / 2 - 16, "BLE scan unavailable", COLOR_WHITE);
        display_text(8, SCR_H / 2 + 2, "radio busy in another mode", COLOR_WHITE);
        display_flush();
        /* Edge-detect a fresh B press so a button still held from launch does
         * not immediately dismiss this screen. */
        int p = input_get_buttons();
        while (1) {
            int h = input_get_buttons();
            if (AKIRA_BTN_PRESSED(h & ~p, AKIRA_BTN_B)) {
                break;
            }
            p = h;
            delay(50000);
        }
        return -1;
    }

    int32_t hdr_h = SCR_H / 8;
    int32_t ftr_h = SCR_H / 10;
    int32_t row_h = 18;
    int     top   = 0;

    int prev = input_get_buttons();

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            ble_scan_stop();
            return 0;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
            for (int i = 0; i < MAXTRACK; i++) {
                g_tracks[i].used = 0;
            }
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_UP) && top > 0) {
            top--;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_DOWN)) {
            top++;
        }

        uint32_t now = (uint32_t)rtc_get_uptime_ms();

        /* Drain everything queued this frame. */
        akira_ble_scan_report_t rep;
        int guard = 0;
        while (ble_scan_pop(&rep, sizeof(rep)) == 1 && guard++ < 64) {
            track_update(&rep, now);
        }
        prune(now);

        /* Alert if any tracker has dwelt too long while staying close. */
        int alert = 0;
        int count = 0;
        for (int i = 0; i < MAXTRACK; i++) {
            if (!g_tracks[i].used) {
                continue;
            }
            count++;
            if ((now - g_tracks[i].first_ms) >= ALERT_MS &&
                g_tracks[i].rssi >= ALERT_RSSI) {
                alert = 1;
            }
        }

        /* ---------- draw ---------- */
        display_clear(COLOR_BLACK);

        /* Header inverts to solid white when alerting. */
        display_rect(0, 0, SCR_W, hdr_h, alert ? COLOR_WHITE : COLOR_BLACK);
        display_rect_outline(0, 0, SCR_W, hdr_h, COLOR_WHITE);
        uint32_t hfg = alert ? COLOR_BLACK : COLOR_WHITE;
        display_text(6, hdr_h / 2 - 4,
                     alert ? "! TRACKER NEARBY" : "STALKER DETECTOR", hfg);
        display_number(SCR_W - 24, hdr_h / 2 - 4, count, hfg);

        int32_t list_y = hdr_h + 2;
        int32_t rows   = (SCR_H - hdr_h - ftr_h) / row_h;

        /* Clamp scroll. */
        if (top > count - (int)rows) {
            top = count - (int)rows;
        }
        if (top < 0) {
            top = 0;
        }

        int shown = 0;
        int idx   = 0;
        for (int i = 0; i < MAXTRACK && shown < rows; i++) {
            if (!g_tracks[i].used) {
                continue;
            }
            if (idx++ < top) {
                continue;
            }
            int32_t ry = list_y + shown * row_h;

            /* Last 3 MAC bytes: "AA:BB:CC" */
            char mac[9];
            uint8_t *a = g_tracks[i].addr;
            mac[0] = hexd(a[3] >> 4);  mac[1] = hexd(a[3] & 0xF); mac[2] = ':';
            mac[3] = hexd(a[4] >> 4);  mac[4] = hexd(a[4] & 0xF); mac[5] = ':';
            mac[6] = hexd(a[5] >> 4);  mac[7] = hexd(a[5] & 0xF); mac[8] = '\0';
            display_text(6, ry, mac, COLOR_WHITE);

            /* RSSI. */
            display_number(84, ry, g_tracks[i].rssi, COLOR_WHITE);

            /* Dwell seconds. */
            int32_t age_s = (int32_t)((now - g_tracks[i].first_ms) / 1000u);
            display_number(132, ry, age_s, COLOR_WHITE);
            display_text(160, ry, "s", COLOR_WHITE);

            /* Suspicious marker. */
            if ((now - g_tracks[i].first_ms) >= ALERT_MS &&
                g_tracks[i].rssi >= ALERT_RSSI) {
                display_text(SCR_W - 40, ry, "!!", COLOR_WHITE);
            } else {
                display_text(SCR_W - 40, ry, "FM", COLOR_WHITE);
            }
            shown++;
        }

        if (count == 0) {
            display_text(SCR_W / 2 - 60, SCR_H / 2 - 4, "No trackers seen", COLOR_WHITE);
        }

        display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
        display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                     "U/D:scroll A:clear B:exit", COLOR_BLACK);

        display_flush();
        delay(100000); /* 10 fps — scanning, not animating */
    }
    return 0;
}
