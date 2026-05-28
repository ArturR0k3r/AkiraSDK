/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * hello_world — minimal Rust WASM application for AkiraOS.
 *
 * Build:
 *   cargo build --release
 *   # Output: target/wasm32-unknown-unknown/release/hello_world.wasm
 *
 * Or via the AkiraSDK build script:
 *   cd AkiraSDK/wasm_apps && ./build.sh rust/hello_world
 */

#![no_std]
#![no_main]

use akira_sdk::{console, display};

/// App entry point — called by the WAMR runtime.
#[no_mangle]
pub extern "C" fn main() -> i32 {
    // ── Console output ─────────────────────────────────────────────────────
    console::println("=================================");
    console::println("  Hello from AkiraOS Rust WASM! ");
    console::println("=================================");
    console::println("[INFO]  This is an info message");
    console::println("[WARN]  This is a warning message");
    console::println("WASM app executed successfully!");
    console::println("SDK: Rust/AkiraOS 1.0.0");

    // ── Display ────────────────────────────────────────────────────────────
    display::clear(display::COLOR_BLACK);
    display::text(10, 10, "Hello from", display::COLOR_CYAN);
    display::text(10, 30, "Rust WASM!", display::COLOR_WHITE);
    display::rounded_rect(5, 5, 120, 45, 6, display::COLOR_CYAN);
    display::flush();

    0
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
