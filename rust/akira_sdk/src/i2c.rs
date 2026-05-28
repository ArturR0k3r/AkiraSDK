/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! I2C register access.
//! Required capability: `i2c`.

extern "C" {
    fn i2c_write_reg(bus_id: i32, dev_addr: i32, reg_addr: i32, buf: *const u8, len: u32) -> i32;
    fn i2c_read_reg(bus_id: i32, dev_addr: i32, reg_addr: i32, buf: *mut u8, len: u32) -> i32;
}

#[inline]
pub fn write_reg(bus_id: i32, dev_addr: i32, reg_addr: i32, buf: &[u8]) -> i32 {
    unsafe { i2c_write_reg(bus_id, dev_addr, reg_addr, buf.as_ptr(), buf.len() as u32) }
}

#[inline]
pub fn read_reg(bus_id: i32, dev_addr: i32, reg_addr: i32, buf: &mut [u8]) -> i32 {
    unsafe { i2c_read_reg(bus_id, dev_addr, reg_addr, buf.as_mut_ptr(), buf.len() as u32) }
}
