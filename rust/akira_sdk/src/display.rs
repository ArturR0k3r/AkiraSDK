/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

//! Display API — RGB565 graphics primitives.
//! Required capability: `display.write`.

use crate::str_to_cbuf;

// ─── RGB565 color constants ───────────────────────────────────────────────────
pub const COLOR_BLACK:      u32 = 0x0000;
pub const COLOR_WHITE:      u32 = 0xFFFF;
pub const COLOR_RED:        u32 = 0xF800;
pub const COLOR_GREEN:      u32 = 0x07E0;
pub const COLOR_BLUE:       u32 = 0x001F;
pub const COLOR_YELLOW:     u32 = 0xFFE0;
pub const COLOR_CYAN:       u32 = 0x07FF;
pub const COLOR_MAGENTA:    u32 = 0xF81F;
pub const COLOR_GRAY:       u32 = 0x7BEF;
pub const COLOR_DARK_GRAY:  u32 = 0x39E7;
pub const COLOR_LIGHT_GRAY: u32 = 0xC618;
pub const COLOR_ORANGE:     u32 = 0xFD20;
pub const COLOR_PURPLE:     u32 = 0x801F;

extern "C" {
    fn display_clear(color: u32) -> i32;
    fn display_pixel(x: i32, y: i32, color: u32) -> i32;
    fn display_rect(x: i32, y: i32, w: i32, h: i32, color: u32) -> i32;
    fn display_rect_outline(x: i32, y: i32, w: i32, h: i32, color: u32) -> i32;
    fn display_text(x: i32, y: i32, text: *const u8, color: u32) -> i32;
    fn display_text_large(x: i32, y: i32, text: *const u8, color: u32) -> i32;
    fn display_number(x: i32, y: i32, value: i32, color: u32) -> i32;
    fn display_flush() -> i32;
    fn display_get_size(w_out: *mut i32, h_out: *mut i32) -> i32;
    fn display_line(x0: i32, y0: i32, x1: i32, y1: i32, color: u32) -> i32;
    fn display_hline(x: i32, y: i32, len: i32, color: u32) -> i32;
    fn display_vline(x: i32, y: i32, len: i32, color: u32) -> i32;
    fn display_circle(cx: i32, cy: i32, r: i32, color: u32) -> i32;
    fn display_circle_fill(cx: i32, cy: i32, r: i32, color: u32) -> i32;
    fn display_triangle(x0: i32, y0: i32, x1: i32, y1: i32, x2: i32, y2: i32, color: u32) -> i32;
    fn display_triangle_fill(x0: i32, y0: i32, x1: i32, y1: i32, x2: i32, y2: i32, color: u32) -> i32;
    fn display_rounded_rect(x: i32, y: i32, w: i32, h: i32, radius: i32, color: u32) -> i32;
    fn display_rounded_rect_fill(x: i32, y: i32, w: i32, h: i32, radius: i32, color: u32) -> i32;
    fn display_progress_bar(x: i32, y: i32, w: i32, h: i32, value: i32, max_val: i32, fg: u32, bg: u32) -> i32;
    fn display_bitmap(x: i32, y: i32, w: i32, h: i32, data: *const u16, data_size: u32) -> i32;
    fn display_bitmap_transparent(x: i32, y: i32, w: i32, h: i32, data: *const u16, data_size: u32, key: u32) -> i32;
    fn display_raw_write(x: i32, y: i32, w: i32, h: i32, data: *const u16, data_size: u32) -> i32;
}

/// Clear the entire display with `color`.
#[inline] pub fn clear(color: u32) -> i32 { unsafe { display_clear(color) } }

/// Set a single pixel.
#[inline] pub fn pixel(x: i32, y: i32, color: u32) -> i32 { unsafe { display_pixel(x, y, color) } }

/// Draw a filled rectangle.
#[inline] pub fn rect(x: i32, y: i32, w: i32, h: i32, color: u32) -> i32 { unsafe { display_rect(x, y, w, h, color) } }

/// Draw a rectangle outline.
#[inline] pub fn rect_outline(x: i32, y: i32, w: i32, h: i32, color: u32) -> i32 { unsafe { display_rect_outline(x, y, w, h, color) } }

/// Draw small-font text (7×10 px/char).
#[inline]
pub fn text(x: i32, y: i32, s: &str, color: u32) -> i32 {
    let mut buf = [0u8; 256];
    str_to_cbuf(s, &mut buf);
    unsafe { display_text(x, y, buf.as_ptr(), color) }
}

/// Draw large-font text (11×18 px/char).
#[inline]
pub fn text_large(x: i32, y: i32, s: &str, color: u32) -> i32 {
    let mut buf = [0u8; 256];
    str_to_cbuf(s, &mut buf);
    unsafe { display_text_large(x, y, buf.as_ptr(), color) }
}

/// Render an integer as decimal text.
#[inline] pub fn number(x: i32, y: i32, value: i32, color: u32) -> i32 { unsafe { display_number(x, y, value, color) } }

/// Flush the back-buffer to the display hardware.
#[inline] pub fn flush() -> i32 { unsafe { display_flush() } }

/// Return `(width, height)` of the display, or `(-1, -1)` on error.
#[inline]
pub fn get_size() -> (i32, i32) {
    let mut w = 0i32;
    let mut h = 0i32;
    unsafe { display_get_size(&mut w, &mut h) };
    (w, h)
}

/// Draw a line between two points.
#[inline] pub fn line(x0: i32, y0: i32, x1: i32, y1: i32, color: u32) -> i32 { unsafe { display_line(x0, y0, x1, y1, color) } }

/// Draw a horizontal run of pixels.
#[inline] pub fn hline(x: i32, y: i32, len: i32, color: u32) -> i32 { unsafe { display_hline(x, y, len, color) } }

/// Draw a vertical run of pixels.
#[inline] pub fn vline(x: i32, y: i32, len: i32, color: u32) -> i32 { unsafe { display_vline(x, y, len, color) } }

/// Draw a circle outline.
#[inline] pub fn circle(cx: i32, cy: i32, r: i32, color: u32) -> i32 { unsafe { display_circle(cx, cy, r, color) } }

/// Draw a filled circle.
#[inline] pub fn circle_fill(cx: i32, cy: i32, r: i32, color: u32) -> i32 { unsafe { display_circle_fill(cx, cy, r, color) } }

/// Draw a triangle outline.
#[inline] pub fn triangle(x0: i32, y0: i32, x1: i32, y1: i32, x2: i32, y2: i32, color: u32) -> i32 { unsafe { display_triangle(x0, y0, x1, y1, x2, y2, color) } }

/// Draw a filled triangle.
#[inline] pub fn triangle_fill(x0: i32, y0: i32, x1: i32, y1: i32, x2: i32, y2: i32, color: u32) -> i32 { unsafe { display_triangle_fill(x0, y0, x1, y1, x2, y2, color) } }

/// Draw a rounded rectangle outline.
#[inline] pub fn rounded_rect(x: i32, y: i32, w: i32, h: i32, radius: i32, color: u32) -> i32 { unsafe { display_rounded_rect(x, y, w, h, radius, color) } }

/// Draw a filled rounded rectangle.
#[inline] pub fn rounded_rect_fill(x: i32, y: i32, w: i32, h: i32, radius: i32, color: u32) -> i32 { unsafe { display_rounded_rect_fill(x, y, w, h, radius, color) } }

/// Draw a horizontal progress bar.
#[inline] pub fn progress_bar(x: i32, y: i32, w: i32, h: i32, value: i32, max_val: i32, fg: u32, bg: u32) -> i32 { unsafe { display_progress_bar(x, y, w, h, value, max_val, fg, bg) } }

/// Blit an RGB565 bitmap.
#[inline]
pub fn bitmap(x: i32, y: i32, w: i32, h: i32, data: &[u16]) -> i32 {
    unsafe { display_bitmap(x, y, w, h, data.as_ptr(), (data.len() * 2) as u32) }
}

/// Blit an RGB565 bitmap with a transparent colour key.
#[inline]
pub fn bitmap_transparent(x: i32, y: i32, w: i32, h: i32, data: &[u16], key: u32) -> i32 {
    unsafe { display_bitmap_transparent(x, y, w, h, data.as_ptr(), (data.len() * 2) as u32, key) }
}

/// Write a packed RGB565 buffer directly to display hardware (fastest path).
#[inline]
pub fn raw_write(x: i32, y: i32, w: i32, h: i32, data: &[u16]) -> i32 {
    unsafe { display_raw_write(x, y, w, h, data.as_ptr(), (data.len() * 2) as u32) }
}
