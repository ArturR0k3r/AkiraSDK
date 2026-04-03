# Akira SDK Best Practices

Patterns and guidelines for writing efficient, reliable WASM apps on AkiraOS.

---

## Table of Contents

- [Main Loop](#main-loop)
- [Memory Management](#memory-management)
- [Display Optimization](#display-optimization)
- [GPIO & Input Polling](#gpio--input-polling)
- [Sensor Reading](#sensor-reading)
- [Error Handling](#error-handling)
- [Power Efficiency](#power-efficiency)
- [Code Organization](#code-organization)

---

## Main Loop

AkiraOS WASM apps run as a single `main()` function. There is no event dispatcher — you poll hardware directly using `delay()` to yield between iterations.

### DO: Keep the main loop simple

```c
#include "akira_api.h"

int main(void) {
    setup();

    while (1) {
        poll_buttons();
        update_display();
        delay(16000);  // ~60 fps
    }
    return 0;
}
```

### DO: Use a timer handle for periodic tasks

```c
int t = timer_create();
timer_start(t);

while (1) {
    if (timer_elapsed(t) >= 1000) {
        timer_start(t);       // reset
        read_and_log_sensors();
    }
    delay(10000);  // yield 10 ms between polls
}
```

### DON'T: Busy-spin without yielding

```c
// BAD — burns 100% CPU
while (1) {
    check_buttons();
}

// GOOD — yields to the scheduler
while (1) {
    check_buttons();
    delay(5000);  // 5 ms
}
```

---

## Memory Management

### DO: Use static buffers for fixed-size data

```c
static char log_buf[256];
static uint16_t frame[320 * 240];  // full framebuffer if needed
```

Static allocation has no heap fragmentation and no allocation failures.

### DON'T: Use large on-stack buffers

```c
// BAD — may overflow WASM stack (4KB default)
void process(void) {
    char buf[4096];  // almost the entire stack!
}

// GOOD — static
static char buf[4096];
void process(void) {
    // use buf
}
```

### DO: Check `mem_alloc()` results

```c
uint32_t ptr = mem_alloc(1024);
if (ptr == 0) {
    printf("alloc failed");
    return;
}
// use ptr...
mem_free(ptr);
```

---

## Display Optimization

### DO: Batch draw calls and flush once per frame

```c
// GOOD — single flush
void draw_frame(void) {
    display_clear(COLOR_BLACK);
    display_text(10, 10, "Line 1", COLOR_WHITE);
    display_text(10, 28, "Line 2", COLOR_WHITE);
    display_text(10, 46, "Line 3", COLOR_WHITE);
    display_flush();  // push everything at once
}
```

### DON'T: Flush after every draw call

```c
// BAD — 3x the work
display_text(10, 10, "Line 1", COLOR_WHITE);
display_flush();
display_text(10, 28, "Line 2", COLOR_WHITE);
display_flush();
```

### DO: Only redraw changed regions

```c
static int last_val = -1;

void update_counter(int val) {
    if (val == last_val) return;
    last_val = val;

    display_rect(100, 50, 80, 20, COLOR_BLACK);  // erase old
    display_number(100, 50, val, COLOR_WHITE);
    display_flush();
}
```

### DO: Limit frame rate

```c
while (1) {
    render_frame();
    delay(33000);  // ~30 fps
}
```

### DO: Define your colour palette up top

```c
#define C_BG     COLOR_BLACK
#define C_TEXT   COLOR_WHITE
#define C_OK     COLOR_GREEN
#define C_ERR    COLOR_RED
#define C_WARN   COLOR_YELLOW
```

---

## GPIO & Input Polling

### DO: Detect rising edges for button presses

```c
static int prev_btn = 0;

void poll_button(void) {
    int cur = gpio_read(BTN_PIN) == 1;
    if (cur && !prev_btn) {
        on_button_press();   // fires once on press
    }
    prev_btn = cur;
}
```

### DO: Configure pins correctly before reading

```c
void gpio_init(void) {
    gpio_configure(LED_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
    gpio_configure(BTN_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
}
```

### DO: Debounce if needed

```c
static int debounce_ms = 0;

void poll_button_debounced(int timer_h) {
    int cur = gpio_read(BTN_PIN) == 1;
    if (cur && debounce_ms == 0) {
        on_button_press();
        debounce_ms = 50;
    }
    if (debounce_ms > 0) {
        // decrement per poll (call every 10 ms → 5 polls = 50 ms)
        debounce_ms -= 10;
        if (debounce_ms < 0) debounce_ms = 0;
    }
}
```

---

## Sensor Reading

### DO: Always guard against `AKIRA_SENSOR_ERROR`

```c
int raw = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
if (raw == AKIRA_SENSOR_ERROR) {
    printf("sensor unavailable");
    return;
}
// raw = 23500 → 23.5 °C; use integer maths:
int whole = raw / 1000;
int frac  = (raw % 1000) / 100;  // one decimal place
printf("Temp: %d.%d C", whole, frac);
```

### DON'T: Use the raw reading without checking

```c
// BAD — raw might be INT32_MIN on failure
int raw = sensor_read(SENSOR_CHAN_ACCEL_X);
do_physics(raw);   // undefined behaviour if raw == AKIRA_SENSOR_ERROR
```

### DO: Throttle sensor polls

```c
#define SENSOR_INTERVAL_MS 500

int t = timer_create();
timer_start(t);

while (1) {
    if (timer_elapsed(t) >= SENSOR_INTERVAL_MS) {
        timer_start(t);
        int raw = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
        if (raw != AKIRA_SENSOR_ERROR) update_display(raw);
    }
    delay(10000);
}
```

---

## Error Handling

### DO: Check return values of host calls

```c
int fd = storage_open("config.txt", STORAGE_O_READ);
if (fd < 0) {
    printf("open failed: %d", fd);
    return;
}
int n = storage_read(fd, buf, sizeof(buf) - 1);
storage_close(fd);
```

### DO: Provide user feedback on errors

```c
void show_error(const char *msg) {
    display_clear(COLOR_BLACK);
    display_text(10, 10, "Error", COLOR_RED);
    display_text(10, 30, msg, COLOR_WHITE);
    display_flush();
    printf("ERR: %s", msg);
}
```

### DO: Retry transient failures

```c
int read_temp_stable(void) {
    for (int i = 0; i < 3; i++) {
        int v = sensor_read(SENSOR_CHAN_AMBIENT_TEMP);
        if (v != AKIRA_SENSOR_ERROR) return v;
        delay(200000);  // wait 200 ms before retry
    }
    return AKIRA_SENSOR_ERROR;
}
```

### DO: Validate pointer parameters

```c
void process(const char *data, int len) {
    if (!data || len <= 0 || len > MAX_LEN) {
        printf("invalid params");
        return;
    }
    // safe to use
}
```

---

## Power Efficiency

### DO: Sleep when idle

```c
while (1) {
    if (has_work()) {
        do_work();
    } else {
        delay(100000);  // 100 ms idle sleep
    }
}
```

### DO: Reduce sensor poll rate on battery

```c
int pct = power_get_battery_level();
int interval_ms = (pct < 20) ? 5000 : 1000;  // slower on low battery
```

### DO: Power down unused peripherals

```c
// Disable RF if not needed
// Disable BLE advertising when not expecting connections
ble_stop_advertise();
```

### DO: Use deep sleep for long waits

```c
power_wake_on_timer(30000);           // wake after 30 s
power_set_mode(POWER_MODE_DEEP_SLEEP);
// execution resumes here after wake
```

---

## Code Organization

### DO: Use constants instead of magic numbers

```c
// GOOD
#define UPDATE_INTERVAL_MS  1000
#define TEMP_WARN_THRESHOLD 3000  // 30.0 °C * 1000
#define LED_PIN             48

// BAD
if (sensor_read(13) > 3000)
    gpio_write(48, 1);
```

### DO: Group related functionality

```c
// sensors.c
void sensors_init(void) { /* ... */ }
int  sensors_read_temp(void) { return sensor_read(SENSOR_CHAN_AMBIENT_TEMP); }

// ui.c
void ui_init(void) { /* ... */ }
void ui_update(int temp_raw) { /* ... */ }

// main.c
int main(void) {
    sensors_init();
    ui_init();
    while (1) {
        ui_update(sensors_read_temp());
        delay(16000);
    }
    return 0;
}
```

### DO: Comment non-obvious logic

```c
// Sensor returns value * 1000; convert to integer degrees
// e.g. 23456 → 23 °C (truncate, not round)
int deg_c = raw / 1000;
```

### DO: Free and close resources

```c
// Timers
timer_stop(t);
timer_free(t);

// Storage
storage_close(fd);

// BLE
ble_stop_advertise();
ble_deinit();

// UART
uart_close(uart_h);
```

---

## Quick Checklist

Before deploying an app, verify:

- [ ] `main()` has an event / poll loop with `delay()`
- [ ] All `sensor_read()` calls check for `AKIRA_SENSOR_ERROR`
- [ ] All host-call return values are checked where failure matters
- [ ] `display_flush()` called once per frame (not after every draw)
- [ ] GPIO pins configured before reading/writing
- [ ] No large on-stack buffers (prefer `static`)
- [ ] Resources closed/freed on exit paths
- [ ] Constants used instead of magic numbers
- [ ] Manifest `capabilities` list is complete

---

## AOT Compilation

### When to use AOT

Use AOT-compiled (`.aot`) binaries instead of bytecode (`.wasm`) when:

- **frame rate matters** — 3D apps (cube3d, imu_3d), games (tetris), animations
- **math-heavy inner loops** — trigonometry, matrix maths, signal processing
- **battery life is a concern** — fewer CPU cycles per instruction
- **final production firmware** — ship fast binaries to end users

Keep using `.wasm` during development (faster iteration, architecture-independent).

### AOT vs interpreter

| Concern | `.wasm` | `.aot` |
|---------|---------|--------|
| Build time | Fast | Adds wamrc step |
| Debug iteration | Best | Rebuild .wasm first, then re-AOT |
| Target portability | Single binary runs everywhere | One `.aot` per CPU |
| Execution speed | Baseline | 10–50× faster |
| File per deployment | `app.wasm` | `app-xtensa.aot` (board-specific) |

### AOT workflow

```bash
# Step 1 — build .wasm (standard flow)
cd wasm_apps
./build.sh                      # all apps → bin/*.wasm

# Step 2 — AOT-compile to native binary
./build.sh aot                  # ESP32-S3 (default xtensa)
./build.sh aot thumb            # nRF54L15 (Cortex-M33)
./build.sh aot thumbv7em        # STM32 (Cortex-M7)
./build.sh aot riscv32          # ESP32-C3 (RISC-V 32)

# Output: bin/<app>-<target>.aot
```

### Building wamrc (one-time setup)

The AkiraOS WAMR submodule ships with pre-configured CMake build directories.
Do **not** run `cmake` yourself — just compile with ninja:

```bash
# 1. Build bundled LLVM with Xtensa backend (~5–15 min)
cd AkiraOS/modules/wasm-micro-runtime/core/deps/llvm/build
ninja -j$(nproc)

# 2. Build wamrc
cd AkiraOS/modules/wasm-micro-runtime/wamr-compiler/build
ninja -j$(nproc)

# Optional: make available system-wide
sudo cp wamrc /usr/local/bin/
# or add to your shell: export WAMRC=/path/to/wamr-compiler/build/wamrc
```

The build tools auto-detect `wamrc` in the build directory, so the install
step is optional.

### Performance pattern for AOT apps

The same source code runs in both modes. There is nothing to change in `main.c`
to benefit from AOT — just upload the `.aot` file instead of `.wasm`. The WAMR
runtime detects the file format at load time.

For maximum AOT benefit, structure compute-heavy work in tight loops:

```c
// Good: tight inner loop — AOT eliminates interpreter overhead entirely
static void matrix_multiply(float a[9], float b[9], float out[9]) {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            out[i*3+j] = 0;
            for (int k = 0; k < 3; k++)
                out[i*3+j] += a[i*3+k] * b[k*3+j];
        }
}
```
