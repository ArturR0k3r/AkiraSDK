/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file macro_pad.c
 * @brief 5-button HID macro pad using direct GPIO polling.
 *
 * Button → action mapping (akiraconsole, ACTIVE_HIGH, PULL_DOWN):
 *   UP   (pin  4) — hid_type_string("akira_run_script\n")
 *   DOWN (pin  5) — Win + PrtScn (screenshot via hid_action_trigger)
 *   LEFT (pin  6) — Play / Pause  (consumer key)
 *   RIGHT(pin  7) — Mouse move +40, +40
 *   A    (pin 15) — Keypress 'A'
 *
 * Capabilities: display.write, gpio.read, hid
 */

#include "include/akira_api.h"

/* ── Button pins (gpio0, ACTIVE_HIGH, PULL_DOWN) ───────────────────────── */
#define PIN_UP    4
#define PIN_DOWN  5
#define PIN_LEFT  6
#define PIN_RIGHT 7
#define PIN_A     15

/* ── Display geometry ───────────────────────────────────────────────────── */
#define DISPLAY_W  320
#define DISPLAY_H  240
#define TITLE_H     26
#define ROW_H       40

/* ── Colours ────────────────────────────────────────────────────────────── */
#define C_HEADER  0x2945
#define C_IDLE    0x4228
#define C_HIT     COLOR_GREEN
#define C_LABEL   COLOR_WHITE
#define C_DIM     COLOR_GRAY

/* ── Macro rows ─────────────────────────────────────────────────────────── */
#define PAD_ROWS  5

typedef struct {
    const char *label;
    const char *hint;
} pad_def_t;

static const pad_def_t k_pads[PAD_ROWS] = {
    { "UP",    "Type: akira_run_script" },
    { "DOWN",  "Win + PrtScn (screenshot)" },
    { "LEFT",  "Play / Pause" },
    { "RIGHT", "Mouse move +40, +40" },
    { "A",     "Key: A" },
};

/* ── Edge-detection state ───────────────────────────────────────────────── */
static int btn_pins[PAD_ROWS] = {PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_A};
static int prev[PAD_ROWS];

static void buttons_init(void)
{
    gpio_configure(PIN_UP,    GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_DOWN,  GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_LEFT,  GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_RIGHT, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_configure(PIN_A,     GPIO_INPUT | GPIO_PULL_DOWN);
}

/* Returns bitmask of newly-pressed buttons (rising edge) */
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
    int y = TITLE_H + row * ROW_H;
    display_rect(0, y, DISPLAY_W, ROW_H - 2, active ? C_HIT : C_IDLE);
    display_rect(0, y, 48, ROW_H - 2, active ? COLOR_ORANGE : 0x18C3);
    display_text_large(6, y + 10, label, C_LABEL);
    display_text(54, y + 4, hint, active ? COLOR_WHITE : C_DIM);
}

static void draw_all(int active_mask)
{
    display_clear(COLOR_BLACK);
    display_rect(0, 0, DISPLAY_W, TITLE_H, C_HEADER);
    display_text_large(8, 5, "Macro Pad", COLOR_WHITE);
    display_text(180, 8, "HID via USB/BLE", C_DIM);
    display_hline(0, TITLE_H, DISPLAY_W, COLOR_LIGHT_GRAY);
    for (int i = 0; i < PAD_ROWS; i++) {
        draw_row(i, k_pads[i].label, k_pads[i].hint, (active_mask >> i) & 1);
    }
    display_flush();
}

/* ── Macro dispatch ─────────────────────────────────────────────────────── */
static void fire_macro(int idx)
{
    switch (idx) {

    case 0: /* UP — type script trigger string */
        hid_type_string("akira_run_script\n");
        break;

    case 1: /* DOWN — Win + PrtScn screenshot */
        hid_action_trigger("screenshot");
        break;

    case 2: /* LEFT — Play / Pause */
        hid_consumer_send(HID_CONSUMER_PLAY_PAUSE);
        break;

    case 3: /* RIGHT — Mouse move +40, +40 */
        hid_mouse_move(40, 40);
        break;

    case 4: /* A — press key 'A' */
        hid_key_press(HID_KEY_A);
        delay(50000);
        hid_key_release_all();
        break;
    }
}

/* ── Main ───────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("[macro_pad] starting\n");
    buttons_init();

    /* ── Self-initialize HID over BLE ──────────────────────────────────── */
    display_clear(COLOR_BLACK);
    display_rect(0, 0, DISPLAY_W, TITLE_H, C_HEADER);
    display_text_large(8, 5, "Macro Pad", COLOR_WHITE);
    display_text(8, TITLE_H + 12, "Enabling HID...", C_DIM);
    display_flush();

    hid_init(HID_TRANSPORT_BLE, HID_DEVICE_COMBO);

    /* Register Win+PrtScn shortcut */
    hid_action_register("screenshot", HID_MOD_LEFT_GUI, HID_KEY_PRTSCN);

    draw_all(0);

    while (1) {
        int edge = buttons_edge();

        if (edge) {
            draw_all(edge);

            for (int i = 0; i < PAD_ROWS; i++) {
                if ((edge >> i) & 1) {
                    /* "[macro_pad] fire X\n" with single-arg printf */
                    char msg[] = "[macro_pad] fire X\n";
                    msg[17] = '0' + i;
                    printf(msg);
                    fire_macro(i);
                }
            }

            delay(150000); /* 150 ms highlight hold */
            draw_all(0);
        }

        delay(16667); /* ~60 Hz scan */
    }

    return 0;
}
