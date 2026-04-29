# AkiraOS WASM Sample Apps - Master Makefile
#
# Build all sample WASM apps for AkiraOS
#
# Usage:
#   make              - Build all samples (.wasm)
#   make clean        - Clean all samples
#   make install      - Install to SD card (if mounted)
#   make check        - Verify WASI SDK installation
#   make aot          - AOT-compile all .wasm → .aot for AOT_TARGET (default: xtensa)
#   make aot-xtensa   - AOT-compile for ESP32-S3 (Xtensa LX7)
#   make aot-thumb    - AOT-compile for nRF54L15/STM32 (Cortex-M33/M7)
#   make aot-riscv32  - AOT-compile for ESP32-C3 (RISC-V 32)
#
# Requirements:
#   - WASI SDK installed (see docs/APP_DEVELOPMENT.md)
#   - Set WASI_SDK environment variable or install to /opt/wasi-sdk
#   - For AOT: wamrc built from modules/wasm-micro-runtime/wamr-compiler

# WASI SDK path (override with: make WASI_SDK=/path/to/wasi-sdk)
WASI_SDK ?= /opt/wasi-sdk

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

# Combined list with category prefixes (for MAKE -C targets)
SAMPLES = $(addprefix generic/,$(GENERIC_APPS)) \
          $(addprefix console_apps/,$(CONSOLE_APPS))

# SD card mount point for install target
SD_MOUNT ?= /media/$(USER)/AKIRA

# Colors
GREEN = \033[0;32m
RED = \033[0;31m
YELLOW = \033[1;33m
NC = \033[0m

.PHONY: all clean install check aot aot-xtensa aot-thumb aot-riscv32 help $(SAMPLES) $(addprefix generic/,$(GENERIC_APPS)) $(addprefix console_apps/,$(CONSOLE_APPS))

all: check $(SAMPLES)
	@echo ""
	@echo -e "$(GREEN)✓ All samples built successfully!$(NC)"
	@echo ""
	@echo "Built WASM files:"
	@find . -name "*.wasm" -type f | while read f; do \
		size=$$(ls -la "$$f" | awk '{print $$5}'); \
		echo "  $$f ($$size bytes)"; \
	done

$(addprefix generic/,$(GENERIC_APPS)) $(addprefix console_apps/,$(CONSOLE_APPS)):
	@echo -e "$(GREEN)Building $@...$(NC)"
	@$(MAKE) -C $@ WASI_SDK=$(WASI_SDK) --no-print-directory

clean:
	@echo "Cleaning all samples..."
	@for dir in $(SAMPLES); do \
		$(MAKE) -C $$dir clean --no-print-directory 2>/dev/null || true; \
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
	@echo "Checking WASI SDK..."
	@if [ ! -d "$(WASI_SDK)" ]; then \
		echo -e "$(RED)Error: WASI SDK not found at $(WASI_SDK)$(NC)"; \
		echo ""; \
		echo "Please install WASI SDK:"; \
		echo "  1. Download from: https://github.com/WebAssembly/wasi-sdk/releases"; \
		echo "  2. Extract to /opt/wasi-sdk"; \
		echo "  3. Or set WASI_SDK=/path/to/wasi-sdk"; \
		echo ""; \
		exit 1; \
	fi
	@if [ ! -x "$(WASI_SDK)/bin/clang" ]; then \
		echo -e "$(RED)Error: clang not found in $(WASI_SDK)/bin$(NC)"; \
		exit 1; \
	fi
	@echo -e "$(GREEN)✓ WASI SDK OK: $(WASI_SDK)$(NC)"
	@$(WASI_SDK)/bin/clang --version | head -1

install: all
	@echo ""
	@echo "Installing to SD card..."
	@if [ ! -d "$(SD_MOUNT)" ]; then \
		echo -e "$(YELLOW)Warning: SD card not mounted at $(SD_MOUNT)$(NC)"; \
		echo "Mount your SD card or set SD_MOUNT=/path/to/mount"; \
		exit 1; \
	fi
	@mkdir -p "$(SD_MOUNT)/apps"
	@for dir in $(SAMPLES); do \
		app=$$(basename $$dir); \
		if [ -f "$$dir/$$app.wasm" ]; then \
			cp "$$dir/$$app.wasm" "$(SD_MOUNT)/apps/"; \
			echo "  Copied $$app.wasm"; \
		fi; \
		if [ -f "$$dir/manifest.json" ]; then \
			cp "$$dir/manifest.json" "$(SD_MOUNT)/apps/$$app.manifest.json"; \
			echo "  Copied $$app.manifest.json"; \
		fi; \
	done
	@sync
	@echo -e "$(GREEN)✓ Apps installed to $(SD_MOUNT)/apps$(NC)"

info:
	@echo "=== AkiraOS WASM Samples ==="
	@echo ""
	@echo "Generic apps:  $(GENERIC_APPS)"
	@echo "Console apps:  $(CONSOLE_APPS)"
	@echo "Retro games:   $(RETRO_GAMES)"
	@echo ""
	@echo "WASI SDK: $(WASI_SDK)"
	@echo "wamrc:    $(WAMRC)"
	@echo ""
	@echo "Targets:"
	@echo "  make              - Build all samples (.wasm)"
	@echo "  make <name>       - Build single sample"
	@echo "  make clean        - Clean all"
	@echo "  make install      - Copy to SD card"
	@echo "  make check        - Verify toolchain"
	@echo "  make aot          - AOT-compile all for AOT_TARGET (default: xtensa)"
	@echo "  make aot-xtensa   - AOT for ESP32-S3"
	@echo "  make aot-thumb    - AOT for nRF54L15 / Cortex-M33"
	@echo "  make aot-riscv32  - AOT for ESP32-C3"

help: info
