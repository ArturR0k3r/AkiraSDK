#!/bin/bash

# Build all AkiraOS WASM applications
#
# Usage:
#   ./build_all.sh              - Build all WASM apps
#   ./build_all.sh clean        - Clean all WASM apps
#   ./build_all.sh <app_name>   - Build a specific app

set -e

# Configuration
WASI_SDK=${WASI_SDK:-/opt/wasi-sdk}
WASM_APPS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(dirname "$WASM_APPS_DIR")"
OUTPUT_DIR="${WASM_APPS_DIR}/bin"
EMBED_SCRIPT="${SDK_ROOT}/scripts/embed_manifest.py"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Ensure WASI SDK is available
if [ ! -d "$WASI_SDK" ]; then
    echo -e "${RED}Error: WASI SDK not found at $WASI_SDK${NC}"
    echo "Set WASI_SDK environment variable or install from:"
    echo "  https://github.com/WebAssembly/wasi-sdk"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Function: Build a single WASM app from its subdirectory
build_app() {
    local app_name=$1
    local app_dir="${WASM_APPS_DIR}/${app_name}"
    local source_file="${app_dir}/main.c"
    local output_file="${OUTPUT_DIR}/${app_name}.wasm"
    local manifest_file="${app_dir}/manifest.json"

    if [ ! -f "$source_file" ]; then
        echo -e "${YELLOW}Warning: Source not found: $source_file${NC}"
        return 1
    fi

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
        -O2 \
        -o "$output_file" \
        "$source_file"

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

# Function: Clean build artifacts
clean_apps() {
    echo -e "${YELLOW}Cleaning WASM apps...${NC}"
    rm -rf "$OUTPUT_DIR"
    find "$WASM_APPS_DIR" -maxdepth 2 -name "*.wasm" -delete
    echo -e "${GREEN}Clean complete${NC}"
}

# Function: List available apps
list_apps() {
    echo -e "${GREEN}Available WASM applications:${NC}"
    for dir in "${WASM_APPS_DIR}"/*/; do
        if [ -f "${dir}main.c" ]; then
            echo "  - $(basename "$dir")"
        fi
    done
}

# Main
main() {
    local command=${1:-build}

    case "$command" in
        clean)
            clean_apps
            ;;
        list)
            list_apps
            ;;
        all|build)
            echo -e "${GREEN}=== Building AkiraOS WASM Applications ===${NC}"
            echo "WASI SDK: $WASI_SDK"
            echo "Output:   $OUTPUT_DIR"
            echo ""

            local failed=0
            for dir in "${WASM_APPS_DIR}"/*/; do
                if [ -f "${dir}main.c" ]; then
                    name=$(basename "$dir")
                    if ! build_app "$name"; then
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
            # Treat as specific app name
            echo -e "${GREEN}=== Building ${command} ===${NC}"
            if ! build_app "$command"; then
                echo ""
                echo "Usage: $0 [clean|list|build|APP_NAME]"
                list_apps
                exit 1
            fi
            ;;
    esac
}

main "$@"
