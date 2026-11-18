# Akira SDK API Reference

Complete reference for all Akira SDK APIs. All functions are declared in `include/akira_api.h`.

---

## Table of Contents

- [Limits and error codes (AkiraOS 1.7)](#limits-and-error-codes-akiraos-17)
- [Logging & Timing](#logging--timing)
- [Display API](#display-api)
- [GPIO API](#gpio-api)
- [Sensor API](#sensor-api)
- [Timer API](#timer-api)
- [BLE API](#ble-api)
- [HID API](#hid-api)
- [Storage API](#storage-api)
- [Network API](#network-api)
- [IPC Pub/Sub API](#ipc-pubsub-api)
- [App Lifecycle API](#app-lifecycle-api)
- [RF API](#rf-api)
- [UART API](#uart-api)
- [I2C API](#i2c-api)
- [PWM API](#pwm-api)
- [Power Management API](#power-management-api)
- [Memory API](#memory-api)
- [Constants & Types](#constants--types)

---

## Limits and error codes (AkiraOS 1.7)

AkiraOS 1.7 makes sure no app can crash or hang the system. Whatever an app's manifest asks for, these fixed rules apply. Full description: [App Containment](https://github.com/ArturR0k3r/AkiraOS/blob/main/docs/architecture/app-containment.md).

**Stopping.** When an app is stopped (`app stop`, an app switch, `app_stop()` from another app), its waits end early: `delay()` returns (still `0`), `msg_recv()`, `app_start()`, `app_stop()` and `app_install_from_sd()` return `-EINTR`, and settings, RF, mesh, MQTT, BLE (`ble_init()`, `ble_add_service()`, `ble_advertise()`) and network calls return `-ECANCELED`; Matter polls return `MATTER_ERR_TIMEOUT`. A thread still running WASM code 1.5 s after the stop is aborted. RF and mesh receive timeouts are capped at 60 s. A crash or trap inside the app, including inside a native call, stops only that app.

**Memory.** Everything the runtime allocates for a running app (instance, linear memory, WASM stack, each `memory.grow`) is capped at `CONFIG_AKIRA_WASM_APP_MEM_LIMIT_KB`: 2048 KB on boards with PSRAM, 1024 KB without. Past it, `memory.grow` returns -1 or the app traps. Without PSRAM, an allocation that would leave less than `CONFIG_AKIRA_WASM_APP_HEAP_RESERVE_KB` (32 KB) of the system heap free is refused.

**Output.** `printf()`, `printf_native()` and WASI stdout/stderr share one budget per app: a 2 KB burst, then 1 KB/s, each line costing its length plus 32 bytes. Lines over budget are dropped and reported as `N line(s) of output dropped (rate limit)`. Lines are cut at 160 characters; control characters other than tab become `.`. Only the first 640 bytes of one `printf_native()` message are read; the rest is dropped without a notice.

**Files** (storage and FS APIs share one directory and one set of limits):

| Rule | Limit |
|------|-------|
| Directory | `/SD:/apps/<app>/` with an SD card, `/lfs/apps/<app>/` without; created on first write, lists as empty before that |
| Paths | relative; `..` → `-EACCES`; `\`, `:` or control characters → `-EINVAL`; over 192 characters → `-ENAMETOOLONG` |
| Open files | 2 per app (`CONFIG_AKIRA_WASM_{STORAGE,FS}_MAX_FILES_PER_APP`), and all apps together leave the OS two filesystem slots → `-EMFILE` |
| File size | 256 KB per file (`CONFIG_AKIRA_WASM_{STORAGE,FS}_QUOTA_BYTES`) → `-ENOSPC` |
| Volume reserve | a write, create or mkdir that would leave less than a quarter of the volume free (64 KB to 8 MB) → `-ENOSPC` |
| Cleanup | files left open are closed when the app exits |

**Settings.** Each app has a private namespace bound to its exact name; OS keys and other apps' keys are out of reach, and keys saved under the old global names before 1.7 are not visible. Keys up to 26 characters (`-ENAMETOOLONG`), values up to 63 bytes (`-E2BIG`) with the default Kconfig; 40 keys per app and 64 for all apps (`-ENOSPC`); writes 40 at once, then one every 2 s (`-EAGAIN`). An app's keys are deleted when it is uninstalled.

**Hardware.** System resources are refused with `-EACCES`:

| Resource | Apps may use |
|----------|--------------|
| GPIO | `CONFIG_AKIRA_WASM_GPIO_APP_PINS` minus devicetree, chip-reserved and strapping pins (AkiraConsole Prod: GPIO39, GPIO40). Only pins the app configured can be read or written (`-EPERM`). |
| ADC | `CONFIG_AKIRA_WASM_ADC_CHANNEL_MASK`, minus channels on system pads (AkiraConsole Prod: SAR ADC1 channel 1, the RV1 pot) |
| I2C | any address except the bus's devicetree children, `0x00`–`0x07`, `0x78`–`0x7F` and, on AkiraConsole Prod, `0x2D`, `0x34`, `0x48`, `0x70`; transfers 1–256 bytes |
| UART | UARTs the system doesn't use; a write sends at most 4096 bytes or ~250 ms per call |
| PWM | LEDC channels no system node (or shared timer) uses, on an allowed pad |
| Radio | raw capture and replay are capped at 2 s per call |

**Ownership.** Pins, PWM channels, UART ports, streams, files, timers, IPC subscriptions, AI model handles, HID actions and Matter endpoints belong to the app that created them and are released when it exits. The display, the app BLE layer and the mesh session have one owner at a time; other apps get `-EBUSY` (display, BLE) or `-EPERM` (mesh).

**Rate-limited calls:**

| Calls | Limit | Error |
|-------|-------|-------|
| `settings_set()`, `settings_delete()` | 40 at once, then 1 per 2 s, per app | `-EAGAIN` |
| `app_start()`, `app_stop()`, `app_switch()`, `app_install_from_sd()`, `app_request_update()` | 4 at once, then 1 per 2 s, per app | `-EAGAIN` |
| `mqtt_publish()`, `ha_light_register()`, `ha_light_report()` | 20 at once, then 10/s, per app (`-EBUSY` also when the broker connection's lock stays busy for 2–3 s) | `-EBUSY` |
| `ble_advertise()`, `ble_stop_advertise()` | 8 at once, then 4/s | `-EBUSY` |
| `ble_add_service()` | `CONFIG_AKIRA_BLE_MAX_SERVICES` at once, then 1 per 2 s | `-EBUSY` |
| `ble_set_local_name()` | 1 per 2 s | `-EBUSY` |
| `power_set_*()`, `power_wake_*()` | 8 at once, then 1 per 250 ms, all apps | `-EAGAIN` |
| `net_connect()`, `net_bind()`, `net_listen()` refused by policy | after 4 refusals in 1 s, refused at once | `-EACCES` |

**Other rules.** `wdt_pet()` no longer feeds the hardware watchdog. Apps can't select light sleep, deep sleep or hibernate (`-ENOTSUP`). Apps can't confirm an OTA image, roll back while an update is in progress, or run more than one OTA operation at a time. An app can't start, stop or switch to itself (`-EINVAL`) or reinstall itself (`-EBUSY`). Network ring buffers need linear memory that can't grow (`--initial-memory` equal to `--max-memory`), or `net_*_bind()` returns `-ENOTSUP`.

**AOT.** An unattested `.aot` file still loads, with a warning and an audit record: AOT is native code that the sandbox only confines if `wamrc` built it from a source you trust. Firmware built with `CONFIG_AKIRA_REFUSE_UNATTESTED_AOT=y` refuses it.

**Common error codes:** `-EPERM` capability not granted (Matter: `MATTER_ERR_NOPERM`; AI: `AIINFER_ERR_INVALID`; `mem_alloc()` returns 0) · `-EACCES` system resource · `-EBUSY` owned by another app, or rate limit (MQTT, BLE) · `-EAGAIN` timeout or rate limit · `-EMFILE` open file limit · `-ENOSPC` quota or reserve · `-EMSGSIZE` input too large · `-EINTR` / `-ECANCELED` app is being stopped · `-ETIMEDOUT` gave up waiting. The numeric values are in [Error Codes](#error-codes).

---

## Logging & Timing

No capability required.

### `printf()`

```c
void printf(const char *fmt, ...);
```

Formats a string and sends it to the AkiraOS host console. Supports `%d` (integer) and `%s` (string). Newlines are stripped — the host adds its own line endings.

```c
printf("Temp: %d milli-C", sensor_read(SENSOR_CHAN_AMBIENT_TEMP));
```

### `printf_native()`

```c
extern int printf_native(const char *message);
```

Sends a pre-formatted, null-terminated string directly to the host logger. Called internally by `printf()`. Returns `0`, or `-EINVAL` for a NULL string.

App output is rate-limited per app (2 KB burst, then 1 KB/s; each line costs its length plus 32 bytes). Lines over budget are dropped and the gap is reported; lines are cut at 160 characters. Don't print in a tight loop.

### `delay()`

```c
extern int delay(uint32_t microseconds);
```

Yields execution for the specified number of microseconds. Use this to yield between polls and avoid spinning the CPU. `delay(0)` just yields. The sleep ends early when the app is being stopped; it returns `0` either way.

```c
delay(10000);  // 10 ms
delay(1000000); // 1 second
```

---

## Display API

**Required capability:** `display.write`

All draw calls write to a back-buffer. Call `display_flush()` to push the frame to the screen. An auto-flush fires 50 ms after the last draw call, but explicit flushing gives smoother animation.

Coordinates start at `(0, 0)` in the top-left corner. Colors are in **RGB565** format (16-bit).

Any coordinate, size or radius is safe: shapes are clipped to the panel. Radii above 4096 are clamped, and `display_text()` / `display_text_large()` draw at most 4096 bytes of the string. The first app that draws owns the display; draw calls from other apps return `-EBUSY` until it exits. A pending auto-flush is cancelled when the app exits.

### Basic Drawing

#### `display_clear()`

```c
extern int display_clear(uint32_t color);
```

Fills the entire display with a solid color.

```c
display_clear(COLOR_BLACK);
```

#### `display_pixel()`

```c
extern int display_pixel(int32_t x, int32_t y, uint32_t color);
```

Sets a single pixel.

#### `display_rect()`

```c
extern int display_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
```

Draws a filled rectangle. `x, y` is the top-left corner.

```c
display_rect(10, 10, 100, 50, COLOR_BLUE);
```

#### `display_rect_outline()`

```c
extern int display_rect_outline(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
```

Draws a rectangle outline (four lines).

#### `display_rounded_rect()`

```c
extern int display_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                                 int32_t radius, uint32_t color);
```

Draws a rounded rectangle outline. `radius` is the corner arc radius in pixels.

#### `display_rounded_rect_fill()`

```c
extern int display_rounded_rect_fill(int32_t x, int32_t y, int32_t w, int32_t h,
                                      int32_t radius, uint32_t color);
```

Draws a filled rounded rectangle.

#### `display_line()`

```c
extern int display_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
```

Draws a straight line between two points (Bresenham algorithm, no FPU required).

#### `display_hline()`

```c
extern int display_hline(int32_t x, int32_t y, int32_t len, uint32_t color);
```

Draws an optimised horizontal run.

#### `display_vline()`

```c
extern int display_vline(int32_t x, int32_t y, int32_t len, uint32_t color);
```

Draws an optimised vertical run.

#### `display_circle()`

```c
extern int display_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);
```

Draws a circle outline (midpoint algorithm, no FPU).

#### `display_circle_fill()`

```c
extern int display_circle_fill(int32_t cx, int32_t cy, int32_t r, uint32_t color);
```

Draws a filled circle.

#### `display_triangle()`

```c
extern int display_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             int32_t x2, int32_t y2, uint32_t color);
```

Draws a triangle outline (three lines).

#### `display_triangle_fill()`

```c
extern int display_triangle_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                  int32_t x2, int32_t y2, uint32_t color);
```

Draws a filled triangle (scanline rasteriser, vertices in any order).

### Text & Numbers

#### `display_text()`

```c
extern int display_text(int32_t x, int32_t y, const char *text, uint32_t color);
```

Renders text using the small font (7×10 pixels per character).

```c
display_text(10, 20, "Hello AkiraOS!", COLOR_WHITE);
```

#### `display_text_large()`

```c
extern int display_text_large(int32_t x, int32_t y, const char *text, uint32_t color);
```

Renders text using the large font (11×18 pixels per character).

#### `display_number()`

```c
extern int display_number(int32_t x, int32_t y, int32_t value, uint32_t color);
```

Renders an integer as decimal text using the small font. No stdlib required.

### Bitmaps

#### `display_bitmap()`

```c
extern int display_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                           const uint16_t *data, uint32_t data_size);
```

Blits an RGB565 bitmap. `data` must be row-major, `w*h*2` bytes, 2-byte aligned. Returns `-EINVAL` if `data_size` is too small or `data` is not 2-byte aligned.

#### `display_bitmap_transparent()`

```c
extern int display_bitmap_transparent(int32_t x, int32_t y, int32_t w, int32_t h,
                                       const uint16_t *data, uint32_t data_size,
                                       uint32_t key);
```

Same as `display_bitmap()` but pixels matching `key` are not written (transparency).

### UI Widgets

#### `display_progress_bar()`

```c
extern int display_progress_bar(int32_t x, int32_t y, int32_t w, int32_t h,
                                 int32_t value, int32_t max_val,
                                 uint32_t fg, uint32_t bg);
```

Draws a horizontal progress bar. `value` is clamped to `[0, max_val]`.

```c
display_progress_bar(10, 100, 200, 20, battery_pct, 100, COLOR_GREEN, COLOR_DARK_GRAY);
```

### Frame Control

#### `display_flush()`

```c
extern int display_flush(void);
```

Pushes the back-buffer to the physical display. Call once per frame after all draw calls.

```c
display_clear(COLOR_BLACK);
display_text(10, 10, "Frame ready", COLOR_WHITE);
display_flush();
```

#### `display_get_size()`

```c
extern int display_get_size(int32_t *w_out, int32_t *h_out);
```

Returns the display resolution in pixels.

```c
int32_t w, h;
display_get_size(&w, &h);
```

### RGB565 Color Format

RGB565 packs 16 bits as: **5 bits red, 6 bits green, 5 bits blue**.

```c
// Predefined colours
COLOR_BLACK       // 0x0000
COLOR_WHITE       // 0xFFFF
COLOR_RED         // 0xF800
COLOR_GREEN       // 0x07E0
COLOR_BLUE        // 0x001F
COLOR_YELLOW      // 0xFFE0
COLOR_CYAN        // 0x07FF
COLOR_MAGENTA     // 0xF81F
COLOR_GRAY        // 0x7BEF
COLOR_DARK_GRAY   // 0x39E7
COLOR_LIGHT_GRAY  // 0xC618
COLOR_ORANGE      // 0xFD20
COLOR_PURPLE      // 0x801F

// Custom colour
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
```

---

## GPIO API

**Required capabilities:** `gpio.read` and/or `gpio.write`

Simple polling-based GPIO. No callbacks — read state directly in your loop.

Pin numbers are SoC GPIO numbers (0–31 = gpio0, 32+ = gpio1). Apps may use only the board's `CONFIG_AKIRA_WASM_GPIO_APP_PINS`, minus every pin the devicetree gives the system, chip-reserved pins and strapping pins; any other pin returns `-EACCES`. On AkiraConsole Prod apps get GPIO39 and GPIO40. A pin belongs to the app that configured it: `gpio_read()` / `gpio_write()` on a pin this app didn't configure return `-EPERM`, `gpio_configure()` on another app's pin returns `-EBUSY`, and the app's pins are disconnected when it exits.

### `gpio_configure()`

```c
extern int gpio_configure(uint32_t pin, uint32_t flags);
```

Configures a GPIO pin. `flags` is a bitmask of:

| Flag | Value | Description |
|------|-------|-------------|
| `GPIO_INPUT` | `1 << 0` | Configure as input |
| `GPIO_OUTPUT` | `1 << 1` | Configure as output |
| `GPIO_OUTPUT_INIT_LOW` | `1 << 2` | Initial output state low |
| `GPIO_OUTPUT_INIT_HIGH` | `1 << 3` | Initial output state high |
| `GPIO_PULL_UP` | `1 << 4` | Enable internal pull-up |
| `GPIO_PULL_DOWN` | `1 << 5` | Enable internal pull-down |
| `GPIO_ACTIVE_LOW` | `1 << 6` | Active-low logic |
| `GPIO_ACTIVE_HIGH` | `1 << 7` | Active-high logic |

```c
gpio_configure(LED_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
gpio_configure(BTN_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
```

Contradictory flags (`GPIO_PULL_UP | GPIO_PULL_DOWN`, both `OUTPUT_INIT_*`, or `OUTPUT_INIT_*` without `GPIO_OUTPUT`) return `-EINVAL`.

### `gpio_read()`

```c
extern int gpio_read(uint32_t pin);
```

Returns `1` (high) or `0` (low). Returns a negative error code on failure.

```c
if (gpio_read(BTN_PIN) == 1) {
    // button pressed
}
```

### `gpio_write()`

```c
extern int gpio_write(uint32_t pin, uint32_t value);
```

Sets a GPIO output pin high (`1`) or low (`0`).

```c
gpio_write(LED_PIN, 1);  // LED on
gpio_write(LED_PIN, 0);  // LED off
```

---

## Sensor API

**Required capability:** `sensor.read`

Single function to read any sensor channel. Values are returned scaled by 1000 — divide by 1000 to get the physical value.

### `sensor_read()`

```c
extern int sensor_read(int32_t channel);
```

Reads the first sensor device that supports the requested channel. Returns `AKIRA_SENSOR_ERROR` (`INT32_MIN`) on failure — always compare against this constant.

```c
#define AKIRA_SENSOR_ERROR  (-2147483647 - 1)  /* INT32_MIN */
```

| Return value | Meaning |
|---|---|
| `value * 1000` | Success — divide by 1000.0 to get physical value |
| `AKIRA_SENSOR_ERROR` | Sensor unavailable or I/O error |

**Sensor channel constants:**

| Constant | Value | Unit |
|----------|-------|------|
| `SENSOR_CHAN_ACCEL_X` | 0 | m/s² |
| `SENSOR_CHAN_ACCEL_Y` | 1 | m/s² |
| `SENSOR_CHAN_ACCEL_Z` | 2 | m/s² |
| `SENSOR_CHAN_GYRO_X` | 4 | rad/s |
| `SENSOR_CHAN_GYRO_Y` | 5 | rad/s |
| `SENSOR_CHAN_GYRO_Z` | 6 | rad/s |
| `SENSOR_CHAN_MAGN_X` | 8 | Gauss |
| `SENSOR_CHAN_MAGN_Y` | 9 | Gauss |
| `SENSOR_CHAN_MAGN_Z` | 10 | Gauss |
| `SENSOR_CHAN_AMBIENT_TEMP` | 13 | °C |
| `SENSOR_CHAN_PRESS` | 14 | kPa |
| `SENSOR_CHAN_HUMIDITY` | 16 | % |
| `SENSOR_CHAN_ALTITUDE` | 23 | m |
| `SENSOR_CHAN_VOLTAGE` | 33 | V |
| `SENSOR_CHAN_CURRENT` | 35 | A |
| `SENSOR_CHAN_POWER` | 36 | W |

```c
int raw = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
if (raw == AKIRA_SENSOR_ERROR) {
    printf("sensor unavailable");
} else {
    // raw = 23500 → 23.5 °C (use integer math: raw / 1000 = 23)
    printf("Temp: %d.%d C", raw / 1000, (raw % 1000) / 100);
}

int ax = sensor_read(SENSOR_CHAN_ACCEL_X);   // m/s² * 1000
int gx = sensor_read(SENSOR_CHAN_GYRO_X);    // rad/s * 1000
```

---

## Timer API

**Required capability:** `timer`

Polling timers backed by the OS uptime counter. Times are in milliseconds. Use `delay()` to yield between polls.

### `timer_create()`

```c
extern int timer_create(void);
```

Allocates a new timer handle. Returns `>= 0` on success, `-ENOMEM` if the pool is full. Each app may hold at most half of `CONFIG_AKIRA_WASM_TIMER_MAX_HANDLES` (4 of the default 8). Handles belong to the app (another app's handle returns `-EPERM`) and are freed when it exits.

### `timer_start()`

```c
extern int timer_start(int32_t handle);
```

Starts (or restarts) a timer, resetting elapsed time to 0.

### `timer_stop()`

```c
extern int timer_stop(int32_t handle);
```

Stops a running timer, preserving the elapsed time.

### `timer_elapsed()`

```c
extern int timer_elapsed(int32_t handle);
```

Returns elapsed milliseconds. For a running timer: time since `timer_start()`. For a stopped timer: time between `timer_start()` and `timer_stop()`.

### `timer_free()`

```c
extern int timer_free(int32_t handle);
```

Releases a timer handle back to the pool.

**Example — periodic task:**

```c
int t = timer_create();
timer_start(t);

while (1) {
    if (timer_elapsed(t) >= 1000) {
        timer_start(t);  // reset
        // do 1-second task
        read_and_display_sensors();
    }
    delay(10000);  // yield 10 ms
}
```

---

## BLE API

**Required capability:** `ble`

Arduino-style GATT server API. Create custom services and characteristics, then poll for events in a loop.

One app owns the app BLE layer at a time: the first app that sets anything up owns it until `ble_deinit()` or until it exits; other apps get `-EBUSY`. When the owner exits, its advert is stopped, its GATT services removed and the device name, bonding and BT mode restored. Rate limits (`-EBUSY`): `ble_advertise()`/`ble_stop_advertise()` 8 at once then 4/s; `ble_add_service()` `CONFIG_AKIRA_BLE_MAX_SERVICES` at once then one per 2 s; `ble_set_local_name()` once per 2 s. `ble_char_write()` with more than `CONFIG_AKIRA_BLE_CHAR_MAX_LEN` bytes returns `-EMSGSIZE`.

### Setup Functions

```c
extern int ble_init(void);
extern int ble_deinit(void);
extern int ble_set_local_name(const char *name);           // max 29 bytes, 1 rename / 2 s
extern int ble_service_create(const char *uuid128_str);    // returns svc handle
extern int ble_char_create(const char *uuid128_str, int32_t props, int32_t max_len); // returns char handle
extern int ble_service_add_char(int32_t svc_h, int32_t char_h);
extern int ble_add_service(int32_t svc_h);
extern int ble_set_advertised_service(int32_t svc_h);
extern int ble_advertise(void);
extern int ble_stop_advertise(void);
```

**Characteristic property flags (`props`):**

| Flag | Meaning |
|------|---------|
| `BLE_PROP_READ` | `0x02` |
| `BLE_PROP_WRITE_WO_RSP` | `0x04` |
| `BLE_PROP_WRITE` | `0x08` |
| `BLE_PROP_NOTIFY` | `0x10` |
| `BLE_PROP_INDICATE` | `0x20` |

### Event Loop

```c
extern int ble_event_pop(uint8_t *buf, uint32_t len);
extern int ble_is_connected(void);
```

`ble_event_pop()` is non-blocking. Event buffer layout:

```
buf[0]    — event type (BLE_EVT_*)
buf[1]    — char_handle (BLE_EVT_CHAR_WRITTEN only)
buf[2-3]  — data_len (little-endian uint16)
buf[4+]   — data payload
```

**Event types:**

| Constant | Value | Description |
|----------|-------|-------------|
| `BLE_EVT_NONE` | 0 | No event |
| `BLE_EVT_CONNECTED` | 1 | Peer connected |
| `BLE_EVT_DISCONNECTED` | 2 | Peer disconnected |
| `BLE_EVT_CHAR_WRITTEN` | 3 | Characteristic written by peer |

### Data I/O

```c
extern int ble_char_write(int32_t char_h, const uint8_t *data, uint32_t len);
extern int ble_char_read(int32_t char_h, uint8_t *buf, uint32_t len);
```

**Full example:**

```c
int svc  = ble_service_create("19B10000-E8F2-537E-4F6C-D104768A1214");
int ch   = ble_char_create("19B10001-E8F2-537E-4F6C-D104768A1214",
                            BLE_PROP_READ | BLE_PROP_WRITE, 1);
ble_service_add_char(svc, ch);
ble_add_service(svc);
ble_set_local_name("AkiraOS_Device");
ble_set_advertised_service(svc);
ble_init();
ble_advertise();

uint8_t evt[68];
while (1) {
    int e = ble_event_pop(evt, sizeof(evt));
    if (e == BLE_EVT_CONNECTED)    printf("connected");
    if (e == BLE_EVT_DISCONNECTED) { printf("disconnected"); ble_advertise(); }
    if (e == BLE_EVT_CHAR_WRITTEN) {
        uint16_t len = evt[2] | (evt[3] << 8);
        // evt[4..4+len-1] = data
    }
    delay(10000);
}
```

---

## HID API

**Required capability:** `hid`

Bluetooth/USB HID device. Supports keyboard, mouse, gamepad, and consumer (media) controls.

When the app exits, everything it left pressed is released, its named actions are dropped, and the transport and device types it changed are restored.

### Initialisation

```c
// One-shot setup (recommended)
extern int hid_init(int transport, int device_types);

// Or step-by-step
extern int hid_set_transport(int32_t transport);
extern int hid_set_device_types(int32_t types);
extern int hid_enable(void);
extern int hid_disable(void);
extern int hid_is_connected(void);
```

**Transport constants:**

| Constant | Value |
|----------|-------|
| `HID_TRANSPORT_NONE` | 0 |
| `HID_TRANSPORT_BLE` | 1 |
| `HID_TRANSPORT_USB` | 2 |

**Device type flags:**

| Constant | Value | Description |
|----------|-------|-------------|
| `HID_DEVICE_KEYBOARD` | 0x01 | Keyboard + media keys |
| `HID_DEVICE_GAMEPAD` | 0x02 | Gamepad / joystick |
| `HID_DEVICE_MOUSE` | 0x04 | Mouse |
| `HID_DEVICE_COMBO` | 0x07 | All types combined |

```c
hid_init(HID_TRANSPORT_BLE, HID_DEVICE_KEYBOARD);
```

### Keyboard

```c
extern int hid_key_press(int32_t keycode);
extern int hid_key_release(int32_t keycode);
extern int hid_key_release_all(void);
extern int hid_type_string(const char *str);  // ASCII, auto-shift for uppercase
```

`hid_type_string()` takes at most 256 characters (`-E2BIG`), about 20 ms per character, and returns `-EINTR` if the app is stopped mid-string.

**Common keycodes:** `HID_KEY_A`…`HID_KEY_Z` (0x04…), `HID_KEY_ENTER` (0x28), `HID_KEY_ESC` (0x29), `HID_KEY_SPACE` (0x2C)

**Modifier constants:** `HID_MOD_LEFT_CTRL`, `HID_MOD_LEFT_SHIFT`, `HID_MOD_LEFT_ALT`, `HID_MOD_LEFT_GUI`, etc.

### Named Shortcuts

```c
extern int hid_action_register(const char *name, int32_t modifier, int32_t keycode);
extern int hid_action_trigger(const char *name);
```

```c
hid_action_register("screenshot", HID_MOD_LEFT_GUI, HID_KEY_PRTSCN);
hid_action_trigger("screenshot");  // fires Win+PrtScn
```

### Mouse

```c
extern int hid_mouse_move(int32_t dx, int32_t dy);       // relative, -127..127
extern int hid_mouse_btn_press(int32_t button);          // HID_MOUSE_BTN_*
extern int hid_mouse_btn_release(int32_t button);
extern int hid_mouse_scroll(int32_t delta);              // -127..127
```

**Mouse button constants:** `HID_MOUSE_BTN_LEFT` (0x01), `HID_MOUSE_BTN_RIGHT` (0x02), `HID_MOUSE_BTN_MIDDLE` (0x04)

### Gamepad

```c
extern int hid_gamepad_press(int32_t btn_mask);
extern int hid_gamepad_release(int32_t btn_mask);
extern int hid_gamepad_set_axis(int32_t axis, int32_t value);  // -32768..32767
extern int hid_gamepad_set_dpad(int32_t direction);  // 0=centre, 1-8=N/NE/E/SE/S/SW/W/NW
extern int hid_gamepad_reset(void);
```

### Consumer / Media Keys

```c
extern int hid_consumer_send(int32_t usage_code);
```

**Consumer key constants:** `HID_CONSUMER_PLAY_PAUSE` (0x00CD), `HID_CONSUMER_VOL_UP` (0x00E9), `HID_CONSUMER_VOL_DOWN` (0x00EA), `HID_CONSUMER_MUTE` (0x00E2), `HID_CONSUMER_NEXT_TRACK` (0x00B5), `HID_CONSUMER_PREV_TRACK` (0x00B6)

### Raw Report

```c
extern int hid_send_raw_report(int32_t report_id, const uint8_t *data_ptr, uint32_t len);
// len 1..64 bytes, report_id 0..255, else -EINVAL
```

---

## Storage API

**Required capabilities:** `storage.read` and/or `storage.write`

File descriptor–based sandboxed storage. Each app is confined to its private directory: `/SD:/apps/<app_name>/` with an SD card, `/lfs/apps/<app_name>/` without (shared with the FS API). Paths are relative; `..` traversal is rejected with `-EACCES`.

Limits: 2 open files per app (`-EMFILE`); a write that would grow a file past 256 KB (`CONFIG_AKIRA_WASM_STORAGE_QUOTA_BYTES`) or leave less than a quarter of the volume free returns `-ENOSPC`. Files left open are closed when the app exits. See [Limits and error codes](#limits-and-error-codes-akiraos-17).

### Open Flags

| Constant | Value | Description |
|----------|-------|-------------|
| `STORAGE_O_READ` | 0 | Read-only; file must exist |
| `STORAGE_O_WRITE` | 1 | Write; create/truncate |
| `STORAGE_O_APPEND` | 2 | Append; create if absent |
| `STORAGE_O_RDWR` | 3 | Read + write; create/truncate |

### Functions

```c
extern int  storage_open(const char *path, int flags);           // returns fd
extern int  storage_read(int fd, void *buf, int len);            // bytes read, 0=EOF
extern int  storage_write(int fd, const void *buf, int len);     // bytes written
extern void storage_close(int fd);
extern int  storage_delete(const char *path);                    // 0, -ENOENT, -ENOTEMPTY
extern int  storage_list(const char *path, char *buf, int len);  // newline-sep list, max 256 entries
```

**Example:**

```c
// Write a config file
int fd = storage_open("config.txt", STORAGE_O_WRITE);
if (fd >= 0) {
    storage_write(fd, "brightness=80\n", 14);
    storage_close(fd);
}

// Read it back
char buf[128];
fd = storage_open("config.txt", STORAGE_O_READ);
if (fd >= 0) {
    int n = storage_read(fd, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    storage_close(fd);
}

// List files
char list[256];
storage_list("", list, sizeof(list));  // "" = sandbox root
printf("files: %s", list);

// Delete
storage_delete("config.txt");
```

---

## Network API

**Required capability:** `network.*`

Async TCP/UDP sockets using shared-memory ring buffers. Data is written into/read from WASM buffers — only `net_tx_flush()` and `net_event_pop()` require host calls.

Streams belong to the app that opened them (another app's handle returns `-ENOENT`) and are closed when it exits; an app may hold `CONFIG_AKIRA_NET_MAX_STREAMS - 1` streams when more than one app can run (`-ENOSPC`). The rings must be in linear memory that can't grow: link with `--initial-memory` equal to `--max-memory`, or `net_tx_bind()`/`net_rx_bind()` return `-ENOTSUP`. `net_connect()`, `net_bind()` and `net_listen()` pass the firmware's egress policy (`-EACCES` when refused). Host names are at most 63 characters (`-ENAMETOOLONG`).

### Socket Lifecycle

```c
extern int net_open(int32_t type);                                     // NET_TYPE_TCP / NET_TYPE_UDP
extern int net_connect(int32_t handle, const char *host, int32_t port); // async, DNS
extern int net_bind(int32_t handle, int32_t port);
extern int net_listen(int32_t handle, int32_t backlog);                // TCP server
extern int net_close(int32_t handle);
```

### Ring Buffer Binding

```c
extern int net_tx_bind(int32_t handle, void *tx_buf, int32_t total_size);
extern int net_rx_bind(int32_t handle, void *rx_buf, int32_t total_size);
extern int net_tx_flush(int32_t handle);  // explicit flush (saves ~10ms); 0 or -ENOTCONN
```

`net_tx_flush()` only wakes the poll thread and returns `0`, not a byte count; send errors arrive later as `NET_EVT_ERROR`.

### Events

```c
extern int net_event_pop(void *buf, int32_t len);  // buf must be >= 4 bytes
```

Event buffer layout: `[type][stream_handle][extra_lo][extra_hi]`

| Constant | Value | Description |
|----------|-------|-------------|
| `NET_EVT_NONE` | 0 | Queue empty |
| `NET_EVT_CONNECTED` | 1 | TCP connected (or UDP peer set) |
| `NET_EVT_DISCONNECTED` | 2 | Peer closed or error |
| `NET_EVT_DATA_READY` | 3 | RX ring has new data |
| `NET_EVT_ACCEPT` | 4 | New inbound connection; extra = new handle |
| `NET_EVT_ERROR` | 5 | Socket error; extra = errno |

### Ring Buffer Helpers

```c
// Write one framed message into a TX ring
static inline int net_ring_write(uint8_t *ring_buf, int buf_size,
                                  const uint8_t *data, int data_len);

// Read one framed message from an RX ring
static inline int net_ring_read(uint8_t *ring_buf, int buf_size,
                                 uint8_t *out, int out_len);
```

**TCP client example:**

```c
static uint8_t tx[512], rx[512];

int h = net_open(NET_TYPE_TCP);
net_tx_bind(h, tx, sizeof(tx));
net_rx_bind(h, rx, sizeof(rx));
net_connect(h, "192.168.1.100", 8080);

uint8_t ev[4];
while (1) {
    int e = net_event_pop(ev, sizeof(ev));
    if (e == NET_EVT_CONNECTED) {
        net_ring_write(tx, sizeof(tx), (uint8_t*)"hello", 5);
        net_tx_flush(h);
    } else if (e == NET_EVT_DATA_READY) {
        uint8_t msg[256];
        int n = net_ring_read(rx, sizeof(rx), msg, sizeof(msg));
        if (n > 0) { /* process msg[0..n-1] */ }
    } else if (e == NET_EVT_DISCONNECTED) {
        net_close(h);
        break;
    }
    delay(5000);
}
```

---

## IPC Pub/Sub API

**Required capability:** `ipc`

In-process publish/subscribe messaging between apps. Max message payload: `CONFIG_AKIRA_IPC_MSG_MAX_SIZE` bytes (Kconfig default 64; 256 on the AkiraConsole boards); larger payloads return `-EMSGSIZE`. Subscriptions belong to the app and are freed when it exits; each app may hold `MIN(CONFIG_AKIRA_IPC_MAX_TOPICS, MAX(2, CONFIG_AKIRA_IPC_MAX_TOPICS / 4))` subscriptions, 6 with the default 24 topics (`-ENOMEM`).

```c
extern int msg_subscribe(const char *topic);
extern int msg_unsubscribe(const char *topic);
extern int msg_publish(const char *topic, const uint8_t *data_ptr, uint32_t len);
extern int msg_recv(const char *topic, uint8_t *buf_ptr, uint32_t buf_len,
                    int32_t timeout_ms);   // 0=non-blocking, -1=until message or stop
extern int msg_try_recv(const char *topic, uint8_t *buf_ptr, uint32_t buf_len);
extern int msg_pending(const char *topic);
```

`msg_recv()` returns bytes received, `-EAGAIN` on timeout, or `-EINTR` if the app is being stopped.

**Subscribe to lifecycle events:**

```c
msg_subscribe("akira.lifecycle");

// In loop:
akira_lifecycle_event_t ev;
int n = msg_try_recv("akira.lifecycle", (uint8_t*)&ev, sizeof(ev));
if (n > 0 && ev.state == APP_STATE_STOPPED) {
    printf("App stopped: %s", ev.name);
}
```

---

## App Lifecycle API

**Required capability:** `app.control` (elevated); `app_get_status()`, `app_list()` and `app_get_self_name()` also accept `app.info`.

Manage other apps. An app cannot stop itself — return `0` from `main()` for self-exit.

`app_start()`, `app_stop()` and `app_install_from_sd()` are queued on the OS lifecycle work queue; the caller waits up to 60 s for the result. They return `-EINTR` if the calling app is being stopped, `-ETIMEDOUT` after 60 s (the request may still complete), `-EBUSY` if 4 requests are already pending, and `-EAGAIN` past a burst of 4 requests per app, then one per 2 s (`app_switch()` and `app_request_update()` share that budget). An app can't start, stop or switch to itself (`-EINVAL`) or reinstall itself (`-EBUSY`).

```c
// APP_STATE_* constants
#define APP_STATE_NEW       0
#define APP_STATE_INSTALLED 1
#define APP_STATE_RUNNING   2
#define APP_STATE_STOPPED   3
#define APP_STATE_ERROR     4
#define APP_STATE_FAILED    5

extern int app_get_status(const char *name);
extern int app_list(uint8_t *buf, uint32_t buf_len);   // "name:STATE\n" entries
extern int app_get_self_name(uint8_t *buf, uint32_t buf_len);
extern int app_start(const char *name);
extern int app_stop(const char *name);
```

**`app_switch()`** — start another app and exit cleanly (requires `app.switch` or `app.control`):

```c
extern int app_switch(const char *name);

// Usage: launch supervisor and exit self
app_switch("supervisor");
return 0;   // clean exit triggers lifecycle event → supervisor redraws
```

---

## RF API

**Required capability:** `rf.transceive`

Low-level RF transceiver control (LoRa / LR1121 and similar).

```c
extern int rf_set_frequency(uint32_t freq_hz);
extern int rf_set_power(int8_t dbm);
extern int rf_get_rssi(void);                              // dBm, or negative errno
extern int rf_send(const uint8_t *payload, uint32_t len);  // len 1..255
```

Receive timeouts (`rf_recv_pop()`, `rf_receive()`) are capped at 60 s and end with `-ECANCELED` when the app is being stopped. `rf_raw_capture()` and `rf_raw_replay()` run at most 2 s per call (sample rate 1000–500000 Hz). The radio is released when the app exits.

---

## UART API

**Required capability:** `uart`

Full-duplex secondary UART. `uart_read()` is non-blocking. UART0 is reserved for the system shell; `port_id` 0 maps to UART1, 1 to UART2.

A UART the system uses (console, shell, Bluetooth HCI, mcumgr, or one with a device node) returns `-EACCES`. Baud rate 1200–3000000, 8N1, no flow control. A port belongs to the app that opened it (`-EPERM` for others) and is closed, with its line settings restored, when the app exits. `uart_write()` sends at most 4096 bytes and about 250 ms of wire time per call, and stops early if the app is being stopped: loop on the returned count.

```c
extern int uart_open(int32_t port_id, int32_t baud_rate);  // returns handle
extern int uart_write(int32_t handle, const uint8_t *buf, uint32_t len);
extern int uart_read(int32_t handle, uint8_t *buf, uint32_t max_len);  // 0=no data
extern int uart_close(int32_t handle);
```

```c
int h = uart_open(0, 115200);
uart_write(h, (uint8_t*)"AT\r\n", 4);

uint8_t resp[64];
while (1) {
    int n = uart_read(h, resp, sizeof(resp));
    if (n > 0) { /* process */ }
    delay(1000);
}
uart_close(h);
```

---

## I2C API

**Required capability:** `i2c`

Stateless raw register access. Addresses must be 7-bit and registers 8-bit; 1–256 bytes per transaction.

Addresses the system uses are refused with `-EACCES` (reads too): every devicetree child of the bus, the reserved ranges `0x00`–`0x07` and `0x78`–`0x7F`, and on AkiraConsole Prod `0x2D`, `0x34`, `0x48` and `0x70`. Read devices the OS drives with `sensor_read()`. A bus busy for 500 ms returns `-EBUSY`.

```c
extern int i2c_write_reg(int32_t bus_id, int32_t dev_addr, int32_t reg_addr,
                          const uint8_t *buf, uint32_t len);
extern int i2c_read_reg(int32_t bus_id, int32_t dev_addr, int32_t reg_addr,
                         uint8_t *buf, uint32_t len);
```

```c
// Read register 0x0F of a sensor you wired to bus 0 (not one the OS drives)
uint8_t who_am_i;
int n = i2c_read_reg(0, MY_SENSOR_ADDR, 0x0F, &who_am_i, 1);  // 1, or -EACCES etc.
```

---

## PWM API

**Required capability:** `pwm`

Channel-indexed PWM. The channel is the LEDC hardware channel (0–3).

A channel is usable only if the devicetree defines it (`-ENODEV` otherwise), no system node uses it or shares its timer, and its pad is a pin apps may use (`-EACCES` otherwise). A channel belongs to the app that set it until `pwm_disable()` (`-EBUSY` from `pwm_set()` for other apps); channels left running are stopped when the app exits.

```c
extern int pwm_set(int32_t channel, int32_t freq_hz, int32_t duty_pct);
// freq_hz: 1–10,000,000   duty_pct: 0–100
extern int pwm_disable(int32_t channel);
```

```c
pwm_set(0, 1000, 50);   // 1 kHz, 50% duty
pwm_set(0, 440,  80);   // 440 Hz buzzer tone
pwm_disable(0);
```

---

## Power Management API

**Required capabilities:** `power.read` and/or `power.control`

Apps can't put the device to sleep: `power_set_mode()` accepts only `POWER_MODE_ACTIVE` and `POWER_MODE_IDLE` and returns `-ENOTSUP` for light sleep, deep sleep and hibernate. `power_wake_on_gpio()` and `power_wake_on_timer()` only record the request. The control calls share one rate limit for all apps (8 at once, then one per 250 ms, `-EAGAIN`) and need firmware built with `CONFIG_AKIRA_WASM_POWER_CONTROL=y` (`-ENOTSUP` otherwise). A missing capability returns `-EPERM`.

```c
// Power modes
#define POWER_MODE_ACTIVE      0
#define POWER_MODE_IDLE        1
#define POWER_MODE_LIGHT_SLEEP 2
#define POWER_MODE_DEEP_SLEEP  3
#define POWER_MODE_HIBERNATE   4

extern int power_get_mode(void);
extern int power_get_battery_level(void);  // 0-100%, -ENODEV if no battery
extern int power_get_battery_status(void *buf, int len);  // buf: 12 bytes
extern int power_set_mode(int mode);
extern int power_wake_on_gpio(int pin, int edge);   // edge: 0=low, 1=high, 2=any
extern int power_wake_on_timer(int ms);
extern int power_set_low_power(int enable);
```

**Battery status buffer layout (12 bytes):**

```
[0]    uint8  level_percent (0-100)
[1]    uint8  flags: BATT_FLAG_CHARGING (1<<0), BATT_FLAG_LOW_BATTERY (1<<1)
[2-3]  pad
[4-7]  int32  voltage_mv (little-endian)
[8-11] int32  current_ma (little-endian; + = charge, - = discharge)
```

```c
// Read the battery and adapt; when the device sleeps is the OS's decision
uint8_t st[12];
if (power_get_battery_status(st, sizeof(st)) == 0 && (st[1] & BATT_FLAG_LOW_BATTERY)) {
    // slow down polling
}
```

---

## Memory API

**Required capability:** `memory`

```c
extern uint32_t mem_alloc(uint32_t size);  // returns WASM address, 0 on failure
extern void     mem_free(uint32_t ptr);
```

`mem_alloc()` takes blocks from the app's own heap inside its linear memory and returns 0 without the capability, for 0 or more than 16 MB, past the manifest `memory_quota`, or when the heap is full. `mem_free()` ignores pointers `mem_alloc()` didn't return. The runtime's allocations for the app are capped separately (see [Limits and error codes](#limits-and-error-codes-akiraos-17)).

Prefer **static buffers** over dynamic allocation in embedded WASM apps — no heap fragmentation.

---

## Constants & Types

### Error Codes

AkiraOS (Zephyr / picolibc) values; natives return them negated. Each is also defined as `AKIRA_E<name>` (e.g. `AKIRA_ETIMEDOUT`). They differ from wasi-libc's `<errno.h>`: if that header is included before `akira_api.h`, compilation stops with an `#error`; define `AKIRA_API_NO_ERRNO_NAMES` to use only the `AKIRA_E*` names. native_sim built with `CONFIG_EXTERNAL_LIBC=y` (host glibc) returns the host's numbers instead.

```c
#define EPERM         1   // Operation not permitted (capability not granted)
#define ENOENT        2   // No such file or directory
#define EINTR         4   // Interrupted: the app is being stopped
#define EIO           5   // I/O error
#define E2BIG         7   // Value too long
#define EBADF         9   // Bad file descriptor
#define EAGAIN        11  // Try again (timeout or rate limit)
#define ENOMEM        12  // Out of memory
#define EACCES        13  // Permission denied (system resource)
#define EFAULT        14  // Bad address
#define EBUSY         16  // Busy, or owned by another app
#define EEXIST        17  // File exists
#define ENODEV        19  // No such device
#define ENOTDIR       20  // Not a directory
#define EISDIR        21  // Is a directory
#define EINVAL        22  // Invalid argument
#define EMFILE        24  // Too many open files
#define ENOSPC        28  // No space left (or quota reached)
#define ENOMSG        35  // No message available
#define ENODATA       61  // No data available
#define ENOSYS        88  // Not implemented
#define ENOTEMPTY     90  // Directory not empty
#define ENAMETOOLONG  91  // Name or key too long
#define ETIMEDOUT     116 // Timed out (earlier SDK headers wrongly said 110)
#define EALREADY      120 // Already in progress
#define EMSGSIZE      122 // Message too long
#define ENOTCONN      128 // Not connected
#define ENOTSUP       134 // Not supported
#define EOVERFLOW     139 // Value too large
#define ECANCELED     140 // Canceled: the app is being stopped
```

### Manifest Capabilities

| Capability | Required for |
|------------|-------------|
| `display.write` | All display functions |
| `input.read` | Button and touch input events |
| `gpio.read` | `gpio_read()`, `gpio_configure()` |
| `gpio.write` | `gpio_write()`, `gpio_configure()` |
| `sensor.read` | `sensor_read()` |
| `timer` | `timer_*()` |
| `ble` | All `ble_*()` |
| `hid` | All `hid_*()` |
| `storage.read` | `storage_open(O_READ)`, `storage_list()` |
| `storage.write` | `storage_open(O_WRITE/APPEND)`, `storage_delete()` |
| `network.*` | All `net_*()` |
| `ipc` | All `msg_*()` |
| `app.control` | `app_start()`, `app_stop()`, `app_check_update()`, `app_request_update()`, `app_install_from_sd()`, `sd_scan_wasm()`; also accepted for `app_get_status()`, `app_list()`, `app_get_self_name()` and `app_switch()` |
| `app.info` | `app_get_status()`, `app_list()`, `app_get_self_name()` |
| `app.switch` | `app_switch()` (or `app.control`) |
| `memory` | `mem_alloc()`, `mem_free()` |
| `rf.transceive` | All `rf_*()` |
| `uart` | All `uart_*()` |
| `i2c` | All `i2c_*()` |
| `pwm` | All `pwm_*()` |
| `power.read` | `power_get_*()` |
| `power.control` | `power_set_*()`, `power_wake_*()` |

---

[Back to Top](#akira-sdk-api-reference)
