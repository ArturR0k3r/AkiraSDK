/*
 * _akira.c — MicroPython C extension module for AkiraOS
 *
 * Wraps every akira_api.h function as a Python-callable object.
 * Compiled into micropython.wasm; import with `import _akira`.
 *
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "py/mpconfig.h"
#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/objint.h"
#include "py/mphal.h"

#include <stdint.h>
#include <string.h>

#ifndef STATIC
#define STATIC static
#endif

/* ── AkiraOS native imports (WASM env module) ─────────────────────────── */

extern int printf_native(const char *msg);
extern int delay(uint32_t us);

extern int display_clear(uint32_t color);
extern int display_pixel(int32_t x, int32_t y, uint32_t color);
extern int display_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
extern int display_rect_outline(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
extern int display_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern int display_text_large(int32_t x, int32_t y, const char *text, uint32_t color);
extern int display_number(int32_t x, int32_t y, int32_t value, uint32_t color);
extern int display_flush(void);
extern int display_get_size(int32_t *w_out, int32_t *h_out);
extern int display_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
extern int display_hline(int32_t x, int32_t y, int32_t len, uint32_t color);
extern int display_vline(int32_t x, int32_t y, int32_t len, uint32_t color);
extern int display_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);
extern int display_circle_fill(int32_t cx, int32_t cy, int32_t r, uint32_t color);
extern int display_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
extern int display_triangle_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
extern int display_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color);
extern int display_rounded_rect_fill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color);
extern int display_progress_bar(int32_t x, int32_t y, int32_t w, int32_t h, int32_t value, int32_t max_val, uint32_t fg, uint32_t bg);

extern int gpio_configure(uint32_t pin, uint32_t flags);
extern int gpio_read(uint32_t pin);
extern int gpio_write(uint32_t pin, uint32_t value);

extern int sensor_read(int32_t channel);

extern int timer_create(void);
extern int timer_start(int32_t handle);
extern int timer_stop(int32_t handle);
extern int timer_elapsed(int32_t handle);
extern int timer_free(int32_t handle);

extern int storage_open(const char *path, int flags);
extern int storage_read(int fd, void *buf, int len);
extern int storage_write(int fd, const void *buf, int len);
extern void storage_close(int fd);
extern int storage_delete(const char *path);
extern int storage_list(const char *path, char *buf, int len);

extern int ble_init(void);
extern int ble_deinit(void);
extern int ble_set_local_name(const char *name);
extern int ble_service_create(const char *uuid128_str);
extern int ble_char_create(const char *uuid128_str, int32_t props, int32_t max_len);
extern int ble_service_add_char(int32_t svc_h, int32_t char_h);
extern int ble_add_service(int32_t svc_h);
extern int ble_set_advertised_service(int32_t svc_h);
extern int ble_advertise(void);
extern int ble_stop_advertise(void);
extern int ble_is_connected(void);
extern int ble_char_write(int32_t char_h, const uint8_t *data, uint32_t len);
extern int ble_char_read(int32_t char_h, uint8_t *buf, uint32_t len);
extern int ble_event_pop(uint8_t *buf, uint32_t len);

extern int msg_subscribe(const char *topic);
extern int msg_unsubscribe(const char *topic);
extern int msg_publish(const char *topic, const uint8_t *data_ptr, uint32_t len);
extern int msg_recv(const char *topic, uint8_t *buf_ptr, uint32_t buf_len, int32_t timeout_ms);
extern int msg_try_recv(const char *topic, uint8_t *buf_ptr, uint32_t buf_len);
extern int msg_pending(const char *topic);

extern int app_get_status(const char *name);
extern int app_list(uint8_t *buf, uint32_t buf_len);
extern int app_get_self_name(uint8_t *buf, uint32_t buf_len);
extern int app_start(const char *name);
extern int app_stop(const char *name);
extern int app_switch(const char *name);

extern int power_get_mode(void);
extern int power_get_battery_level(void);
extern int power_get_battery_status(void *buf, int len);
extern int power_set_mode(int mode);
extern int power_wake_on_gpio(int pin, int edge);
extern int power_wake_on_timer(int ms);
extern int power_set_low_power(int enable);

extern int hid_init(int transport, int device_types);
extern int hid_enable(void);
extern int hid_disable(void);
extern int hid_is_connected(void);
extern int hid_key_press(int32_t keycode);
extern int hid_key_release(int32_t keycode);
extern int hid_key_release_all(void);
extern int hid_type_string(const char *str);
extern int hid_gamepad_press(int32_t btn_mask);
extern int hid_gamepad_release(int32_t btn_mask);
extern int hid_gamepad_set_axis(int32_t axis, int32_t value);
extern int hid_gamepad_set_dpad(int32_t direction);
extern int hid_gamepad_reset(void);
extern int hid_mouse_move(int32_t dx, int32_t dy);
extern int hid_mouse_btn_press(int32_t button);
extern int hid_mouse_btn_release(int32_t button);
extern int hid_mouse_scroll(int32_t delta);
extern int hid_consumer_send(int32_t usage_code);
extern int hid_set_transport(int32_t transport);
extern int hid_set_device_types(int32_t types);

extern int uart_open(int32_t port_id, int32_t baud_rate);
extern int uart_write(int32_t handle, const uint8_t *buf, uint32_t len);
extern int uart_read(int32_t handle, uint8_t *buf, uint32_t max_len);
extern int uart_close(int32_t handle);

extern int i2c_write_reg(int32_t bus_id, int32_t dev_addr, int32_t reg_addr, const uint8_t *buf, uint32_t len);
extern int i2c_read_reg(int32_t bus_id, int32_t dev_addr, int32_t reg_addr, uint8_t *buf, uint32_t len);

extern int pwm_set(int32_t channel, int32_t freq_hz, int32_t duty_pct);
extern int pwm_disable(int32_t channel);

extern int adc_read(int channel);
extern int adc_read_mv(int channel);

extern int wdt_pet(void);

extern int rtc_get_unix_time(void);
extern int rtc_get_uptime_ms(void);
extern int rtc_set_unix_time(int32_t unix_time);
extern int rtc_set_alarm(int32_t unix_time);
extern int rtc_alarm_fired(void);

extern int settings_get(const char *key, char *buf, int32_t len);
extern int settings_set(const char *key, const char *value);
extern int settings_delete(const char *key);

extern int fs_open(const char *path, int flags);
extern int fs_close(int fd);
extern int fs_read(int fd, void *buf, int len);
extern int fs_write(int fd, const void *buf, int len);
extern int fs_seek(int fd, int offset, int whence);
extern int fs_tell(int fd);
extern int fs_unlink(const char *path);
extern int fs_mkdir(const char *path);
extern int fs_readdir(const char *path, char *buf, int len);

extern int crypto_sha256(const void *input, int in_len, uint8_t *out);
extern int crypto_random(void *buf, int len);

extern int net_open(int32_t type);
extern int net_connect(int32_t handle, const char *host, int32_t port);
extern int net_bind(int32_t handle, int32_t port);
extern int net_listen(int32_t handle, int32_t backlog);
extern int net_close(int32_t handle);
extern int net_tx_flush(int32_t handle);
extern int net_event_pop(void *buf, int32_t len);
extern int net_get_ip(char *buf, int32_t len);

extern int rf_set_frequency(uint32_t freq_hz);
extern int rf_set_power(int8_t dbm);
extern int rf_get_rssi(void);

extern int sd_scan_wasm(char *buf, int len);
extern int app_install_from_sd(const char *name);

/* ── Helpers ──────────────────────────────────────────────────────────── */

/* Read bytes/bytearray from a Python object into a C buffer pointer + len */
static inline void get_buffer(mp_obj_t obj, const uint8_t **data, size_t *len) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(obj, &bufinfo, MP_BUFFER_READ);
    *data = (const uint8_t *)bufinfo.buf;
    *len  = bufinfo.len;
}

#define SCRATCH_SIZE 4096
static char scratch[SCRATCH_SIZE];

/* ── Console ──────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_printf_native(mp_obj_t msg_obj) {
    const char *msg = mp_obj_str_get_str(msg_obj);
    return mp_obj_new_int(printf_native(msg));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_printf_native_obj, akira_printf_native);

STATIC mp_obj_t akira_delay(mp_obj_t ms_obj) {
    return mp_obj_new_int(delay((uint32_t)mp_obj_get_int(ms_obj) * 1000u));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_delay_obj, akira_delay);

/* ── Display ─────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_display_clear(mp_obj_t c) {
    return mp_obj_new_int(display_clear((uint32_t)mp_obj_get_int(c)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_display_clear_obj, akira_display_clear);

STATIC mp_obj_t akira_display_pixel(mp_obj_t x, mp_obj_t y, mp_obj_t c) {
    return mp_obj_new_int(display_pixel(mp_obj_get_int(x), mp_obj_get_int(y), (uint32_t)mp_obj_get_int(c)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(akira_display_pixel_obj, akira_display_pixel);

STATIC mp_obj_t akira_display_rect(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_rect(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), mp_obj_get_int(a[3]), (uint32_t)mp_obj_get_int(a[4])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_rect_obj, 5, 5, akira_display_rect);

STATIC mp_obj_t akira_display_rect_outline(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_rect_outline(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), mp_obj_get_int(a[3]), (uint32_t)mp_obj_get_int(a[4])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_rect_outline_obj, 5, 5, akira_display_rect_outline);

STATIC mp_obj_t akira_display_text(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_text(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_str_get_str(a[2]), (uint32_t)mp_obj_get_int(a[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_text_obj, 4, 4, akira_display_text);

STATIC mp_obj_t akira_display_text_large(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_text_large(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_str_get_str(a[2]), (uint32_t)mp_obj_get_int(a[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_text_large_obj, 4, 4, akira_display_text_large);

STATIC mp_obj_t akira_display_number(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_number(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), (uint32_t)mp_obj_get_int(a[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_number_obj, 4, 4, akira_display_number);

STATIC mp_obj_t akira_display_flush(void) {
    return mp_obj_new_int(display_flush());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_display_flush_obj, akira_display_flush);

STATIC mp_obj_t akira_display_get_size(void) {
    int32_t w = 0, h = 0;
    display_get_size(&w, &h);
    mp_obj_t t[2] = { mp_obj_new_int(w), mp_obj_new_int(h) };
    return mp_obj_new_tuple(2, t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_display_get_size_obj, akira_display_get_size);

STATIC mp_obj_t akira_display_line(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_line(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), mp_obj_get_int(a[3]), (uint32_t)mp_obj_get_int(a[4])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_line_obj, 5, 5, akira_display_line);

STATIC mp_obj_t akira_display_hline(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_hline(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), (uint32_t)mp_obj_get_int(a[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_hline_obj, 4, 4, akira_display_hline);

STATIC mp_obj_t akira_display_vline(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_vline(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), (uint32_t)mp_obj_get_int(a[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_vline_obj, 4, 4, akira_display_vline);

STATIC mp_obj_t akira_display_circle(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_circle(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), (uint32_t)mp_obj_get_int(a[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_circle_obj, 4, 4, akira_display_circle);

STATIC mp_obj_t akira_display_circle_fill(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_circle_fill(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), (uint32_t)mp_obj_get_int(a[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_circle_fill_obj, 4, 4, akira_display_circle_fill);

STATIC mp_obj_t akira_display_triangle(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_triangle(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), mp_obj_get_int(a[3]),
        mp_obj_get_int(a[4]), mp_obj_get_int(a[5]), (uint32_t)mp_obj_get_int(a[6])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_triangle_obj, 7, 7, akira_display_triangle);

STATIC mp_obj_t akira_display_triangle_fill(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_triangle_fill(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), mp_obj_get_int(a[3]),
        mp_obj_get_int(a[4]), mp_obj_get_int(a[5]), (uint32_t)mp_obj_get_int(a[6])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_triangle_fill_obj, 7, 7, akira_display_triangle_fill);

STATIC mp_obj_t akira_display_rounded_rect(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_rounded_rect(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), mp_obj_get_int(a[3]),
        mp_obj_get_int(a[4]), (uint32_t)mp_obj_get_int(a[5])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_rounded_rect_obj, 6, 6, akira_display_rounded_rect);

STATIC mp_obj_t akira_display_rounded_rect_fill(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_rounded_rect_fill(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), mp_obj_get_int(a[3]),
        mp_obj_get_int(a[4]), (uint32_t)mp_obj_get_int(a[5])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_rounded_rect_fill_obj, 6, 6, akira_display_rounded_rect_fill);

STATIC mp_obj_t akira_display_progress_bar(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(display_progress_bar(
        mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), mp_obj_get_int(a[3]),
        mp_obj_get_int(a[4]), mp_obj_get_int(a[5]),
        (uint32_t)mp_obj_get_int(a[6]), (uint32_t)mp_obj_get_int(a[7])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_display_progress_bar_obj, 8, 8, akira_display_progress_bar);

/* ── GPIO ─────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_gpio_configure(mp_obj_t pin, mp_obj_t flags) {
    return mp_obj_new_int(gpio_configure((uint32_t)mp_obj_get_int(pin), (uint32_t)mp_obj_get_int(flags)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_gpio_configure_obj, akira_gpio_configure);

STATIC mp_obj_t akira_gpio_read(mp_obj_t pin) {
    return mp_obj_new_int(gpio_read((uint32_t)mp_obj_get_int(pin)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_gpio_read_obj, akira_gpio_read);

STATIC mp_obj_t akira_gpio_write(mp_obj_t pin, mp_obj_t value) {
    return mp_obj_new_int(gpio_write((uint32_t)mp_obj_get_int(pin), (uint32_t)mp_obj_get_int(value)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_gpio_write_obj, akira_gpio_write);

/* ── Sensor ───────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_sensor_read(mp_obj_t channel) {
    return mp_obj_new_int(sensor_read(mp_obj_get_int(channel)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_sensor_read_obj, akira_sensor_read);

/* ── Timer ────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_timer_create(void) {
    return mp_obj_new_int(timer_create());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_timer_create_obj, akira_timer_create);

STATIC mp_obj_t akira_timer_start(mp_obj_t h) {
    return mp_obj_new_int(timer_start(mp_obj_get_int(h)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_timer_start_obj, akira_timer_start);

STATIC mp_obj_t akira_timer_stop(mp_obj_t h) {
    return mp_obj_new_int(timer_stop(mp_obj_get_int(h)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_timer_stop_obj, akira_timer_stop);

STATIC mp_obj_t akira_timer_elapsed(mp_obj_t h) {
    return mp_obj_new_int(timer_elapsed(mp_obj_get_int(h)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_timer_elapsed_obj, akira_timer_elapsed);

STATIC mp_obj_t akira_timer_free(mp_obj_t h) {
    return mp_obj_new_int(timer_free(mp_obj_get_int(h)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_timer_free_obj, akira_timer_free);

/* ── Storage ──────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_storage_open(mp_obj_t path, mp_obj_t flags) {
    return mp_obj_new_int(storage_open(mp_obj_str_get_str(path), mp_obj_get_int(flags)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_storage_open_obj, akira_storage_open);

STATIC mp_obj_t akira_storage_read(mp_obj_t fd, mp_obj_t length) {
    int len = mp_obj_get_int(length);
    uint8_t *buf = m_new(uint8_t, len);
    int n = storage_read(mp_obj_get_int(fd), buf, len);
    if (n < 0) { m_del(uint8_t, buf, len); return mp_obj_new_int(n); }
    mp_obj_t result = mp_obj_new_bytes(buf, n);
    m_del(uint8_t, buf, len);
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_storage_read_obj, akira_storage_read);

STATIC mp_obj_t akira_storage_write(mp_obj_t fd, mp_obj_t data) {
    const uint8_t *ptr; size_t len;
    get_buffer(data, &ptr, &len);
    return mp_obj_new_int(storage_write(mp_obj_get_int(fd), ptr, (int)len));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_storage_write_obj, akira_storage_write);

STATIC mp_obj_t akira_storage_close(mp_obj_t fd) {
    storage_close(mp_obj_get_int(fd));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_storage_close_obj, akira_storage_close);

STATIC mp_obj_t akira_storage_delete(mp_obj_t path) {
    return mp_obj_new_int(storage_delete(mp_obj_str_get_str(path)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_storage_delete_obj, akira_storage_delete);

STATIC mp_obj_t akira_storage_list(mp_obj_t path) {
    int n = storage_list(mp_obj_str_get_str(path), scratch, SCRATCH_SIZE);
    if (n < 0) return mp_obj_new_int(n);
    return mp_obj_new_str(scratch, strlen(scratch));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_storage_list_obj, akira_storage_list);

/* ── BLE ──────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_ble_init(void) { return mp_obj_new_int(ble_init()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_ble_init_obj, akira_ble_init);

STATIC mp_obj_t akira_ble_deinit(void) { return mp_obj_new_int(ble_deinit()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_ble_deinit_obj, akira_ble_deinit);

STATIC mp_obj_t akira_ble_set_local_name(mp_obj_t name) {
    return mp_obj_new_int(ble_set_local_name(mp_obj_str_get_str(name)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_ble_set_local_name_obj, akira_ble_set_local_name);

STATIC mp_obj_t akira_ble_service_create(mp_obj_t uuid) {
    return mp_obj_new_int(ble_service_create(mp_obj_str_get_str(uuid)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_ble_service_create_obj, akira_ble_service_create);

STATIC mp_obj_t akira_ble_char_create(mp_obj_t uuid, mp_obj_t props, mp_obj_t max_len) {
    return mp_obj_new_int(ble_char_create(mp_obj_str_get_str(uuid),
        mp_obj_get_int(props), mp_obj_get_int(max_len)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(akira_ble_char_create_obj, akira_ble_char_create);

STATIC mp_obj_t akira_ble_service_add_char(mp_obj_t svc, mp_obj_t ch) {
    return mp_obj_new_int(ble_service_add_char(mp_obj_get_int(svc), mp_obj_get_int(ch)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_ble_service_add_char_obj, akira_ble_service_add_char);

STATIC mp_obj_t akira_ble_add_service(mp_obj_t svc) {
    return mp_obj_new_int(ble_add_service(mp_obj_get_int(svc)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_ble_add_service_obj, akira_ble_add_service);

STATIC mp_obj_t akira_ble_set_advertised_service(mp_obj_t svc) {
    return mp_obj_new_int(ble_set_advertised_service(mp_obj_get_int(svc)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_ble_set_advertised_service_obj, akira_ble_set_advertised_service);

STATIC mp_obj_t akira_ble_advertise(void) { return mp_obj_new_int(ble_advertise()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_ble_advertise_obj, akira_ble_advertise);

STATIC mp_obj_t akira_ble_stop_advertise(void) { return mp_obj_new_int(ble_stop_advertise()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_ble_stop_advertise_obj, akira_ble_stop_advertise);

STATIC mp_obj_t akira_ble_is_connected(void) { return mp_obj_new_int(ble_is_connected()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_ble_is_connected_obj, akira_ble_is_connected);

STATIC mp_obj_t akira_ble_char_write(mp_obj_t char_h, mp_obj_t data) {
    const uint8_t *ptr; size_t len;
    get_buffer(data, &ptr, &len);
    return mp_obj_new_int(ble_char_write(mp_obj_get_int(char_h), ptr, (uint32_t)len));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_ble_char_write_obj, akira_ble_char_write);

STATIC mp_obj_t akira_ble_char_read(mp_obj_t char_h, mp_obj_t max_len) {
    int len = mp_obj_get_int(max_len);
    uint8_t *buf = m_new(uint8_t, len);
    int n = ble_char_read(mp_obj_get_int(char_h), buf, (uint32_t)len);
    mp_obj_t result = (n > 0) ? mp_obj_new_bytes(buf, n) : mp_obj_new_bytes(NULL, 0);
    m_del(uint8_t, buf, len);
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_ble_char_read_obj, akira_ble_char_read);

STATIC mp_obj_t akira_ble_event_pop(mp_obj_t buf_len_obj) {
    int buf_len = mp_obj_get_int(buf_len_obj);
    uint8_t *buf = m_new(uint8_t, buf_len);
    int evt = ble_event_pop(buf, (uint32_t)buf_len);
    mp_obj_t t[3];
    t[0] = mp_obj_new_int(evt > 0 ? buf[0] : 0);
    t[1] = mp_obj_new_int(evt > 0 && buf_len > 1 ? buf[1] : 0);
    t[2] = (evt > 0 && buf_len > 4)
           ? mp_obj_new_bytes(buf + 4, buf_len - 4)
           : mp_obj_new_bytes(NULL, 0);
    m_del(uint8_t, buf, buf_len);
    return mp_obj_new_tuple(3, t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_ble_event_pop_obj, akira_ble_event_pop);

/* ── IPC ──────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_msg_subscribe(mp_obj_t topic) {
    return mp_obj_new_int(msg_subscribe(mp_obj_str_get_str(topic)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_msg_subscribe_obj, akira_msg_subscribe);

STATIC mp_obj_t akira_msg_unsubscribe(mp_obj_t topic) {
    return mp_obj_new_int(msg_unsubscribe(mp_obj_str_get_str(topic)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_msg_unsubscribe_obj, akira_msg_unsubscribe);

STATIC mp_obj_t akira_msg_publish(mp_obj_t topic, mp_obj_t data) {
    const uint8_t *ptr; size_t len;
    get_buffer(data, &ptr, &len);
    return mp_obj_new_int(msg_publish(mp_obj_str_get_str(topic), ptr, (uint32_t)len));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_msg_publish_obj, akira_msg_publish);

STATIC mp_obj_t akira_msg_recv(size_t n, const mp_obj_t *a) {
    int max_len = mp_obj_get_int(a[1]);
    int32_t timeout_ms = (n >= 3) ? mp_obj_get_int(a[2]) : 0;
    uint8_t *buf = m_new(uint8_t, max_len);
    int got = msg_recv(mp_obj_str_get_str(a[0]), buf, (uint32_t)max_len, timeout_ms);
    mp_obj_t result = (got > 0) ? mp_obj_new_bytes(buf, got) : mp_obj_new_bytes(NULL, 0);
    m_del(uint8_t, buf, max_len);
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_msg_recv_obj, 2, 3, akira_msg_recv);

STATIC mp_obj_t akira_msg_try_recv(mp_obj_t topic, mp_obj_t max_len_obj) {
    int max_len = mp_obj_get_int(max_len_obj);
    uint8_t *buf = m_new(uint8_t, max_len);
    int got = msg_try_recv(mp_obj_str_get_str(topic), buf, (uint32_t)max_len);
    mp_obj_t result = (got > 0) ? mp_obj_new_bytes(buf, got) : mp_obj_new_bytes(NULL, 0);
    m_del(uint8_t, buf, max_len);
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_msg_try_recv_obj, akira_msg_try_recv);

STATIC mp_obj_t akira_msg_pending(mp_obj_t topic) {
    return mp_obj_new_int(msg_pending(mp_obj_str_get_str(topic)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_msg_pending_obj, akira_msg_pending);

/* ── App lifecycle ────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_app_get_status(mp_obj_t name) {
    return mp_obj_new_int(app_get_status(mp_obj_str_get_str(name)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_app_get_status_obj, akira_app_get_status);

STATIC mp_obj_t akira_app_list(void) {
    int n = app_list((uint8_t *)scratch, SCRATCH_SIZE);
    if (n < 0) return mp_obj_new_int(n);
    return mp_obj_new_str(scratch, strlen(scratch));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_app_list_obj, akira_app_list);

STATIC mp_obj_t akira_app_get_self_name(void) {
    int n = app_get_self_name((uint8_t *)scratch, SCRATCH_SIZE);
    if (n < 0) return mp_obj_new_int(n);
    return mp_obj_new_str(scratch, n);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_app_get_self_name_obj, akira_app_get_self_name);

STATIC mp_obj_t akira_app_start(mp_obj_t name) {
    return mp_obj_new_int(app_start(mp_obj_str_get_str(name)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_app_start_obj, akira_app_start);

STATIC mp_obj_t akira_app_stop(mp_obj_t name) {
    return mp_obj_new_int(app_stop(mp_obj_str_get_str(name)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_app_stop_obj, akira_app_stop);

STATIC mp_obj_t akira_app_switch(mp_obj_t name) {
    return mp_obj_new_int(app_switch(mp_obj_str_get_str(name)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_app_switch_obj, akira_app_switch);

/* ── Power ────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_power_get_mode(void) { return mp_obj_new_int(power_get_mode()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_power_get_mode_obj, akira_power_get_mode);

STATIC mp_obj_t akira_power_get_battery_level(void) { return mp_obj_new_int(power_get_battery_level()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_power_get_battery_level_obj, akira_power_get_battery_level);

STATIC mp_obj_t akira_power_get_battery_status(void) {
    uint8_t buf[12] = {0};
    power_get_battery_status(buf, 12);
    int32_t mv = (int32_t)(buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24));
    int32_t ma = (int32_t)(buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24));
    mp_obj_t items[4];
    items[0] = mp_obj_new_int(buf[0]);      /* level_percent */
    items[1] = mp_obj_new_bool(buf[1] & 1); /* charging */
    items[2] = mp_obj_new_int(mv);          /* voltage_mv */
    items[3] = mp_obj_new_int(ma);          /* current_ma */
    return mp_obj_new_tuple(4, items);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_power_get_battery_status_obj, akira_power_get_battery_status);

STATIC mp_obj_t akira_power_set_mode(mp_obj_t mode) {
    return mp_obj_new_int(power_set_mode(mp_obj_get_int(mode)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_power_set_mode_obj, akira_power_set_mode);

STATIC mp_obj_t akira_power_wake_on_gpio(mp_obj_t pin, mp_obj_t edge) {
    return mp_obj_new_int(power_wake_on_gpio(mp_obj_get_int(pin), mp_obj_get_int(edge)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_power_wake_on_gpio_obj, akira_power_wake_on_gpio);

STATIC mp_obj_t akira_power_wake_on_timer(mp_obj_t ms) {
    return mp_obj_new_int(power_wake_on_timer(mp_obj_get_int(ms)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_power_wake_on_timer_obj, akira_power_wake_on_timer);

STATIC mp_obj_t akira_power_set_low_power(mp_obj_t enable) {
    return mp_obj_new_int(power_set_low_power(mp_obj_get_int(enable)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_power_set_low_power_obj, akira_power_set_low_power);

STATIC mp_obj_t akira_wdt_pet(void) { return mp_obj_new_int(wdt_pet()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_wdt_pet_obj, akira_wdt_pet);

/* ── HID ──────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_hid_init(mp_obj_t transport, mp_obj_t device_types) {
    return mp_obj_new_int(hid_init(mp_obj_get_int(transport), mp_obj_get_int(device_types)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_hid_init_obj, akira_hid_init);

STATIC mp_obj_t akira_hid_enable(void) { return mp_obj_new_int(hid_enable()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_hid_enable_obj, akira_hid_enable);

STATIC mp_obj_t akira_hid_disable(void) { return mp_obj_new_int(hid_disable()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_hid_disable_obj, akira_hid_disable);

STATIC mp_obj_t akira_hid_is_connected(void) { return mp_obj_new_int(hid_is_connected()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_hid_is_connected_obj, akira_hid_is_connected);

STATIC mp_obj_t akira_hid_key_press(mp_obj_t keycode) {
    return mp_obj_new_int(hid_key_press(mp_obj_get_int(keycode)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_key_press_obj, akira_hid_key_press);

STATIC mp_obj_t akira_hid_key_release(mp_obj_t keycode) {
    return mp_obj_new_int(hid_key_release(mp_obj_get_int(keycode)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_key_release_obj, akira_hid_key_release);

STATIC mp_obj_t akira_hid_key_release_all(void) { return mp_obj_new_int(hid_key_release_all()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_hid_key_release_all_obj, akira_hid_key_release_all);

STATIC mp_obj_t akira_hid_type_string(mp_obj_t s) {
    return mp_obj_new_int(hid_type_string(mp_obj_str_get_str(s)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_type_string_obj, akira_hid_type_string);

STATIC mp_obj_t akira_hid_gamepad_press(mp_obj_t mask) {
    return mp_obj_new_int(hid_gamepad_press(mp_obj_get_int(mask)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_gamepad_press_obj, akira_hid_gamepad_press);

STATIC mp_obj_t akira_hid_gamepad_release(mp_obj_t mask) {
    return mp_obj_new_int(hid_gamepad_release(mp_obj_get_int(mask)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_gamepad_release_obj, akira_hid_gamepad_release);

STATIC mp_obj_t akira_hid_gamepad_set_axis(mp_obj_t axis, mp_obj_t value) {
    return mp_obj_new_int(hid_gamepad_set_axis(mp_obj_get_int(axis), mp_obj_get_int(value)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_hid_gamepad_set_axis_obj, akira_hid_gamepad_set_axis);

STATIC mp_obj_t akira_hid_gamepad_set_dpad(mp_obj_t dir) {
    return mp_obj_new_int(hid_gamepad_set_dpad(mp_obj_get_int(dir)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_gamepad_set_dpad_obj, akira_hid_gamepad_set_dpad);

STATIC mp_obj_t akira_hid_gamepad_reset(void) { return mp_obj_new_int(hid_gamepad_reset()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_hid_gamepad_reset_obj, akira_hid_gamepad_reset);

STATIC mp_obj_t akira_hid_mouse_move(mp_obj_t dx, mp_obj_t dy) {
    return mp_obj_new_int(hid_mouse_move(mp_obj_get_int(dx), mp_obj_get_int(dy)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_hid_mouse_move_obj, akira_hid_mouse_move);

STATIC mp_obj_t akira_hid_mouse_btn_press(mp_obj_t btn) {
    return mp_obj_new_int(hid_mouse_btn_press(mp_obj_get_int(btn)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_mouse_btn_press_obj, akira_hid_mouse_btn_press);

STATIC mp_obj_t akira_hid_mouse_btn_release(mp_obj_t btn) {
    return mp_obj_new_int(hid_mouse_btn_release(mp_obj_get_int(btn)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_mouse_btn_release_obj, akira_hid_mouse_btn_release);

STATIC mp_obj_t akira_hid_mouse_scroll(mp_obj_t delta) {
    return mp_obj_new_int(hid_mouse_scroll(mp_obj_get_int(delta)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_mouse_scroll_obj, akira_hid_mouse_scroll);

STATIC mp_obj_t akira_hid_consumer_send(mp_obj_t code) {
    return mp_obj_new_int(hid_consumer_send(mp_obj_get_int(code)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_consumer_send_obj, akira_hid_consumer_send);

STATIC mp_obj_t akira_hid_set_transport(mp_obj_t t) {
    return mp_obj_new_int(hid_set_transport(mp_obj_get_int(t)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_set_transport_obj, akira_hid_set_transport);

STATIC mp_obj_t akira_hid_set_device_types(mp_obj_t t) {
    return mp_obj_new_int(hid_set_device_types(mp_obj_get_int(t)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_hid_set_device_types_obj, akira_hid_set_device_types);

/* ── UART ─────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_uart_open(mp_obj_t port, mp_obj_t baud) {
    return mp_obj_new_int(uart_open(mp_obj_get_int(port), mp_obj_get_int(baud)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_uart_open_obj, akira_uart_open);

STATIC mp_obj_t akira_uart_write(mp_obj_t handle, mp_obj_t data) {
    const uint8_t *ptr; size_t len;
    get_buffer(data, &ptr, &len);
    return mp_obj_new_int(uart_write(mp_obj_get_int(handle), ptr, (uint32_t)len));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_uart_write_obj, akira_uart_write);

STATIC mp_obj_t akira_uart_read(mp_obj_t handle, mp_obj_t max_len_obj) {
    int max_len = mp_obj_get_int(max_len_obj);
    uint8_t *buf = m_new(uint8_t, max_len);
    int n = uart_read(mp_obj_get_int(handle), buf, (uint32_t)max_len);
    mp_obj_t result = (n > 0) ? mp_obj_new_bytes(buf, n) : mp_obj_new_bytes(NULL, 0);
    m_del(uint8_t, buf, max_len);
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_uart_read_obj, akira_uart_read);

STATIC mp_obj_t akira_uart_close(mp_obj_t handle) {
    return mp_obj_new_int(uart_close(mp_obj_get_int(handle)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_uart_close_obj, akira_uart_close);

/* ── I2C ──────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_i2c_write_reg(size_t n, const mp_obj_t *a) {
    const uint8_t *ptr; size_t len;
    get_buffer(a[3], &ptr, &len);
    return mp_obj_new_int(i2c_write_reg(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), ptr, (uint32_t)len));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_i2c_write_reg_obj, 4, 4, akira_i2c_write_reg);

STATIC mp_obj_t akira_i2c_read_reg(size_t n, const mp_obj_t *a) {
    int len = mp_obj_get_int(a[3]);
    uint8_t *buf = m_new(uint8_t, len);
    int got = i2c_read_reg(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]),
        mp_obj_get_int(a[2]), buf, (uint32_t)len);
    mp_obj_t result = (got > 0) ? mp_obj_new_bytes(buf, got) : mp_obj_new_bytes(NULL, 0);
    m_del(uint8_t, buf, len);
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(akira_i2c_read_reg_obj, 4, 4, akira_i2c_read_reg);

/* ── PWM ──────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_pwm_set(mp_obj_t ch, mp_obj_t freq, mp_obj_t duty) {
    return mp_obj_new_int(pwm_set(mp_obj_get_int(ch), mp_obj_get_int(freq), mp_obj_get_int(duty)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(akira_pwm_set_obj, akira_pwm_set);

STATIC mp_obj_t akira_pwm_disable(mp_obj_t ch) {
    return mp_obj_new_int(pwm_disable(mp_obj_get_int(ch)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_pwm_disable_obj, akira_pwm_disable);

/* ── ADC ──────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_adc_read(mp_obj_t ch) {
    return mp_obj_new_int(adc_read(mp_obj_get_int(ch)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_adc_read_obj, akira_adc_read);

STATIC mp_obj_t akira_adc_read_mv(mp_obj_t ch) {
    return mp_obj_new_int(adc_read_mv(mp_obj_get_int(ch)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_adc_read_mv_obj, akira_adc_read_mv);

/* ── RTC ──────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_rtc_get_unix_time(void) { return mp_obj_new_int(rtc_get_unix_time()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_rtc_get_unix_time_obj, akira_rtc_get_unix_time);

STATIC mp_obj_t akira_rtc_get_uptime_ms(void) { return mp_obj_new_int(rtc_get_uptime_ms()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_rtc_get_uptime_ms_obj, akira_rtc_get_uptime_ms);

STATIC mp_obj_t akira_rtc_set_unix_time(mp_obj_t t) {
    return mp_obj_new_int(rtc_set_unix_time(mp_obj_get_int(t)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_rtc_set_unix_time_obj, akira_rtc_set_unix_time);

STATIC mp_obj_t akira_rtc_set_alarm(mp_obj_t t) {
    return mp_obj_new_int(rtc_set_alarm(mp_obj_get_int(t)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_rtc_set_alarm_obj, akira_rtc_set_alarm);

STATIC mp_obj_t akira_rtc_alarm_fired(void) { return mp_obj_new_int(rtc_alarm_fired()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_rtc_alarm_fired_obj, akira_rtc_alarm_fired);

/* ── Settings ─────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_settings_get(mp_obj_t key) {
    int n = settings_get(mp_obj_str_get_str(key), scratch, SCRATCH_SIZE);
    if (n < 0) return mp_obj_new_int(n);
    return mp_obj_new_str(scratch, strlen(scratch));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_settings_get_obj, akira_settings_get);

STATIC mp_obj_t akira_settings_set(mp_obj_t key, mp_obj_t value) {
    return mp_obj_new_int(settings_set(mp_obj_str_get_str(key), mp_obj_str_get_str(value)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_settings_set_obj, akira_settings_set);

STATIC mp_obj_t akira_settings_delete(mp_obj_t key) {
    return mp_obj_new_int(settings_delete(mp_obj_str_get_str(key)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_settings_delete_obj, akira_settings_delete);

/* ── FS ───────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_fs_open(mp_obj_t path, mp_obj_t flags) {
    return mp_obj_new_int(fs_open(mp_obj_str_get_str(path), mp_obj_get_int(flags)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_fs_open_obj, akira_fs_open);

STATIC mp_obj_t akira_fs_close(mp_obj_t fd) {
    return mp_obj_new_int(fs_close(mp_obj_get_int(fd)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_fs_close_obj, akira_fs_close);

STATIC mp_obj_t akira_fs_read(mp_obj_t fd, mp_obj_t length) {
    int len = mp_obj_get_int(length);
    uint8_t *buf = m_new(uint8_t, len);
    int n = fs_read(mp_obj_get_int(fd), buf, len);
    mp_obj_t result = (n > 0) ? mp_obj_new_bytes(buf, n) : mp_obj_new_bytes(NULL, 0);
    m_del(uint8_t, buf, len);
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_fs_read_obj, akira_fs_read);

STATIC mp_obj_t akira_fs_write(mp_obj_t fd, mp_obj_t data) {
    const uint8_t *ptr; size_t len;
    get_buffer(data, &ptr, &len);
    return mp_obj_new_int(fs_write(mp_obj_get_int(fd), ptr, (int)len));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_fs_write_obj, akira_fs_write);

STATIC mp_obj_t akira_fs_seek(mp_obj_t fd, mp_obj_t offset, mp_obj_t whence) {
    return mp_obj_new_int(fs_seek(mp_obj_get_int(fd), mp_obj_get_int(offset), mp_obj_get_int(whence)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(akira_fs_seek_obj, akira_fs_seek);

STATIC mp_obj_t akira_fs_tell(mp_obj_t fd) {
    return mp_obj_new_int(fs_tell(mp_obj_get_int(fd)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_fs_tell_obj, akira_fs_tell);

STATIC mp_obj_t akira_fs_unlink(mp_obj_t path) {
    return mp_obj_new_int(fs_unlink(mp_obj_str_get_str(path)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_fs_unlink_obj, akira_fs_unlink);

STATIC mp_obj_t akira_fs_mkdir(mp_obj_t path) {
    return mp_obj_new_int(fs_mkdir(mp_obj_str_get_str(path)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_fs_mkdir_obj, akira_fs_mkdir);

STATIC mp_obj_t akira_fs_readdir(mp_obj_t path) {
    int n = fs_readdir(mp_obj_str_get_str(path), scratch, SCRATCH_SIZE);
    if (n < 0) return mp_obj_new_int(n);
    return mp_obj_new_str(scratch, strlen(scratch));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_fs_readdir_obj, akira_fs_readdir);

/* ── Crypto ───────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_crypto_sha256(mp_obj_t data) {
    const uint8_t *ptr; size_t len;
    get_buffer(data, &ptr, &len);
    uint8_t out[32];
    int r = crypto_sha256(ptr, (int)len, out);
    if (r < 0) return mp_obj_new_int(r);
    return mp_obj_new_bytes(out, 32);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_crypto_sha256_obj, akira_crypto_sha256);

STATIC mp_obj_t akira_crypto_random(mp_obj_t length) {
    int len = mp_obj_get_int(length);
    uint8_t *buf = m_new(uint8_t, len);
    int r = crypto_random(buf, len);
    mp_obj_t result = (r == 0) ? mp_obj_new_bytes(buf, len) : mp_obj_new_int(r);
    m_del(uint8_t, buf, len);
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_crypto_random_obj, akira_crypto_random);

/* ── Network ──────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_net_open(mp_obj_t type) {
    return mp_obj_new_int(net_open(mp_obj_get_int(type)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_net_open_obj, akira_net_open);

STATIC mp_obj_t akira_net_connect(mp_obj_t handle, mp_obj_t host, mp_obj_t port) {
    return mp_obj_new_int(net_connect(mp_obj_get_int(handle),
        mp_obj_str_get_str(host), mp_obj_get_int(port)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(akira_net_connect_obj, akira_net_connect);

STATIC mp_obj_t akira_net_bind(mp_obj_t handle, mp_obj_t port) {
    return mp_obj_new_int(net_bind(mp_obj_get_int(handle), mp_obj_get_int(port)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_net_bind_obj, akira_net_bind);

STATIC mp_obj_t akira_net_listen(mp_obj_t handle, mp_obj_t backlog) {
    return mp_obj_new_int(net_listen(mp_obj_get_int(handle), mp_obj_get_int(backlog)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(akira_net_listen_obj, akira_net_listen);

STATIC mp_obj_t akira_net_close(mp_obj_t handle) {
    return mp_obj_new_int(net_close(mp_obj_get_int(handle)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_net_close_obj, akira_net_close);

STATIC mp_obj_t akira_net_tx_flush(mp_obj_t handle) {
    return mp_obj_new_int(net_tx_flush(mp_obj_get_int(handle)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_net_tx_flush_obj, akira_net_tx_flush);

STATIC mp_obj_t akira_net_event_pop(void) {
    uint8_t buf[4] = {0};
    int evt = net_event_pop(buf, 4);
    mp_obj_t t[4] = {
        mp_obj_new_int(buf[0]),
        mp_obj_new_int(buf[1]),
        mp_obj_new_int(buf[2] | (buf[3] << 8)),
        mp_obj_new_int(evt),
    };
    return mp_obj_new_tuple(4, t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_net_event_pop_obj, akira_net_event_pop);

STATIC mp_obj_t akira_net_get_ip(void) {
    char buf[16] = {0};
    int r = net_get_ip(buf, 16);
    if (r < 0) return mp_obj_new_int(r);
    return mp_obj_new_str(buf, strlen(buf));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_net_get_ip_obj, akira_net_get_ip);

/* ── RF ───────────────────────────────────────────────────────────────── */

STATIC mp_obj_t akira_rf_set_frequency(mp_obj_t freq) {
    return mp_obj_new_int(rf_set_frequency((uint32_t)mp_obj_get_int(freq)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_rf_set_frequency_obj, akira_rf_set_frequency);

STATIC mp_obj_t akira_rf_set_power(mp_obj_t dbm) {
    return mp_obj_new_int(rf_set_power((int8_t)mp_obj_get_int(dbm)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_rf_set_power_obj, akira_rf_set_power);

STATIC mp_obj_t akira_rf_get_rssi(void) { return mp_obj_new_int(rf_get_rssi()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_rf_get_rssi_obj, akira_rf_get_rssi);

/* ── SD / App install ─────────────────────────────────────────────────── */

STATIC mp_obj_t akira_sd_scan_wasm(void) {
    int n = sd_scan_wasm(scratch, SCRATCH_SIZE);
    if (n < 0) return mp_obj_new_int(n);
    return mp_obj_new_str(scratch, strlen(scratch));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(akira_sd_scan_wasm_obj, akira_sd_scan_wasm);

STATIC mp_obj_t akira_app_install_from_sd(mp_obj_t name) {
    return mp_obj_new_int(app_install_from_sd(mp_obj_str_get_str(name)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(akira_app_install_from_sd_obj, akira_app_install_from_sd);

/* ── Module table ─────────────────────────────────────────────────────── */

STATIC const mp_rom_map_elem_t akira_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__akira) },

    /* Console */
    { MP_ROM_QSTR(MP_QSTR_print),            MP_ROM_PTR(&akira_printf_native_obj) },
    { MP_ROM_QSTR(MP_QSTR_printf_native),    MP_ROM_PTR(&akira_printf_native_obj) },
    { MP_ROM_QSTR(MP_QSTR_delay),            MP_ROM_PTR(&akira_delay_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep),            MP_ROM_PTR(&akira_delay_obj) },

    /* Display */
    { MP_ROM_QSTR(MP_QSTR_display_clear),            MP_ROM_PTR(&akira_display_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_pixel),            MP_ROM_PTR(&akira_display_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_rect),             MP_ROM_PTR(&akira_display_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_rect_outline),     MP_ROM_PTR(&akira_display_rect_outline_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_text),             MP_ROM_PTR(&akira_display_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_text_large),       MP_ROM_PTR(&akira_display_text_large_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_number),           MP_ROM_PTR(&akira_display_number_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_flush),            MP_ROM_PTR(&akira_display_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_get_size),         MP_ROM_PTR(&akira_display_get_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_line),             MP_ROM_PTR(&akira_display_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_hline),            MP_ROM_PTR(&akira_display_hline_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_vline),            MP_ROM_PTR(&akira_display_vline_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_circle),           MP_ROM_PTR(&akira_display_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_circle_fill),      MP_ROM_PTR(&akira_display_circle_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_triangle),         MP_ROM_PTR(&akira_display_triangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_triangle_fill),    MP_ROM_PTR(&akira_display_triangle_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_rounded_rect),     MP_ROM_PTR(&akira_display_rounded_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_rounded_rect_fill),MP_ROM_PTR(&akira_display_rounded_rect_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_progress_bar),     MP_ROM_PTR(&akira_display_progress_bar_obj) },

    /* GPIO */
    { MP_ROM_QSTR(MP_QSTR_gpio_configure), MP_ROM_PTR(&akira_gpio_configure_obj) },
    { MP_ROM_QSTR(MP_QSTR_gpio_read),      MP_ROM_PTR(&akira_gpio_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_gpio_write),     MP_ROM_PTR(&akira_gpio_write_obj) },

    /* Sensor */
    { MP_ROM_QSTR(MP_QSTR_sensor_read), MP_ROM_PTR(&akira_sensor_read_obj) },

    /* Timer */
    { MP_ROM_QSTR(MP_QSTR_timer_create),  MP_ROM_PTR(&akira_timer_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_timer_start),   MP_ROM_PTR(&akira_timer_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_timer_stop),    MP_ROM_PTR(&akira_timer_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_timer_elapsed), MP_ROM_PTR(&akira_timer_elapsed_obj) },
    { MP_ROM_QSTR(MP_QSTR_timer_free),    MP_ROM_PTR(&akira_timer_free_obj) },

    /* Storage */
    { MP_ROM_QSTR(MP_QSTR_storage_open),   MP_ROM_PTR(&akira_storage_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_read),   MP_ROM_PTR(&akira_storage_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_write),  MP_ROM_PTR(&akira_storage_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_close),  MP_ROM_PTR(&akira_storage_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_delete), MP_ROM_PTR(&akira_storage_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_list),   MP_ROM_PTR(&akira_storage_list_obj) },

    /* BLE */
    { MP_ROM_QSTR(MP_QSTR_ble_init),                  MP_ROM_PTR(&akira_ble_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_deinit),                MP_ROM_PTR(&akira_ble_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_set_local_name),        MP_ROM_PTR(&akira_ble_set_local_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_service_create),        MP_ROM_PTR(&akira_ble_service_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_char_create),           MP_ROM_PTR(&akira_ble_char_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_service_add_char),      MP_ROM_PTR(&akira_ble_service_add_char_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_add_service),           MP_ROM_PTR(&akira_ble_add_service_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_set_advertised_service),MP_ROM_PTR(&akira_ble_set_advertised_service_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_advertise),             MP_ROM_PTR(&akira_ble_advertise_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_stop_advertise),        MP_ROM_PTR(&akira_ble_stop_advertise_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_is_connected),          MP_ROM_PTR(&akira_ble_is_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_char_write),            MP_ROM_PTR(&akira_ble_char_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_char_read),             MP_ROM_PTR(&akira_ble_char_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_event_pop),             MP_ROM_PTR(&akira_ble_event_pop_obj) },

    /* IPC */
    { MP_ROM_QSTR(MP_QSTR_msg_subscribe),   MP_ROM_PTR(&akira_msg_subscribe_obj) },
    { MP_ROM_QSTR(MP_QSTR_msg_unsubscribe), MP_ROM_PTR(&akira_msg_unsubscribe_obj) },
    { MP_ROM_QSTR(MP_QSTR_msg_publish),     MP_ROM_PTR(&akira_msg_publish_obj) },
    { MP_ROM_QSTR(MP_QSTR_msg_recv),        MP_ROM_PTR(&akira_msg_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_msg_try_recv),    MP_ROM_PTR(&akira_msg_try_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_msg_pending),     MP_ROM_PTR(&akira_msg_pending_obj) },

    /* App lifecycle */
    { MP_ROM_QSTR(MP_QSTR_app_get_status),    MP_ROM_PTR(&akira_app_get_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_app_list),          MP_ROM_PTR(&akira_app_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_app_get_self_name), MP_ROM_PTR(&akira_app_get_self_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_app_start),         MP_ROM_PTR(&akira_app_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_app_stop),          MP_ROM_PTR(&akira_app_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_app_switch),        MP_ROM_PTR(&akira_app_switch_obj) },

    /* Power */
    { MP_ROM_QSTR(MP_QSTR_power_get_mode),           MP_ROM_PTR(&akira_power_get_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_power_get_battery_level),  MP_ROM_PTR(&akira_power_get_battery_level_obj) },
    { MP_ROM_QSTR(MP_QSTR_power_get_battery_status), MP_ROM_PTR(&akira_power_get_battery_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_power_set_mode),           MP_ROM_PTR(&akira_power_set_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_power_wake_on_gpio),       MP_ROM_PTR(&akira_power_wake_on_gpio_obj) },
    { MP_ROM_QSTR(MP_QSTR_power_wake_on_timer),      MP_ROM_PTR(&akira_power_wake_on_timer_obj) },
    { MP_ROM_QSTR(MP_QSTR_power_set_low_power),      MP_ROM_PTR(&akira_power_set_low_power_obj) },
    { MP_ROM_QSTR(MP_QSTR_wdt_pet),                  MP_ROM_PTR(&akira_wdt_pet_obj) },

    /* HID */
    { MP_ROM_QSTR(MP_QSTR_hid_init),              MP_ROM_PTR(&akira_hid_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_enable),            MP_ROM_PTR(&akira_hid_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_disable),           MP_ROM_PTR(&akira_hid_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_is_connected),      MP_ROM_PTR(&akira_hid_is_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_key_press),         MP_ROM_PTR(&akira_hid_key_press_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_key_release),       MP_ROM_PTR(&akira_hid_key_release_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_key_release_all),   MP_ROM_PTR(&akira_hid_key_release_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_type_string),       MP_ROM_PTR(&akira_hid_type_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_gamepad_press),     MP_ROM_PTR(&akira_hid_gamepad_press_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_gamepad_release),   MP_ROM_PTR(&akira_hid_gamepad_release_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_gamepad_set_axis),  MP_ROM_PTR(&akira_hid_gamepad_set_axis_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_gamepad_set_dpad),  MP_ROM_PTR(&akira_hid_gamepad_set_dpad_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_gamepad_reset),     MP_ROM_PTR(&akira_hid_gamepad_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_mouse_move),        MP_ROM_PTR(&akira_hid_mouse_move_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_mouse_btn_press),   MP_ROM_PTR(&akira_hid_mouse_btn_press_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_mouse_btn_release), MP_ROM_PTR(&akira_hid_mouse_btn_release_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_mouse_scroll),      MP_ROM_PTR(&akira_hid_mouse_scroll_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_consumer_send),     MP_ROM_PTR(&akira_hid_consumer_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_set_transport),     MP_ROM_PTR(&akira_hid_set_transport_obj) },
    { MP_ROM_QSTR(MP_QSTR_hid_set_device_types),  MP_ROM_PTR(&akira_hid_set_device_types_obj) },

    /* UART */
    { MP_ROM_QSTR(MP_QSTR_uart_open),  MP_ROM_PTR(&akira_uart_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_write), MP_ROM_PTR(&akira_uart_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_read),  MP_ROM_PTR(&akira_uart_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_close), MP_ROM_PTR(&akira_uart_close_obj) },

    /* I2C */
    { MP_ROM_QSTR(MP_QSTR_i2c_write_reg), MP_ROM_PTR(&akira_i2c_write_reg_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_read_reg),  MP_ROM_PTR(&akira_i2c_read_reg_obj) },

    /* PWM */
    { MP_ROM_QSTR(MP_QSTR_pwm_set),     MP_ROM_PTR(&akira_pwm_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_pwm_disable), MP_ROM_PTR(&akira_pwm_disable_obj) },

    /* ADC */
    { MP_ROM_QSTR(MP_QSTR_adc_read),    MP_ROM_PTR(&akira_adc_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_adc_read_mv), MP_ROM_PTR(&akira_adc_read_mv_obj) },

    /* RTC */
    { MP_ROM_QSTR(MP_QSTR_rtc_get_unix_time), MP_ROM_PTR(&akira_rtc_get_unix_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_rtc_get_uptime_ms), MP_ROM_PTR(&akira_rtc_get_uptime_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_rtc_set_unix_time), MP_ROM_PTR(&akira_rtc_set_unix_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_rtc_set_alarm),     MP_ROM_PTR(&akira_rtc_set_alarm_obj) },
    { MP_ROM_QSTR(MP_QSTR_rtc_alarm_fired),   MP_ROM_PTR(&akira_rtc_alarm_fired_obj) },

    /* Settings */
    { MP_ROM_QSTR(MP_QSTR_settings_get),    MP_ROM_PTR(&akira_settings_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_settings_set),    MP_ROM_PTR(&akira_settings_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_settings_delete), MP_ROM_PTR(&akira_settings_delete_obj) },

    /* FS */
    { MP_ROM_QSTR(MP_QSTR_fs_open),    MP_ROM_PTR(&akira_fs_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_close),   MP_ROM_PTR(&akira_fs_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_read),    MP_ROM_PTR(&akira_fs_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_write),   MP_ROM_PTR(&akira_fs_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_seek),    MP_ROM_PTR(&akira_fs_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_tell),    MP_ROM_PTR(&akira_fs_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_unlink),  MP_ROM_PTR(&akira_fs_unlink_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_mkdir),   MP_ROM_PTR(&akira_fs_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_readdir), MP_ROM_PTR(&akira_fs_readdir_obj) },

    /* Crypto */
    { MP_ROM_QSTR(MP_QSTR_crypto_sha256),  MP_ROM_PTR(&akira_crypto_sha256_obj) },
    { MP_ROM_QSTR(MP_QSTR_crypto_random),  MP_ROM_PTR(&akira_crypto_random_obj) },

    /* Network */
    { MP_ROM_QSTR(MP_QSTR_net_open),      MP_ROM_PTR(&akira_net_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_connect),   MP_ROM_PTR(&akira_net_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_bind),      MP_ROM_PTR(&akira_net_bind_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_listen),    MP_ROM_PTR(&akira_net_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_close),     MP_ROM_PTR(&akira_net_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_tx_flush),  MP_ROM_PTR(&akira_net_tx_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_event_pop), MP_ROM_PTR(&akira_net_event_pop_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_get_ip),    MP_ROM_PTR(&akira_net_get_ip_obj) },

    /* RF */
    { MP_ROM_QSTR(MP_QSTR_rf_set_frequency), MP_ROM_PTR(&akira_rf_set_frequency_obj) },
    { MP_ROM_QSTR(MP_QSTR_rf_set_power),     MP_ROM_PTR(&akira_rf_set_power_obj) },
    { MP_ROM_QSTR(MP_QSTR_rf_get_rssi),      MP_ROM_PTR(&akira_rf_get_rssi_obj) },

    /* SD / App install */
    { MP_ROM_QSTR(MP_QSTR_sd_scan_wasm),        MP_ROM_PTR(&akira_sd_scan_wasm_obj) },
    { MP_ROM_QSTR(MP_QSTR_app_install_from_sd), MP_ROM_PTR(&akira_app_install_from_sd_obj) },
};

STATIC MP_DEFINE_CONST_DICT(akira_module_globals, akira_module_globals_table);

const mp_obj_module_t mp_module_akira = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&akira_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__akira, mp_module_akira);

/* ── WAMR entry point ─────────────────────────────────────────────────────
 * Called by the AkiraOS runtime after loading the WASM.
 * mp_js_init() sets up the MicroPython heap and GC.
 * mp_js_do_exec() compiles and runs a Python source string.
 *
 * The Python script is injected by py_to_wasm.py as a WASM data segment
 * at the reserved address AKIRA_SCRIPT_ADDR. Layout:
 *   [uint32_t length LE][utf-8 source bytes]
 * ──────────────────────────────────────────────────────────────────────── */

/* Forward declarations from micropython webassembly port */
extern void mp_js_init(int pystack_size, int heap_size);
extern void mp_js_do_exec(const char *src, size_t len, uint32_t *out);

/* Reserved memory address where py_to_wasm.py injects the script.
 * Must match AKIRA_SCRIPT_ADDR in py_to_wasm.py.
 * Placed at 192KB — safely after MicroPython's static data (~130KB). */
#define AKIRA_SCRIPT_ADDR  196608   /* 0x30000 = 192 KB */
#define AKIRA_PYSTACK_SIZE (32  * 1024)
#define AKIRA_HEAP_SIZE    (128 * 1024)

int main(void) {
    mp_js_init(AKIRA_PYSTACK_SIZE, AKIRA_HEAP_SIZE);

    /* Read script header: [uint32 length][source bytes] */
    volatile uint32_t *hdr = (volatile uint32_t *)AKIRA_SCRIPT_ADDR;
    uint32_t script_len = *hdr;
    if (script_len == 0 || script_len > 65536) {
        return 1;
    }

    const char *src = (const char *)(AKIRA_SCRIPT_ADDR + 4);
    uint32_t out[2] = {0, 0};
    mp_js_do_exec(src, script_len, out);
    return (int)out[0];
}
