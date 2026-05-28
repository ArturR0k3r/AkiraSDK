/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! GPIO digital I/O.
//! Required capabilities: `gpio.read` and/or `gpio.write`.

pub const GPIO_INPUT:            u32 = 1 << 0;
pub const GPIO_OUTPUT:           u32 = 1 << 1;
pub const GPIO_OUTPUT_INIT_LOW:  u32 = 1 << 2;
pub const GPIO_OUTPUT_INIT_HIGH: u32 = 1 << 3;
pub const GPIO_PULL_UP:          u32 = 1 << 4;
pub const GPIO_PULL_DOWN:        u32 = 1 << 5;
pub const GPIO_ACTIVE_LOW:       u32 = 1 << 6;
pub const GPIO_ACTIVE_HIGH:      u32 = 1 << 7;

extern "C" {
    fn gpio_configure(pin: u32, flags: u32) -> i32;
    fn gpio_read(pin: u32) -> i32;
    fn gpio_write(pin: u32, value: u32) -> i32;
}

/// Configure a GPIO pin with the given flags.
#[inline] pub fn configure(pin: u32, flags: u32) -> i32 { unsafe { gpio_configure(pin, flags) } }

/// Read pin state: returns `1` (high), `0` (low), or a negative error code.
#[inline] pub fn read(pin: u32) -> i32 { unsafe { gpio_read(pin) } }

/// Write `0` or `1` to an output pin.
#[inline] pub fn write(pin: u32, value: u32) -> i32 { unsafe { gpio_write(pin, value) } }
