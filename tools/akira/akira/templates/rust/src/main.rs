#![no_std]
#![no_main]
use akira_sdk::display;

// akira_sdk provides the #[panic_handler]; do not define another here.
#[no_mangle]
pub extern "C" fn main() -> i32 {
    display::clear(display::COLOR_BLACK);
    display::text(10, 10, "Hello from __APP_NAME__", display::COLOR_CYAN);
    display::flush();
    0
}
