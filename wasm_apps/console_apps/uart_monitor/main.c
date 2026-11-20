/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file uart_monitor.c
 * @brief Pocket serial sniffer.
 *
 * Opens a secondary UART (port 0 = UART1) and shows incoming bytes as a live
 * scrolling hex + ASCII dump — a hardware debugging tool the console can be
 * that a phone cannot. Wire the target's TX to the console's RX at a matching
 * baud and watch the traffic.
 *
 *   LEFT/RIGHT — change baud
 *   A          — pause / resume capture
 *   X          — clear
 *   B          — exit
 *
 * Capabilities: display.write, input.read, uart
 */

#include "akira_api.h"

static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

static const int32_t BAUDS[] = { 9600, 19200, 38400, 57600, 115200 };
#define NBAUD ((int)(sizeof(BAUDS) / sizeof(BAUDS[0])))

#define BYTES_PER_ROW 8
#define RING_ROWS     16               /* scrollback rows kept */
#define RING_SZ       (BYTES_PER_ROW * RING_ROWS)

static uint8_t ring[RING_SZ];
static int     ring_len; /* valid bytes at tail of ring */

static char hexd(int n) { return (char)(n < 10 ? ('0' + n) : ('A' + n - 10)); }

static void ring_push(const uint8_t *data, int n)
{
    for (int i = 0; i < n; i++) {
        if (ring_len < RING_SZ) {
            ring[ring_len++] = data[i];
        } else {
            /* full — shift up one row to make space (cheap, called rarely) */
            for (int k = BYTES_PER_ROW; k < RING_SZ; k++) {
                ring[k - BYTES_PER_ROW] = ring[k];
            }
            ring_len = RING_SZ - BYTES_PER_ROW;
            ring[ring_len++] = data[i];
        }
    }
}

int main(void)
{
    display_get_size(&SCR_W, &SCR_H);

    int baud_idx = NBAUD - 1; /* default 115200 */
    int handle   = uart_open(0, BAUDS[baud_idx]);
    int paused   = 0;
    unsigned long total = 0;

    int prev = input_get_buttons();

    while (1) {
        int held    = input_get_buttons();
        int pressed = held & ~prev;
        prev        = held;

        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_B)) {
            if (handle >= 0) {
                uart_close(handle);
            }
            return 0;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_A)) {
            paused = !paused;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_X)) {
            ring_len = 0;
            total    = 0;
        }
        if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT) ||
            AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_RIGHT)) {
            if (AKIRA_BTN_PRESSED(pressed, AKIRA_BTN_LEFT)) {
                baud_idx = (baud_idx + NBAUD - 1) % NBAUD;
            } else {
                baud_idx = (baud_idx + 1) % NBAUD;
            }
            if (handle >= 0) {
                uart_close(handle);
            }
            handle = uart_open(0, BAUDS[baud_idx]);
        }

        if (!paused && handle >= 0) {
            uint8_t buf[64];
            int n = uart_read(handle, buf, sizeof(buf));
            if (n > 0) {
                ring_push(buf, n);
                total += (unsigned long)n;
            }
        }

        /* ---------- draw ---------- */
        display_clear(COLOR_BLACK);

        int32_t hdr_h = SCR_H / 10;
        display_rect(0, 0, SCR_W, hdr_h, COLOR_WHITE);
        display_text(4, hdr_h / 2 - 4, "UART MON", COLOR_BLACK);
        display_number(80, hdr_h / 2 - 4, BAUDS[baud_idx], COLOR_BLACK);
        display_text(150, hdr_h / 2 - 4, "8N1", COLOR_BLACK);
        if (handle < 0) {
            display_text(200, hdr_h / 2 - 4, "OPEN-ERR", COLOR_BLACK);
        } else if (paused) {
            display_text(200, hdr_h / 2 - 4, "PAUSED", COLOR_BLACK);
        } else {
            display_text(200, hdr_h / 2 - 4, "LIVE", COLOR_BLACK);
        }
        display_number(258, hdr_h / 2 - 4, (int)total, COLOR_BLACK);

        /* Hex + ASCII rows (newest at the bottom). */
        int32_t row_h  = 12;
        int32_t area_y = hdr_h + 2;
        int32_t ftr_h  = SCR_H / 10;
        int rows_fit   = (int)((SCR_H - hdr_h - ftr_h) / row_h);
        int total_rows = (ring_len + BYTES_PER_ROW - 1) / BYTES_PER_ROW;
        int start_row  = total_rows > rows_fit ? total_rows - rows_fit : 0;

        int draw = 0;
        for (int r = start_row; r < total_rows && draw < rows_fit; r++, draw++) {
            int base = r * BYTES_PER_ROW;
            char line[BYTES_PER_ROW * 3 + BYTES_PER_ROW + 2];
            int  p = 0;
            for (int c = 0; c < BYTES_PER_ROW; c++) {
                if (base + c < ring_len) {
                    uint8_t v = ring[base + c];
                    line[p++] = hexd(v >> 4);
                    line[p++] = hexd(v & 0xF);
                    line[p++] = ' ';
                } else {
                    line[p++] = ' '; line[p++] = ' '; line[p++] = ' ';
                }
            }
            line[p++] = '|';
            for (int c = 0; c < BYTES_PER_ROW; c++) {
                if (base + c < ring_len) {
                    uint8_t v = ring[base + c];
                    line[p++] = (v >= 32 && v < 127) ? (char)v : '.';
                } else {
                    line[p++] = ' ';
                }
            }
            line[p] = '\0';
            display_text(4, area_y + draw * row_h, line, COLOR_WHITE);
        }

        if (ring_len == 0) {
            display_text(8, area_y + 8, "waiting for data...", COLOR_WHITE);
        }

        display_rect(0, SCR_H - ftr_h, SCR_W, ftr_h, COLOR_WHITE);
        display_text(4, SCR_H - ftr_h + (ftr_h / 2 - 4),
                     "L/R:baud A:pause X:clear B:exit", COLOR_BLACK);

        display_flush();
        delay(30000);
    }
    return 0;
}
