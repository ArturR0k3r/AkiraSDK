#!/usr/bin/env bash
# build.sh — Build micropython.wasm for AkiraOS
#
# Requires: git, python3, make, emcc (Emscripten)
#
# Install Emscripten first:
#   git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
#   ~/emsdk/emsdk install latest
#   ~/emsdk/emsdk activate latest
#   source ~/emsdk/emsdk_env.sh
#
# Then run:
#   bash python/runtime/build.sh
#
# Output: python/runtime/micropython.wasm

set -e

SDK_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK_DIR="$SDK_ROOT/python/runtime/_build"
AKIRA_C="$SDK_ROOT/python/native/_akira.c"
OUT="$SDK_ROOT/python/runtime/micropython.wasm"

# ── Check emcc ────────────────────────────────────────────────────────────────
if ! command -v emcc &>/dev/null; then
    echo "ERROR: emcc not found. Install Emscripten:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git ~/emsdk"
    echo "  ~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest"
    echo "  source ~/emsdk/emsdk_env.sh"
    exit 1
fi

echo "[1/5] emcc $(emcc --version | head -1)"

# ── Clone MicroPython ─────────────────────────────────────────────────────────
MP_DIR="$WORK_DIR/micropython"
if [ ! -d "$MP_DIR" ]; then
    echo "[2/5] Cloning MicroPython..."
    mkdir -p "$WORK_DIR"
    git clone --depth=1 https://github.com/micropython/micropython.git "$MP_DIR"
else
    echo "[2/5] MicroPython already cloned."
fi

# ── Init submodules ───────────────────────────────────────────────────────────
echo "[3a/5] Initializing submodules..."
make -C "$MP_DIR/ports/webassembly" submodules 2>&1 | tail -5

# ── Build mpy-cross (native host tool) ───────────────────────────────────────
echo "[3b/5] Building mpy-cross..."
make -C "$MP_DIR/mpy-cross" -j$(nproc) 2>&1 | tail -3

# ── Patch: strip browser deps, small memory, export main ─────────────────────
MK="$MP_DIR/ports/webassembly/Makefile"
AKIRA_MEM=393216   # 6 WASM pages = 384 KB

if ! grep -q "AKIRA_PATCHED" "$MK"; then
    # 1) Remove JS proxy source files (eliminates call0/lookup_attr/etc. imports)
    sed -i '/^\tmodjs\.c \\/d;/^\tmodjsffi\.c \\/d;/^\tobjjsproxy\.c \\/d;/^\tproxy_c\.c \\/d' "$MK"

    # 2) Remove proxy_c exports (these are referenced by removed files)
    sed -i '/^[[:space:]]*_proxy_c_/d' "$MK"

    # 3) Remove JS library (only needed by proxy files)
    sed -i '/--js-library library\.js/d' "$MK"

    # 4) Remove MODULARIZE (browser-only, irrelevant for WAMR .wasm loading)
    sed -i 's/-s MODULARIZE -s EXPORT_NAME=_createMicroPythonModule//' "$MK"


    # 5) SUPPORT_LONGJMP=none: no invoke_* wrappers, no WASM exception instructions.
    # setjmp is stubbed to always return 0; longjmp calls _emscripten_throw_longjmp.
    # WAMR can load and run this — Python exceptions that escape to C will trap
    # (call _emscripten_throw_longjmp stub) but simple scripts work fine.
    # Must be set in BOTH CFLAGS (compile time) and JSFLAGS (link time).
    sed -i 's/-s SUPPORT_LONGJMP=emscripten/-s SUPPORT_LONGJMP=none/' "$MK"
    sed -i '/^CFLAGS += -std=/a CFLAGS += -sSUPPORT_LONGJMP=none' "$MK"

    # 6) Append AkiraOS JSFLAGS block
    cat >> "$MK" << 'EOF'

# AkiraOS patches — do not remove this marker
# AKIRA_PATCHED
JSFLAGS += -s FILESYSTEM=0
JSFLAGS += -s INITIAL_MEMORY=393216
JSFLAGS += -s MAXIMUM_MEMORY=393216
JSFLAGS += -s ALLOW_MEMORY_GROWTH=0
JSFLAGS += -s STACK_SIZE=32768
JSFLAGS += -Wl,--allow-undefined
JSFLAGS += -s NO_EXIT_RUNTIME=1
# Override runtime method exports — no FS/PATH/PATH_FS, those require FILESYSTEM=1
JSFLAGS += -s EXPORTED_RUNTIME_METHODS="ccall,cwrap,HEAPU8,UTF8ToString,getValue,lengthBytesUTF8,setValue,stringToUTF8"
EOF
fi

# ── Patch: suppress unused-but-set-global in main.c ─────────────────────────
MAIN_C="$MP_DIR/ports/webassembly/main.c"
if ! grep -q "Wno-unused-but-set-global" "$MAIN_C"; then
    sed -i '1s/^/#pragma clang diagnostic ignored "-Wunused-but-set-global"\n/' "$MAIN_C"
fi

# ── Patch: inject AkiraOS source files ───────────────────────────────────────
echo "[4/5] Injecting _akira module..."

# Copy our source files into the port directory
cp "$SDK_ROOT/python/native/_akira.c"      "$MP_DIR/ports/webassembly/_akira.c"
cp "$SDK_ROOT/python/native/akira_stubs.c" "$MP_DIR/ports/webassembly/akira_stubs.c"
# Replace mphalport.c with our AkiraOS version
cp "$SDK_ROOT/python/native/mphalport.c"   "$MP_DIR/ports/webassembly/mphalport.c"

# Register the module in mpconfigport.h (idempotent)
CONFIG="$MP_DIR/ports/webassembly/mpconfigport.h"
if ! grep -q "mp_module_akira" "$CONFIG" 2>/dev/null; then
    cat >> "$CONFIG" << 'EOF'

/* AkiraOS native module — disable JS hook (no browser event loop) */
#undef  MICROPY_VARIANT_ENABLE_JS_HOOK
#define MICROPY_VARIANT_ENABLE_JS_HOOK 0

extern const struct _mp_obj_module_t mp_module_akira;
#define MICROPY_EXTRA_BUILTIN_MODULES \
    { MP_ROM_QSTR(MP_QSTR__akira), MP_ROM_PTR(&mp_module_akira) },
EOF
fi

# Add our .c files to SRC_C (idempotent)
MK="$MP_DIR/ports/webassembly/Makefile"
if ! grep -q "_akira.c" "$MK" 2>/dev/null; then
    sed -i '/^SRC_C/a \\t_akira.c \\\n\takira_stubs.c \\' "$MK"
fi

# ── Build micropython.wasm ────────────────────────────────────────────────────
echo "[5/5] Building micropython.wasm (this takes ~5 min)..."
# INITIAL_MEMORY / MAXIMUM_MEMORY must be multiples of 65536 (one WASM page).
# 393216 = 6 pages = 384 KB: enough for static data (~130KB) + heap (128KB) +
# stack (32KB) + script slot (64KB) + overhead.
AKIRA_MEM=393216
make -C "$MP_DIR/ports/webassembly" -j$(nproc) \
    MICROPY_WITH_AKIRA=1 \
    CFLAGS_EXTRA="-Wno-unused-but-set-variable -Wno-unused-but-set-global" \
    2>&1 | grep -E "^(EM|emcc|error:|Error)" | head -40 || true

# Find the output .wasm
BUILT="$(find "$MP_DIR/ports/webassembly" -name "micropython.wasm" 2>/dev/null | head -1)"
if [ -z "$BUILT" ]; then
    echo "Build failed. Full output:"
    make -C "$MP_DIR/ports/webassembly" \
        MICROPY_WITH_AKIRA=1 \
        CFLAGS_EXTRA="-Wno-unused-but-set-variable -Wno-unused-but-set-global" \
        2>&1 | tail -30
    exit 1
fi

cp "$BUILT" "$OUT"

# ── Post-process: Binaryen size optimization ──────────────────────────────────
WASM_OPT="$(dirname "$(which emcc)")/../bin/wasm-opt"
if [ -x "$WASM_OPT" ]; then
    BEFORE=$(wc -c < "$OUT")
    "$WASM_OPT" -Oz \
        --enable-bulk-memory --enable-bulk-memory-opt \
        --enable-nontrapping-float-to-int \
        --enable-exception-handling \
        --strip-producers --strip-target-features \
        -o "$OUT" "$OUT"
    AFTER=$(wc -c < "$OUT")
    echo "  wasm-opt: ${BEFORE} → ${AFTER} bytes ($(( (BEFORE - AFTER) * 100 / BEFORE ))% smaller)"
fi

echo ""
echo "OK: $OUT ($(wc -c < "$OUT") bytes)"
