/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * @file akira_api.h
 * @brief AkiraOS native API imports for WASM modules.
 *        All functions are provided by the host runtime via WAMR native binding.
 */

#ifndef AKIRA_API_H
#define AKIRA_API_H

#include <stdint.h>

/* ── WASM import/export macros ──────────────────────────────────────────── */
#ifdef __wasm__
#define WASM_IMPORT __attribute__((import_module("akira")))
#define WASM_EXPORT __attribute__((visibility("default")))
#else
/* Desktop stub: declarations only — implementations in hal_stub.c */
#define WASM_IMPORT extern
#define WASM_EXPORT
#endif

/* ── Display (240×135 px, RGB565, landscape) ────────────────────────────── */

/* Fill entire framebuffer with a solid RGB565 color */
WASM_IMPORT void akira_display_fill(uint16_t color);

/* Draw a filled rectangle at (x,y) of size (w,h) with RGB565 color */
WASM_IMPORT void akira_display_rect(int x, int y, int w, int h, uint16_t color);

/* Set a single pixel at (x,y) to RGB565 color */
WASM_IMPORT void akira_display_pixel(int x, int y, uint16_t color);

/* Flush the host framebuffer to the physical display */
WASM_IMPORT void akira_display_flush(void);

/* Return display width in pixels */
WASM_IMPORT int akira_display_get_width(void);

/* Return display height in pixels */
WASM_IMPORT int akira_display_get_height(void);

/* ── IMU ────────────────────────────────────────────────────────────────── */

/* Return gravity angle in radians.
 * 0 = device upright, positive = tilt right, negative = tilt left.
 * Source: ICM-42688-P accelerometer; falls back to MPU-6050 if unavailable. */
WASM_IMPORT float akira_imu_gravity_angle(void);

/* ── Input ──────────────────────────────────────────────────────────────── */

/* Return button bitmask.
 * Bit 0 = BTN_A, bit 1 = BTN_B, bit 2 = BTN_UP, bit 3 = BTN_DOWN. */
WASM_IMPORT uint8_t akira_input_get(void);

/* ── Time ───────────────────────────────────────────────────────────────── */

/* Return monotonic millisecond counter since boot */
WASM_IMPORT uint32_t akira_time_ms(void);

/* ── Audio ──────────────────────────────────────────────────────────────── */

/* Emit a square-wave beep at freq_hz for duration_ms milliseconds */
WASM_IMPORT void akira_audio_beep(uint16_t freq_hz, uint16_t duration_ms);

#endif /* AKIRA_API_H */
