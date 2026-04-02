# AkiraOS Retro Game Emulation

Run classic console game ROMs on AkiraOS hardware.  A ROM file is compiled
into a self-contained `.aot` binary (Ahead-Of-Time compiled native code)
that can be deployed like any other AkiraOS app — via USB, BLE, or SD card.

> **WASM vs AOT** — ROM files can also be compiled to `.wasm` for interpreter
> execution, but AOT binaries run **10–50× faster** and are the recommended
> format for real hardware.

## Quick Start

```bash
# Convert a ROM to a deployable AOT binary (recommended)
python3 tools/rom_to_aot.py roms/nes/tetris.nes -o tetris.aot

# Deploy to AkiraOS (SD card example)
cp tetris.aot /media/$USER/AKIRA/apps/

# The app appears in the supervisor launcher and can be started normally
```

### WASM-only (interpreter)

```bash
# If you only need a .wasm file (slower, no wamrc required)
python3 tools/rom_to_wasm.py tetris.nes -o tetris.wasm
```

## Requirements

- [WASI SDK](https://github.com/WebAssembly/wasi-sdk/releases) installed at
  `/opt/wasi-sdk` (or specify `--wasi-sdk /path/to/wasi-sdk`)
- [wamrc](https://github.com/nicogig/AkiraOS) AOT compiler (for `.aot` builds)
- Python 3.6+
- AkiraOS hardware with PSRAM (ESP32-S3 recommended)

## Supported Platforms

| Platform | Extension | Status | Mapper support |
|---|---|---|---|
| NES | `.nes` | **Available** | 0, 1, 2, 3, 4, 7, 9, 66, 206 |
| Game Boy / GBC | `.gb` / `.gbc` | Coming soon | — |
| Sega Master System / Game Gear | `.sms` / `.gg` | Coming soon | — |
| Atari 2600 | `.a26` | Coming soon | — |
| SNES | `.sfc` / `.smc` | Coming soon | — |

### NES — Supported Mappers

| Mapper | Name | Example games |
|---|---|---|
| 0 | NROM | Donkey Kong, Balloon Fight, Ice Climber, Pinball, Tennis, Pac-Man, Galaxian, Super Mario Bros |
| 1 | MMC1 / SxROM | Mega Man 2, The Legend of Zelda, Metroid, Tetris (U), Final Fantasy |
| 2 | UxROM | Contra, Mega Man, Castlevania, Duck Tales |
| 3 | CNROM | Arkanoid, Gradius, Paperboy, Excitebike (PAL) |
| 4 | MMC3 / TxROM | Super Mario Bros. 3, Kirby's Adventure, Mega Man 3–6, Contra III |
| 7 | AxROM | Battletoads, Wizards & Warriors, Marble Madness |
| 9 | MMC2 / PxROM | Punch-Out!! |
| 66 | GxROM | Gumshoe, Super Mario Bros. + Duck Hunt (multicart) |
| 206 | Namco 108 | Galaga '90, Namco titles |

> **Unsupported mapper?** The emulator shows an error screen and returns to
> the supervisor.  File a request or add the mapper to `cores/nes/mapper.c`.

## Tool Usage

### rom_to_aot.py (recommended)

Two-stage pipeline: ROM → WASM → AOT native binary.

```
python3 tools/rom_to_aot.py <rom_file> [options]

Options:
  -o, --output <file>      Output AOT file (default: <rom_name>.aot)
  -p, --platform <name>    Platform: auto|nes (default: auto)
  -n, --name <name>        App name in manifest
  --wasi-sdk <path>        Path to WASI SDK (default: /opt/wasi-sdk)
  --wamrc <path>           Path to wamrc binary (default: auto-detect)
  --target <arch>          AOT target architecture (default: xtensa)
  --cpu <cpu>              AOT target CPU (default: esp32s3)
  --opt-level <0-3>        Optimization level (default: 2)
  --size-level <0-3>       Size optimization level (default: 2)
  --keep-wasm              Keep intermediate .wasm file
  -v, --verbose            Verbose output
```

#### AOT Examples

```bash
# Auto-detect platform, compile to AOT
python3 tools/rom_to_aot.py tetris.nes

# Keep the intermediate .wasm file
python3 tools/rom_to_aot.py galaga.nes -o galaga.aot --keep-wasm

# Maximum speed (larger binary)
python3 tools/rom_to_aot.py mario.nes --opt-level=3 --size-level=0

# Custom wamrc path, verbose output
python3 tools/rom_to_aot.py mario.nes --wamrc ~/tools/wamrc -v
```

### rom_to_wasm.py (interpreter only)

```
python3 tools/rom_to_wasm.py <rom_file> [options]

Options:
  -o, --output <file>      Output WASM file (default: <rom_name>.wasm)
  -p, --platform <name>    Platform: auto|nes|gb|sms|atari|snes
  -n, --name <name>        App name shown in launcher
  --wasi-sdk <path>        Path to WASI SDK (default: /opt/wasi-sdk)
  --dry-run                Print compile command without running it
  -v, --verbose            Verbose output
```

#### WASM Examples

```bash
# Auto-detect platform from file extension
python3 tools/rom_to_wasm.py tetris.nes

# Custom output name and app label
python3 tools/rom_to_wasm.py donkey_kong.nes -o dk.wasm -n "Donkey Kong"

# Show what would be compiled (no files created)
python3 tools/rom_to_wasm.py mario.nes --dry-run

# Use a non-default WASI SDK path
python3 tools/rom_to_wasm.py mario.nes --wasi-sdk ~/tools/wasi-sdk-21
```

## Controls (akiraconsole hardware)

| AkiraOS Button | GPIO | NES mapping |
|---|---|---|
| D-pad Up | 4 | Up |
| D-pad Down | 5 | Down |
| D-pad Left | 6 | Left |
| D-pad Right | 7 | Right |
| A | 15 | A |
| B | 16 | B |
| Settings | 2 | Start / Pause menu |
| X | 17 | Select |

**Pause menu** — Press Settings during gameplay to open the pause overlay.
Options: Resume, Restart, Exit to supervisor.

## Display

NES games render at 256×240 pixels, centered on the 320×240 AkiraOS display
with 32-pixel black bars on each side.  Output is RGB565.

## Memory

The tool automatically calculates the required WASM memory based on ROM size:

| ROM size | WASM memory allocated |
|---|---|
| ≤ 32 KB (Mapper 0) | 256 KB |
| 32–96 KB | 320 KB |
| 96–224 KB | 448 KB |
| 224–480 KB | 768 KB |

AkiraOS hardware must have PSRAM available (ESP32-S3 has 8 MB).
The `native_sim` build target also supports emulator apps.

## Adding New Platforms

To add support for a new platform (e.g. Game Boy):

1. Create `cores/gb/` with:
   - `gb.h` / `gb.c` — emulator core
   - `main.c` — AkiraOS app wrapper
   - `manifest.json` — capabilities
   - `Makefile` — build rules
2. Add an entry to `tools/rom_to_wasm.py` `PLATFORMS` dict with
   the extensions, magic bytes, and source file list.
3. Test with `--dry-run` before a full build.

## Directory Structure

```
retro/
├── cores/
│   └── nes/                    NES emulator source
│       ├── cpu6502.h/c         MOS 6502 CPU interpreter
│       ├── mapper.h/c          ROM mapper abstraction (mappers 0,1,2,3,4,7,9,66,206)
│       ├── ppu.h/c             Picture Processing Unit renderer
│       ├── nes.h/c             NES machine top-level
│       ├── main.c              AkiraOS app entry point
│       ├── manifest.json       Capabilities manifest
│       └── Makefile            Manual build rules
├── roms/
│   └── nes/                    Your .nes ROM files go here
└── tools/
    ├── rom_to_aot.py           ROM → AOT packaging tool (recommended)
    └── rom_to_wasm.py          ROM → WASM packaging tool
```
