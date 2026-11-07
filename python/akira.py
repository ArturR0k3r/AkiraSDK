"""
akira.py — AkiraOS Python API for MicroPython WASM apps.

Import this module in your Python app to call AkiraOS native functions.
This module is designed for MicroPython running under the WAMR runtime as
a WASM binary (micropython.wasm).

Native functions are exposed through the '_akira' C extension module that is
compiled into micropython.wasm. This file provides a clean Python API on top.

Copyright (c) 2025 AkiraOS Contributors
SPDX-License-Identifier: Apache-2.0
"""

# The _akira module is a native C extension compiled into micropython.wasm.
# It maps each symbol directly to the corresponding WASM import from "env".
import _akira  # noqa: F401 — native module, always available in micropython.wasm


# =============================================================================
# Color constants (RGB565)
# =============================================================================
COLOR_BLACK      = 0x0000
COLOR_WHITE      = 0xFFFF
COLOR_RED        = 0xF800
COLOR_GREEN      = 0x07E0
COLOR_BLUE       = 0x001F
COLOR_YELLOW     = 0xFFE0
COLOR_CYAN       = 0x07FF
COLOR_MAGENTA    = 0xF81F
COLOR_GRAY       = 0x7BEF
COLOR_DARK_GRAY  = 0x39E7
COLOR_LIGHT_GRAY = 0xC618
COLOR_ORANGE     = 0xFD20
COLOR_PURPLE     = 0x801F

# =============================================================================
# GPIO flags
# =============================================================================
GPIO_INPUT            = 1 << 0
GPIO_OUTPUT           = 1 << 1
GPIO_OUTPUT_INIT_LOW  = 1 << 2
GPIO_OUTPUT_INIT_HIGH = 1 << 3
GPIO_PULL_UP          = 1 << 4
GPIO_PULL_DOWN        = 1 << 5
GPIO_ACTIVE_LOW       = 1 << 6
GPIO_ACTIVE_HIGH      = 1 << 7

# =============================================================================
# Sensor channel IDs
# =============================================================================
SENSOR_CHAN_ACCEL_X      = 0
SENSOR_CHAN_ACCEL_Y      = 1
SENSOR_CHAN_ACCEL_Z      = 2
SENSOR_CHAN_GYRO_X       = 4
SENSOR_CHAN_GYRO_Y       = 5
SENSOR_CHAN_GYRO_Z       = 6
SENSOR_CHAN_MAGN_X       = 8
SENSOR_CHAN_MAGN_Y       = 9
SENSOR_CHAN_MAGN_Z       = 10
SENSOR_CHAN_AMBIENT_TEMP = 13
SENSOR_CHAN_PRESS        = 14
SENSOR_CHAN_HUMIDITY     = 16
SENSOR_CHAN_ALTITUDE     = 23
SENSOR_CHAN_VOLTAGE      = 33
SENSOR_CHAN_CURRENT      = 35
SENSOR_CHAN_POWER        = 36

# =============================================================================
# Log levels
# =============================================================================
LOG_LEVEL_ERR = 1
LOG_LEVEL_WRN = 2
LOG_LEVEL_INF = 3

# =============================================================================
# BLE constants
# =============================================================================
BLE_PROP_READ        = 0x02
BLE_PROP_WRITE_WO_RSP= 0x04
BLE_PROP_WRITE       = 0x08
BLE_PROP_NOTIFY      = 0x10
BLE_PROP_INDICATE    = 0x20
BLE_EVT_NONE         = 0
BLE_EVT_CONNECTED    = 1
BLE_EVT_DISCONNECTED = 2
BLE_EVT_CHAR_WRITTEN = 3

# =============================================================================
# HID constants
# =============================================================================
HID_TRANSPORT_NONE = 0
HID_TRANSPORT_BLE  = 1
HID_TRANSPORT_USB  = 2
HID_DEVICE_KEYBOARD= 0x01
HID_DEVICE_GAMEPAD = 0x02
HID_DEVICE_MOUSE   = 0x04
HID_DEVICE_COMBO   = 0x07

# =============================================================================
# Storage flags
# =============================================================================
STORAGE_O_READ   = 0
STORAGE_O_WRITE  = 1
STORAGE_O_APPEND = 2
STORAGE_O_RDWR   = 3

# =============================================================================
# Power modes
# =============================================================================
POWER_MODE_ACTIVE      = 0
POWER_MODE_IDLE        = 1
POWER_MODE_LIGHT_SLEEP = 2
POWER_MODE_DEEP_SLEEP  = 3
POWER_MODE_HIBERNATE   = 4


# =============================================================================
# Console API
# =============================================================================
def printf(message: str) -> int:
    """Print a message to the AkiraOS console."""
    return _akira.printf_native(message)


def print(message: str) -> int:
    """Alias for printf."""
    return _akira.printf_native(message)


def delay(microseconds: int) -> int:
    """Busy-wait for the given number of microseconds."""
    return _akira.delay(microseconds)


def delay_ms(ms: int) -> int:
    """Busy-wait for the given number of milliseconds."""
    return _akira.delay(ms * 1000)


# =============================================================================
# Display API
# =============================================================================
def display_clear(color: int) -> int:
    return _akira.display_clear(color)


def display_pixel(x: int, y: int, color: int) -> int:
    return _akira.display_pixel(x, y, color)


def display_rect(x: int, y: int, w: int, h: int, color: int) -> int:
    return _akira.display_rect(x, y, w, h, color)


def display_rect_outline(x: int, y: int, w: int, h: int, color: int) -> int:
    return _akira.display_rect_outline(x, y, w, h, color)


def display_text(x: int, y: int, text: str, color: int) -> int:
    return _akira.display_text(x, y, text, color)


def display_text_large(x: int, y: int, text: str, color: int) -> int:
    return _akira.display_text_large(x, y, text, color)


def display_number(x: int, y: int, value: int, color: int) -> int:
    return _akira.display_number(x, y, value, color)


def display_flush() -> int:
    return _akira.display_flush()


def display_get_size() -> tuple:
    """Return (width, height) of the display."""
    return _akira.display_get_size()


def display_line(x0: int, y0: int, x1: int, y1: int, color: int) -> int:
    return _akira.display_line(x0, y0, x1, y1, color)


def display_hline(x: int, y: int, length: int, color: int) -> int:
    return _akira.display_hline(x, y, length, color)


def display_vline(x: int, y: int, length: int, color: int) -> int:
    return _akira.display_vline(x, y, length, color)


def display_circle(cx: int, cy: int, r: int, color: int) -> int:
    return _akira.display_circle(cx, cy, r, color)


def display_circle_fill(cx: int, cy: int, r: int, color: int) -> int:
    return _akira.display_circle_fill(cx, cy, r, color)


def display_triangle(x0: int, y0: int, x1: int, y1: int, x2: int, y2: int, color: int) -> int:
    return _akira.display_triangle(x0, y0, x1, y1, x2, y2, color)


def display_triangle_fill(x0: int, y0: int, x1: int, y1: int, x2: int, y2: int, color: int) -> int:
    return _akira.display_triangle_fill(x0, y0, x1, y1, x2, y2, color)


def display_rounded_rect(x: int, y: int, w: int, h: int, radius: int, color: int) -> int:
    return _akira.display_rounded_rect(x, y, w, h, radius, color)


def display_rounded_rect_fill(x: int, y: int, w: int, h: int, radius: int, color: int) -> int:
    return _akira.display_rounded_rect_fill(x, y, w, h, radius, color)


def display_progress_bar(x: int, y: int, w: int, h: int, value: int, max_val: int, fg: int, bg: int) -> int:
    return _akira.display_progress_bar(x, y, w, h, value, max_val, fg, bg)


# =============================================================================
# GPIO API
# =============================================================================
def gpio_configure(pin: int, flags: int) -> int:
    return _akira.gpio_configure(pin, flags)


def gpio_read(pin: int) -> int:
    return _akira.gpio_read(pin)


def gpio_write(pin: int, value: int) -> int:
    return _akira.gpio_write(pin, value)


# =============================================================================
# Sensor API
# =============================================================================
def sensor_read(channel: int) -> int:
    """Read sensor channel. Returns value x1000; divide by 1000.0 for physical units."""
    return _akira.sensor_read(channel)


def sensor_read_float(channel: int):
    """Read sensor channel and return as float. Returns None on error."""
    raw = _akira.sensor_read(channel)
    if raw == -2147483648:  # INT32_MIN = AKIRA_SENSOR_ERROR
        return None
    return raw / 1000.0


# =============================================================================
# Timer API
# =============================================================================
def timer_create() -> int:
    return _akira.timer_create()


def timer_start(handle: int) -> int:
    return _akira.timer_start(handle)


def timer_stop(handle: int) -> int:
    return _akira.timer_stop(handle)


def timer_elapsed(handle: int) -> int:
    """Return elapsed milliseconds."""
    return _akira.timer_elapsed(handle)


def timer_free(handle: int) -> int:
    return _akira.timer_free(handle)


# =============================================================================
# Storage API
# =============================================================================
def storage_open(path: str, flags: int) -> int:
    return _akira.storage_open(path, flags)


def storage_read(fd: int, length: int) -> bytes:
    return _akira.storage_read(fd, length)


def storage_write(fd: int, data: bytes) -> int:
    return _akira.storage_write(fd, data)


def storage_close(fd: int) -> None:
    _akira.storage_close(fd)


def storage_delete(path: str) -> int:
    return _akira.storage_delete(path)


def storage_list(path: str) -> str:
    return _akira.storage_list(path)


# =============================================================================
# BLE API
# =============================================================================
def ble_init() -> int:
    return _akira.ble_init()


def ble_deinit() -> int:
    return _akira.ble_deinit()


def ble_set_local_name(name: str) -> int:
    return _akira.ble_set_local_name(name)


def ble_service_create(uuid128: str) -> int:
    return _akira.ble_service_create(uuid128)


def ble_char_create(uuid128: str, props: int, max_len: int) -> int:
    return _akira.ble_char_create(uuid128, props, max_len)


def ble_service_add_char(svc_h: int, char_h: int) -> int:
    return _akira.ble_service_add_char(svc_h, char_h)


def ble_add_service(svc_h: int) -> int:
    return _akira.ble_add_service(svc_h)


def ble_set_advertised_service(svc_h: int) -> int:
    return _akira.ble_set_advertised_service(svc_h)


def ble_advertise() -> int:
    return _akira.ble_advertise()


def ble_stop_advertise() -> int:
    return _akira.ble_stop_advertise()


def ble_is_connected() -> bool:
    return _akira.ble_is_connected() == 1


def ble_char_write(char_h: int, data: bytes) -> int:
    return _akira.ble_char_write(char_h, data)


def ble_char_read(char_h: int, max_len: int) -> bytes:
    return _akira.ble_char_read(char_h, max_len)


def ble_event_pop(buf_len: int = 68) -> tuple:
    """Return (event_type, char_handle, data_bytes) or (0, 0, b'') if empty."""
    return _akira.ble_event_pop(buf_len)


# =============================================================================
# IPC pub/sub API
# =============================================================================
def msg_subscribe(topic: str) -> int:
    return _akira.msg_subscribe(topic)


def msg_unsubscribe(topic: str) -> int:
    return _akira.msg_unsubscribe(topic)


def msg_publish(topic: str, data: bytes) -> int:
    return _akira.msg_publish(topic, data)


def msg_recv(topic: str, max_len: int, timeout_ms: int = 0) -> bytes:
    return _akira.msg_recv(topic, max_len, timeout_ms)


def msg_try_recv(topic: str, max_len: int) -> bytes:
    return _akira.msg_try_recv(topic, max_len)


def msg_pending(topic: str) -> int:
    return _akira.msg_pending(topic)


# =============================================================================
# App lifecycle API
# =============================================================================
def app_get_status(name: str) -> int:
    return _akira.app_get_status(name)


def app_list() -> str:
    return _akira.app_list()


def app_get_self_name() -> str:
    return _akira.app_get_self_name()


def app_start(name: str) -> int:
    return _akira.app_start(name)


def app_stop(name: str) -> int:
    return _akira.app_stop(name)


def app_switch(name: str) -> int:
    return _akira.app_switch(name)


# =============================================================================
# Power API
# =============================================================================
def power_get_mode() -> int:
    return _akira.power_get_mode()


def power_get_battery_level() -> int:
    return _akira.power_get_battery_level()


def power_get_battery_status() -> dict:
    """Return {'level': int, 'charging': bool, 'voltage_mv': int, 'current_ma': int}."""
    return _akira.power_get_battery_status()


def wdt_pet() -> int:
    return _akira.wdt_pet()


# =============================================================================
# HID API
# =============================================================================
def hid_init(transport: int, device_types: int) -> int:
    return _akira.hid_init(transport, device_types)


def hid_enable() -> int:
    return _akira.hid_enable()


def hid_disable() -> int:
    return _akira.hid_disable()


def hid_is_connected() -> bool:
    return _akira.hid_is_connected() == 1


def hid_key_press(keycode: int) -> int:
    return _akira.hid_key_press(keycode)


def hid_key_release(keycode: int) -> int:
    return _akira.hid_key_release(keycode)


def hid_key_release_all() -> int:
    return _akira.hid_key_release_all()


def hid_type_string(s: str) -> int:
    return _akira.hid_type_string(s)


def hid_mouse_move(dx: int, dy: int) -> int:
    return _akira.hid_mouse_move(dx, dy)


def hid_consumer_send(usage_code: int) -> int:
    return _akira.hid_consumer_send(usage_code)


# =============================================================================
# UART API
# =============================================================================
def uart_open(port_id: int, baud_rate: int) -> int:
    return _akira.uart_open(port_id, baud_rate)


def uart_write(handle: int, data: bytes) -> int:
    return _akira.uart_write(handle, data)


def uart_read(handle: int, max_len: int) -> bytes:
    return _akira.uart_read(handle, max_len)


def uart_close(handle: int) -> int:
    return _akira.uart_close(handle)


# =============================================================================
# I2C API
# =============================================================================
def i2c_write_reg(bus_id: int, dev_addr: int, reg_addr: int, data: bytes) -> int:
    return _akira.i2c_write_reg(bus_id, dev_addr, reg_addr, data)


def i2c_read_reg(bus_id: int, dev_addr: int, reg_addr: int, length: int) -> bytes:
    return _akira.i2c_read_reg(bus_id, dev_addr, reg_addr, length)


# =============================================================================
# PWM API
# =============================================================================
def pwm_set(channel: int, freq_hz: int, duty_pct: int) -> int:
    return _akira.pwm_set(channel, freq_hz, duty_pct)


def pwm_disable(channel: int) -> int:
    return _akira.pwm_disable(channel)


# =============================================================================
# ADC API
# =============================================================================
def adc_read(channel: int) -> int:
    return _akira.adc_read(channel)


def adc_read_mv(channel: int) -> int:
    return _akira.adc_read_mv(channel)
