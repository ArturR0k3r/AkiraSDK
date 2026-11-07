# Python Apps Guide for AkiraOS

This guide explains how to build WASM applications for AkiraOS using Python
(MicroPython).

## Architecture

```
                    build time
Python script ──┐
                ▼
         py_to_wasm.py ──► micropython.wasm + akira_py_script custom section
                                               │
                    runtime (WAMR)             │
                                               ▼
                              MicroPython reads section, executes script
                              Python calls _akira (native C module)
                              _akira calls WASM imports from "env" module
                              AkiraOS native API runs on Zephyr
```

The key components are:

| Component | Location | Purpose |
|-----------|----------|---------|
| `micropython.wasm` | `AkiraSDK/python/runtime/` | Prebuilt MicroPython + `_akira` module |
| `akira.py` | `AkiraSDK/python/akira.py` | Pure Python API wrapper |
| `py_to_wasm.py` | `AkiraSDK/scripts/py_to_wasm.py` | Packages script into WASM |

## Prerequisites

- Python 3.8+
- `micropython.wasm` with `_akira` native module (see [Obtaining micropython.wasm](#obtaining-micropythonwasm))
- No C compiler needed

## Project Structure

```
my_app/
├── main.py          # Application code (imports akira)
└── manifest.json    # AkiraOS app manifest
```

A ready-to-use template is at `AkiraSDK/python/apps/hello_world/`.

## Writing the App

```python
import akira

def main():
    akira.print("Hello from Python WASM!")

    akira.display_clear(akira.COLOR_BLACK)
    akira.display_text(10, 10, "Hello Python!", akira.COLOR_WHITE)
    akira.display_flush()

main()
```

The `akira` module provides the full AkiraOS API; import it at the top of
every app.  The module is automatically available because `py_to_wasm.py`
copies `akira.py` into `micropython.wasm`'s `sys.path`.

## manifest.json

```json
{
  "name": "my_app",
  "version": "1.0.0",
  "capabilities": ["input.read"],
  "memory_quota": 262144,
  "min_akiraos_version": "1.0.0"
}
```

> Python apps need **256 KB** of memory (the MicroPython heap).

## Building

```sh
# Basic build
python3 AkiraSDK/scripts/py_to_wasm.py main.py -o my_app.wasm

# With explicit manifest
python3 AkiraSDK/scripts/py_to_wasm.py main.py \
    --manifest manifest.json -o my_app.wasm

# With explicit runtime location
MICROPYTHON_WASM=/path/to/micropython.wasm \
    python3 AkiraSDK/scripts/py_to_wasm.py main.py -o my_app.wasm
```

Or use the AkiraSDK build script:
```sh
cd AkiraSDK/wasm_apps
./build.sh python/my_app
```

Or via make:
```sh
cd AkiraSDK/wasm_apps
make python/my_app
# or all Python apps:
make build-python
```

## Obtaining micropython.wasm

### Option 1 — Prebuilt binary (recommended)

Download `micropython.wasm` from the AkiraOS releases page and place it at:
```
AkiraSDK/python/runtime/micropython.wasm
```
or set the environment variable:
```sh
export MICROPYTHON_WASM=/path/to/micropython.wasm
```

### Option 2 — Build from source

Requires Emscripten or WASI SDK:
```sh
git clone https://github.com/micropython/micropython.git
cd micropython
make -C mpy-cross
cp <AkiraSDK>/python/native/_akira.c ports/webassembly/modules/
cd ports/webassembly && make MICROPY_WITH_AKIRA=1
cp build/micropython.wasm <AkiraSDK>/python/runtime/
```

## API Reference

All AkiraOS functions are available via `import akira`:

### console / timing
```python
akira.print("message")
akira.printf("val=%d\n", 42)
akira.delay_ms(500)
```

### display
```python
akira.display_clear(akira.COLOR_BLACK)
akira.display_text(x, y, "text", akira.COLOR_WHITE)
akira.display_text_large(x, y, "big", akira.COLOR_YELLOW)
akira.display_number(x, y, 42, akira.COLOR_GREEN)
akira.display_rect(x, y, w, h, akira.COLOR_RED)
akira.display_rect_outline(x, y, w, h, akira.COLOR_BLUE)
akira.display_rounded_rect(x, y, w, h, radius, color)
akira.display_circle(cx, cy, r, color)
akira.display_circle_fill(cx, cy, r, color)
akira.display_line(x0, y0, x1, y1, color)
akira.display_progress_bar(x, y, w, h, pct, color)
akira.display_flush()
w, h = akira.display_get_size()
```

**Color constants:** `COLOR_BLACK`, `COLOR_WHITE`, `COLOR_RED`, `COLOR_GREEN`,
`COLOR_BLUE`, `COLOR_YELLOW`, `COLOR_CYAN`, `COLOR_MAGENTA`, `COLOR_ORANGE`,
`COLOR_PURPLE`

### gpio
```python
akira.gpio_configure(pin, akira.GPIO_OUTPUT)
akira.gpio_write(pin, 1)
v = akira.gpio_read(pin)
```

**Flags:** `GPIO_INPUT`, `GPIO_OUTPUT`, `GPIO_PULL_UP`, `GPIO_PULL_DOWN`,
`GPIO_ACTIVE_LOW`, `GPIO_ACTIVE_HIGH`

### sensor
```python
raw   = akira.sensor_read(akira.SENSOR_CHAN_ACCEL_X)
float_val = akira.sensor_read_float(akira.SENSOR_CHAN_GYRO_Z)
```

**Channels:** `SENSOR_CHAN_ACCEL_{X,Y,Z}`, `SENSOR_CHAN_GYRO_{X,Y,Z}`,
`SENSOR_CHAN_AMBIENT_TEMP`, `SENSOR_CHAN_HUMIDITY`, `SENSOR_CHAN_PRESSURE`,
`SENSOR_CHAN_ALTITUDE`, `SENSOR_CHAN_LIGHT`

### storage
```python
fd = akira.storage_open("/data/log.txt", akira.O_WRITE)
akira.storage_write(fd, "hello\n")
akira.storage_close(fd)
data = akira.storage_read(fd, 256)
akira.storage_delete("/data/old.txt")
listing = akira.storage_list("/data/")
```

### BLE
```python
akira.ble_init("MyDevice")
akira.ble_char_add(0, akira.BLE_PROP_NOTIFY | akira.BLE_PROP_READ)
akira.ble_notify(0, b"hello")
connected = akira.ble_is_connected()
```

### IPC (messaging)
```python
akira.msg_subscribe("sensors")
data = akira.msg_recv("sensors", 1000)    # timeout_ms
akira.msg_publish("events", b"click")
```

### net
```python
sock = akira.net_open(akira.NET_TYPE_TCP)
akira.net_connect(sock, "192.168.1.10", 8080)
```

### app control
```python
status = akira.app_get_status("other_app")
akira.app_start("other_app")
akira.app_switch_to("other_app")
name = akira.app_get_self_name()
```

### power
```python
mode  = akira.power_get_mode()
level = akira.power_get_battery_level()
akira.power_pet_watchdog()
```

### HID
```python
akira.hid_keyboard_press(akira.HID_KEY_A)
akira.hid_keyboard_release(akira.HID_KEY_A)
akira.hid_mouse_move(dx, dy)
akira.hid_consumer_press(akira.HID_CONSUMER_VOLUME_UP)
```

### UART
```python
h = akira.uart_open(0, 115200)
akira.uart_write(h, b"AT\r\n")
data = akira.uart_read(h, 64)
akira.uart_close(h)
```

### I2C
```python
akira.i2c_write_reg(bus_id=0, dev_addr=0x48, reg_addr=0x01, data=b"\x00\x00")
data = akira.i2c_read_reg(bus_id=0, dev_addr=0x48, reg_addr=0x00, length=2)
```

### PWM / ADC
```python
akira.pwm_set(channel=0, freq_hz=1000, duty_pct=50)
akira.pwm_disable(channel=0)
raw_mv = akira.adc_read_mv(channel=0)
```

## Memory Constraints

Python apps use 256 KB memory (MicroPython heap + stack). Keep in mind:

- Avoid large buffers/lists in memory
- Prefer generators over list comprehensions for big data
- Use `akira.storage_*` for persistent data
- MicroPython does not support all CPython modules

## Limitations

- Modules available: `sys`, `struct`, `json`, `re`, `math`, `utime`, and
  other MicroPython built-ins
- No `threading`, `asyncio`, or `subprocess`
- No floating-point printf format (use `str(f)`)
- Module import is limited to what is frozen into `micropython.wasm`
- `akira.py` is always available (frozen at build time)
