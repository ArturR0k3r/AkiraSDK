# Akira SDK — WASM App Development Guide 🚀

This `AkiraSDK` folder provides the headers and samples you need to build
WebAssembly applications for AkiraOS. The SDK is intentionally small and
works in two modes:

- **Header-only (only mode)**
  - You compile against `include/akira_api.h` and the runtime supplies the
    implementations (via dynamic native registration). The SDK is intentionally
    header-only — this is the only supported usage for building apps.

- **`sdk_export` (optional helper)**
  - When working inside the full `AkiraOS` repository you may provide an
    optional `sdk_export` component that builds `libakira_api.a` for local
    linking. This is a development convenience only and does not change the
    SDK's header-only nature.

Contents
 - include/akira_api.h — canonical app-facing header
 - wasm_apps/ — sample apps (Makefile + small manifests)
 - build_wasm_app.sh — small helper script to build apps easily
 - CMakeLists.txt — enables optional `sdk_export` usage, otherwise header-only

Prerequisites
 - WASI SDK installed (clang/ld). Typical path: `/opt/wasi-sdk`.

Quick Start — recommended workflow
1. Clone the repo and install WASI SDK.
2. Build a sample with the helper script (recommended):

```bash
cd AkiraSDK/wasm_apps/hello_world
../../build_wasm_app.sh -o hello_world.wasm main.c
```

3. Or use the sample Makefile (it uses the helper script):

```bash
cd AkiraSDK/wasm_apps/sensor_demo
make
```

4. Optional CMake flow (CMake sample will link `akira_api` only if `sdk_export`
   is present):

```bash
cd AkiraSDK/wasm_apps/hello_world
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/wasi-sdk/share/cmake/wasi-sdk.cmake
cmake --build .
```

Build script usage
- The helper script detects the WASI SDK and sets appropriate flags for
  libc-builtin builds (-nostdlib, -Wl,--allow-undefined). It also adds
  `AkiraSDK/include` to include paths and will prefer an OS-provided
  `sdk_export` (if `AKIRA_OS_ROOT` is set).

Header-only details
- Apps compiled in header-only mode will contain undefined imports for
  `akira_*` symbols; the host runtime must provide them (that's normal for
  WAMR libc-builtin). The SDK does not ship function stubs by default.

Integrators: runtime registration
- To expose host functions to WASM apps register a native module using the
  generic OCRE API: `ocre_register_native_module("akira", symbols, count)`.
  See `AkiraOS/src/akira/akira_native_exports.c` for an example of registering
  display, storage, and HTTP helpers.

Samples & Manifests
- Each sample in `wasm_apps/` contains a `manifest.json` describing required
  permissions (e.g., gpio, sensor, storage). Make sure the runtime grants
  those capabilities to the app before executing it.

Testing & CI
- Add CI that runs a few representative builds (e.g., `hello_world`,
  `sensor_demo`, `blink_led`) using `build_wasm_app.sh` to catch regressions.

Contributing samples
- Add new samples under `wasm_apps/` with:
  - `main.c`
  - `manifest.json`
  - Makefile that uses `../../build_wasm_app.sh`

Extras & Troubleshooting
- If your app can't find `akira_api.h` when using the helper script, ensure
  you're running it from a sample directory (e.g., `wasm_apps/sensor_demo`) so
  the script can add the SDK include path.
- If you need local linking against `akira_api`, add `sdk_export` in the root
  of the repo or set `AKIRA_OS_ROOT` to point to the AkiraOS repository.

------
If you'd like I can:
- Add a CI job to verify sample builds (I recommend doing this), or
- Add a short CONTRIBUTING guide for adding samples.

Happy hacking! ✨
# Akira SDK (samples + headers)

This folder contains the Akira SDK used by embedded WebAssembly samples for AkiraOS.

Design goals (aligned with project-ocre/ocre-sdk):
- Provide a simple CMake consumable SDK (target: `akira_api`).
- Include sample WASM apps (Makefile-based and CMake-based examples).
- Provide a small build helper script at `scripts/build_wasm_app.sh` for Makefile workflows.

Getting started (CMake sample):

1. From a sample's CMake project (e.g. `wasm_apps/hello_world`), add the SDK as a subdirectory:

```cmake
set(AKIRA_SDK_PATH "../../")
if(NOT EXISTS "${AKIRA_SDK_PATH}/CMakeLists.txt")
  message(FATAL_ERROR "Akira SDK not found at ${AKIRA_SDK_PATH}")
endif()
add_subdirectory(${AKIRA_SDK_PATH} akira-sdk-build)

add_executable(hello-world.wasm main.c)
if(TARGET akira_api)
  target_link_libraries(hello-world.wasm PRIVATE akira_api)
else()
  # Header-only mode: runtime must provide implementations at execution time
  message(WARNING "Building header-only; runtime must provide akira_api implementations")
endif()
```

Standalone build
-----------------
The SDK is header-only: samples compile against `akira_api.h` and rely on the
host runtime to provide implementations at execution time (via native module
registration with OCRE). If you need a local static library for development
you may provide a `sdk_export` component to build `libakira_api.a`, but this
is optional and not a separate SDK mode.

To build when `sdk_export` is present:

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

This will produce `libakira_api.a` (if `sdk_export` is present) and optionally
`libocre_api.a` if the compatibility target is available.

2. The SDK also supports Makefile workflows; use `scripts/build_wasm_app.sh` or per-sample Makefiles.

Notes & suggestions
- Add `CMakeLists.txt` to samples to demonstrate CMake-based builds (WASI toolchain integration).
- Consider adding an `install()` step and packaging rules later (for system-wide use).

Using the AkiraOS `sdk_export`
-----------------------------
If you're developing within the full `AkiraOS` tree, the SDK will prefer the
`sdk_export` provided by the OS. For the helper script and Makefile flows set
the `AKIRA_OS_ROOT` environment variable to the path of the repository root
so the build script can pick up the exported `akira_api` sources / headers:

```bash
export AKIRA_OS_ROOT=/path/to/Akira
./scripts/build_wasm_app.sh -o blink_led.wasm main.c
```

If `sdk_export` is available, CMake samples will automatically add it and
alias the `akira_api` target so samples can link it. The default and only
SDK usage remains header-only: apps built with the SDK depend on the runtime
to provide `akira_*` functions unless you explicitly link against a local
`libakira_api.a` produced by `sdk_export`.
