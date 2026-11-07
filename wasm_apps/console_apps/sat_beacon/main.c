/*
 * sat_beacon — AkiraOS satellite beacon WASM app
 *
 * Sends AX.25 APRS packets through amateur LEO satellites using the LR2021.
 *
 * Supported satellites:
 *   ISS UHF  — 437.825 MHz, 1k2 AFSK,        alias ARISS
 *   CroCube   — 436.775 MHz, 9k6 GFSK,        9A0CC
 *   LASARsat  — 436.925 MHz, 9k6 GFSK,        OK0LSR
 *   Marina    — 436.680 MHz, 9k6 GFSK
 *   GRBBeta   — 436.785 MHz, 9k6 GFSK,        HA2GRB
 *
 * FOR AUTHORIZED USE ONLY — requires valid amateur radio license.
 *
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 */

#include "akira_api.h"
#include "ax25.h"

/* ── Note: All WASM native imports are declared in akira_api.h ────────── */
/* rf_send now takes (const uint8_t *payload, uint32_t len) — pass pointer directly */

/* ── Satellite presets ─────────────────────────────────────────────────── */
typedef struct {
    const char *name;        /* display name */
    const char *callsign;    /* satellite callsign */
    const char *alias;       /* digipeater alias (ARISS for ISS) */
    uint32_t    freq_hz;     /* TX/RX frequency */
    int         bitrate;     /* bitrate in bps */
    int         tx_power;    /* dBm */
} sat_preset_t;

static const sat_preset_t SAT_PRESETS[] = {
    {"ISS UHF",    "NA1SS",   "ARISS", 437825000, 1200, 22},
    {"CroCube",    "9A0CC",   NULL,    436775000, 9600, 22},
    {"LASARsat",   "OK0LSR",  NULL,    436925000, 9600, 22},
    {"Marina",     "MARINA",  NULL,    436680000, 9600, 22},
    {"GRBBeta",    "HA2GRB",  NULL,    436785000, 9600, 22},
};
#define N_PRESETS (sizeof(SAT_PRESETS) / sizeof(SAT_PRESETS[0]))

/* ── App state ─────────────────────────────────────────────────────────── */
static int g_cur_sat = 0;       /* index into SAT_PRESETS[] */
static int g_radio_ready = 0;   /* 1 if LR2021 initialized */
static int g_tx_count = 0;      /* beacons sent this session */
static int g_last_tx_status = 0;/* 0=ok, -1=not yet, <0=errno */
static char g_last_msg[64];     /* status line */
static int32_t g_scr_w = 320;
static int32_t g_scr_h = 240;
/* AX.25 frame buffer — MUST be static for WAMR ptr validation */
static uint8_t g_frame[340];

/* ── Simple button read (GPIO-based) ──────────────────────────────────── */
/* Button GPIO pins on akiraconsole_prod — configure as inputs with pull-up/pull-down */
#define BTN_A     15   /* GPIO15, active-high, pull-down */
#define BTN_B     16   /* GPIO16, active-high, pull-down */
#define BTN_Y     41   /* GPIO41, active-high, pull-down */
#define BTN_UP    4    /* GPIO4,  active-high, pull-down */
#define BTN_DOWN  5    /* GPIO5,  active-high, pull-down */

static int read_button_a(void) {
    return gpio_read(BTN_A);
}
static int read_button_y(void) {
    return gpio_read(BTN_Y);
}
static int read_button_up(void) {
    return gpio_read(BTN_UP);
}
static int read_button_down(void) {
    return gpio_read(BTN_DOWN);
}

/* ── Radio init ────────────────────────────────────────────────────────── */
static int sat_radio_init(void)
{
    if (g_radio_ready) return 0;

    /* Select LR2021 — chip index from AKIRA_RF_CHIP_LR2021 = 5 */
    if (rf_select(5) < 0) return -1;

    g_radio_ready = 1;
    return 0;
}

/* ── Send beacon ───────────────────────────────────────────────────────── */
static int sat_send_beacon(const sat_preset_t *sat, const char *msg)
{
    int frame_len;

    /* Build AX.25 UI frame into global buffer (must be static for WAMR) */
    const char *dest = sat->alias ? sat->alias : sat->callsign;
    const char *digi  = sat->alias ? sat->alias : NULL;

    frame_len = ax25_build_ui_frame(
        dest, 0,           /* destination + SSID */
        "AKIRA", 0,        /* source callsign + SSID */
        digi,              /* digipeater path (ARISS for ISS) */
        msg,               /* payload */
        g_frame, sizeof(g_frame)
    );
    if (frame_len < 0) return frame_len;

    /* Configure radio for this satellite */
    rf_set_frequency(sat->freq_hz);
    rf_set_power((int8_t)sat->tx_power);
    rf_set_modulation(RADIO_MOD_GFSK);   /* GFSK for AX.25 */
    rf_set_bitrate(sat->bitrate);

    /* Transmit the AX.25 frame */
    int ret = rf_send(g_frame, (uint32_t)frame_len);
    g_tx_count++;
    return ret;
}

/* ── Simple string copy (no libc) ──────────────────────────────────────── */
static void sat_strcpy(char *dst, const char *src, int max)
{
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ── Draw UI ───────────────────────────────────────────────────────────── */
static void draw_ui(void)
{
    /* Clear screen */
    display_rect(0, 0, g_scr_w, g_scr_h, COLOR_BLACK);

    /* Header */
    display_text_large(4, 6, "SAT BEACON", COLOR_CYAN);
    display_text(4, 28, "LEO Satellite Comms", COLOR_DARK_GRAY);

    /* ── Satellite list ───── */
    int list_y = 48;
    for (int i = 0; i < (int)N_PRESETS; i++) {
        int y = list_y + i * 22;
        int color = (i == g_cur_sat) ? COLOR_YELLOW : COLOR_WHITE;
        display_text(8, y, SAT_PRESETS[i].name, color);

        /* Frequency on same line */
        char fb[32];
        int freq_mhz = (int)(SAT_PRESETS[i].freq_hz / 1000000);
        int freq_khz = (int)((SAT_PRESETS[i].freq_hz % 1000000) / 1000);
        fb[0] = '0' + (char)(freq_mhz / 100);
        fb[1] = '0' + (char)((freq_mhz / 10) % 10);
        fb[2] = '0' + (char)(freq_mhz % 10);
        fb[3] = '.';
        fb[4] = '0' + (char)((freq_khz / 100) % 10);
        fb[5] = '0' + (char)((freq_khz / 10) % 10);
        fb[6] = '0' + (char)(freq_khz % 10);
        fb[7] = ' ';
        fb[8] = 'M';
        fb[9] = 'H';
        fb[10] = 'z';
        fb[11] = '\0';
        display_text(130, y, fb, COLOR_GRAY);
    }

    /* ── Status bar ───── */
    int status_y = g_scr_h - 52;
    display_rect(0, status_y, g_scr_w, 2, COLOR_DARK_GRAY);

    /* Radio status */
    display_text(4, status_y + 8, g_radio_ready ? "Radio: LR2021 OK" : "Radio: NOT INIT",
                 g_radio_ready ? COLOR_GREEN : COLOR_RED);

    /* TX count */
    char txb[20];
    txb[0] = 'T';
    txb[1] = 'X';
    txb[2] = ':';
    txb[3] = ' ';
    txb[4] = '0' + (char)(g_tx_count / 10);
    txb[5] = '0' + (char)(g_tx_count % 10);
    txb[6] = '\0';
    display_text(180, status_y + 8, txb, COLOR_WHITE);

    /* Last TX status */
    display_text(4, status_y + 26, g_last_msg, g_last_tx_status == 0 ? COLOR_GREEN : COLOR_YELLOW);

    /* ── Footer hints ───── */
    int hint_y = g_scr_h - 16;
    display_rect(0, hint_y, g_scr_w, 16, COLOR_DARK_GRAY);
    display_text(4, hint_y + 2, "A:Send  Y:NextSat  UP/DN:Select", COLOR_GRAY);

    display_flush();
}

/* ── Entry point ───────────────────────────────────────────────────────── */
int main(void)
{
    /* Get display size (may differ on Sharp vs ST7789) */
    display_get_size(&g_scr_w, &g_scr_h);

    /* Initialize radio */
    int ret = sat_radio_init();
    if (ret < 0) {
        sat_strcpy(g_last_msg, "Radio init err", 63);
        g_last_tx_status = -1;
    } else {
        sat_strcpy(g_last_msg, "Ready. Check n2yo.com for pass times.", 63);
        g_last_tx_status = 0;
    }

    draw_ui();

    /* ── Main loop ─────────────────────────────────────────── */
    int prev_a = 0;
    int prev_y = 0;
    int prev_up = 0;
    int prev_down = 0;

    for (;;) {
        int btn_a = read_button_a();
        int btn_y_v = read_button_y();
        int btn_up = read_button_up();
        int btn_down_v = read_button_down();

        /* Button A (pressed — detect rising edge) */
        if (btn_a && !prev_a) {
            if (g_radio_ready) {
                const sat_preset_t *sat = &SAT_PRESETS[g_cur_sat];
                char beacon_msg[64];
                beacon_msg[0] = 'B';
                beacon_msg[1] = 'e';
                beacon_msg[2] = 'a';
                beacon_msg[3] = 'c';
                beacon_msg[4] = 'o';
                beacon_msg[5] = 'n';
                beacon_msg[6] = ' ';
                beacon_msg[7] = '#';
                beacon_msg[8] = '0' + (char)((g_tx_count + 1) / 10);
                beacon_msg[9] = '0' + (char)((g_tx_count + 1) % 10);
                beacon_msg[10] = ' ';
                beacon_msg[11] = 'f';
                beacon_msg[12] = 'r';
                beacon_msg[13] = 'o';
                beacon_msg[14] = 'm';
                beacon_msg[15] = ' ';
                beacon_msg[16] = 'A';
                beacon_msg[17] = 'k';
                beacon_msg[18] = 'i';
                beacon_msg[19] = 'r';
                beacon_msg[20] = 'a';
                beacon_msg[21] = 'C';
                beacon_msg[22] = 'o';
                beacon_msg[23] = 'n';
                beacon_msg[24] = 's';
                beacon_msg[25] = 'o';
                beacon_msg[26] = 'l';
                beacon_msg[27] = 'e';
                beacon_msg[28] = '\0';

                g_last_tx_status = sat_send_beacon(sat, beacon_msg);
                if (g_last_tx_status == 0) {
                    sat_strcpy(g_last_msg, "BEACON SENT! Check aprs.fi", 63);
                } else {
                    /* Show actual error code */
                    char errbuf[32];
                    errbuf[0] = 'T';
                    errbuf[1] = 'X';
                    errbuf[2] = ' ';
                    errbuf[3] = 'e';
                    errbuf[4] = 'r';
                    errbuf[5] = 'r';
                    errbuf[6] = ' ';
                    errbuf[7] = '-';
                    int e = -g_last_tx_status;
                    errbuf[8] = '0' + (char)(e / 10);
                    errbuf[9] = '0' + (char)(e % 10);
                    errbuf[10] = '\0';
                    sat_strcpy(g_last_msg, errbuf, 31);
                }
            } else {
                sat_strcpy(g_last_msg, "Radio not initialized", 63);
                g_last_tx_status = -2;
            }
            draw_ui();
        }

        /* Button Y — cycle to next satellite */
        if (btn_y_v && !prev_y) {
            g_cur_sat = (g_cur_sat + 1) % (int)N_PRESETS;
            draw_ui();
        }

        /* UP — previous satellite */
        if (btn_up && !prev_up) {
            g_cur_sat = (g_cur_sat - 1 + (int)N_PRESETS) % (int)N_PRESETS;
            draw_ui();
        }

        /* DOWN — next satellite */
        if (btn_down_v && !prev_down) {
            g_cur_sat = (g_cur_sat + 1) % (int)N_PRESETS;
            draw_ui();
        }

        prev_a = btn_a;
        prev_y = btn_y_v;
        prev_up = btn_up;
        prev_down = btn_down_v;

        delay(50000);  /* 50 ms = 50000 us (delay takes microseconds) */
    }

    return 0;
}
