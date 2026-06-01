# AkiraOS WASM API test suite — Makefile
#
# Usage (from repo root):
#   make -C wasm_apps/test/api_test          # build → wasm_apps/bin/api_test.wasm
#   make -C wasm_apps/test/api_test clean    # remove artefact
#
# Or from this directory:
#   make
#   make clean
#
# Environment overrides:
#   WASI_SDK=/opt/wasi-sdk       Path to WASI SDK (default: /opt/wasi-sdk)
#   SDK_ROOT=../../..            Path to AkiraSDK root  (auto-detected)

# ── Toolchain ────────────────────────────────────────────────────────────────

WASI_SDK  ?= /opt/wasi-sdk
CC         = $(WASI_SDK)/bin/clang

# ── Paths ────────────────────────────────────────────────────────────────────

THIS_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SDK_ROOT  ?= $(abspath $(THIS_DIR)/../../..)
BIN_DIR    = $(SDK_ROOT)/wasm_apps/bin
OUTPUT     = $(BIN_DIR)/api_test.wasm
MANIFEST   = $(THIS_DIR)/manifest.json
EMBED_SCR  = $(SDK_ROOT)/scripts/embed_manifest.py

SRCS       = $(THIS_DIR)/main.c

# ── Compiler flags ───────────────────────────────────────────────────────────

CFLAGS  = \
	-target wasm32-unknown-unknown \
	-nostdlib \
	-fvisibility=hidden \
	-Wl,--no-entry \
	-Wl,--export=main \
	-Wl,--allow-undefined \
	-Wl,--strip-all \
	-z stack-size=8192 \
	-Wl,--initial-memory=131072 \
	-Wl,--max-memory=131072 \
	-I$(SDK_ROOT)/include \
	-I$(SDK_ROOT) \
	-O2 \
	-Wno-incompatible-library-redeclaration \
	$(EXTRA_CFLAGS)

# ── Targets ──────────────────────────────────────────────────────────────────

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(SDK_ROOT)/include/akira_api.h
	@if [ ! -d "$(WASI_SDK)" ]; then \
		echo "ERROR: WASI SDK not found at $(WASI_SDK)"; \
		echo "  Set WASI_SDK=/path/to/wasi-sdk or install from:"; \
		echo "  https://github.com/WebAssembly/wasi-sdk/releases"; \
		exit 1; \
	fi
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(OUTPUT) $(SRCS)
	@if [ -f "$(EMBED_SCR)" ] && [ -f "$(MANIFEST)" ]; then \
		python3 $(EMBED_SCR) $(OUTPUT) $(MANIFEST) $(OUTPUT); \
		cp $(MANIFEST) $(BIN_DIR)/api_test.json; \
		echo "  Manifest embedded and copied to $(BIN_DIR)/api_test.json"; \
	fi
	@SIZE=$$(stat -c%s $(OUTPUT) 2>/dev/null || stat -f%z $(OUTPUT)); \
		echo "Built: $(OUTPUT)  ($${SIZE} bytes)"

clean:
	rm -f $(OUTPUT) $(BIN_DIR)/api_test.json
