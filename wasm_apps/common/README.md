# akira_ui — AkiraConsole shared UI kit (WASM side)

One consistent 1-bit visual language for WASM apps on the AkiraConsole display. This is the
WASM-app mirror of the native shell kit in `src/console_shell/ui/akira_ui.{h,c}`
— **identical design system, identical function signatures**, so a screen can be
reasoned about once and rendered on either layer.

**Header-only.** `akira_ui.h` is all `static inline`: the console SDK's
`akira_api.h` *defines* non-static helpers (printf/itoa), so it may only be
pulled into one translation unit per app. Apps are single-file (`main.c`), so
including the header there compiles the kit inline with zero duplicate symbols —
there is no `.c` to build or link.

## Design system

Every state is one of four primitives, in escalating pixel weight — never a hue:

1. **outline** (1px) — neutral / idle / unselected
2. **solid fill** (inverted) — selected / active / on
3. **dither** (50% checker) — caution / disabled / non-actionable
4. **heavy 3px stroke** — Capability-Guard confirmation **only** (`akira_ui_confirm_dialog`)

Only two colours are ever used: `AKIRA_UI_INK` (black) and `AKIRA_UI_PAPER`
(white). The kit is built on the *core* `akira_api.h` display subset
(`clear/pixel/rect/text/flush/get_size`) plus `akira_input_read_buttons` and
`akira_system_sleep`, so it links on any app regardless of which extended
primitives the runtime exposes.

## Usage

```c
#include "akira_ui.h"
#include "akira_icons.h"   /* wifi/subghz/lora/... 24x24 1-bit glyphs */

akira_ui_status_t sb = { .title = "wifi.scan", .clock = "12:00:00",
                         .battery_pct = 82, .show_bt = true, .bt_on = true,
                         .icon_bt = akira_icon_ble };
akira_ui_status_bar(&sb);

akira_ui_list_row(24, 20, "HomeNet_5G", "ch6", 4, /*selected=*/true);

/* Any restricted syscall must be gated: */
if (akira_ui_confirm_dialog("RF_RAW_TX", "confirm TX at 433.92 MHz?")) {
    akira_rf_send(frame, len);
}
```

Icons use the `akira_icons.h` format directly: MSB-first, row-major `uint8_t`
arrays, `(w+7)/8` bytes per row.

## Build

Nothing to build — just include the header in your app's `main.c`:

```c
#include "akira_api.h"
#include "../../common/akira_ui.h"
```

`make` in this dir runs a standalone header syntax-check and builds `ui_demo`.

## Migrating an app

Route these through the kit, delete the hand-rolled equivalents:

| Hand-rolled today            | Replace with                       |
|------------------------------|------------------------------------|
| custom top/status bar        | `akira_ui_status_bar()`            |
| list rows + selection tint   | `akira_ui_list_row()` (selection inverts) |
| home-style icon grid         | `akira_ui_grid_tile()`            |
| confirm-before-TX / deauth   | `akira_ui_confirm_dialog()` (**mandatory** for restricted syscalls) |
| rolling-code / error screens | `akira_ui_alert()`                |

Bespoke screens (spectrum waterfall, `rf.dial-radio` hero value) keep their
custom body but should still adopt the shared status bar and dialogs.
