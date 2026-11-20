/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! Polling timer API.
//! Required capability: `timer`.

extern "C" {
    fn timer_create() -> i32;
    fn timer_start(handle: i32) -> i32;
    fn timer_stop(handle: i32) -> i32;
    fn timer_elapsed(handle: i32) -> i32;
    fn timer_free(handle: i32) -> i32;
}

/// Allocate a timer.  Returns a handle ≥0 on success, or negative on error.
#[inline] pub fn create() -> i32 { unsafe { timer_create() } }

/// Start (or restart) a timer.
#[inline] pub fn start(handle: i32) -> i32 { unsafe { timer_start(handle) } }

/// Stop a timer (preserves elapsed time).
#[inline] pub fn stop(handle: i32) -> i32 { unsafe { timer_stop(handle) } }

/// Return elapsed milliseconds since the last `start()`.
#[inline] pub fn elapsed(handle: i32) -> i32 { unsafe { timer_elapsed(handle) } }

/// Release a timer handle.
#[inline] pub fn free(handle: i32) -> i32 { unsafe { timer_free(handle) } }
