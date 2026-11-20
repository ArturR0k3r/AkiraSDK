/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! PWM output control.
//! Required capability: `pwm`.

extern "C" {
    fn pwm_set(channel: i32, freq_hz: i32, duty_pct: i32) -> i32;
    fn pwm_disable(channel: i32) -> i32;
}

/// Set a PWM channel's frequency (Hz) and duty cycle (0–100%).
#[inline] pub fn set(channel: i32, freq_hz: i32, duty_pct: i32) -> i32 { unsafe { pwm_set(channel, freq_hz, duty_pct) } }

/// Disable a PWM channel (output held low).
#[inline] pub fn disable(channel: i32) -> i32 { unsafe { pwm_disable(channel) } }
