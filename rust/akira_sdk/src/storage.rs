/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! Sandboxed file storage API.
//! Required capabilities: `storage.read` and/or `storage.write`.

pub const O_READ:   i32 = 0;
pub const O_WRITE:  i32 = 1;
pub const O_APPEND: i32 = 2;
pub const O_RDWR:   i32 = 3;

extern "C" {
    fn storage_open(path: *const u8, flags: i32) -> i32;
    fn storage_read(fd: i32, buf: *mut u8, len: i32) -> i32;
    fn storage_write(fd: i32, buf: *const u8, len: i32) -> i32;
    fn storage_close(fd: i32);
    fn storage_delete(path: *const u8) -> i32;
    fn storage_list(path: *const u8, buf: *mut u8, len: i32) -> i32;
}

/// Open a file relative to the app sandbox.
#[inline]
pub fn open(path: &str, flags: i32) -> i32 {
    let mut buf = [0u8; 256];
    crate::str_to_cbuf(path, &mut buf);
    unsafe { storage_open(buf.as_ptr(), flags) }
}

/// Read up to `buf.len()` bytes from an open fd.
#[inline]
pub fn read(fd: i32, buf: &mut [u8]) -> i32 {
    unsafe { storage_read(fd, buf.as_mut_ptr(), buf.len() as i32) }
}

/// Write `buf` to an open fd.
#[inline]
pub fn write(fd: i32, buf: &[u8]) -> i32 {
    unsafe { storage_write(fd, buf.as_ptr(), buf.len() as i32) }
}

/// Close an open fd.
#[inline]
pub fn close(fd: i32) {
    unsafe { storage_close(fd) }
}

/// Delete a file from the app sandbox.
#[inline]
pub fn delete(path: &str) -> i32 {
    let mut buf = [0u8; 256];
    crate::str_to_cbuf(path, &mut buf);
    unsafe { storage_delete(buf.as_ptr()) }
}

/// List files in a sandbox directory into `out`.  Returns bytes written.
#[inline]
pub fn list(path: &str, out: &mut [u8]) -> i32 {
    let mut pbuf = [0u8; 256];
    crate::str_to_cbuf(path, &mut pbuf);
    unsafe { storage_list(pbuf.as_ptr(), out.as_mut_ptr(), out.len() as i32) }
}
