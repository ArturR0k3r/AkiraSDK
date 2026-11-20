# Rust Apps Guide for AkiraOS

This guide explains how to build WASM applications for AkiraOS using Rust.

## Prerequisites

1. Install Rust: <https://rustup.rs/>
2. Add the WASM target:
   ```sh
   rustup target add wasm32-unknown-unknown
   ```

No WASI SDK or C toolchain is required for Rust apps.

## Project Structure

```
my_app/
├── Cargo.toml          # Crate manifest (cdylib + release profile)
├── .cargo/
│   └── config.toml     # Default target + linker flags
├── manifest.json       # AkiraOS app manifest
└── src/
    └── main.rs         # Application code
```

A ready-to-use template is at `AkiraSDK/wasm_apps/rust/hello_world/`.

## Cargo.toml

```toml
[package]
name = "my_app"
version = "0.1.0"
edition = "2021"

# cdylib produces a .wasm file with a C-compatible export table
[lib]
crate-type = ["cdylib"]
name = "my_app"
path = "src/main.rs"

[dependencies]
akira_sdk = { path = "../../rust/akira_sdk" }

[profile.release]
opt-level = "s"
lto = true
codegen-units = 1
panic = "abort"
strip = true
```

## .cargo/config.toml

```toml
[build]
target = "wasm32-unknown-unknown"

[target.wasm32-unknown-unknown]
rustflags = [
    "-C", "link-arg=--export=main",
    "-C", "link-arg=--allow-undefined",
    "-C", "link-arg=--no-entry",
    "-C", "link-arg=--initial-memory=65536",
    "-C", "link-arg=--max-memory=65536",
    "-C", "link-arg=-z,stack-size=4096",
]
```

## Writing the App

```rust
#![no_std]
#![no_main]

use akira_sdk::{console, display};

#[no_mangle]
pub extern "C" fn main() -> i32 {
    console::println("Hello from Rust WASM!");

    display::clear(display::COLOR_BLACK);
    display::text(10, 10, "Hello Rust!", display::COLOR_WHITE);
    display::flush();

    0
}
```

Key rules:
- `#![no_std]` — Rust standard library is not available in bare-metal WASM
- `#![no_main]` — prevents the default main shim
- `#[no_mangle] pub extern "C" fn main()` — entry point called by WAMR
- Return `0` for success; non-zero signals an error to AkiraOS

## Building

```sh
# From the app directory
cargo build --target wasm32-unknown-unknown --release

# Output: target/wasm32-unknown-unknown/release/<crate_name>.wasm
```

Or use the AkiraSDK build script:
```sh
cd AkiraSDK/wasm_apps
./build.sh rust/my_app
```

Or via make:
```sh
cd AkiraSDK/wasm_apps
make rust/my_app
# or all Rust apps:
make build-rust
```

## manifest.json

```json
{
  "name": "my_app",
  "version": "1.0.0",
  "capabilities": ["input.read"],
  "memory_quota": 65536,
  "min_akiraos_version": "1.0.0"
}
```

## API Reference

All modules are re-exported from `akira_sdk`. Import them by module:

```rust
use akira_sdk::{console, display, gpio, sensor, ble, hid, ipc, net,
                storage, timer, app, power, uart, i2c, pwm, adc};
```

### console
| Function | Description |
|----------|-------------|
| `console::print(s: &str)` | Print without newline |
| `console::println(s: &str)` | Print with newline |
| `console::delay_ms(ms: u32)` | Delay (milliseconds) |

### display
| Constant/Function | Description |
|-------------------|-------------|
| `display::COLOR_*` | RGB565 color constants |
| `display::clear(color)` | Fill screen |
| `display::text(x, y, s, color)` | Draw text |
| `display::text_large(x, y, s, color)` | Draw large text |
| `display::number(x, y, n, color)` | Draw integer |
| `display::rect(x, y, w, h, color)` | Filled rectangle |
| `display::rect_outline(x, y, w, h, color)` | Rectangle outline |
| `display::rounded_rect(x, y, w, h, r, color)` | Rounded rect |
| `display::line(x0, y0, x1, y1, color)` | Line |
| `display::circle(cx, cy, r, color)` | Circle outline |
| `display::circle_fill(cx, cy, r, color)` | Filled circle |
| `display::progress_bar(x, y, w, h, pct, color)` | Progress bar |
| `display::bitmap(pixels: &[u16])` | Raw RGB565 framebuffer |
| `display::flush()` | Push buffer to screen |
| `display::get_size() -> (i32, i32)` | Screen dimensions |

### gpio
```rust
use akira_sdk::gpio;
gpio::configure(pin, gpio::GPIO_OUTPUT);
gpio::write(pin, 1);
let v = gpio::read(pin);
```

### sensor
```rust
use akira_sdk::sensor;
let raw = sensor::read(sensor::SENSOR_CHAN_ACCEL_X);
let f   = sensor::read_f32(sensor::SENSOR_CHAN_GYRO_Z);
```

### storage
```rust
use akira_sdk::storage;
let fd = storage::open("/data/log.txt", storage::O_WRITE);
storage::write(fd, b"data\n");
storage::close(fd);
```

### ble
```rust
use akira_sdk::ble;
ble::init(b"MyDevice\0");
ble::char_add(0, ble::BLE_PROP_NOTIFY | ble::BLE_PROP_READ);
ble::notify(0, b"hello\0");
let connected = ble::is_connected();
```

### ipc
```rust
use akira_sdk::ipc;
ipc::subscribe(b"sensors\0");
let mut buf = [0u8; 64];
let n = ipc::recv(b"sensors\0", &mut buf, 1000);
```

### net
```rust
use akira_sdk::net;
let sock = net::open(net::NET_TYPE_TCP);
net::connect(sock, b"192.168.1.10\0", 8080);
net::tx_bind(sock, &tx_ring_buf);
net::tx_flush(sock);
```

## Memory Constraints

AkiraOS WASM apps run in a sandboxed 64 KB linear memory by default.

- Keep your data small
- Avoid deep recursion
- Use `akira_sdk::storage` for persistence instead of large in-memory buffers
- AOT compilation significantly reduces execution overhead

## AOT Compilation

Pre-compile for faster startup:
```sh
# Build WASM first, then AOT
cargo build --target wasm32-unknown-unknown --release
wamrc --target=xtensa --cpu=esp32s3 \
      -o my_app-esp32s3.aot \
      target/wasm32-unknown-unknown/release/my_app.wasm
```
