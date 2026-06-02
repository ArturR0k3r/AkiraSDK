/*
 * mphalport.c — AkiraOS HAL for MicroPython webassembly port.
 *
 * Replaces the browser-specific mphalport.c.  All I/O goes through
 * AkiraOS native imports; no WASI or Emscripten filesystem needed.
 *
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "py/mphal.h"

/* AkiraOS native imports */
extern int printf_native(const char *msg);
extern int rtc_get_uptime_ms(void);
extern int rtc_get_unix_time(void);

/* ── stdout ────────────────────────────────────────────────────────────────
 * printf_native expects a null-terminated string.  Buffer lines and flush
 * on newline or when the buffer is nearly full.
 */
static char _stdout_buf[512];
static int  _stdout_pos = 0;

static void _flush(void) {
    if (_stdout_pos > 0) {
        _stdout_buf[_stdout_pos] = '\0';
        printf_native(_stdout_buf);
        _stdout_pos = 0;
    }
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (_stdout_pos >= (int)(sizeof(_stdout_buf) - 1)) {
            _flush();
        }
        if (str[i] == '\n') {
            _flush();
        } else {
            _stdout_buf[_stdout_pos++] = str[i];
        }
    }
    return len;
}

static void _stderr_strn(void *env, const char *str, size_t len) {
    (void)env;
    mp_hal_stdout_tx_strn(str, len);
}
const mp_print_t mp_stderr_print = {NULL, _stderr_strn};

/* ── Timing ────────────────────────────────────────────────────────────── */

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)rtc_get_uptime_ms();
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)rtc_get_uptime_ms() * 1000u;
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return 0;
}

uint64_t mp_hal_time_ms(void) {
    return (uint64_t)rtc_get_unix_time() * 1000ULL;
}

uint64_t mp_hal_time_ns(void) {
    return mp_hal_time_ms() * 1000000ULL;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    mp_uint_t start = mp_hal_ticks_ms();
    while (mp_hal_ticks_ms() - start < ms) {}
}

void mp_hal_delay_us(mp_uint_t us) {
    mp_uint_t start = mp_hal_ticks_us();
    while (mp_hal_ticks_us() - start < us) {}
}

/* ── Interrupt char ────────────────────────────────────────────────────── */

extern int mp_interrupt_char;
int mp_hal_get_interrupt_char(void) {
    return mp_interrupt_char;
}
