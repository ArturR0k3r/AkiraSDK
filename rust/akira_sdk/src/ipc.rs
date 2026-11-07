/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! IPC pub/sub messaging.
//! Required capability: `ipc`.

extern "C" {
    fn msg_subscribe(topic: *const u8) -> i32;
    fn msg_unsubscribe(topic: *const u8) -> i32;
    fn msg_publish(topic: *const u8, data_ptr: *const u8, len: u32) -> i32;
    fn msg_recv(topic: *const u8, buf_ptr: *mut u8, buf_len: u32, timeout_ms: i32) -> i32;
    fn msg_try_recv(topic: *const u8, buf_ptr: *mut u8, buf_len: u32) -> i32;
    fn msg_pending(topic: *const u8) -> i32;
}

#[inline]
pub fn subscribe(topic: &str) -> i32 {
    let mut buf = [0u8; 64];
    crate::str_to_cbuf(topic, &mut buf);
    unsafe { msg_subscribe(buf.as_ptr()) }
}

#[inline]
pub fn unsubscribe(topic: &str) -> i32 {
    let mut buf = [0u8; 64];
    crate::str_to_cbuf(topic, &mut buf);
    unsafe { msg_unsubscribe(buf.as_ptr()) }
}

#[inline]
pub fn publish(topic: &str, data: &[u8]) -> i32 {
    let mut tbuf = [0u8; 64];
    crate::str_to_cbuf(topic, &mut tbuf);
    unsafe { msg_publish(tbuf.as_ptr(), data.as_ptr(), data.len() as u32) }
}

/// Receive next message.  `timeout_ms`: 0=non-blocking, -1=wait forever.
#[inline]
pub fn recv(topic: &str, buf: &mut [u8], timeout_ms: i32) -> i32 {
    let mut tbuf = [0u8; 64];
    crate::str_to_cbuf(topic, &mut tbuf);
    unsafe { msg_recv(tbuf.as_ptr(), buf.as_mut_ptr(), buf.len() as u32, timeout_ms) }
}

/// Non-blocking receive.
#[inline]
pub fn try_recv(topic: &str, buf: &mut [u8]) -> i32 {
    let mut tbuf = [0u8; 64];
    crate::str_to_cbuf(topic, &mut tbuf);
    unsafe { msg_try_recv(tbuf.as_ptr(), buf.as_mut_ptr(), buf.len() as u32) }
}

/// Return number of pending messages, or negative on error.
#[inline]
pub fn pending(topic: &str) -> i32 {
    let mut tbuf = [0u8; 64];
    crate::str_to_cbuf(topic, &mut tbuf);
    unsafe { msg_pending(tbuf.as_ptr()) }
}
