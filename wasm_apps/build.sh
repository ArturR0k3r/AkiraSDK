#!/bin/bash

# Build all AkiraOS WASM applications
#
# Usage:
#   ./build.sh                     - Build all WASM apps (.wasm)
#   ./build.sh clean               - Clean all WASM apps
#   ./build.sh list                - List available apps
#   ./build.sh <app_name>          - Build a specific app (C, Rust, or Python)
#   ./build.sh rust/<app_name>     - Build a Rust app (e.g. rust/hello_world)
#   ./build.sh python/<app_name>   - Build a Python app (e.g. python/hello_world)
#   ./build.sh aot [target]        - AOT-compile all .wasm → .aot
#                                    targets: xtensa (default, ESP32-S3),
#                                             thumb (nRF54L15),
#                                             thumbv7em (STM32),
#                                             riscv32 (ESP32-C3),
#                                             x86_64 (native_sim)
#   ./build.sh <app_name> --aot[=target]  - Build .wasm then AOT-compile it
#                                    (same targets as above, default xtensa)
#
# Environment variables:
#   WASI_SDK=/opt/wasi-sdk         - Path to WASI SDK (C apps)
#   WAMRC=wamrc                    - Path to wamrc AOT compiler
#   MICROPYTHON_WASM=...           - Path to micropython.wasm (Python apps)
#   CARGO=cargo                    - Path to the cargo binary (Rust apps)

set -e

# Configuration
WASI_SDK=${WASI_SDK:-/opt/wasi-sdk}
WAMRC=${WAMRC:-wamrc}
CARGO=${CARGO:-cargo}
WASM_APPS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(dirname "$WASM_APPS_DIR")"
OUTPUT_DIR="${WASM_APPS_DIR}/bin"
EMBED_SCRIPT="${SDK_ROOT}/scripts/embed_manifest.py"
PY_TO_WASM="${SDK_ROOT}/scripts/py_to_wasm.py"
MICROPYTHON_WASM=${MICROPYTHON_WASM:-${SDK_ROOT}/python/runtime/micropython.wasm}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# WASI SDK is required for C apps only — warn but do not abort
if [ ! -d "$WASI_SDK" ]; then
    echo -e "${YELLOW}Warning: WASI SDK not found at $WASI_SDK (C apps will not build)${NC}"
    echo "  Set WASI_SDK or install from: https://github.com/WebAssembly/wasi-sdk"
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Function: Build a single WASM app from its subdirectory
# Args: app_name [app_dir]  (app_dir defaults to ${WASM_APPS_DIR}/${app_name})
build_app() {
    local app_name=$1
    local app_dir=${2:-"${WASM_APPS_DIR}/${app_name}"}
    local source_file="${app_dir}/main.c"
    local output_file="${OUTPUT_DIR}/${app_name}.wasm"
    local manifest_file="${app_dir}/manifest.json"

    if [ ! -f "${app_dir}/main.c" ]; then
        echo -e "${YELLOW}Warning: Source not found: ${app_dir}/main.c${NC}"
        return 1
    fi

    # If app has its own Makefile, delegate to it
    if [ -f "${app_dir}/Makefile" ]; then
        echo -e "${GREEN}Building ${app_name} (Makefile)...${NC}"
        if make -C "$app_dir" 2>&1; then
            local built_wasm="${app_dir}/${app_name}.wasm"
            if [ -f "$built_wasm" ]; then
                cp "$built_wasm" "$output_file"
                local size
                size=$(stat -c%s "$output_file" 2>/dev/null || stat -f%z "$output_file")
                echo -e "${GREEN}✓ Built: ${app_name}.wasm (${size} bytes)${NC}"
                if [ -f "$manifest_file" ]; then
                    cp "$manifest_file" "${OUTPUT_DIR}/${app_name}.json"
                fi
                return 0
            else
                echo -e "${RED}✗ Makefile succeeded but ${app_name}.wasm not found${NC}"
                return 1
            fi
        else
            echo -e "${RED}✗ Build failed: ${app_name}${NC}"
            return 1
        fi
    fi

    # Collect all .c source files in the app directory
    local source_files=()
    for src in "${app_dir}"/*.c; do
        [ -f "$src" ] && source_files+=("$src")
    done

    echo -e "${GREEN}Building ${app_name}...${NC}"

    "${WASI_SDK}/bin/clang" \
        -target wasm32-unknown-unknown \
        -nostdlib \
        -fvisibility=hidden \
        -Wl,--no-entry \
        -Wl,--export=main \
        -Wl,--allow-undefined \
        -Wl,--strip-all \
        -z stack-size=4096 \
        -Wl,--initial-memory=65536 \
        -Wl,--max-memory=65536 \
        -I"${SDK_ROOT}/include" \
        -I"${SDK_ROOT}" \
        -O2 \
        -Wno-incompatible-library-redeclaration \
        -o "$output_file" \
        "${source_files[@]}"

    if [ $? -eq 0 ]; then
        local size
        size=$(stat -c%s "$output_file" 2>/dev/null || stat -f%z "$output_file")
        echo -e "${GREEN}✓ Built: ${app_name}.wasm (${size} bytes)${NC}"

        if [ -f "$manifest_file" ] && [ -f "$EMBED_SCRIPT" ]; then
            python3 "$EMBED_SCRIPT" "$output_file" "$manifest_file" "$output_file"
            cp "$manifest_file" "${OUTPUT_DIR}/${app_name}.json"
            echo -e "  ${GREEN}✓ Manifest embedded${NC}"
        fi
        return 0
    else
        echo -e "${RED}✗ Build failed: ${app_name}${NC}"
        return 1
    fi
}

# Function: Map a friendly target name to wamrc flags
# Supported aliases:
#   xtensa / esp32s3          → ESP32-S3 (Xtensa LX7)
#   thumb  / nrf / cortex-m33 → nRF54L15, nRF54L (Cortex-M33)
#   thumbv7em / stm32         → STM32 (Cortex-M7)
#   riscv32 / esp32c3         → ESP32-C3 (RISC-V 32-bit)
#   x86_64  / native          → native_sim / host
get_aot_flags() {
    case "$1" in
        xtensa|esp32s3)      echo "--target=xtensa --cpu=esp32s3" ;;
        thumb|nrf|nrf54l15|cortex-m33) echo "--target=thumb --cpu=cortex-m33" ;;
        thumbv7em|stm32|cortex-m7)     echo "--target=thumb --cpu=cortex-m7" ;;
        riscv32|esp32c3)     echo "--target=riscv32 --cpu=generic-rv32 --target-abi=ilp32" ;;
        x86_64|native)       echo "--target=x86_64" ;;
        *)                   echo "--target=$1" ;;
    esac
}

# Function: AOT-compile a single .wasm to .aot using wamrc
aot_app() {
    local app_name=$1
    local target=${2:-xtensa}
    local wasm_file="${OUTPUT_DIR}/${app_name}.wasm"
    local aot_file="${OUTPUT_DIR}/${app_name}-${target}.aot"

    if [ ! -f "$wasm_file" ]; then
        echo -e "${YELLOW}Warning: WASM not found: $wasm_file — build first${NC}"
        return 1
    fi

    if ! command -v "$WAMRC" &>/dev/null; then
        echo -e "${RED}Error: wamrc not found in PATH (WAMRC=${WAMRC})${NC}"
        echo "Build wamrc from the WAMR source:"
        echo "  cd modules/wasm-micro-runtime/wamr-compiler"
        echo "  cmake . -DWAMR_BUILD_PLATFORM=linux"
        echo "  make"
        echo "  sudo cp wamrc /usr/local/bin/"
        return 1
    fi

    local flags
    flags=$(get_aot_flags "$target")

    echo -e "${GREEN}AOT compiling ${app_name} → ${target}...${NC}"
    # --emit-custom-sections carries the .akira.manifest WASM custom section
    # (embedded by build_app) into the AOT file's own custom-section format —
    # without it cap_mask=0 at runtime and every capability check is denied.
    # shellcheck disable=SC2086
    "$WAMRC" $flags --opt-level=3 --size-level=1 \
        --emit-custom-sections=.akira.manifest \
        -o "$aot_file" "$wasm_file"

    if [ $? -eq 0 ]; then
        local size
        size=$(stat -c%s "$aot_file" 2>/dev/null || stat -f%z "$aot_file")
        echo -e "${GREEN}✓ AOT: ${app_name}-${target}.aot (${size} bytes)${NC}"
        return 0
    else
        echo -e "${RED}✗ AOT failed: ${app_name}${NC}"
        return 1
    fi
}

# Function: Clean build artifacts
clean_apps() {
    echo -e "${YELLOW}Cleaning WASM apps...${NC}"
    rm -rf "$OUTPUT_DIR"
    find "$WASM_APPS_DIR" -maxdepth 3 -name "*.wasm" -delete
    echo -e "${GREEN}Clean complete${NC}"
}

# Function: Build a Rust WASM app
# Args: app_name app_dir
build_rust_app() {
    local app_name=$1
    local app_dir=$2
    local output_file="${OUTPUT_DIR}/${app_name}.wasm"
    local manifest_file="${app_dir}/manifest.json"

    if ! command -v "$CARGO" &>/dev/null; then
        echo -e "${RED}Error: cargo not found (CARGO=${CARGO})${NC}"
        echo "  Install Rust: https://rustup.rs/"
        echo "  Then: rustup target add wasm32-unknown-unknown"
        return 1
    fi

    echo -e "${GREEN}Building ${app_name} (Rust)...${NC}"
    if ! "$CARGO" build --manifest-path "${app_dir}/Cargo.toml" \
            --target wasm32-unknown-unknown --release 2>&1; then
        echo -e "${RED}✗ Rust build failed: ${app_name}${NC}"
        return 1
    fi

    # cargo puts the output at target/wasm32-unknown-unknown/release/<name>.wasm
    # For cdylib the name matches the [lib] name in Cargo.toml; use a glob to find it.
    local built_wasm
    built_wasm=$(find "${app_dir}/target/wasm32-unknown-unknown/release" \
        -maxdepth 1 -name '*.wasm' ! -name '*-*' 2>/dev/null | head -1)

    if [ -z "$built_wasm" ] || [ ! -f "$built_wasm" ]; then
        echo -e "${RED}✗ WASM output not found after cargo build${NC}"
        return 1
    fi

    cp "$built_wasm" "$output_file"

    if [ -f "$manifest_file" ] && [ -f "$EMBED_SCRIPT" ]; then
        python3 "$EMBED_SCRIPT" "$output_file" "$manifest_file" "$output_file"
        cp "$manifest_file" "${OUTPUT_DIR}/${app_name}.json"
        echo -e "  ${GREEN}✓ Manifest embedded${NC}"
    fi

    local size
    size=$(stat -c%s "$output_file" 2>/dev/null || stat -f%z "$output_file")
    echo -e "${GREEN}✓ Built: ${app_name}.wasm (${size} bytes)${NC}"
    return 0
}

# Function: Build a Python WASM app via py_to_wasm.py
# Args: app_name app_dir
build_python_app() {
    local app_name=$1
    local app_dir=$2
    local output_file="${OUTPUT_DIR}/${app_name}.wasm"
    local manifest_file="${app_dir}/manifest.json"
    local script_file="${app_dir}/main.py"

    if [ ! -f "$script_file" ]; then
        echo -e "${YELLOW}Warning: main.py not found in ${app_dir}${NC}"
        return 1
    fi

    if [ ! -f "$MICROPYTHON_WASM" ]; then
        echo -e "${RED}Error: micropython.wasm not found at ${MICROPYTHON_WASM}${NC}"
        echo "  Set MICROPYTHON_WASM=/path/to/micropython.wasm"
        echo "  See: ${SDK_ROOT}/python/runtime/README.md"
        return 1
    fi

    echo -e "${GREEN}Building ${app_name} (Python)...${NC}"

    local py_args=("$script_file" -o "$output_file"
        --runtime "$MICROPYTHON_WASM"
        --sdk-root "$SDK_ROOT")
    [ -f "$manifest_file" ] && py_args+=(--manifest "$manifest_file")

    if ! python3 "$PY_TO_WASM" "${py_args[@]}"; then
        echo -e "${RED}✗ Python WASM build failed: ${app_name}${NC}"
        return 1
    fi

    if [ -f "$manifest_file" ]; then
        cp "$manifest_file" "${OUTPUT_DIR}/${app_name}.json"
    fi

    local size
    size=$(stat -c%s "$output_file" 2>/dev/null || stat -f%z "$output_file")
    echo -e "${GREEN}✓ Built: ${app_name}.wasm (${size} bytes)${NC}"
    return 0
}

# Function: List available apps
list_apps() {
    echo -e "${GREEN}Available WASM applications:${NC}"
    echo "  [C]"
    for dir in "${WASM_APPS_DIR}"/generic/*/ "${WASM_APPS_DIR}"/console_apps/*/ "${WASM_APPS_DIR}"/retro_games/*/; do
        if [ -f "${dir}main.c" ]; then
            echo "    - $(basename "$dir")"
        fi
    done
    echo "  [Rust]"
    for dir in "${WASM_APPS_DIR}"/rust/*/; do
        if [ -f "${dir}Cargo.toml" ]; then
            echo "    - rust/$(basename "$dir")"
        fi
    done
    echo "  [Python]"
    for dir in "${SDK_ROOT}/python/apps/"*/ "${WASM_APPS_DIR}/python/"*/; do
        if [ -f "${dir}main.py" ]; then
            echo "    - python/$(basename "$dir")"
        fi
    done
}

# Main
main() {
    # Pull --aot[=target] out of the args so it can trail any app_name form.
    local do_aot=0
    local aot_target="xtensa"
    local args=()
    for arg in "$@"; do
        case "$arg" in
            --aot) do_aot=1 ;;
            --aot=*) do_aot=1; aot_target="${arg#--aot=}" ;;
            *) args+=("$arg") ;;
        esac
    done
    set -- "${args[@]}"

    local command=${1:-build}

    case "$command" in
        clean)
            clean_apps
            ;;
        list)
            list_apps
            ;;
        aot)
            local aot_target=${2:-xtensa}
            echo -e "${GREEN}=== AOT Compiling for ${aot_target} ===${NC}"
            echo "wamrc:  $WAMRC"
            echo "target: $aot_target ($(get_aot_flags "$aot_target"))"
            echo "output: $OUTPUT_DIR"
            echo ""

            local failed=0
            for dir in "${WASM_APPS_DIR}"/generic/*/ "${WASM_APPS_DIR}"/console_apps/*/ "${WASM_APPS_DIR}"/retro_games/*/; do
                if [ -f "${dir}main.c" ]; then
                    name=$(basename "$dir")
                    if ! aot_app "$name" "$aot_target"; then
                        ((failed++)) || true
                    fi
                fi
            done

            echo ""
            if [ "$failed" -eq 0 ]; then
                echo -e "${GREEN}✓ All AOT compilations successful${NC}"
                ls -lh "$OUTPUT_DIR"/*.aot 2>/dev/null || true
            else
                echo -e "${RED}✗ ${failed} AOT compilation(s) failed${NC}"
                exit 1
            fi
            ;;
        all|build)
            echo -e "${GREEN}=== Building AkiraOS WASM Applications ===${NC}"
            echo "WASI SDK:          $WASI_SDK"
            echo "MICROPYTHON_WASM:  $MICROPYTHON_WASM"
            echo "Output:            $OUTPUT_DIR"
            echo ""

            local failed=0

            # ── C / Makefile apps ────────────────────────────────────────────
            for dir in "${WASM_APPS_DIR}"/generic/*/ "${WASM_APPS_DIR}"/console_apps/*/ "${WASM_APPS_DIR}"/retro_games/*/; do
                if [ -f "${dir}main.c" ]; then
                    name=$(basename "$dir")
                    if ! build_app "$name" "$dir"; then
                        ((failed++)) || true
                    fi
                fi
            done

            # ── Rust apps ────────────────────────────────────────────────────
            for dir in "${WASM_APPS_DIR}"/rust/*/; do
                if [ -f "${dir}Cargo.toml" ]; then
                    name=$(basename "$dir")
                    if ! build_rust_app "$name" "$dir"; then
                        ((failed++)) || true
                    fi
                fi
            done

            # ── Python apps ──────────────────────────────────────────────────
            # Check both AkiraSDK/python/apps/ (SDK templates) and wasm_apps/python/ (user)
            for dir in "${SDK_ROOT}/python/apps/"*/ "${WASM_APPS_DIR}/python/"*/; do
                if [ -f "${dir}main.py" ]; then
                    name=$(basename "$dir")
                    if ! build_python_app "$name" "$dir"; then
                        ((failed++)) || true
                    fi
                fi
            done

            echo ""
            if [ "$failed" -eq 0 ]; then
                echo -e "${GREEN}✓ All builds successful${NC}"
                ls -lh "$OUTPUT_DIR"/*.wasm 2>/dev/null || true
            else
                echo -e "${RED}✗ ${failed} build(s) failed${NC}"
                exit 1
            fi
            ;;
        *)
            # Treat as specific app name: rust/<name>, python/<name>, or plain <name>
            echo -e "${GREEN}=== Building ${command} ===${NC}"

            if [[ "$command" == rust/* ]]; then
                # Explicit Rust prefix: e.g. rust/hello_world
                app_name=$(basename "$command")
                app_dir="${WASM_APPS_DIR}/rust/${app_name}"
                if [ ! -f "${app_dir}/Cargo.toml" ]; then
                    echo -e "${RED}Error: Rust app not found: ${app_dir}${NC}"
                    exit 1
                fi
                build_rust_app "$app_name" "$app_dir" || exit 1
                if [ "$do_aot" -eq 1 ]; then aot_app "$app_name" "$aot_target" || exit 1; fi

            elif [[ "$command" == python/* ]]; then
                # Explicit Python prefix: e.g. python/hello_world
                app_name=$(basename "$command")
                # Search SDK templates, then wasm_apps/python
                app_dir="${SDK_ROOT}/python/apps/${app_name}"
                if [ ! -f "${app_dir}/main.py" ]; then
                    app_dir="${WASM_APPS_DIR}/python/${app_name}"
                fi
                if [ ! -f "${app_dir}/main.py" ]; then
                    echo -e "${RED}Error: Python app not found: ${app_name}${NC}"
                    exit 1
                fi
                build_python_app "$app_name" "$app_dir" || exit 1
                if [ "$do_aot" -eq 1 ]; then aot_app "$app_name" "$aot_target" || exit 1; fi

            else
                # Plain name — probe C paths
                app_dir="${WASM_APPS_DIR}/generic/${command}"
                if [ ! -f "${app_dir}/main.c" ]; then
                    app_dir="${WASM_APPS_DIR}/console_apps/${command}"
                fi
                if [ ! -f "${app_dir}/main.c" ]; then
                    app_dir="${WASM_APPS_DIR}/retro_games/${command}"
                fi
                if ! build_app "$command" "$app_dir"; then
                    echo ""
                    echo "Usage: $0 [clean|list|build|aot [target]|rust/<name>|python/<name>|APP_NAME] [--aot[=target]]"
                    list_apps
                    exit 1
                fi
                if [ "$do_aot" -eq 1 ]; then aot_app "$command" "$aot_target" || exit 1; fi
            fi
            ;;
    esac
}

main "$@"
