/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! # AkiraOS WASM SDK for Rust
//!
//! Provides safe Rust bindings to the AkiraOS native API for apps compiled to
//! `wasm32-unknown-unknown`.  Import this crate in your app, then call the
//! module-level functions directly:
//!
//! ```rust
//! use akira_sdk::console;
//! use akira_sdk::display;
//!
//! #[no_mangle]
//! pub extern "C" fn main() -> i32 {
//!     console::println("Hello from Rust WASM!");
//!     display::clear(display::COLOR_BLACK);
//!     display::text(10, 20, "Hello!", display::COLOR_WHITE);
//!     display::flush();
//!     0
//! }
//! ```
//!
//! Each module corresponds to one API group in `akira_api.h`.  All unsafe
//! FFI calls are encapsulated here; app code only sees safe Rust wrappers.

#![no_std]

pub mod adc;
pub mod app;
pub mod ble;
pub mod console;
pub mod display;
pub mod gpio;
pub mod hid;
pub mod i2c;
pub mod ipc;
pub mod net;
pub mod power;
pub mod pwm;
pub mod sensor;
pub mod storage;
pub mod timer;
pub mod uart;

/// Panic handler required for `#![no_std]` WASM targets.
/// Apps should not panic in production — use error return codes instead.
#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

// ─── Shared utility ──────────────────────────────────────────────────────────

/// Copy `src` into `dst` as a null-terminated C string, truncating to `dst.len()-1`.
/// Returns the number of bytes written (excluding the null terminator).
#[inline]
pub(crate) fn str_to_cbuf(src: &str, dst: &mut [u8]) -> usize {
    let bytes = src.as_bytes();
    let n = bytes.len().min(dst.len().saturating_sub(1));
    dst[..n].copy_from_slice(&bytes[..n]);
    dst[n] = 0;
    n
}
