/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! HID (keyboard, mouse, gamepad, media keys) API.
//! Required capability: `hid`.

pub const HID_TRANSPORT_NONE: i32 = 0;
pub const HID_TRANSPORT_BLE:  i32 = 1;
pub const HID_TRANSPORT_USB:  i32 = 2;

pub const HID_DEVICE_KEYBOARD: i32 = 0x01;
pub const HID_DEVICE_GAMEPAD:  i32 = 0x02;
pub const HID_DEVICE_MOUSE:    i32 = 0x04;
pub const HID_DEVICE_COMBO:    i32 = 0x07;

pub const HID_MOD_NONE:        i32 = 0x00;
pub const HID_MOD_LEFT_CTRL:   i32 = 0x01;
pub const HID_MOD_LEFT_SHIFT:  i32 = 0x02;
pub const HID_MOD_LEFT_ALT:    i32 = 0x04;
pub const HID_MOD_LEFT_GUI:    i32 = 0x08;
pub const HID_MOD_RIGHT_CTRL:  i32 = 0x10;
pub const HID_MOD_RIGHT_SHIFT: i32 = 0x20;
pub const HID_MOD_RIGHT_ALT:   i32 = 0x40;
pub const HID_MOD_RIGHT_GUI:   i32 = 0x80;

pub const HID_MOUSE_BTN_LEFT:   i32 = 0x01;
pub const HID_MOUSE_BTN_RIGHT:  i32 = 0x02;
pub const HID_MOUSE_BTN_MIDDLE: i32 = 0x04;

pub const HID_KEY_ENTER: i32 = 0x28;
pub const HID_KEY_ESC:   i32 = 0x29;
pub const HID_KEY_SPACE: i32 = 0x2C;
pub const HID_KEY_A:     i32 = 0x04;
pub const HID_KEY_B:     i32 = 0x05;
pub const HID_KEY_C:     i32 = 0x06;
pub const HID_KEY_S:     i32 = 0x16;

pub const HID_CONSUMER_PLAY_PAUSE:    i32 = 0x00CD;
pub const HID_CONSUMER_STOP:          i32 = 0x00B7;
pub const HID_CONSUMER_NEXT_TRACK:    i32 = 0x00B5;
pub const HID_CONSUMER_PREV_TRACK:    i32 = 0x00B6;
pub const HID_CONSUMER_VOL_UP:        i32 = 0x00E9;
pub const HID_CONSUMER_VOL_DOWN:      i32 = 0x00EA;
pub const HID_CONSUMER_MUTE:          i32 = 0x00E2;

extern "C" {
    fn hid_init(transport: i32, device_types: i32) -> i32;
    fn hid_enable() -> i32;
    fn hid_disable() -> i32;
    fn hid_is_connected() -> i32;
    fn hid_set_transport(transport: i32) -> i32;
    fn hid_set_device_types(types: i32) -> i32;
    fn hid_key_press(keycode: i32) -> i32;
    fn hid_key_release(keycode: i32) -> i32;
    fn hid_key_release_all() -> i32;
    fn hid_type_string(str: *const u8) -> i32;
    fn hid_gamepad_press(btn_mask: i32) -> i32;
    fn hid_gamepad_release(btn_mask: i32) -> i32;
    fn hid_gamepad_set_axis(axis: i32, value: i32) -> i32;
    fn hid_gamepad_set_dpad(direction: i32) -> i32;
    fn hid_gamepad_reset() -> i32;
    fn hid_mouse_move(dx: i32, dy: i32) -> i32;
    fn hid_mouse_btn_press(button: i32) -> i32;
    fn hid_mouse_btn_release(button: i32) -> i32;
    fn hid_mouse_scroll(delta: i32) -> i32;
    fn hid_consumer_send(usage_code: i32) -> i32;
    fn hid_send_raw_report(report_id: i32, data_ptr: *const u8, len: u32) -> i32;
    fn hid_action_register(name: *const u8, modifier: i32, keycode: i32) -> i32;
    fn hid_action_trigger(name: *const u8) -> i32;
}

#[inline] pub fn init(transport: i32, device_types: i32) -> i32 { unsafe { hid_init(transport, device_types) } }
#[inline] pub fn enable() -> i32 { unsafe { hid_enable() } }
#[inline] pub fn disable() -> i32 { unsafe { hid_disable() } }
#[inline] pub fn is_connected() -> bool { unsafe { hid_is_connected() } == 1 }
#[inline] pub fn set_transport(transport: i32) -> i32 { unsafe { hid_set_transport(transport) } }
#[inline] pub fn set_device_types(types: i32) -> i32 { unsafe { hid_set_device_types(types) } }
#[inline] pub fn key_press(keycode: i32) -> i32 { unsafe { hid_key_press(keycode) } }
#[inline] pub fn key_release(keycode: i32) -> i32 { unsafe { hid_key_release(keycode) } }
#[inline] pub fn key_release_all() -> i32 { unsafe { hid_key_release_all() } }

#[inline]
pub fn type_string(s: &str) -> i32 {
    let mut buf = [0u8; 256];
    crate::str_to_cbuf(s, &mut buf);
    unsafe { hid_type_string(buf.as_ptr()) }
}

#[inline] pub fn gamepad_press(btn_mask: i32) -> i32 { unsafe { hid_gamepad_press(btn_mask) } }
#[inline] pub fn gamepad_release(btn_mask: i32) -> i32 { unsafe { hid_gamepad_release(btn_mask) } }
#[inline] pub fn gamepad_set_axis(axis: i32, value: i32) -> i32 { unsafe { hid_gamepad_set_axis(axis, value) } }
#[inline] pub fn gamepad_set_dpad(direction: i32) -> i32 { unsafe { hid_gamepad_set_dpad(direction) } }
#[inline] pub fn gamepad_reset() -> i32 { unsafe { hid_gamepad_reset() } }
#[inline] pub fn mouse_move(dx: i32, dy: i32) -> i32 { unsafe { hid_mouse_move(dx, dy) } }
#[inline] pub fn mouse_btn_press(button: i32) -> i32 { unsafe { hid_mouse_btn_press(button) } }
#[inline] pub fn mouse_btn_release(button: i32) -> i32 { unsafe { hid_mouse_btn_release(button) } }
#[inline] pub fn mouse_scroll(delta: i32) -> i32 { unsafe { hid_mouse_scroll(delta) } }
#[inline] pub fn consumer_send(usage_code: i32) -> i32 { unsafe { hid_consumer_send(usage_code) } }

#[inline]
pub fn send_raw_report(report_id: i32, data: &[u8]) -> i32 {
    unsafe { hid_send_raw_report(report_id, data.as_ptr(), data.len() as u32) }
}

#[inline]
pub fn action_register(name: &str, modifier: i32, keycode: i32) -> i32 {
    let mut buf = [0u8; 16];
    crate::str_to_cbuf(name, &mut buf);
    unsafe { hid_action_register(buf.as_ptr(), modifier, keycode) }
}

#[inline]
pub fn action_trigger(name: &str) -> i32 {
    let mut buf = [0u8; 16];
    crate::str_to_cbuf(name, &mut buf);
    unsafe { hid_action_trigger(buf.as_ptr()) }
}
