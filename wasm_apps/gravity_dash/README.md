# gravity_dash

A 2-D gravity-platformer WASM game for **AkiraOS v1.4.9 "Glitch"** running on
AkiraConsole (ESP32-S3).  Physically tilt the console to rotate the gravity
vector and navigate three hand-crafted levels — inspired by *Yoshi's Universal
Gravitation* (GBA, 2004).

---

## Hardware Prerequisites

| Requirement | Detail |
|---|---|
| Board | AkiraConsole (ESP32-S3, N4R2 or better) |
| Display | SPI TFT 240×135 px, 16-bit RGB565, landscape |
| IMU | ICM-42688-P or MPU-6050 (I2C).  If absent, BTN_UP/DOWN rotate gravity ±15°/s as fallback |
| PSRAM | 2 MB minimum (WASM heap) |
| AkiraOS | v1.4.9 "Glitch" or later with `CONFIG_AKIRA_WASM_RUNTIME=y` |
| WASI SDK | `/opt/wasi-sdk` (v20+) for building |

---

## Build

### 1. Install wasi-sdk

```bash
# Download the appropriate release for your host OS from:
# https://github.com/WebAssembly/wasi-sdk/releases
# Default install path: /opt/wasi-sdk

wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/\
wasi-sdk-20.0-linux.tar.gz
sudo tar -xf wasi-sdk-20.0-linux.tar.gz -C /opt
sudo ln -s /opt/wasi-sdk-20.0 /opt/wasi-sdk
```

### 2. Build the WASM binary

```bash
cd AkiraSDK/wasm_apps/gravity_dash
make wasm
# Output: build/gravity_dash.wasm
```

The build prints the binary size and runs `wasm-validate` if available.  The
target budget is **≤ 48 KB**.

### 3. Build the desktop stub (Linux / macOS)

Requires SDL2:

```bash
# Ubuntu / Debian
sudo apt install libsdl2-dev

# macOS (Homebrew)
brew install sdl2

make stub
./build/gravity_dash_stub
```

**Controls in the stub:**

| Key | Action |
|---|---|
| ← / → | Tilt gravity left / right (simulates IMU) |
| Z or Enter | BTN_A (confirm / restart) |
| X | BTN_B |
| ↑ / ↓ | BTN_UP / BTN_DOWN |

---

## AkiraOS Deployment

### Option A — SecureOTA (recommended)

```bash
# Copy the .wasm to your OTA server, then trigger install from the device shell:
akira> ota install http://<server>/gravity_dash.wasm
```

### Option B — Direct flash via esptool

```bash
make flash SERIAL_PORT=/dev/ttyUSB0
# Flashes to offset 0x310000 (AkiraOS module partition)
```

### Option C — USB mass-storage (if enabled)

Copy `build/gravity_dash.wasm` to the `/apps/` directory that appears when
AkiraConsole is connected via USB in MSC mode.

After installation, launch from the shell:

```
akira> wasm run /apps/gravity_dash.wasm
```

---

## Levels

| # | Name | Description | Estimated time |
|---|---|---|---|
| 1 | Introduction | Flat ground, 5 coins, EXIT right. No tilting needed. | 5–10 s |
| 2 | Gravity Flip | Must tilt ~90° to reach the raised platform and EXIT. One spike at the bottom. | 10–20 s |
| 3 | Puzzle Maze | 3 distinct gravity rotations, moving platforms, 8 coins, 4 spikes. | 20–40 s |

---

## Memory Usage

| Region | Size | Notes |
|---|---|---|
| `Game g` (main state) | ~512 B | Player, state machine, dirty map |
| `LevelData g_levels[3]` | ~1 800 B | Maps + platform state |
| `s_coin_taken[120]` | 120 B | Per-level coin flags |
| `s_prev_player` | 16 B | Previous frame position |
| Code + font + read-only data | ~20–28 KB | Depends on optimisation |
| **Total (static)** | **≈ 30–35 KB** | Well within 64 KB WASM heap |

> No framebuffer is allocated in WASM — all drawing is done via host-side
> `akira_display_rect` / `akira_display_pixel` native calls.  The dirty-tile
> strategy minimises pixel-write API calls per frame.

---

## IMU Fallback

If `akira_imu_gravity_angle()` returns `0.0` (IMU not present or not
calibrated), the module activates the button fallback:

- **BTN_UP** rotates gravity counter-clockwise (±15°/s)
- **BTN_DOWN** rotates gravity clockwise (±15°/s)

The accumulated angle is clamped to ±90°.  This is sufficient to complete all
three levels without a physical IMU.

---

## Known Limitations

- No persistent save state — coin count resets on unload.
- Moving platforms use integer tile positions; sub-tile interpolation is visual
  only and does not affect collision.
- `akira_display_pixel` intensive operations (coin circles, spike triangles) may
  be slower than `akira_display_rect` on some host implementations.  If frame
  budget is exceeded, replace coin/spike rendering with plain rectangles.
- The 4×5 pixel font does not include lowercase letters; HUD text is uppercase only.

---

## License

GPL-3.0-only — see [LICENSE](../../../LICENSE).
