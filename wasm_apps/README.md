# AkiraOS WASM Applications

Sample WebAssembly applications for AkiraOS running on the WAMR runtime.

---

## Available Apps

| App | Capabilities | Description |
|-----|-------------|-------------|
| `hello_world` | — | Minimal `printf` example |
| `display_test` | `display.write` | Display primitives: shapes, text, colors |
| `gpio` | `gpio.read`, `gpio.write` | GPIO read/write demo |
| `imu_3d` | `display.write`, `sensor.read` | 3D board orientation using accelerometer |
| `imu_timer_test` | `sensor.read`, `timer` | IMU polling with timer |
| `inclinometer` | `display.write`, `sensor.read` | Tilt-angle display |
| `compass` | `display.write`, `sensor.read` | Magnetometer compass |
| `cube3d` | `display.write` | Rotating 3D wireframe cube |
| `ble_led` | `ble`, `gpio.write`, `gpio.read` | BLE GATT LED control |
| `macro_pad` | `display.write`, `gpio.read`, `hid`, `timer` | 5-button HID macro pad |
| `storage_test` | `storage.read`, `storage.write` | File read/write/list/delete |
| `net_echo` | `network.*` | TCP echo client |
| `net_server` | `network.*` | TCP echo server |
| `logic_analyzer` | `gpio.read`, `display.write`, `timer` | GPIO logic analyser |
| `supervisor` | `display.write`, `app.control`, `ipc` | App launcher UI |
| `tetris` | `display.write`, `gpio.read`, `timer` | Tetris game |

---

## Prerequisites

**WASI SDK** must be installed:

```bash
WASI_VERSION=24
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_VERSION}/wasi-sdk-${WASI_VERSION}.0-x86_64-linux.tar.gz
sudo tar xvf wasi-sdk-${WASI_VERSION}.0-x86_64-linux.tar.gz -C /opt
sudo ln -sf /opt/wasi-sdk-${WASI_VERSION}.0 /opt/wasi-sdk
```

Override the path: `export WASI_SDK=/path/to/your/wasi-sdk`

---

## Building

### All apps

```bash
./build.sh          # script
# or
make                # Makefile
```

### A specific app

```bash
./build.sh hello_world
# or
make -C hello_world
```

### Per-app make

```bash
cd hello_world
make
make clean
```

---

## App Structure

Each app directory contains:

```
hello_world/
├── main.c          # Application source
├── Makefile        # Per-app build rules
└── manifest.json   # App metadata and capabilities
```

### `manifest.json` fields

```json
{
  "name": "my_app",
  "version": "1.0.0",
  "capabilities": ["display.write", "sensor.read"],
  "memory_quota": 65536
}
```

- **`capabilities`** — list of required permissions (see capability table in main README)
- **`memory_quota`** — WASM linear memory limit in bytes (max 65536 = one page)

The manifest is automatically embedded into the `.wasm` binary by `make` using `../scripts/embed_manifest.py`.

---

## Writing Apps

### Include the SDK header

```c
#include "akira_api.h"  // relative path from app directory is ../../include/akira_api.h
```

### Entry point

```c
int main(void) {
    // your code
    return 0;
}
```

There is no `AKIRA_APP_MAIN()` macro — plain `main()` is the entry point.

### Logging

```c
printf("value: %d", my_int);  // defined in akira_api.h, no #include <stdio.h>
```

### Yielding

```c
delay(10000);   // 10 000 µs = 10 ms
delay(1000000); // 1 second
```

Always call `delay()` in your main loop to avoid spinning the CPU at 100%.

### No stdlib

Do **not** include standard library headers:

```c
// WRONG — pulls in WASI imports
#include <stdio.h>
#include <string.h>

// CORRECT — akira_api.h provides printf() and inline helpers
#include "akira_api.h"
```

---

## Build Flags (reference)

All apps use these flags (defined in each app's Makefile):

```makefile
CFLAGS  = -O2 -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -I../../include

LDFLAGS  = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=4096
LDFLAGS += -Wl,--initial-memory=65536
LDFLAGS += -Wl,--max-memory=65536
```

---

## AOT Compilation (Optional)

Apps ship as `.wasm` bytecode and run in the WAMR interpreter by default. For
performance-critical apps (games, 3D, signal processing), you can optionally
AOT-compile them to native machine code using `wamrc`.

### AOT targets

| Command | Architecture | Board |
|---------|-------------|-------|
| `./build.sh aot` | Xtensa LX7 (default) | ESP32-S3, Akira Console |
| `./build.sh aot thumb` | Cortex-M33 | nRF54L15DK |
| `./build.sh aot thumbv7em` | Cortex-M7 | STM32 |
| `./build.sh aot riscv32` | RISC-V 32 | ESP32-C3 |
| `./build.sh aot x86_64` | x86-64 | native_sim |

### AOT workflow

```bash
# 1. Build .wasm first
./build.sh            # produces bin/*.wasm

# 2. AOT-compile to native .aot binaries
./build.sh aot                     # default: xtensa (ESP32-S3)
./build.sh aot thumb               # nRF54L15

# Equivalent with make:
make aot                           # xtensa
make aot-xtensa
make aot-thumb
make aot AOT_TARGET=riscv32
```

Output files are placed in `bin/` alongside the `.wasm` files:
```
bin/tetris.wasm           ← portable interpreter bytecode
bin/tetris-xtensa.aot     ← native code for ESP32-S3
bin/tetris-thumb.aot      ← native code for nRF54L15
```

### Building wamrc

`wamrc` is in the WAMR submodule (already part of AkiraOS):

```bash
cd /path/to/AkiraOS/modules/wasm-micro-runtime/wamr-compiler
cmake . -DWAMR_BUILD_PLATFORM=linux
make
sudo cp wamrc /usr/local/bin/
```

Or point directly: `WAMRC=/path/to/wamrc ./build.sh aot`

> See [docs/BEST_PRACTICES.md](../docs/BEST_PRACTICES.md#aot-compilation) for
> guidance on when to use AOT vs interpreter.

---

## Deploying to Device

### Via AkiraOS shell

```
akira> app scan
akira> app start hello_world
```

### Via HTTP upload

```bash
curl -X POST -F "app=@hello_world.wasm" http://<device-ip>/api/apps/install
```

### Via SD card

```bash
make install SD_MOUNT=/media/$USER/AKIRA
```

---

## Memory Layout

```
Linear Memory (64 KB = one WASM page)
┌──────────────────────────────────┐ 65536
│  Stack        (4 KB from top)    │
├──────────────────────────────────┤ 61440
│                                  │
│  Heap + static data (~60 KB)     │
│                                  │
├──────────────────────────────────┤
│  Data & BSS                      │
└──────────────────────────────────┘ 0
```

Tips for keeping size down:
- Use `-Os` for size-optimized builds
- Avoid floating-point — pulls in significant polyfill code; use fixed-point (`× 1000`) instead
- Prefer `static` buffers over `malloc()`
- Strip with `-Wl,--strip-all`

---

## Troubleshooting

See [docs/TROUBLESHOOTING.md](../docs/TROUBLESHOOTING.md) for detailed solutions. Quick checklist:

- **Black screen** → did you call `display_flush()`?
- **Undefined reference** → check `-Wl,--allow-undefined` and correct include path
- **`sensor_read()` == `AKIRA_SENSOR_ERROR`** → sensor not present or missing capability
- **Binary too large** → use `-Os`, avoid FP, check static array sizes
- **App crashes** → check for large on-stack buffers; move to `static`
