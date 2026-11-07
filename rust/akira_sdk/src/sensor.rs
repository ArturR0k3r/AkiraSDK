/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! Sensor reading API.
//! Required capability: `sensor.read`.
//!
//! All readings are scaled by 1000 — divide by 1000.0 to get the physical value.

pub const SENSOR_CHAN_ACCEL_X:      i32 = 0;
pub const SENSOR_CHAN_ACCEL_Y:      i32 = 1;
pub const SENSOR_CHAN_ACCEL_Z:      i32 = 2;
pub const SENSOR_CHAN_GYRO_X:       i32 = 4;
pub const SENSOR_CHAN_GYRO_Y:       i32 = 5;
pub const SENSOR_CHAN_GYRO_Z:       i32 = 6;
pub const SENSOR_CHAN_MAGN_X:       i32 = 8;
pub const SENSOR_CHAN_MAGN_Y:       i32 = 9;
pub const SENSOR_CHAN_MAGN_Z:       i32 = 10;
pub const SENSOR_CHAN_AMBIENT_TEMP: i32 = 13;
pub const SENSOR_CHAN_PRESS:        i32 = 14;
pub const SENSOR_CHAN_HUMIDITY:     i32 = 16;
pub const SENSOR_CHAN_ALTITUDE:     i32 = 23;
pub const SENSOR_CHAN_VOLTAGE:      i32 = 33;
pub const SENSOR_CHAN_CURRENT:      i32 = 35;
pub const SENSOR_CHAN_POWER:        i32 = 36;

/// Sentinel returned on sensor error (equivalent to `INT32_MIN`).
pub const SENSOR_ERROR: i32 = i32::MIN;

extern "C" {
    fn sensor_read(channel: i32) -> i32;
}

/// Read a sensor channel.  Returns the value ×1000 on success, or `SENSOR_ERROR`.
#[inline]
pub fn read(channel: i32) -> i32 {
    unsafe { sensor_read(channel) }
}

/// Read a sensor channel and convert to `f32`.  Returns `None` on error.
#[inline]
pub fn read_f32(channel: i32) -> Option<f32> {
    let raw = unsafe { sensor_read(channel) };
    if raw == SENSOR_ERROR {
        None
    } else {
        Some(raw as f32 / 1000.0)
    }
}
