/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! Console output and delay.
//! Required capability: none.

use crate::str_to_cbuf;

extern "C" {
    fn printf_native(message: *const u8) -> i32;
    /// Busy-wait (or yield) for `microseconds` µs.
    pub fn delay(microseconds: u32) -> i32;
}

/// Print a string to the AkiraOS console (no newline needed).
#[inline]
pub fn print(s: &str) {
    let mut buf = [0u8; 256];
    str_to_cbuf(s, &mut buf);
    unsafe { printf_native(buf.as_ptr()); }
}

/// Print a string to the AkiraOS console (identical to `print` — host adds line endings).
#[inline]
pub fn println(s: &str) {
    print(s);
}

/// Busy-wait for `ms` milliseconds.
#[inline]
pub fn delay_ms(ms: u32) {
    unsafe { delay(ms * 1000); }
}
