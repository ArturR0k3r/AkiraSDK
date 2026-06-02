# micropython.wasm Runtime

This directory holds the `micropython.wasm` binary used by `py_to_wasm.py`
to package Python apps for AkiraOS.

`micropython.wasm` must be **built from source** — there is no prebuilt
binary. Run the build script once; afterwards only `py_to_wasm.py` is needed
for each new app.

## Build from source

Requirements: `emcc` (Emscripten), `python3`, `make`, `git`

```bash
# Install Emscripten (once)
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh

# Build micropython.wasm
source ~/emsdk/emsdk_env.sh
bash AkiraSDK/python/runtime/build.sh
```

Output: `AkiraSDK/python/runtime/micropython.wasm` (~290 KB)

Build time: ~5 minutes on first run. Subsequent runs reuse the cached clone.

## Internals

- Built from the upstream MicroPython `webassembly` port with AkiraOS patches
- Patches applied by `build.sh`:
  - Injects `_akira.c` — C extension module wrapping all AkiraOS native APIs
  - Replaces `mphalport.c` with AkiraOS HAL (stdout → `printf_native`)
  - Sets fixed 384 KB WASM memory (`INITIAL_MEMORY=393216`, no growth)
  - `SUPPORT_LONGJMP=none` — no invoke_* or WASM exceptions; compatible with WAMR
  - `NO_EXIT_RUNTIME=1` — prevents exit trap after main() returns
- Post-processed with `wasm-opt -Oz` (Binaryen)
- Python script injected by `py_to_wasm.py` as a data segment at `0x30000`

## Notes

- Import the native module as `import _akira as akira` in your Python apps
- Memory: 384 KB fixed (6 WASM pages); MicroPython heap is ~128 KB of that
- MicroPython version: latest upstream (cloned at build time)
- `_akira` module exports the full AkiraOS API — see `python/native/_akira.c`
