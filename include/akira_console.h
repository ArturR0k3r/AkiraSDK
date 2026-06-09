/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file akira_console.h
 * @brief AkiraConsole board constants — display geometry, color theme, button IDs.
 *
 * Pure-macro header. No firmware dependencies. Safe to include from any WASM app.
 * Automatically included by akira_api.h so apps do not need a separate include.
 */

#ifndef AKIRA_CONSOLE_H
#define AKIRA_CONSOLE_H

/* ─── Board identity ──────────────────────────────────────────────────────── */
#define AKIRA_CONSOLE_BOARD_NAME    "AkiraConsole"
#define AKIRA_CONSOLE_BOARD_REV     "1.0"
#define AKIRA_CONSOLE_MODEL_STRING  "AkiraConsole/ESP32-S3-N16R8"

/* ─── Display geometry (ST7789V 320×240) ─────────────────────────────────── */
#define CONSOLE_WIDTH       320
#define CONSOLE_HEIGHT      240

/* Fixed chrome zones shared by all shell screens */
#define CONSOLE_HEADER_H    20          /* rows 0–19:   title bar            */
#define CONSOLE_FOOTER_Y    220         /* rows 220–239: hint bar            */
#define CONSOLE_FOOTER_H    20
#define CONSOLE_CONTENT_Y   CONSOLE_HEADER_H
#define CONSOLE_CONTENT_H   (CONSOLE_FOOTER_Y - CONSOLE_HEADER_H)  /* 200px */

/* List row metrics */
#define CONSOLE_ROW_H       25          /* pixels per list row               */
#define CONSOLE_VISIBLE_ROWS (CONSOLE_CONTENT_H / CONSOLE_ROW_H)   /* 8     */

/* ─── Color theme (RGB565) ───────────────────────────────────────────────── */
/*
 * All colors are RGB565 values that can be passed directly to display_rect(),
 * display_text(), and every other display_* primitive.
 */
#define CONSOLE_COLOR_BG        0x0000U  /* pure black — default background  */
#define CONSOLE_COLOR_HEADER    0x2104U  /* very dark gray — header/footer   */
#define CONSOLE_COLOR_SEP       0x39E7U  /* separator line                   */
#define CONSOLE_COLOR_ACCENT    0x07FFU  /* cyan — cursor / selection ring   */
#define CONSOLE_COLOR_TEXT      0xFFFFU  /* white — primary text             */
#define CONSOLE_COLOR_DIM       0x7BEFU  /* gray — secondary text / hints    */
#define CONSOLE_COLOR_SEL_BG    0x001FU  /* blue — selected-row background   */

/* State badge colors */
#define CONSOLE_COLOR_OK        0x07E0U  /* green  — RUNNING                 */
#define CONSOLE_COLOR_WARN      0xFD20U  /* orange — STOPPED                 */
#define CONSOLE_COLOR_ERR       0xF800U  /* red    — ERROR / FAILED          */
#define CONSOLE_COLOR_READY     0x39E7U  /* dim cyan — READY / INSTALLED     */

/* ─── Button IDs (match zephyr,code in DTS overlay) ─────────────────────── */
/*
 * Values mirror the `zephyr,code` properties in akiraconsole_esp32s3_procpu.overlay:
 *   gpio-keys {
 *     btn_up    { zephyr,code = <2>; };   // UP
 *     btn_down  { zephyr,code = <3>; };   // DOWN
 *     btn_left  { zephyr,code = <4>; };   // LEFT
 *     btn_right { zephyr,code = <5>; };   // RIGHT
 *     btn_a     { zephyr,code = <6>; };   // A (confirm / select)
 *     btn_b     { zephyr,code = <7>; };   // B (back / cancel)
 *     btn_x     { zephyr,code = <8>; };   // X (info / detail)
 *     btn_y     { zephyr,code = <9>; };   // Y (quick action)
 *   };
 *
 * input_get_buttons() returns a bitmask where bit N = (1 << zephyr,code).
 */
#define AKIRA_BTN_ID_UP     2
#define AKIRA_BTN_ID_DOWN   3
#define AKIRA_BTN_ID_LEFT   4
#define AKIRA_BTN_ID_RIGHT  5
#define AKIRA_BTN_ID_A      6
#define AKIRA_BTN_ID_B      7
#define AKIRA_BTN_ID_X      8
#define AKIRA_BTN_ID_Y      9

/* Bitmasks — OR these against the value returned by input_get_buttons() */
#define AKIRA_BTN_UP    (1U << AKIRA_BTN_ID_UP)
#define AKIRA_BTN_DOWN  (1U << AKIRA_BTN_ID_DOWN)
#define AKIRA_BTN_LEFT  (1U << AKIRA_BTN_ID_LEFT)
#define AKIRA_BTN_RIGHT (1U << AKIRA_BTN_ID_RIGHT)
#define AKIRA_BTN_A     (1U << AKIRA_BTN_ID_A)
#define AKIRA_BTN_B     (1U << AKIRA_BTN_ID_B)
#define AKIRA_BTN_X     (1U << AKIRA_BTN_ID_X)
#define AKIRA_BTN_Y     (1U << AKIRA_BTN_ID_Y)

/** Test whether a button is set in a bitmask from input_get_buttons(). */
#define AKIRA_BTN_PRESSED(mask, btn) (((mask) & (btn)) != 0U)

/**
 * Maximum value returned by input_get_dial().
 * The dial reports 0 (counter-clockwise stop) to AKIRA_DIAL_MAX (clockwise stop).
 */
#define AKIRA_DIAL_MAX 255

/* ─── Display type detection ──────────────────────────────────────────────── */
/*
 * Sharp LS027B7DH01 is 400×240 monochrome.  Call akira_display_is_mono() once
 * at app startup and cache the result.  On monochrome, use inverted selection
 * (white bg + black text) so selected rows are visually distinct.
 *
 * Color-safe selection palette helper:
 *   sel_bg  = akira_sel_bg(is_mono)   → 0x001F (blue) or 0xFFFF (white)
 *   sel_txt = akira_sel_txt(is_mono)  → 0xFFFF (white) or 0x0000 (black)
 */
extern int display_get_size(int32_t *w_out, int32_t *h_out);

static inline int akira_display_is_mono(void) {
    int32_t w = 0, h = 0;
    display_get_size(&w, &h);
    return (w >= 400);
}

#define AKIRA_SEL_BG(mono)  ((mono) ? 0xFFFFU : CONSOLE_COLOR_SEL_BG)
#define AKIRA_SEL_TXT(mono) ((mono) ? 0x0000U : CONSOLE_COLOR_TEXT)
#define AKIRA_HDR_BG(mono)  (0x0000U)  /* always black — works on both */

/* ─── AkiraConsole input API ──────────────────────────────────────────────── */
/*
 * Required capability: "input.read"
 *
 * input_get_buttons() returns a bitmask of currently held buttons (non-blocking).
 * Bit N is set when the button with zephyr,code == N is pressed.
 * Use AKIRA_BTN_* masks to test individual bits.
 *
 * input_poll_event() drains one edge event (press or release) from the ring
 * buffer. Returns 1 if an event was written, 0 if the queue is empty.
 */

/** Packed button edge event returned by input_poll_event(). */
typedef struct {
    uint32_t button_id; /**< AKIRA_BTN_ID_* value — matches zephyr,code */
    uint32_t pressed;   /**< 1 = press, 0 = release                      */
} akira_input_event_t;

/** @brief Return bitmask of currently held buttons (non-blocking). */
extern int input_get_buttons(void);

/** @brief Drain one edge event from the ring buffer (non-blocking).
 *  @return 1 if event written, 0 if empty, negative on error. */
extern int input_poll_event(akira_input_event_t *evt);

/**
 * @brief Read the current rotary dial position (non-blocking).
 *
 * The dial (RK10J12R0A0B potentiometer + RC oscillator on AkiraConsole
 * Production) reports an absolute position in the range 0–AKIRA_DIAL_MAX.
 * The value is refreshed every ~50 ms by the PWM-dial driver and is always
 * safe to poll in a tight loop — no event queue to drain.
 *
 * Requires capability: "input.read"
 *
 * @return 0 (CCW stop) .. 255 (CW stop), or 0 if no dial hardware is present.
 */
extern int input_get_dial(void);

#endif /* AKIRA_CONSOLE_H */
