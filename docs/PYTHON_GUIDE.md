# Python Apps Guide for AkiraOS

Write Python apps for AkiraOS using MicroPython compiled to WASM.

## Architecture

```
                    build time
Python script ──┐
                ▼
         py_to_wasm.py ──► hello_world.wasm
                           (micropython.wasm + script data segment at 0x30000)
                                               │
                    runtime (WAMR)             │
                                               ▼
                              MicroPython reads script from memory, executes it
                              Python calls _akira (native C module)
                              _akira calls env.* WASM imports
                              WAMR links to AkiraOS native functions (Zephyr)
```

| Component | Location | Purpose |
|-----------|----------|---------|
| `micropython.wasm` | `python/runtime/` | MicroPython + `_akira` module, built once |
| `_akira.c` | `python/native/` | C extension — wraps all AkiraOS APIs |
| `py_to_wasm.py` | `scripts/` | Injects Python script into `micropython.wasm` |

## Prerequisites

1. Build `micropython.wasm` once (requires Emscripten):

```bash
source ~/emsdk/emsdk_env.sh
bash python/runtime/build.sh
```

See [python/runtime/README.md](../python/runtime/README.md) for full build instructions.

2. Python 3.8+ (for running `py_to_wasm.py` on the host)

## Writing an App

```python
import _akira as akira

def main():
    akira.printf_native("Hello from Python!")

    akira.display_clear(0x0000)
    akira.display_text(10, 10, "Hello Python!", 0xFFFF)
    akira.display_flush()

    while True:
        akira.delay(100000)  # 100 ms in microseconds

main()
```

- Import the native module as `import _akira as akira`
- Apps that run continuously should loop with `while True` — apps are expected
  to keep running; returning from `main()` causes a WASM trap
- `akira.delay(us)` takes **microseconds**

## Building

```bash
python3 scripts/py_to_wasm.py python/apps/my_app/main.py \
    -o wasm_apps/bin/my_app.wasm
```

With explicit runtime:
```bash
MICROPYTHON_WASM=/path/to/micropython.wasm \
    python3 scripts/py_to_wasm.py main.py -o my_app.wasm
```

## Project Structure

```
python/apps/my_app/
├── main.py          # Application code
└── manifest.json    # AkiraOS app manifest
```

## manifest.json

```json
{
  "name": "my_app",
  "version": "1.0.0",
  "capabilities": [],
  "memory_quota": 262144,
  "min_akiraos_version": "1.0.0"
}
```

Memory quota should be at least 262144 (256 KB) for the MicroPython heap.

## API Reference

All functions are on the `_akira` module (imported as `akira` by convention).

### Console / Timing

```python
akira.printf_native("message")   # print to AkiraOS log
akira.delay(ms)                  # delay in milliseconds
akira.sleep(ms)                  # alias for delay
```

### Display

```python
akira.display_clear(color)
akira.display_text(x, y, "text", color)
akira.display_text_large(x, y, "big", color)
akira.display_number(x, y, 42, color)
akira.display_rect(x, y, w, h, color)
akira.display_rect_outline(x, y, w, h, color)
akira.display_rounded_rect(x, y, w, h, radius, color)
akira.display_circle(cx, cy, r, color)
akira.display_circle_fill(cx, cy, r, color)
akira.display_line(x0, y0, x1, y1, color)
akira.display_progress_bar(x, y, w, h, value, max_val, fg, bg)
akira.display_flush()
akira.display_get_size()         # returns (w, h) — not yet wrapped, use display_rect for bounds
```

Colors are 16-bit RGB565 integers (e.g. `0xFFFF` = white, `0x0000` = black).

### GPIO

```python
akira.gpio_configure(pin, flags)
akira.gpio_write(pin, value)
v = akira.gpio_read(pin)
```

### Sensor

```python
raw = akira.sensor_read(channel)
```

### Timer

```python
t = akira.timer_create()
akira.timer_start(t, ms)
elapsed = akira.timer_elapsed(t)
akira.timer_stop(t)
akira.timer_free(t)
```

### Storage (key-value)

```python
h = akira.storage_open("mystore")
akira.storage_write(h, buf, len)
akira.storage_read(h, buf, len)
akira.storage_close(h)
akira.storage_delete("mystore")
```

### Settings (NVS)

```python
akira.settings_set("key", "value")
akira.settings_get("key", buf, len)
akira.settings_delete("key")
```

### Filesystem

```python
fd = akira.fs_open("path", flags)
akira.fs_write(fd, buf, len)
akira.fs_read(fd, buf, len)
akira.fs_seek(fd, offset, whence)
akira.fs_tell(fd)
akira.fs_close(fd)
akira.fs_unlink("path")
akira.fs_mkdir("path")
akira.fs_readdir("path")
```

### IPC (messaging)

```python
akira.msg_subscribe("topic")
akira.msg_unsubscribe("topic")
akira.msg_publish("topic", data, len)
akira.msg_recv("topic", buf, len)
akira.msg_try_recv("topic", buf, len)
pending = akira.msg_pending()
```

### BLE

```python
akira.ble_init()
akira.ble_set_local_name("MyDevice")
akira.ble_advertise()
connected = akira.ble_is_connected()
akira.ble_deinit()
```

### Network

```python
sock = akira.net_open(type)
akira.net_connect(sock, "host", port)
akira.net_bind(sock, port)
akira.net_listen(sock, backlog)
akira.net_close(sock)
```

### Power

```python
level = akira.power_get_battery_level()
status = akira.power_get_battery_status()
mode = akira.power_get_mode()
akira.power_set_low_power(enable)
akira.wdt_pet()
```

### HID

```python
akira.hid_init()
akira.hid_key_press(key)
akira.hid_key_release(key)
akira.hid_key_release_all()
akira.hid_type_string("hello")
akira.hid_mouse_move(dx, dy)
akira.hid_consumer_send(code)
```

### UART

```python
akira.uart_open(port, baud)
akira.uart_write(port, data, len)
akira.uart_read(port, buf, len)
akira.uart_close(port)
```

### I2C

```python
akira.i2c_write_reg(addr, reg, data, len)
akira.i2c_read_reg(addr, reg, buf, len)
```

### PWM / ADC

```python
akira.pwm_set(pin, freq, duty)
akira.pwm_disable(pin)
raw = akira.adc_read(channel)
mv = akira.adc_read_mv(channel)
```

### RTC

```python
ms = akira.rtc_get_uptime_ms()
t = akira.rtc_get_unix_time()
akira.rtc_set_unix_time(t)
akira.rtc_set_alarm(t)
fired = akira.rtc_alarm_fired()
```

### Crypto

```python
akira.crypto_sha256(data, len, out)
akira.crypto_random(buf, len)
```

## Memory Constraints

WASM memory is fixed at 384 KB. MicroPython heap is ~128 KB of that.

- Avoid large buffers/lists
- Prefer generators over list comprehensions for large data
- Use `akira.storage_*` or `akira.fs_*` for persistent data

## Limitations

- Python exceptions that propagate to C level will trap (no recovery)
  — catch exceptions in Python before they escape
- `longjmp` is a no-op: unhandled exceptions cause a WASM unreachable trap
- No `threading`, `asyncio`, or `subprocess`
- Module imports are limited to MicroPython built-ins + `_akira`
- Available built-ins: `sys`, `struct`, `json`, `re`, `math`, `utime`, etc.
