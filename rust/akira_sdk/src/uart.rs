/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! UART serial port API.
//! Required capability: `uart`.

extern "C" {
    fn uart_open(port_id: i32, baud_rate: i32) -> i32;
    fn uart_write(handle: i32, buf: *const u8, len: u32) -> i32;
    fn uart_read(handle: i32, buf: *mut u8, max_len: u32) -> i32;
    fn uart_close(handle: i32) -> i32;
}

#[inline] pub fn open(port_id: i32, baud_rate: i32) -> i32 { unsafe { uart_open(port_id, baud_rate) } }
#[inline] pub fn write(handle: i32, buf: &[u8]) -> i32 { unsafe { uart_write(handle, buf.as_ptr(), buf.len() as u32) } }
#[inline] pub fn read(handle: i32, buf: &mut [u8]) -> i32 { unsafe { uart_read(handle, buf.as_mut_ptr(), buf.len() as u32) } }
#[inline] pub fn close(handle: i32) -> i32 { unsafe { uart_close(handle) } }
