# AkiraOS WASM Sample Apps - Master Makefile
#
# Build all sample WASM apps for AkiraOS
#
# Usage:
#   make                    - Build all samples (.wasm) — C, Rust, and Python
#   make build-rust         - Build Rust apps only
#   make build-python       - Build Python apps only
#   make clean              - Clean all samples
#   make install            - Install to SD card (if mounted)
#   make check              - Verify toolchain (WASI SDK + Rust + Python)
#   make aot                - AOT-compile all .wasm → .aot for AOT_TARGET (default: xtensa)
#   make aot-xtensa         - AOT-compile for ESP32-S3 (Xtensa LX7)
#   make aot-thumb          - AOT-compile for nRF54L15/STM32 (Cortex-M33/M7)
#   make aot-riscv32        - AOT-compile for ESP32-C3 (RISC-V 32)
#
# Requirements:
#   C apps:      WASI SDK installed (see docs/APP_DEVELOPMENT.md)
#   Rust apps:   cargo + rustup target add wasm32-unknown-unknown
#   Python apps: python3 + MICROPYTHON_WASM=/path/to/micropython.wasm
#   AOT:         wamrc built from modules/wasm-micro-runtime/wamr-compiler

# WASI SDK path (override with: make WASI_SDK=/path/to/wasi-sdk)
WASI_SDK ?= /opt/wasi-sdk

# Rust toolchain
CARGO ?= cargo

# Python runtime for Python WASM apps
# Set to the prebuilt micropython.wasm binary that includes the _akira module
SDK_ROOT := $(shell dirname $(CURDIR))
MICROPYTHON_WASM ?= $(SDK_ROOT)/python/runtime/micropython.wasm
PY_TO_WASM := $(SDK_ROOT)/scripts/py_to_wasm.py

# AOT compiler (wamrc from WAMR source)
WAMRC ?= wamrc

# AOT target architecture (override with: make aot AOT_TARGET=thumbv7em)
# Supported: xtensa (ESP32-S3), thumb (nRF54L15), thumbv7em (STM32),
#            riscv32 (ESP32-C3), x86_64 (native_sim)
AOT_TARGET ?= xtensa

# wamrc flags per target
AOT_FLAGS_xtensa   := --target=xtensa --cpu=esp32s3
AOT_FLAGS_thumb    := --target=thumb --cpu=cortex-m33
AOT_FLAGS_thumbv7em := --target=thumb --cpu=cortex-m7
AOT_FLAGS_riscv32  := --target=riscv32 --cpu=generic-rv32 --target-abi=ilp32
AOT_FLAGS_x86_64   := --target=x86_64

# App directories by category
GENERIC_APPS = ble_led gpio hello_world net_echo net_server storage_test

CONSOLE_APPS = akira_shell compass cube3d display_test \
               glasses_mystery gravity_dash imu_3d imu_timer_test inclinometer \
               logic_analyzer macro_pad pixel_dungeon pixel_farmer \
               space_invaders supervisor tetris

# Retro games: pre-built .aot files (no wasi-sdk compile step needed)
RETRO_GAMES = galaga megaman megaman3 persia tetris zelda

# Rust apps (in wasm_apps/rust/<name>/)
RUST_APPS = hello_world

# Python apps (in AkiraSDK/python/apps/<name>/)
PYTHON_APPS = hello_world

# Combined C list with category prefixes
SAMPLES = $(addprefix generic/,$(GENERIC_APPS)) \
          $(addprefix console_apps/,$(CONSOLE_APPS))

# SD card mount point for install target
SD_MOUNT ?= /media/$(USER)/AKIRA

# Output binary directory
BIN_DIR := bin

# Colors
GREEN = \033[0;32m
RED = \033[0;31m
YELLOW = \033[1;33m
NC = \033[0m

.PHONY: all build-rust build-python clean install check aot aot-xtensa aot-thumb aot-riscv32 help info \
        $(SAMPLES) $(addprefix generic/,$(GENERIC_APPS)) $(addprefix console_apps/,$(CONSOLE_APPS)) \
        $(addprefix rust/,$(RUST_APPS)) $(addprefix python/,$(PYTHON_APPS))

all: $(SAMPLES) build-rust build-python
	@echo ""
	@echo -e "$(GREEN)✓ All samples built successfully!$(NC)"
	@echo ""
	@echo "Built WASM files:"
	@find $(BIN_DIR) -name "*.wasm" -type f 2>/dev/null | while read f; do \
		size=$$(ls -la "$$f" | awk '{print $$5}'); \
		echo "  $$f ($$size bytes)"; \
	done

$(addprefix generic/,$(GENERIC_APPS)) $(addprefix console_apps/,$(CONSOLE_APPS)):
	@echo -e "$(GREEN)Building $@...$(NC)"
	@$(MAKE) -C $@ WASI_SDK=$(WASI_SDK) --no-print-directory

# ──────────────────────────────────────────────────────────────────────────────
# Rust apps
# ──────────────────────────────────────────────────────────────────────────────
build-rust: $(addprefix rust/,$(RUST_APPS))
	@echo -e "$(GREEN)✓ Rust apps built$(NC)"

$(addprefix rust/,$(RUST_APPS)):
	$(eval APP_NAME := $(notdir $@))
	$(eval APP_DIR  := rust/$(APP_NAME))
	@echo -e "$(GREEN)Building rust/$(APP_NAME)...$(NC)"
	@mkdir -p $(BIN_DIR)
	$(CARGO) build --manifest-path $(APP_DIR)/Cargo.toml \
		--target wasm32-unknown-unknown --release
	@BUILT=$$(find $(APP_DIR)/target/wasm32-unknown-unknown/release \
		-maxdepth 1 -name '*.wasm' ! -name '*-*' 2>/dev/null | head -1); \
	if [ -z "$$BUILT" ]; then \
		echo -e "$(RED)✗ WASM output not found$(NC)"; exit 1; \
	fi; \
	cp "$$BUILT" $(BIN_DIR)/$(APP_NAME).wasm; \
	echo -e "$(GREEN)  ✓ $(BIN_DIR)/$(APP_NAME).wasm$$(stat -c ' (%s bytes)' $(BIN_DIR)/$(APP_NAME).wasm 2>/dev/null || true)$(NC)"

# ──────────────────────────────────────────────────────────────────────────────
# Python apps (sources in AkiraSDK/python/apps/<name>/)
# ──────────────────────────────────────────────────────────────────────────────
build-python: $(addprefix python/,$(PYTHON_APPS))
	@echo -e "$(GREEN)✓ Python apps built$(NC)"

$(addprefix python/,$(PYTHON_APPS)):
	$(eval APP_NAME := $(notdir $@))
	$(eval APP_DIR  := $(SDK_ROOT)/python/apps/$(APP_NAME))
	@echo -e "$(GREEN)Building python/$(APP_NAME)...$(NC)"
	@if [ ! -f "$(MICROPYTHON_WASM)" ]; then \
		echo -e "$(RED)Error: micropython.wasm not found at $(MICROPYTHON_WASM)$(NC)"; \
		echo "  Set MICROPYTHON_WASM=/path/to/micropython.wasm"; \
		echo "  See: $(SDK_ROOT)/python/runtime/README.md"; \
		exit 1; \
	fi
	@mkdir -p $(BIN_DIR)
	@MANIFEST_ARG=""; \
	if [ -f "$(APP_DIR)/manifest.json" ]; then MANIFEST_ARG="--manifest $(APP_DIR)/manifest.json"; fi; \
	python3 $(PY_TO_WASM) $(APP_DIR)/main.py -o $(BIN_DIR)/$(APP_NAME).wasm \
		--runtime $(MICROPYTHON_WASM) --sdk-root $(SDK_ROOT) $$MANIFEST_ARG

clean:
	@echo "Cleaning all samples..."
	@for dir in $(SAMPLES); do \
		$(MAKE) -C $$dir clean --no-print-directory 2>/dev/null || true; \
	done
	@for app in $(RUST_APPS); do \
		$(CARGO) clean --manifest-path rust/$$app/Cargo.toml 2>/dev/null || true; \
	done
	@rm -rf bin/
	@echo -e "$(GREEN)✓ Clean complete$(NC)"

# AOT compilation — converts pre-built .wasm files to native .aot
# Requires wamrc from the WAMR toolchain.
#   Build wamrc: cd modules/wasm-micro-runtime/wamr-compiler && cmake . && make
aot:
	@if ! command -v $(WAMRC) >/dev/null 2>&1; then \
		echo -e "$(RED)Error: wamrc not found (WAMRC=$(WAMRC))$(NC)"; \
		echo "Build it: cd modules/wasm-micro-runtime/wamr-compiler && cmake . && make"; \
		exit 1; \
	fi
	@echo -e "$(GREEN)=== AOT Compiling for $(AOT_TARGET) ===$(NC)"
	@FLAGS="$(AOT_FLAGS_$(AOT_TARGET))"; \
	if [ -z "$$FLAGS" ]; then FLAGS="--target=$(AOT_TARGET)"; fi; \
	for app in $(GENERIC_APPS) $(CONSOLE_APPS); do \
		wasm="bin/$$app.wasm"; \
		out="bin/$$app-$(AOT_TARGET).aot"; \
		if [ ! -f "$$wasm" ]; then \
			echo -e "$(YELLOW)Skip $$app — $$wasm not found (run make first)$(NC)"; \
			continue; \
		fi; \
		echo -e "$(GREEN)AOT: $$app → $(AOT_TARGET)$(NC)"; \
		$(WAMRC) $$FLAGS --opt-level=3 --size-level=1 -o "$$out" "$$wasm" && \
			echo "  ✓ $$out" || echo -e "$(RED)  ✗ Failed: $$app$(NC)"; \
	done

aot-xtensa:
	$(MAKE) aot AOT_TARGET=xtensa

aot-thumb:
	$(MAKE) aot AOT_TARGET=thumb

aot-riscv32:
	$(MAKE) aot AOT_TARGET=riscv32

check:
	@echo "Checking toolchain..."
	@if [ ! -d "$(WASI_SDK)" ]; then \
		echo -e "$(YELLOW)Warning: WASI SDK not found at $(WASI_SDK) (C apps will not build)$(NC)"; \
		echo "  Install from: https://github.com/WebAssembly/wasi-sdk/releases"; \
	else \
		echo -e "$(GREEN)✓ WASI SDK OK: $(WASI_SDK)$(NC)"; \
		$(WASI_SDK)/bin/clang --version | head -1; \
	fi
	@if command -v $(CARGO) >/dev/null 2>&1; then \
		echo -e "$(GREEN)✓ Cargo OK: $$($(CARGO) --version)$(NC)"; \
		if rustup target list --installed 2>/dev/null | grep -q wasm32-unknown-unknown; then \
			echo -e "$(GREEN)✓ Rust target wasm32-unknown-unknown installed$(NC)"; \
		else \
			echo -e "$(YELLOW)Warning: wasm32-unknown-unknown not installed: run 'rustup target add wasm32-unknown-unknown'$(NC)"; \
		fi; \
	else \
		echo -e "$(YELLOW)Warning: cargo not found (Rust apps will not build)$(NC)"; \
	fi
	@if [ -f "$(MICROPYTHON_WASM)" ]; then \
		echo -e "$(GREEN)✓ micropython.wasm OK: $(MICROPYTHON_WASM)$(NC)"; \
	else \
		echo -e "$(YELLOW)Warning: micropython.wasm not found at $(MICROPYTHON_WASM) (Python apps will not build)$(NC)"; \
		echo "  See: $(SDK_ROOT)/python/runtime/README.md"; \
	fi

install: all
	@echo ""
	@echo "Installing to SD card..."
	@if [ ! -d "$(SD_MOUNT)" ]; then \
		echo -e "$(YELLOW)Warning: SD card not mounted at $(SD_MOUNT)$(NC)"; \
		echo "Mount your SD card or set SD_MOUNT=/path/to/mount"; \
		exit 1; \
	fi
	@mkdir -p "$(SD_MOUNT)/apps"
	@for f in $(BIN_DIR)/*.wasm; do \
		[ -f "$$f" ] || continue; \
		cp "$$f" "$(SD_MOUNT)/apps/"; \
		echo "  Copied $$(basename $$f)"; \
	done
	@for f in $(BIN_DIR)/*.json; do \
		[ -f "$$f" ] || continue; \
		cp "$$f" "$(SD_MOUNT)/apps/"; \
		echo "  Copied $$(basename $$f)"; \
	done
	@sync
	@echo -e "$(GREEN)✓ Apps installed to $(SD_MOUNT)/apps$(NC)"

info:
	@echo "=== AkiraOS WASM Samples ==="
	@echo ""
	@echo "Generic apps:  $(GENERIC_APPS)"
	@echo "Console apps:  $(CONSOLE_APPS)"
	@echo "Retro games:   $(RETRO_GAMES)"
	@echo "Rust apps:     $(RUST_APPS)"
	@echo "Python apps:   $(PYTHON_APPS)"
	@echo ""
	@echo "WASI SDK:          $(WASI_SDK)"
	@echo "Cargo:             $(CARGO)"
	@echo "MICROPYTHON_WASM:  $(MICROPYTHON_WASM)"
	@echo "wamrc:             $(WAMRC)"
	@echo ""
	@echo "Targets:"
	@echo "  make                    - Build all samples (.wasm)"
	@echo "  make build-rust         - Build Rust apps only"
	@echo "  make build-python       - Build Python apps only"
	@echo "  make rust/<name>        - Build a single Rust app"
	@echo "  make python/<name>      - Build a single Python app"
	@echo "  make <cat>/<name>       - Build a single C app"
	@echo "  make clean              - Clean all"
	@echo "  make install            - Copy to SD card"
	@echo "  make check              - Verify toolchain"
	@echo "  make aot                - AOT-compile all for AOT_TARGET (default: xtensa)"
	@echo "  make aot-xtensa         - AOT for ESP32-S3"
	@echo "  make aot-thumb          - AOT for nRF54L15 / Cortex-M33"
	@echo "  make aot-riscv32        - AOT for ESP32-C3"

help: info
