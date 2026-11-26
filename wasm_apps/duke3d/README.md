# Duke Nukem 3D for AkiraOS

[Chocolate Duke3D](https://github.com/fabiensanglard/chocolate_duke3D), via the
[ESP32 port by jkirsons](https://github.com/jkirsons/Duke3D) (upstream `4118f74`), as an
AkiraOS AOT app. Plays the shareware episode or your own v1.3D/1.4/1.5 `DUKE3D.GRP`. No sound.

## Build

Needs the WASI SDK (`WASI_SDK`, default `~/wasi-sdk`) and `wamrc` (`WAMRC`).

```bash
make aot     # build/duke3d.aot  (xtensa / esp32s3)
make data    # data/DUKE3D.GRP, shareware v1.3D from the Internet Archive, checksum-verified
make host    # host/out/duke3d_host: native 32-bit test build (see host/host_natives.c)
```

## Install

Copy to the SD card:

```
apps/duke3d.aot
apps/duke3d/DUKE3D.GRP
```

Start it from the launcher or with `app start duke3d`. The game writes `duke3d.cfg` and save
games next to the GRP.

## Memory (AkiraConsole Prod)

Fixed linear memory of 49 pages, 3.06 MiB (`MEMORY`): 2.11 MB static data and stack, ~0.45 MB
heap, and a 512 KB tile cache (`CACHE`). The AOT is 1.10 MB: the game at `-Os` (`OPT`), the
renderer and frame conversion at `-O3` (`FAST_OPT`). It plays at 13-19 fps. Firmware
requirements:

- `CONFIG_ESP_SPIRAM_HEAP_SIZE=5767168` (5.5 MiB)
- `CONFIG_AKIRA_WASM_APP_MEM_LIMIT_KB=5120`
- the runtime must not keep a second copy of the app file
- manifest `"stack_kb"` support: the app needs an 11 KB native stack (the default is 8 KB)

A bigger cache reads less from the SD card while playing (host, first 1000 frames: 2 MB reads
2.6 MB, 640 KB reads 4.7 MB). The log shows fps every 5 s, and `duke3d: no frame for ...` if a
frame takes longer than 3 s.

## Controls

| Button | In game | In menus |
|---|---|---|
| D-pad | move / turn | cursor |
| Y + left/right | strafe | |
| A | fire | select |
| B | open / use | back |
| X | jump | "yes" |
| Y + X | crouch | |
| Y + A | next weapon | |
| Y + B | use inventory item | |
| Y + up | map | |
| Y + down | next inventory item | |
| HOME tap | menu | |
| HOME hold | back to AkiraOS | |

## Changes to the vendored code

`duke3d/` is the ESP32 port's `Engine`, `Game` and SDL headers, plus four `audiolib` headers.
Changes:

- globals defined in headers made `extern` with one definition (WASM has no common symbols)
- `game.c`: `main` renamed `duke_main` (the runtime calls `main(void)` in `akira_sdl.c`)
- `cache.c`: game directory is the app sandbox; `tiles.c`: tile cache capped at `DUKE_CACHE_MAX`
- `esp32_compat.h`: its `mkdir` macro removed (`akira_shim.h` maps it)
- out-of-bounds accesses: negative texture columns wrapped (`engine.c`), automap spans clipped to
  the screen (`fillpolygon`), no `sector[-1]`/`sprite[-1]` reads (`parascan`, `WATERDRIP`)

ESP-IDF headers are stubbed in `stubs/`. `akira_sdl.c` replaces the port's SDL and LCD code,
`akira_nosound.c` its audiolib and joystick code, and `akira_shim.c` routes stdio, POSIX files
and directories to the `fs_*` natives.

## Licences

Game code: GPL-2.0 (`duke3d/LICENSE`). Build engine: Ken Silverman's Build licence, which
forbids commercial use (`duke3d/BUILDLIC.txt`). Never move this code into firmware `src/`. The
shareware data's licence allows individuals to copy the unmodified package; `make data`
downloads it at build time and it is not stored in this repository.
