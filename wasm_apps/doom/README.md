# Doom for AkiraOS

[doomgeneric](https://github.com/ozkl/doomgeneric) (Chocolate Doom based) as an AkiraOS AOT app,
playing [Freedoom](https://freedoom.github.io/) Phase 1. No sound.

## Build

Needs the WASI SDK (`WASI_SDK`, default `~/wasi-sdk`) and `wamrc` (`WAMRC`).

```bash
make aot     # build/doom.aot  (xtensa / esp32s3)
make wad     # build/freedoom1.wad, downloaded from the Freedoom release and checksum-verified
```

## Install

Copy to the SD card:

```
apps/doom.aot
apps/doom/freedoom1.wad
```

Start it from the launcher or with `app start doom`. Any Doom IWAD named `freedoom1.wad` works
(e.g. your own `DOOM1.WAD`, renamed), within the memory limits below.

## Firmware requirements (AkiraConsole Prod)

Doom uses a fixed 3.7 MB linear memory (2.75 MB zone), plus ~0.9 MB for its AOT file and code,
all in one PSRAM block:

- `CONFIG_ESP_SPIRAM_HEAP_SIZE=5767168` (5.5 MiB)
- `CONFIG_AKIRA_WASM_APP_MEM_LIMIT_KB=5120`
- the runtime must not keep a second copy of the app file (AkiraOS `fix(runtime)` handover)

## Controls

| Button | In game | In menus |
|---|---|---|
| D-pad up/down | forward / back | cursor |
| D-pad left/right | turn | change value |
| Y + left/right | strafe | |
| A | fire | confirm |
| B | use / open | back |
| X / Y + X | next / previous weapon | |
| Y + A | automap | |
| HOME tap | menu | |
| HOME hold | back to AkiraOS | |

Run is always on.

## Licences

doomgeneric: GPL-2.0-or-later (`doomgeneric/LICENSE`). Freedoom data: BSD-style, downloaded at
build time, not stored in this repository.
