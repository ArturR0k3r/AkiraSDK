# gravity_dash — Makefile
# Targets:
#   make wasm   → gravity_dash.wasm  (WASM binary for AkiraOS, ≤48 KB)
#   make stub   → gravity_dash_stub  (SDL2 desktop test binary)
#   make flash  → deploy .wasm to AkiraConsole via esptool
#   make clean  → remove build artefacts
#
# Requirements:
#   wasm target: wasi-sdk at /opt/wasi-sdk  (override with WASI_SDK=...)
#   stub target: gcc + SDL2 (libsdl2-dev / sdl2 Homebrew)
#   flash target: esptool.py in PATH, SERIAL_PORT set

# ── Toolchain paths ──────────────────────────────────────────────────────────
WASI_SDK      ?= /opt/wasi-sdk
CC_WASM        = $(WASI_SDK)/bin/clang
WASM_VALIDATE ?= wasm-validate

# ── Directories ──────────────────────────────────────────────────────────────
SRC_DIR     = src
INC_DIR     = include
STUB_DIR    = stub
BUILD_DIR   = build

# ── Source files ─────────────────────────────────────────────────────────────
WASM_SRCS = \
    $(SRC_DIR)/main.c    \
    $(SRC_DIR)/physics.c \
    $(SRC_DIR)/renderer.c \
    $(SRC_DIR)/levels.c  \
    $(SRC_DIR)/font.c

STUB_SRCS = \
    $(STUB_DIR)/hal_stub.c \
    $(SRC_DIR)/main.c      \
    $(SRC_DIR)/physics.c   \
    $(SRC_DIR)/renderer.c  \
    $(SRC_DIR)/levels.c    \
    $(SRC_DIR)/font.c

# ── Output artefacts ─────────────────────────────────────────────────────────
WASM_OUT  = $(BUILD_DIR)/gravity_dash.wasm
STUB_OUT  = $(BUILD_DIR)/gravity_dash_stub

# ── WASM compiler flags ───────────────────────────────────────────────────────
CFLAGS_WASM = \
    -O2                          \
    -std=c99                     \
    -Wall -Wextra                \
    -Wno-unused-parameter        \
    -nostdlib                    \
    -target wasm32-wasi          \
    -fno-builtin                 \
    -fno-stack-protector         \
    -I$(INC_DIR)

LDFLAGS_WASM = \
    -Wl,--no-entry               \
    -Wl,--export=app_init        \
    -Wl,--export=app_tick        \
    -Wl,--export=app_destroy     \
    -Wl,--import-undefined       \
    -Wl,--allow-undefined        \
    -lc -lm

# ── Desktop stub compiler flags ───────────────────────────────────────────────
CC_STUB    = gcc
CFLAGS_STUB = \
    -O2                          \
    -std=c99                     \
    -Wall -Wextra                \
    -Wno-unused-parameter        \
    -I$(INC_DIR)

LDFLAGS_STUB = \
    $(shell sdl2-config --libs)  \
    -lm

SDL2_CFLAGS = $(shell sdl2-config --cflags)

# ── Flash settings ────────────────────────────────────────────────────────────
SERIAL_PORT   ?= /dev/ttyUSB0
FLASH_OFFSET  ?= 0x310000
ESPTOOL       ?= esptool.py

# ── Default target ────────────────────────────────────────────────────────────
.PHONY: all
all: wasm

# ── WASM target ───────────────────────────────────────────────────────────────
.PHONY: wasm
wasm: $(WASM_OUT)

$(WASM_OUT): $(WASM_SRCS) $(INC_DIR)/game.h $(INC_DIR)/akira_api.h
	@mkdir -p $(BUILD_DIR)
	$(CC_WASM) $(CFLAGS_WASM) $(WASM_SRCS) $(LDFLAGS_WASM) -o $@
	@echo "==> Built: $@"
	@$(WASM_VALIDATE) $@ && echo "==> wasm-validate: OK" || echo "==> wasm-validate not found — skipped"
	@wasm_size=$$(wc -c < $@); \
	 echo "==> Binary size: $${wasm_size} bytes ($$(( wasm_size / 1024 )) KB)"; \
	 if [ "$$wasm_size" -gt 49152 ]; then \
	     echo "WARNING: binary exceeds 48 KB budget!"; \
	 fi

# ── Desktop stub target ───────────────────────────────────────────────────────
.PHONY: stub
stub: $(STUB_OUT)

$(STUB_OUT): $(STUB_SRCS) $(INC_DIR)/game.h $(INC_DIR)/akira_api.h
	@mkdir -p $(BUILD_DIR)
	$(CC_STUB) $(CFLAGS_STUB) $(SDL2_CFLAGS) $(STUB_SRCS) $(LDFLAGS_STUB) -o $@
	@echo "==> Built: $@"
	@echo "==> Run:   ./$(STUB_OUT)"
	@echo "     LEFT/RIGHT arrow keys = IMU tilt"
	@echo "     Z / Enter = BTN_A,  X = BTN_B"

# ── Flash target ──────────────────────────────────────────────────────────────
.PHONY: flash
flash: $(WASM_OUT)
	$(ESPTOOL) --chip esp32s3 --port $(SERIAL_PORT) write_flash \
	    $(FLASH_OFFSET) $(WASM_OUT)
	@echo "==> Flashed $(WASM_OUT) to $(SERIAL_PORT) at offset $(FLASH_OFFSET)"

# ── Clean ─────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	@echo "==> Cleaned build artefacts"
