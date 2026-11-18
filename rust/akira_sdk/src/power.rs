/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! Power management API.
//! Required capabilities: `power.read` and/or `power.control`.

// Power modes, as returned by get_mode(). Since AkiraOS 1.7 apps can't select
// LIGHT_SLEEP, DEEP_SLEEP or HIBERNATE: the firmware's power_set_mode()
// returns -ENOTSUP (-134) for them; only ACTIVE and IDLE are accepted.
pub const POWER_MODE_ACTIVE:      i32 = 0;
pub const POWER_MODE_IDLE:        i32 = 1;
pub const POWER_MODE_LIGHT_SLEEP: i32 = 2;
pub const POWER_MODE_DEEP_SLEEP:  i32 = 3;
pub const POWER_MODE_HIBERNATE:   i32 = 4;

pub const BATT_FLAG_CHARGING:     u8 = 1 << 0;
pub const BATT_FLAG_LOW_BATTERY:  u8 = 1 << 1;

extern "C" {
    fn power_get_mode() -> i32;
    fn power_get_battery_level() -> i32;
    fn power_get_battery_status(buf: *mut u8, len: i32) -> i32;
    fn wdt_pet() -> i32;
}

#[inline] pub fn get_mode() -> i32 { unsafe { power_get_mode() } }
#[inline] pub fn get_battery_level() -> i32 { unsafe { power_get_battery_level() } }

/// Read full battery status into a 12-byte buffer.
/// Layout: [0]=level%, [1]=flags, [4-7]=voltage_mv(i32 LE), [8-11]=current_ma(i32 LE).
#[inline]
pub fn get_battery_status(buf: &mut [u8; 12]) -> i32 {
    unsafe { power_get_battery_status(buf.as_mut_ptr(), 12) }
}

/// Report whether the system watchdog is running (`wdt_pet`, capability `wdt`).
///
/// Since AkiraOS 1.7 this does NOT feed the hardware watchdog; the OS feeds it
/// itself. Returns 0 if a watchdog is active, -ENODEV (-19) if not, -EPERM (-1)
/// without the capability.
#[inline] pub fn pet_watchdog() -> i32 { unsafe { wdt_pet() } }
