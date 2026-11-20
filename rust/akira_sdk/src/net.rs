/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! Networking (TCP/UDP ring-buffer API).
//! Required capability: `network`.

pub const NET_TYPE_TCP: i32 = 0;
pub const NET_TYPE_UDP: i32 = 1;
pub const NET_TYPE_TLS: i32 = 2;

pub const NET_EVT_NONE:         i32 = 0;
pub const NET_EVT_CONNECTED:    i32 = 1;
pub const NET_EVT_DISCONNECTED: i32 = 2;
pub const NET_EVT_DATA_READY:   i32 = 3;
pub const NET_EVT_ACCEPT:       i32 = 4;
pub const NET_EVT_ERROR:        i32 = 5;

/// Ring buffer header size in bytes.
pub const RING_HDR_SIZE: usize = 16;

extern "C" {
    fn net_open(sock_type: i32) -> i32;
    fn net_connect(handle: i32, host: *const u8, port: i32) -> i32;
    fn net_bind(handle: i32, port: i32) -> i32;
    fn net_listen(handle: i32, backlog: i32) -> i32;
    fn net_close(handle: i32) -> i32;
    fn net_tx_bind(handle: i32, tx_buf: *mut u8, total_size: i32) -> i32;
    fn net_rx_bind(handle: i32, rx_buf: *mut u8, total_size: i32) -> i32;
    fn net_tx_flush(handle: i32) -> i32;
    fn net_event_pop(buf: *mut u8, len: i32) -> i32;
    fn net_get_ip(buf: *mut u8, len: i32) -> i32;
}

#[inline] pub fn open(sock_type: i32) -> i32 { unsafe { net_open(sock_type) } }

#[inline]
pub fn connect(handle: i32, host: &str, port: i32) -> i32 {
    let mut buf = [0u8; 256];
    crate::str_to_cbuf(host, &mut buf);
    unsafe { net_connect(handle, buf.as_ptr(), port) }
}

#[inline] pub fn bind(handle: i32, port: i32) -> i32 { unsafe { net_bind(handle, port) } }
#[inline] pub fn listen(handle: i32, backlog: i32) -> i32 { unsafe { net_listen(handle, backlog) } }
#[inline] pub fn close(handle: i32) -> i32 { unsafe { net_close(handle) } }
#[inline] pub fn tx_bind(handle: i32, tx_buf: &mut [u8]) -> i32 { unsafe { net_tx_bind(handle, tx_buf.as_mut_ptr(), tx_buf.len() as i32) } }
#[inline] pub fn rx_bind(handle: i32, rx_buf: &mut [u8]) -> i32 { unsafe { net_rx_bind(handle, rx_buf.as_mut_ptr(), rx_buf.len() as i32) } }
#[inline] pub fn tx_flush(handle: i32) -> i32 { unsafe { net_tx_flush(handle) } }
#[inline] pub fn event_pop(buf: &mut [u8]) -> i32 { unsafe { net_event_pop(buf.as_mut_ptr(), buf.len() as i32) } }

/// Get device IP address as a null-terminated string written into `buf`.
#[inline]
pub fn get_ip(buf: &mut [u8]) -> i32 {
    unsafe { net_get_ip(buf.as_mut_ptr(), buf.len() as i32) }
}
