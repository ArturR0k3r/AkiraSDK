/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief 5-button HID macro pad using direct GPIO polling.
 *
 * Button → action mapping (akiraconsole):
 *   UP   (pin 4)  — type: akira_run_script\n
 *   DOWN (pin 5)  — Win + PrtScn (screenshot)
 *   LEFT (pin 7)  — Play / Pause  (consumer key)
 *   RIGHT(pin 6)  — Mouse move +40, +40
 *   A    (pin 15) — Keypress 'A'
 *
 * Capabilities: display.write, gpio.read, hid
 */

#include "../include/akira_api.h"

/* ── Button pins ─────────────────────────────────────────────────────────── */
#define PIN_UP    4
#define PIN_DOWN  5
#define PIN_LEFT  6
#define PIN_RIGHT 7
#define PIN_A     15

/* ── Display geometry ───────────────────────────────────────────────────── */
static int32_t DISPLAY_W = 320, DISPLAY_H = 240;
static int g_mono = 0;   /* 1 = Sharp monochrome 400×240 */

/* Sharp-safe colour palette (all render correctly under INVERT_COLORS=y) */
#define C_BG        0x0000u   /* black → displays WHITE on Sharp */
#define C_FG        0xFFFFu   /* white → displays BLACK on Sharp */
#define C_ACCENT    0xFFFFu   /* same on mono */
#define C_DIM       0xFFFFu
#define C_INV_BG    0xFFFFu   /* inverted-row bg */
#define C_INV_FG    0x0000u   /* inverted-row text */

/* Colour/mono-aware helpers */
#define ROW_BG(active)   ((active) ? C_INV_BG : C_BG)
#define ROW_FG(active)   ((active) ? C_INV_FG : C_FG)
#define KEY_BG(active)   ((active) ? C_INV_BG : C_FG)   /* key box always inverted vs row */
#define KEY_FG(active)   ((active) ? C_INV_FG : C_BG)

/* Layout */
#define HEADER_H   26
#define ROW_H      38   /* 5 rows × 38 = 190 + header 26 = 216 ≤ 240 */
#define KEY_W      52   /* width of left key-label box */
#define PAD_ROWS   5

/* ── Macro definitions ──────────────────────────────────────────────────── */
typedef struct { const char *label; const char *hint; } pad_def_t;

static const pad_def_t k_pads[PAD_ROWS] = {
    { "UP",    "Type: akira_run_script" },
    { "DOWN",  "Win+PrtScn screenshot"  },
    { "LEFT",  "Play / Pause"           },
    { "RIGHT", "Mouse move +40,+40"     },
    { "A",     "Keypress: A"            },
};

/* ── Edge-detection ─────────────────────────────────────────────────────── */
static int btn_pins[PAD_ROWS] = {PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_A};
static int prev[PAD_ROWS];

static void buttons_init(void)
{
    gpio_configure(PIN_UP,    GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(PIN_DOWN,  GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(PIN_LEFT,  GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(PIN_RIGHT, GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
    gpio_configure(PIN_A,     GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
}

static int buttons_edge(void)
{
    int mask = 0;
    for (int i = 0; i < PAD_ROWS; i++) {
        int cur = gpio_read(btn_pins[i]) == 1;
        if (cur && !prev[i]) mask |= (1 << i);
        prev[i] = cur;
    }
    return mask;
}

/* ── Drawing ────────────────────────────────────────────────────────────── */
static void draw_row(int row, const char *label, const char *hint, int active)
{
    int y = HEADER_H + row * ROW_H;

    /* Row background */
    display_rect(0, y, DISPLAY_W, ROW_H - 1, ROW_BG(active));

    /* Left key-label box — always inverted vs row */
    display_rect(0, y, KEY_W, ROW_H - 1, KEY_BG(active));

    /* Key label centred in box */
    int llen = 0; while(label[llen]) llen++;
    int lx = (KEY_W - llen * 8) / 2;
    if(lx < 2) lx = 2;
    display_text_large(lx, y + 10, label, KEY_FG(active));

    /* Hint text to the right of the key box */
    display_text(KEY_W + 6, y + 12, hint, ROW_FG(active));

    /* Row separator (white on Sharp) */
    display_hline(0, y + ROW_H - 1, DISPLAY_W, C_FG);
}

static void draw_header(int ble_active)
{
    /* Header box */
    display_rect(0, 0, DISPLAY_W, HEADER_H, C_FG);   /* white box */
    display_text_large(6, 5, "Macro Pad", C_BG);       /* black title */

    /* BLE status on the right */
    const char *ble_lbl = ble_active ? "[BLE]" : "[ -- ]";
    int blen = 0; while(ble_lbl[blen]) blen++;
    display_text(DISPLAY_W - blen * 8 - 6, 8, ble_lbl, C_BG);

    /* Bottom separator of header */
    display_hline(0, HEADER_H, DISPLAY_W, C_FG);
}

static void draw_all(int active_mask, int ble_active)
{
    display_clear(C_BG);
    draw_header(ble_active);
    for (int i = 0; i < PAD_ROWS; i++) {
        draw_row(i, k_pads[i].label, k_pads[i].hint, (active_mask >> i) & 1);
    }
    display_flush();
}

/* ── Macro dispatch ─────────────────────────────────────────────────────── */
static void fire_macro(int idx)
{
    switch (idx) {
    case 0: hid_type_string("akira_run_script\n"); break;
    case 1: hid_action_trigger("screenshot"); break;
    case 2: hid_consumer_send(HID_CONSUMER_PLAY_PAUSE); break;
    case 3: hid_mouse_move(40, 40); break;
    case 4:
        hid_key_press(HID_KEY_A);
        delay(50000);
        hid_key_release_all();
        break;
    }
}

/* ── Main ───────────────────────────────────────────────────────────────── */
int main(void)
{
    display_get_size(&DISPLAY_W, &DISPLAY_H);
    g_mono = (DISPLAY_W >= 400);
    buttons_init();

    /* Init splash */
    display_clear(C_BG);
    display_rect(0, 0, DISPLAY_W, HEADER_H, C_FG);
    display_text_large(6, 5, "Macro Pad", C_BG);
    display_text(6, HEADER_H + 12, "Initialising BLE HID...", C_FG);
    display_flush();

    hid_init(HID_TRANSPORT_BLE, HID_DEVICE_COMBO);
    hid_action_register("screenshot", HID_MOD_LEFT_GUI, HID_KEY_PRTSCN);

    draw_all(0, 1);

    while (1) {
        int edge = buttons_edge();

        if (edge) {
            draw_all(edge, 1);

            for (int i = 0; i < PAD_ROWS; i++) {
                if ((edge >> i) & 1) fire_macro(i);
            }

            delay(150000);   /* 150 ms highlight */
            draw_all(0, 1);
        }

        delay(16667);   /* ~60 Hz */
    }

    return 0;
}
