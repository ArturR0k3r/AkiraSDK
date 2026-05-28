# micropython.wasm Runtime

This directory holds the prebuilt `micropython.wasm` binary used by
`py_to_wasm.py` to package Python apps for AkiraOS.

## Obtaining micropython.wasm

### Option 1 — Download the prebuilt binary (recommended)

Check the AkiraOS releases page for a prebuilt `micropython.wasm` that includes
the `_akira` native C module:

```
https://github.com/AkiraOS/AkiraOS/releases
```

Place the downloaded file here as `micropython.wasm`.

### Option 2 — Build from source

Requirements: `emcc` (Emscripten) or `wasi-sdk` + `cmake` + `python3`

```bash
# Clone MicroPython
git clone https://github.com/micropython/micropython.git
cd micropython

# Build the mpy-cross compiler
make -C mpy-cross

# Copy the AkiraOS _akira native module
cp <AkiraSDK>/python/native/_akira.c ports/webassembly/modules/

# Build for wasm32 targeting WAMR (wasm32-unknown-unknown)
cd ports/webassembly
make MICROPY_WITH_AKIRA=1

# Copy the output
cp build/micropython.wasm <AkiraSDK>/python/runtime/micropython.wasm
```

See [PYTHON_GUIDE.md](../../docs/PYTHON_GUIDE.md) for full instructions.

## Notes

- `micropython.wasm` targets `wasm32-unknown-unknown` (bare-metal, no WASI)
- The `_akira` native module maps all `akira_*` WASM imports to Python callable objects
- Memory is limited to 256 KB by default (set at build time)
- MicroPython version: ≥1.24
