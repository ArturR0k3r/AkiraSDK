# Akira SDK Troubleshooting Guide

Common issues and solutions when building and running WASM apps on AkiraOS.

---

## Table of Contents

- [Build Errors](#build-errors)
- [Display Issues](#display-issues)
- [Sensor Problems](#sensor-problems)
- [GPIO Issues](#gpio-issues)
- [BLE Issues](#ble-issues)
- [HID Issues](#hid-issues)
- [Storage Problems](#storage-problems)
- [Network Issues](#network-issues)
- [Memory Problems](#memory-problems)
- [AkiraOS 1.7 Limits](#akiraos-17-limits)
- [General Debugging](#general-debugging)

---

## Build Errors

### "Undefined reference to `display_clear`" (or any API function)

**Cause:** Missing `-nostdlib` or incorrect include path.

**Fix:**
```bash
# Correct flags:
$(WASI_SDK)/bin/clang \
    -nostdlib \
    -Wl,--no-entry \
    -Wl,--export=main \
    -Wl,--allow-undefined \
    -I../../include \
    -o app.wasm main.c
```

All `extern` functions in `akira_api.h` are provided by the AkiraOS runtime at link time via `--allow-undefined`. Do not add any `.c` runtime files to the build.

### "region `dram0_0_seg' overflowed" or binary > 64 KB

**Cause:** App is too large. WASM linear memory is limited to one 64 KB page.

**Fix:**
- Use `-Os` (optimize for size) instead of `-O0`
- Avoid floating-point where possible — FP pulls in significant polyfill code
- Reduce static buffer sizes
- Split functionality across multiple smaller apps

### "failed to link import function (env, printf_native)"

**Cause:** Including `<stdio.h>` brings in WASI imports that the runtime cannot satisfy.

**Fix:** Never include standard library headers. Use the `printf()` defined in `akira_api.h`:

```c
// WRONG
#include <stdio.h>
printf("value: %d\n", val);

// CORRECT
#include "akira_api.h"
printf("value: %d", val);  // akira_api.h provides printf()
```

### "allocate linear memory failed" on device

**Cause:** WAMR heap too small or PSRAM not configured on ESP32-S3.

**Fix:** Check that `CONFIG_MEMC=y` is set and the PSRAM heap is large enough. Reduce `memory_quota` in the app manifest if needed.

---

## Display Issues

### Screen stays black after drawing

**Cause:** `display_flush()` was not called.

**Fix:** Always call `display_flush()` after composing a frame:

```c
display_clear(COLOR_BLACK);
display_text(10, 10, "Hello", COLOR_WHITE);
display_flush();  // required!
```

### Text appears corrupted or partially drawn

**Cause 1:** Drawing outside screen bounds.

```c
// Check dimensions first
int32_t w, h;
display_get_size(&w, &h);
if (x >= 0 && x < w && y >= 0 && y < h) {
    display_text(x, y, text, color);
}
```

**Cause 2:** Passing a NULL or non-null-terminated string.

```c
if (text && text[0] != '\0') {
    display_text(x, y, text, color);
}
```

### Screen flickers

**Cause:** Flushing after every draw call instead of once per frame.

**Fix:** Compose the full frame, then flush once:

```c
// BAD
display_rect(0, 0, 100, 10, COLOR_RED);   display_flush();
display_text(5, 0, "label", COLOR_WHITE); display_flush();

// GOOD
display_rect(0, 0, 100, 10, COLOR_RED);
display_text(5, 0, "label", COLOR_WHITE);
display_flush();
```

### Colors look wrong

**Cause:** Confusion between RGB888 and RGB565 format. AkiraOS uses RGB565.

```c
// RGB565 layout: RRRRRGGGGGGBBBBB
#define COLOR_RED    0xF800   // R=31, G=0,  B=0
#define COLOR_GREEN  0x07E0   // R=0,  G=63, B=0
#define COLOR_BLUE   0x001F   // R=0,  G=0,  B=31

// Predefined constants are in akira_api.h
```

---

## Sensor Problems

### `sensor_read()` returns `AKIRA_SENSOR_ERROR`

**Cause 1:** Sensor not present or not enabled in device tree.

**Fix:** Check that the hardware is connected and the overlay enables the sensor. Test with a known sensor first (e.g. `SENSOR_CHAN_AMBIENT_TEMP` on boards with an onboard thermometer).

**Cause 2:** Missing `sensor.read` capability in manifest.

**Fix:** Add it to `manifest.json`:
```json
{
  "capabilities": ["sensor.read"]
}
```

**Cause 3:** Reading too soon after boot — some sensors need warm-up time.

**Fix:** Add `delay(500000)` (500 ms) before the first read.

### Sensor values seem constant or unrealistic

**Cause:** Not dividing by 1000. `sensor_read()` returns the value scaled by 1000.

```c
// WRONG — treating raw units as physical
int temp = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
printf("Temp %d C", temp);  // prints 23500, not 23

// CORRECT — integer maths, no FPU needed
int raw = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
if (raw != AKIRA_SENSOR_ERROR) {
    printf("Temp %d.%d C", raw / 1000, (raw % 1000) / 100);
}
```

---

## GPIO Issues

### `gpio_read()` always returns 0 or stuck

**Cause 1:** Pin not configured before reading.

**Fix:** Call `gpio_configure()` first:
```c
gpio_configure(BTN_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
```

**Cause 2:** Wrong pull direction — button wiring may expect pull-up.

```c
// Active-high button (button connects pin to VCC)
gpio_configure(BTN_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
// button pressed → pin = 1

// Active-low button (button connects pin to GND)
gpio_configure(BTN_PIN, GPIO_INPUT | GPIO_PULL_UP);
// button pressed → pin = 0
```

**Cause 3:** Missing `gpio.read` capability in manifest.

### `gpio_write()` has no effect

**Cause:** Pin not configured as output or missing `gpio.write` capability.

```c
gpio_configure(LED_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
// Then:
gpio_write(LED_PIN, 1);
```

If a GPIO call returns `-EACCES`, `-EPERM` or `-EBUSY`, see [`gpio_configure()` returns `-EACCES`](#gpio_configure-returns--eacces).

---

## BLE Issues

### `ble_init()` returns `-EBUSY`

**Cause 1:** HID mode is already active on the BLE stack.

**Fix:** Only one BLE mode can be active at a time. Disable HID first (`hid_disable()`) or design the app to use one or the other.

**Cause 2:** Another app owns the app BLE layer. The first app that sets up BLE owns it until it calls `ble_deinit()` or exits.

**Cause 3 (other BLE calls):** a rate limit. `ble_advertise()`/`ble_stop_advertise()` allow 8 calls at once then 4 per second, `ble_add_service()` one per 2 s after the first few, `ble_set_local_name()` one rename per 2 s.

### No `BLE_EVT_CONNECTED` event

**Cause 1:** `ble_advertise()` not called after `ble_init()`.

```c
ble_init();
ble_advertise();  // required
```

**Cause 2:** Event loop not running — `ble_event_pop()` must be called repeatedly.

```c
while (1) {
    int e = ble_event_pop(evt_buf, sizeof(evt_buf));
    if (e == BLE_EVT_CONNECTED) { /* ... */ }
    delay(10000);
}
```

**Cause 3:** App stopped advertising after a previous disconnect.

**Fix:** Re-advertise on disconnect:
```c
if (e == BLE_EVT_DISCONNECTED) {
    ble_advertise();
}
```

### Characteristic writes not received

**Cause:** Characteristic not created with `BLE_PROP_WRITE` or not added to the service before `ble_add_service()`.

```c
int ch = ble_char_create(UUID, BLE_PROP_READ | BLE_PROP_WRITE, 20);
ble_service_add_char(svc, ch);
ble_add_service(svc);  // must call AFTER adding all chars
```

---

## HID Issues

### `hid_type_string()` / key presses have no effect

**Cause 1:** `hid_init()` not called, or wrong transport.

```c
hid_init(HID_TRANSPORT_BLE, HID_DEVICE_KEYBOARD);
// wait for connection:
while (!hid_is_connected()) delay(100000);
```

**Cause 2:** Missing `hid` capability in manifest.

**Cause 3:** BLE HID requires a paired host — make sure the device is paired and the HID profile is active.

### Named shortcut does nothing

**Cause:** `hid_action_register()` must be called before `hid_action_trigger()`.

```c
hid_action_register("screenshot", HID_MOD_LEFT_GUI, HID_KEY_PRTSCN);
// later:
hid_action_trigger("screenshot");
```

---

## Storage Problems

### `storage_open()` returns a negative error code

**Cause 1:** File does not exist and `STORAGE_O_READ` was used.

```c
// Check with STORAGE_O_WRITE first if file may not exist
int fd = storage_open("config.txt", STORAGE_O_WRITE);
```

**Cause 2:** Missing `storage.read` / `storage.write` capability.

**Cause 3:** Path contains `..` — rejected for security.

```c
// WRONG
storage_open("../other_app/secret.txt", STORAGE_O_READ);  // -EACCES

// CORRECT — relative paths only
storage_open("config.txt", STORAGE_O_READ);
storage_open("logs/app.log", STORAGE_O_APPEND);
```

**Cause 4:** `-EMFILE` (too many open files) or `-ENOSPC` (volume reserve). See [AkiraOS 1.7 Limits](#akiraos-17-limits).

### `storage_write()` returns `-ENOSPC`

**Cause:** The file would grow past the per-file quota (256 KB by default), or the write would leave less than a quarter of the volume free. See [`fs_*` or `storage_*` returns `-ENOSPC` with free space left](#fs_-or-storage_-returns--enospc-with-free-space-left).

**Fix:** Delete old files, or split data across several files:
```c
storage_delete("old_data.bin");
storage_delete("logs/app.log");  // implement log rotation
```

### Forgetting to close a file descriptor

Always close after use — an app may hold only 2 open files (storage and FS APIs each), and all apps together leave the OS two filesystem slots. The next open returns `-EMFILE`. Files still open when the app exits are closed for it:

```c
int fd = storage_open("x.txt", STORAGE_O_READ);
if (fd >= 0) {
    int n = storage_read(fd, buf, sizeof(buf));
    storage_close(fd);   // always close
}
```

---

## Network Issues

### `net_connect()` never produces `NET_EVT_CONNECTED`

**Cause 1:** Not polling `net_event_pop()` in the loop.

**Cause 2:** DNS resolution failed (no network). Check that the device is connected to Wi-Fi first.

**Cause 3:** RX/TX ring buffers not bound before connecting.

```c
int h = net_open(NET_TYPE_TCP);
net_tx_bind(h, tx_buf, sizeof(tx_buf));   // must bind before connecting
net_rx_bind(h, rx_buf, sizeof(rx_buf));
net_connect(h, "example.com", 80);
```

**Cause 4:** `net_tx_bind()` / `net_rx_bind()` returned `-ENOTSUP`: the app's linear memory can grow. Link with `-Wl,--initial-memory=N -Wl,--max-memory=N` (the same N).

**Cause 5:** `net_connect()` returned `-EACCES`: the firmware's egress policy refused the host or port.

### Data sent but never received on the other end

**Cause:** `net_tx_flush()` not called after writing to the ring.

```c
net_ring_write(tx_buf, sizeof(tx_buf), (uint8_t*)"ping", 4);
net_tx_flush(h);   // required to push data to network stack
```

### `net_ring_write()` returns `-1`

**Cause:** TX ring is full — previous messages not flushed yet.

**Fix:** Call `net_tx_flush(h)` before writing the next message, or increase the ring buffer size.

---

## Memory Problems

### App crashes or behaves erratically

**Likely cause:** Stack overflow from large on-stack allocations.

```c
// BAD — almost the entire 4 KB stack
void process(void) {
    uint8_t frame[3000];
}

// GOOD — static, off-stack
static uint8_t frame[3000];
void process(void) {
    // use frame
}
```

### `mem_alloc()` returns 0

**Cause 1:** WASM heap exhausted. The entire memory budget is 64 KB (or `memory_quota` in the manifest), shared by stack, static data, and heap.

**Cause 2:** The manifest lacks the `memory` capability.

**Fix:** Use static buffers and reduce heap allocations. Everything the runtime allocates for one app is also capped at `CONFIG_AKIRA_WASM_APP_MEM_LIMIT_KB` (2048 KB with PSRAM, 1024 KB without); past that, `memory.grow` fails and the log shows "hit its N KB memory limit".

---

## AkiraOS 1.7 Limits

AkiraOS 1.7 applies fixed limits to every app. The full list is in [API_REFERENCE.md](API_REFERENCE.md#limits-and-error-codes-akiraos-17).

### Settings I saved before 1.7 are gone

**Cause:** Each app now has a private settings namespace bound to its exact name. Keys saved under the old global names (for example `"nes/frameskip"` written by an older firmware) are no longer visible to apps, and OS keys such as Wi-Fi credentials can't be read at all.

**Fix:** Save the values again with `settings_set()`. Keep keys to 26 characters (`-ENAMETOOLONG`) and values to 63 bytes (`-E2BIG`) with the default configuration. An app has 40 keys (`-ENOSPC`), and writes are limited to 40 at once, then one every 2 s (`-EAGAIN`): don't save state on every frame.

### `gpio_configure()` returns `-EACCES`

**Cause:** The pin is a system pin (used by a devicetree node, chip-reserved, or a strapping pin) or not in the board's `CONFIG_AKIRA_WASM_GPIO_APP_PINS`. On AkiraConsole Prod apps get GPIO39 and GPIO40 only.

**Related:** `-EBUSY` means another app configured the pin; `-EPERM` from `gpio_read()`/`gpio_write()` means this app didn't configure it first. The same kind of rule refuses system I2C addresses, ADC channels, UARTs and PWM channels with `-EACCES`.

### `fs_*` or `storage_*` returns `-ENOSPC` with free space left

**Cause 1:** A write, create or mkdir would leave less than a quarter of the volume free (at least 64 KB, at most 8 MB is kept for the OS and installed apps).

**Cause 2:** The file would grow past the per-file quota, 256 KB by default (`CONFIG_AKIRA_WASM_FS_QUOTA_BYTES` / `CONFIG_AKIRA_WASM_STORAGE_QUOTA_BYTES`). For the FS API, seeking past the end of a file open for writing counts too.

**Fix:** Keep files small, rotate logs, delete what you no longer need.

### `fs_open()` / `storage_open()` returns `-EMFILE`

**Cause:** The app already has 2 files open, or all apps together hold every file slot apps may use. Close files as soon as you're done.

### My app gets `-EINTR` or `-ECANCELED`

**Cause:** The app is being stopped (`app stop`, an app switch, or another app's `app_stop()`). Waiting calls end early so the stop doesn't have to wait for them: `msg_recv()`, `app_start()`, `app_stop()` and `app_install_from_sd()` return `-EINTR`; settings, RF, mesh, MQTT, BLE and network calls return `-ECANCELED`. `delay()` returns early too, but still returns `0`.

**Fix:** Treat these codes as "exit now": clean up and return from `main()`. An app still running WASM code 1.5 s after the stop is aborted.

### `printf()` output is missing

**Cause:** App output is rate-limited per app: a 2 KB burst, then 1 KB/s, each line costing its length plus 32 bytes. Dropped lines show up as `N line(s) of output dropped (rate limit)`. Lines longer than 160 characters are cut.

**Fix:** Don't print inside tight loops; print on state changes or once per second.

### `power_set_mode()` returns `-ENOTSUP`

**Cause:** Apps can't select light sleep, deep sleep or hibernate. Only `POWER_MODE_ACTIVE` and `POWER_MODE_IDLE` are accepted. `-EAGAIN` means the power-control rate limit (8 calls, then one per 250 ms, shared by all apps).

### `wdt_pet()` no longer keeps the device from resetting

**Cause:** `wdt_pet()` doesn't feed the hardware watchdog any more; it only reports whether one is running. The OS feeds the watchdog itself and apps can't starve it, so a long-running app needs no watchdog calls.

### `app_start()` / `app_stop()` returns `-EAGAIN`, `-EINVAL` or `-ETIMEDOUT`

**Cause:** Lifecycle requests are queued and rate-limited: 4 at once per app, then one per 2 s (`-EAGAIN`). An app can't start, stop or switch to itself (`-EINVAL`) or reinstall itself with `app_install_from_sd()` (`-EBUSY`). The caller waits at most 60 s for the result (`-ETIMEDOUT`; the request may still complete).

---

## General Debugging

### Add progress logging

```c
printf("init done");
// ... code ...
printf("loop start");
```

### Test hardware components in isolation

```c
int main(void) {
    // Test display only
    display_clear(COLOR_RED);
    display_flush();
    delay(1000000);

    // Test sensor only
    int raw = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
    printf("temp raw: %d", raw);

    return 0;
}
```

### Verify sensor channels available on the board

Not every board has every sensor. Use the shell to check:

```
akira> sensor list
```

### Display an error overlay

```c
void show_error(const char *msg) {
    display_rect(0, 0, 320, 20, COLOR_RED);
    display_text(4, 4, msg, COLOR_WHITE);
    display_flush();
    printf("ERROR: %s", msg);
}
```

### Monitor timing

```c
int t = timer_create();
timer_start(t);
do_something();
printf("elapsed: %d ms", timer_elapsed(t));
timer_free(t);
```

### Debugging checklist

- [ ] Logs visible on the host console (`printf()`)
- [ ] All return values of host calls checked
- [ ] `display_flush()` called after composing each frame
- [ ] `sensor_read()` result compared against `AKIRA_SENSOR_ERROR`
- [ ] `gpio_configure()` called before `gpio_read()`/`gpio_write()`
- [ ] All file descriptors closed after use
- [ ] Manifest `capabilities` list matches every API used
- [ ] `delay()` present in all polling loops

---

## AOT Compilation Issues

### `wamrc: command not found`

The `wamrc` binary hasn't been built yet. The AkiraOS WAMR submodule ships
with pre-configured CMake build directories — you just need to compile:

```bash
# Step 1 — build the bundled LLVM with Xtensa backend (one-time, ~5–15 min)
cd AkiraOS/modules/wasm-micro-runtime/core/deps/llvm/build
ninja -j$(nproc)

# Step 2 — build wamrc
cd AkiraOS/modules/wasm-micro-runtime/wamr-compiler/build
ninja -j$(nproc)
```

`rom_to_aot.py` and the `wasm_apps` Makefile auto-detect `wamrc` in that
build directory, so no installation is required. If you prefer it on your
PATH:

```bash
# Option A: install system-wide
sudo cp wamrc /usr/local/bin/

# Option B: env var (per-session or add to ~/.bashrc)
export WAMRC=$(pwd)/wamrc
```

> **Do not** run `cmake .` from the `wamr-compiler` source directory — that
> would create an in-source build unconfigured for Xtensa. Always use the
> existing `wamr-compiler/build` directory.

### `wamrc: unsupported target 'xtensa'`

The locally-built `wamrc` wasn't compiled with Xtensa LLVM backend support. The
AkiraOS WAMR fork (`ArturR0k3r/wasm-micro-runtime`, branch `AkiraOS_Patch`)
includes the Xtensa backend. Make sure you're building from that submodule, not
a generic WAMR checkout.

### `.aot` file fails to load on device

**Possible causes:**

| Symptom | Cause | Fix |
|---------|-------|-----|
| "invalid AOT file" | Wrong target arch | Rebuild with correct `AOT_TARGET` |
| "incompatible version" | wamrc/runtime version mismatch | Rebuild wamrc from same WAMR commit as AkiraOS firmware |
| "failed to instantiate" | Unsupported AOT feature | Use `.wasm` fallback; file an issue |

Always verify which WAMR commit AkiraOS is built against and use the `wamrc`
from that same commit:

```bash
cd modules/wasm-micro-runtime
git log --oneline -1   # shows the commit
```

If the log says `AOT binary refused: '<app>' is not attested`, the firmware was built with `CONFIG_AKIRA_REFUSE_UNATTESTED_AOT=y` and runs AOT only for attested apps: install a signed package (`akira-cli sign`, verified against the firmware's `CONFIG_AKIRA_APP_PUBKEY`) or the `.wasm` build.

### AOT app logs "is unattested AOT native code"

This is a warning, not an error: the app still runs. An `.aot` file is native code, and the WASM sandbox only confines it if `wamrc` built it from a source you trust; the loader can't tell. AkiraOS loads unattested AOT apps with this warning and an audit record, so installing one is your choice and risk. To get rid of the warning, install the app as a package signed with `akira-cli sign` for a key the firmware trusts (`CONFIG_AKIRA_APP_PUBKEY`; it is then attested), or use the `.wasm` build.

### AOT binary is larger than .wasm

This is expected. AOT binaries contain native machine code (typically 2–5× the
`.wasm` size) but execute without interpreter overhead.

Use `--size-level=1` and `--opt-level=3` (both set by default in `build.sh` and
the Makefile) to balance size vs speed.

### App works in .wasm but crashes in .aot

The AOT-compiled binary runs native code, so memory-safety bugs that were masked
by the interpreter (e.g., out-of-bounds static arrays, stack overflow) may crash
differently. Debug with the `.wasm` version first, then test the `.aot`.

**Common fix:** increase `-z stack-size` if stack-heavy functions segfault only
in AOT mode — but remember the 64 KB total limit.
