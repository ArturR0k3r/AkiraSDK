

# Akira SDK

<div align="center">

![Akira SDK](https://img.shields.io/badge/Akira-SDK-blue?style=for-the-badge)
![WASM](https://img.shields.io/badge/WASM-Ready-purple?style=for-the-badge)
![IoT](https://img.shields.io/badge/IoT-Powered-green?style=for-the-badge)

**Build embedded WebAssembly applications for AkiraOS**

[API Reference](docs/API_REFERENCE.md) • [Best Practices](docs/BEST_PRACTICES.md) • [Troubleshooting](docs/TROUBLESHOOTING.md) • [Sample Apps](wasm_apps/)

</div>

---

## What is Akira SDK?

Akira SDK provides the C header (`include/akira_api.h`) and sample WASM apps for building applications on AkiraOS. Apps compile to a standalone `.wasm` binary and run inside the WAMR runtime with access to display, GPIO, sensors, BLE, HID, networking, and more — all in 64 KB of linear memory.

### Features

- **Display** — RGB565 graphics: shapes, text (two sizes), bitmaps, progress bars, rounded rects
- **GPIO** — Simple read/write with configurable pull-ups/pull-downs
- **Sensors** — IMU, temperature, pressure, humidity, magnetometer, power monitoring
- **Timers** — Polling timers with elapsed-time queries
- **BLE** — Arduino-style GATT server with event-loop API
- **HID** — Keyboard, mouse, gamepad, and consumer (media) keys over BLE or USB
- **Storage** — Sandboxed per-app file system with FD-based API
- **Networking** — Async TCP/UDP via shared-memory ring buffers
- **IPC** — In-process pub/sub messaging between apps
- **UART / I2C / PWM** — Low-level peripheral access
- **Power Management** — Battery status, sleep modes, wake sources
- **Capability-based security** — Every API group requires an explicit manifest capability

---

## Repository Structure

```
AkiraSDK/
├── include/
│   └── akira_api.h        # All API declarations — include this in your app
├── scripts/
│   └── embed_manifest.py  # Embeds manifest.json into the WASM binary
├── wasm_apps/
│   ├── build.sh           # Build all apps (or a single named app)
│   ├── Makefile           # Master Makefile (make / make clean / make install)
│   ├── akiraconsole/      # Sample applications (display_test, compass, cube3d, etc.)
│   ├── hello_world/       # Minimal printf example
│   ├── ble_led/           # BLE GATT LED control
│   ├── macro_pad/         # 5-button HID macro pad
│   ├── imu_3d/            # 3D orientation visualiser (display + sensor)
│   ├── cube3d/            # 3D wireframe cube
│   ├── compass/           # Magnetometer compass
│   ├── display_test/      # Display primitives test
│   ├── gpio/              # GPIO read/write demo
│   ├── imu_timer_test/    # IMU + timer demo
│   ├── inclinometer/      # Tilt-angle display
│   ├── logic_analyzer/    # GPIO logic analyser
│   ├── net_echo/          # TCP echo client
│   ├── net_server/        # TCP echo server
│   ├── storage_test/      # Storage read/write/list demo
│   ├── supervisor/        # App launcher / supervisor UI
│   └── tetris/            # Tetris game
└── docs/
    ├── API_REFERENCE.md   # Complete API documentation
    ├── BEST_PRACTICES.md  # Patterns and guidelines
    └── TROUBLESHOOTING.md # Common issues and solutions
```

---

## Architecture

```
┌──────────────────────────────┐
│      Your WASM Application   │
│        (main.c)              │
└────────────┬─────────────────┘
             │  #include "akira_api.h"
             │  extern int display_rect(...);
             │  extern int sensor_read(...);
             ▼
┌──────────────────────────────┐
│   AkiraOS WAMR Runtime       │
│  (resolves extern symbols    │
│   from the env module)       │
└────────────┬─────────────────┘
             ▼
┌──────────────────────────────┐
│   AkiraOS HAL / Zephyr RTOS  │
│  GPIO │ Display │ BLE │ Net  │
└──────────────────────────────┘
```

Apps are **header-only consumers** — `akira_api.h` declares all functions as `extern`. The runtime provides them at execution time. Do not link any C implementation files.

---

## Getting Started

### Prerequisites

- [WASI SDK](https://github.com/WebAssembly/wasi-sdk/releases) installed at `/opt/wasi-sdk` (or set `WASI_SDK`)
- AkiraOS-compatible hardware (or `native_sim` for host testing)

### Quick install of WASI SDK

```bash
WASI_VERSION=24
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_VERSION}/wasi-sdk-${WASI_VERSION}.0-x86_64-linux.tar.gz
sudo tar xvf wasi-sdk-${WASI_VERSION}.0-x86_64-linux.tar.gz -C /opt
sudo ln -sf /opt/wasi-sdk-${WASI_VERSION}.0 /opt/wasi-sdk
```

### Hello World

```c
// main.c
#include "akira_api.h"

int main(void) {
    printf("Hello from AkiraOS WASM!");
    return 0;
}
```

```bash
cd wasm_apps/hello_world
make
# produces hello_world.wasm
```

### Minimal manifest

```json
{
  "name": "hello_world",
  "version": "1.0.0",
  "capabilities": [],
  "memory_quota": 65536
}
```

The manifest is embedded into the WASM binary by `scripts/embed_manifest.py` (called automatically by each app's Makefile).

---

## Building Apps

### Build all apps

```bash
cd wasm_apps
./build.sh            # or: make
```

### Build a specific app

```bash
cd wasm_apps
./build.sh hello_world
# or:
make -C hello_world
```

### Build flags

All apps use these standard flags:

```
-nostdlib                    # No standard library / no WASI imports
-Wl,--no-entry               # No _start entry point
-Wl,--export=main            # export main() as WASM entry
-Wl,--allow-undefined        # Allow env module imports from runtime
-Wl,--strip-all              # Strip symbols for minimal size
-z stack-size=4096           # 4 KB stack
-Wl,--initial-memory=65536   # One 64 KB WASM page
-Wl,--max-memory=65536
-I../../include              # akira_api.h
```

---

## AOT Compilation (Optional)

By default, apps run as **interpreter bytecode** (`.wasm`). AkiraOS also supports
**Ahead-of-Time (AOT) compilation** via `wamrc` — the WAMR AOT compiler — which
cross-compiles a `.wasm` bytecode file into a **native machine-code `.aot` binary**
that executes directly on the target CPU.

### Interpreter vs AOT

| | `.wasm` (Interpreter) | `.aot` (AOT) |
|---|---|---|
| Execution | WAMR interprets bytecode | Native machine code |
| Speed | Baseline | **10–50× faster** |
| Portability | Runs on any AkiraOS target | One `.aot` per architecture |
| Use case | Development, testing, all targets | Compute-heavy apps, games, UI |
| File required | `app.wasm` | `app-<target>.aot` |

### When to use AOT

- **Games / 3D rendering** (cube3d, tetris, imu_3d) — clear benefit from native speed
- **Signal processing / math-heavy apps** — interpreter overhead eliminated
- **Battery-sensitive deployments** — fewer cycles per instruction
- Keep using `.wasm` for development iteration and multi-target deployments

### Supported AOT targets

| Target alias | Architecture | Board |
|---|---|---|
| `xtensa` (default) | Xtensa LX7 | ESP32-S3, Akira Console |
| `thumb` | Cortex-M33 | nRF54L15DK |
| `thumbv7em` | Cortex-M7 | STM32 (`b_u585i_iot02a`, `steval_stwinbx1`) |
| `riscv32` | RISC-V 32-bit | ESP32-C3 |
| `x86_64` | x86-64 | `native_sim` |

### Building wamrc

`wamrc` is part of the WAMR source tree (included as a submodule in AkiraOS).
The submodule ships with pre-configured CMake build directories — compile with
ninja directly:

```bash
# 1. Build bundled LLVM with Xtensa backend (one-time, ~5–15 min)
cd modules/wasm-micro-runtime/core/deps/llvm/build
ninja -j$(nproc)

# 2. Build wamrc
cd modules/wasm-micro-runtime/wamr-compiler/build
ninja -j$(nproc)

# Optional: install to PATH (tools auto-detect wamrc without this)
sudo cp wamrc /usr/local/bin/
# or: export WAMRC=/path/to/wasm-micro-runtime/wamr-compiler/build/wamrc
```

> **Do not** run `cmake .` from the `wamr-compiler` source directory. Use the
> existing `build/` subdirectory which is pre-configured for the Xtensa LLVM
> backend.

### AOT compilation workflow

```bash
# Step 1 — build .wasm normally
cd wasm_apps
./build.sh            # produces bin/*.wasm

# Step 2 — AOT-compile all apps for ESP32-S3 (default target)
./build.sh aot
# produces bin/*-xtensa.aot

# Step 2 — AOT-compile for a specific target
./build.sh aot thumb           # nRF54L15
./build.sh aot thumbv7em       # STM32
./build.sh aot riscv32         # ESP32-C3

# Or with make:
make aot                       # xtensa (default)
make aot-xtensa
make aot-thumb
make aot AOT_TARGET=riscv32
make aot WAMRC=/custom/path/wamrc AOT_TARGET=x86_64
```

### Deploying an AOT binary

Upload the `.aot` file to the AkiraOS storage in place of (or alongside) the
`.wasm` file. AkiraOS/WAMR detects the file type at load time and executes it
as native code. The manifest is still embedded in the same way.

```bash
# Copy to SD card / storage
cp bin/tetris-xtensa.aot /media/$USER/AKIRA/apps/tetris.aot
```

> **Note:** A `.aot` binary is architecture-specific. Do not use an `xtensa.aot`
> on an ARM board — it will fail to load. Keep the `.wasm` as the portable
> fallback for cross-target deployments.

---

## Writing a New App

1. Create a directory under `wasm_apps/my_app/`
2. Write `main.c` — entry point is `int main(void)`
3. Create `manifest.json` with required capabilities
4. Copy a `Makefile` from an existing app and update `APP_NAME`

### Minimal app template

```c
#include "akira_api.h"

int main(void) {
    // Initialize
    display_clear(COLOR_BLACK);
    display_text(10, 10, "My App", COLOR_WHITE);
    display_flush();

    // Poll loop
    int t = timer_create();
    timer_start(t);

    while (1) {
        if (timer_elapsed(t) >= 1000) {
            timer_start(t);
            // do periodic work
        }
        delay(10000);  // yield 10 ms
    }

    return 0;
}
```

### Key rules

- **No `#include <stdio.h>`** or other stdlib headers — use `akira_api.h`'s `printf()` instead
- **No WASI imports** — all functions come from the `env` module provided by WAMR
- **64 KB limit** — use `-Os`, avoid floating-point where possible, prefer static buffers
- **`delay(microseconds)`** — always yield in your poll loop (not `sleep()`)
- **Sensor values × 1000** — divide by 1000 to get physical units

---

## Capability Reference

| Capability | Required for |
|------------|-------------|
| `display.write` | `display_*()` |
| `gpio.read` | `gpio_read()`, `gpio_configure()` |
| `gpio.write` | `gpio_write()`, `gpio_configure()` |
| `sensor.read` | `sensor_read()` |
| `timer` | `timer_*()` |
| `ble` | `ble_*()` |
| `hid` | `hid_*()` |
| `storage.read` | `storage_open(O_READ)`, `storage_list()` |
| `storage.write` | `storage_open(O_WRITE/APPEND)`, `storage_delete()` |
| `network.*` | `net_*()` |
| `ipc` | `msg_*()` |
| `app.control` | `app_start()`, `app_stop()`, `app_list()` |
| `app.switch` | `app_switch()` |
| `rf.transceive` | `rf_*()` |
| `uart` | `uart_*()` |
| `i2c` | `i2c_*()` |
| `pwm` | `pwm_*()` |
| `power.read` | `power_get_*()` |
| `power.control` | `power_set_*()`, `power_wake_*()` |

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/API_REFERENCE.md](docs/API_REFERENCE.md) | Complete function signatures and examples |
| [docs/BEST_PRACTICES.md](docs/BEST_PRACTICES.md) | Patterns, memory management, power efficiency |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Build errors, hardware issues, common mistakes |
| [wasm_apps/README.md](wasm_apps/README.md) | Sample app overview and build instructions |

---

## License

Apache-2.0 — see [LICENSE](LICENSE).
