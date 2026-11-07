/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! BLE GATT server API.
//! Required capability: `ble`.

pub const BLE_PROP_READ:        i32 = 0x02;
pub const BLE_PROP_WRITE_WO_RSP: i32 = 0x04;
pub const BLE_PROP_WRITE:       i32 = 0x08;
pub const BLE_PROP_NOTIFY:      i32 = 0x10;
pub const BLE_PROP_INDICATE:    i32 = 0x20;

pub const BLE_EVT_NONE:         i32 = 0;
pub const BLE_EVT_CONNECTED:    i32 = 1;
pub const BLE_EVT_DISCONNECTED: i32 = 2;
pub const BLE_EVT_CHAR_WRITTEN: i32 = 3;

extern "C" {
    fn ble_init() -> i32;
    fn ble_deinit() -> i32;
    fn ble_set_local_name(name: *const u8) -> i32;
    fn ble_service_create(uuid128_str: *const u8) -> i32;
    fn ble_char_create(uuid128_str: *const u8, props: i32, max_len: i32) -> i32;
    fn ble_service_add_char(svc_h: i32, char_h: i32) -> i32;
    fn ble_add_service(svc_h: i32) -> i32;
    fn ble_set_advertised_service(svc_h: i32) -> i32;
    fn ble_advertise() -> i32;
    fn ble_stop_advertise() -> i32;
    fn ble_is_connected() -> i32;
    fn ble_char_write(char_h: i32, data: *const u8, len: u32) -> i32;
    fn ble_char_read(char_h: i32, buf: *mut u8, len: u32) -> i32;
    fn ble_event_pop(buf: *mut u8, len: u32) -> i32;
}

#[inline] pub fn init() -> i32 { unsafe { ble_init() } }
#[inline] pub fn deinit() -> i32 { unsafe { ble_deinit() } }

#[inline]
pub fn set_local_name(name: &str) -> i32 {
    let mut buf = [0u8; 32];
    crate::str_to_cbuf(name, &mut buf);
    unsafe { ble_set_local_name(buf.as_ptr()) }
}

#[inline]
pub fn service_create(uuid128: &str) -> i32 {
    let mut buf = [0u8; 40];
    crate::str_to_cbuf(uuid128, &mut buf);
    unsafe { ble_service_create(buf.as_ptr()) }
}

#[inline]
pub fn char_create(uuid128: &str, props: i32, max_len: i32) -> i32 {
    let mut buf = [0u8; 40];
    crate::str_to_cbuf(uuid128, &mut buf);
    unsafe { ble_char_create(buf.as_ptr(), props, max_len) }
}

#[inline] pub fn service_add_char(svc_h: i32, char_h: i32) -> i32 { unsafe { ble_service_add_char(svc_h, char_h) } }
#[inline] pub fn add_service(svc_h: i32) -> i32 { unsafe { ble_add_service(svc_h) } }
#[inline] pub fn set_advertised_service(svc_h: i32) -> i32 { unsafe { ble_set_advertised_service(svc_h) } }
#[inline] pub fn advertise() -> i32 { unsafe { ble_advertise() } }
#[inline] pub fn stop_advertise() -> i32 { unsafe { ble_stop_advertise() } }
#[inline] pub fn is_connected() -> bool { unsafe { ble_is_connected() == 1 } }

#[inline]
pub fn char_write(char_h: i32, data: &[u8]) -> i32 {
    unsafe { ble_char_write(char_h, data.as_ptr(), data.len() as u32) }
}

#[inline]
pub fn char_read(char_h: i32, buf: &mut [u8]) -> i32 {
    unsafe { ble_char_read(char_h, buf.as_mut_ptr(), buf.len() as u32) }
}

#[inline]
pub fn event_pop(buf: &mut [u8]) -> i32 {
    unsafe { ble_event_pop(buf.as_mut_ptr(), buf.len() as u32) }
}
