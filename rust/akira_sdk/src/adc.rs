/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! ADC (Analog-to-Digital Converter) API.
//! Required capability: `adc`.

extern "C" {
    fn adc_read(channel: i32) -> i32;
    fn adc_read_mv(channel: i32) -> i32;
}

/// Read a raw ADC sample from the specified channel.
#[inline] pub fn read(channel: i32) -> i32 { unsafe { adc_read(channel) } }

/// Read an ADC channel and return the result in millivolts.
#[inline] pub fn read_mv(channel: i32) -> i32 { unsafe { adc_read_mv(channel) } }
