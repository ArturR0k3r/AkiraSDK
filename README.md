# Akira SDK — Quick Start (for newcomers) 🎯

Short and simple — this folder contains the headers and tiny samples you
need to build WebAssembly apps for AkiraOS.

What it is
- **Header-only SDK** — include `include/akira_api.h` in your app. The
  runtime provides implementations (you do not link a local library by
  default).

Prerequisites
- Install a WASI toolchain (wasi-sdk). Typical path: `/opt/wasi-sdk`.

Quick Start — build a sample
1. Build the small hello-world sample:

```bash
cd AkiraSDK/wasm_apps/hello_world
../../build.sh -o hello_world.wasm main.c
```

2. Build the sensor demo:

```bash
cd AkiraSDK/wasm_apps/sensor_demo
../../build.sh -o sensor_demo.wasm main.c
```

Notes
- The SDK is header-only. Apps will have undefined `akira_*` imports that the
  host runtime must resolve at execution time (this is expected).
- If you need to test against a local AkiraOS checkout, add an include path:

```bash
../../build.sh -I /path/to/Akira/include -o my_app.wasm main.c
```

Using CMake
- `CMakeLists.txt` provided in samples links an INTERFACE target `akira_api` so
  CMake projects can call `target_link_libraries(... PRIVATE akira_api)` and
  get the headers automatically.

Troubleshooting
- If `akira_api.h` is not found, ensure you're running `build.sh` from inside
  a sample folder, or pass `-I` with the path to `include/`.

Want help?
- I can add a tiny CI check that builds the samples and verifies they produce
  wasm artifacts — say the word and I'll add it.

Happy hacking! ✨
# Akira SDK — WASM App Development Guide 🚀

This `AkiraSDK` folder provides the headers and samples you need to build
WebAssembly applications for AkiraOS. The SDK is intentionally small and is
header-only: apps compile against `include/akira_api.h` and the runtime
supplies implementations (via dynamic native registration). This is the
only supported SDK usage.

Contents
 - include/akira_api.h — canonical app-facing header
 - wasm_apps/ — sample apps (Makefile + small manifests)
 - build_wasm_app.sh — small helper script to build apps easily
 - CMakeLists.txt — provides a header-only INTERFACE target `akira_api`

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

4. Optional CMake flow (CMake sample uses the header-only INTERFACE target):

```bash
cd AkiraSDK/wasm_apps/hello_world
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/wasi-sdk/share/cmake/wasi-sdk.cmake
cmake --build .
```

Build script usage
- The helper script detects the WASI SDK and sets appropriate flags for
  libc-builtin builds (-nostdlib, -Wl,--allow-undefined). It also adds
  `AkiraSDK/include` to include paths.

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
 - If you need to use a local copy of `akira_api.h` from the AkiraOS tree,
   add an include path that points to your checkout (for example `-I/path/to/Akira/include`).

Vendoring / local testing
- To test against a local AkiraOS checkout, you can either:
  - Add an include path when using the helper script:

    ```bash
    ../../build_wasm_app.sh -I /path/to/Akira/include -o my_app.wasm main.c
    ```

  - Or, for CMake-based samples, add the AkiraOS include directory to your
    target and (optionally) pass implementation sources if you want to link
    a local implementation during development:

    ```cmake
    target_include_directories(hello-world.wasm PRIVATE /path/to/Akira/include)
    target_sources(hello-world.wasm PRIVATE /path/to/Akira/src/akira_api.c)
    target_link_libraries(hello-world.wasm PRIVATE akira_api)
    ```

  Notes:
  - The SDK itself remains header-only; these steps are for local testing or
    development of runtime APIs and are not required for normal app authors.
  - If you prefer copying headers into the SDK for an isolated test, place
    `akira_api.h` into `AkiraSDK/include/` (for quick local experiments).

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
target_link_libraries(hello-world.wasm PRIVATE akira_api)
```

Standalone build
-----------------
The SDK is header-only: samples compile against `akira_api.h` and rely on the
host runtime to provide implementations at execution time (via native module
registration with OCRE).

2. The SDK also supports Makefile workflows; use `scripts/build_wasm_app.sh` or per-sample Makefiles.

Notes & suggestions
- Add `CMakeLists.txt` to samples to demonstrate CMake-based builds (WASI toolchain integration).
- Consider adding an `install()` step and packaging rules later (for system-wide use).

Using AkiraOS
-------------
If you're developing within the full `AkiraOS` tree you may still consume
`akira_api.h` from the OS source tree directly, but `AkiraSDK` itself is
header-only and does not attempt to link any runtime source files.
