/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! App lifecycle control.
//! Required capabilities: `app.control` or `app.switch`.

pub const APP_STATE_NEW:       i32 = 0;
pub const APP_STATE_INSTALLED: i32 = 1;
pub const APP_STATE_RUNNING:   i32 = 2;
pub const APP_STATE_STOPPED:   i32 = 3;
pub const APP_STATE_ERROR:     i32 = 4;
pub const APP_STATE_FAILED:    i32 = 5;

extern "C" {
    fn app_get_status(name: *const u8) -> i32;
    fn app_list(buf: *mut u8, buf_len: u32) -> i32;
    fn app_get_self_name(buf: *mut u8, buf_len: u32) -> i32;
    fn app_start(name: *const u8) -> i32;
    fn app_stop(name: *const u8) -> i32;
    fn app_switch(name: *const u8) -> i32;
}

#[inline]
pub fn get_status(name: &str) -> i32 {
    let mut buf = [0u8; 64];
    crate::str_to_cbuf(name, &mut buf);
    unsafe { app_get_status(buf.as_ptr()) }
}

#[inline]
pub fn list(out: &mut [u8]) -> i32 {
    unsafe { app_list(out.as_mut_ptr(), out.len() as u32) }
}

#[inline]
pub fn get_self_name(out: &mut [u8]) -> i32 {
    unsafe { app_get_self_name(out.as_mut_ptr(), out.len() as u32) }
}

#[inline]
pub fn start(name: &str) -> i32 {
    let mut buf = [0u8; 64];
    crate::str_to_cbuf(name, &mut buf);
    unsafe { app_start(buf.as_ptr()) }
}

#[inline]
pub fn stop(name: &str) -> i32 {
    let mut buf = [0u8; 64];
    crate::str_to_cbuf(name, &mut buf);
    unsafe { app_stop(buf.as_ptr()) }
}

/// Hand off to another app and return 0 from `main()` to complete the switch.
#[inline]
pub fn switch_to(name: &str) -> i32 {
    let mut buf = [0u8; 64];
    crate::str_to_cbuf(name, &mut buf);
    unsafe { app_switch(buf.as_ptr()) }
}
