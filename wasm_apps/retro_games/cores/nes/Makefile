# NES emulator template Makefile
# Used by rom_to_wasm.py — can also be invoked directly for testing.
#
# Usage (manual):
#   1. Generate rom_data.c first:
#      python3 ../../tools/rom_to_wasm.py game.nes --gen-rom-only -o rom_data.c
#   2. Build:
#      make GAME=game ROM_DATA=rom_data.c WASM_MEMORY=262144
#   3. Output: game.wasm (with embedded manifest)

GAME        ?= nes_game
ROM_DATA    ?= rom_data.c
WASI_SDK    ?= /opt/wasi-sdk
CC           = $(WASI_SDK)/bin/clang

# WASM linear memory (bytes): calculated by rom_to_wasm.py based on ROM size.
# Default 256KB covers Mapper-0 ROMs up to 32KB PRG + 8KB CHR.
WASM_MEMORY ?= 262144

TARGET  = $(GAME).wasm

SRCS    = main.c nes.c cpu6502.c ppu.c mapper.c $(ROM_DATA)

CFLAGS  = -O3 -flto -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -I$(CURDIR) -I../../../../include

LDFLAGS  = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=8192
LDFLAGS += -Wl,--initial-memory=$(WASM_MEMORY)
LDFLAGS += -Wl,--max-memory=$(WASM_MEMORY)

all: $(TARGET)
	@if [ -f manifest.json ] && [ -f ../../../../scripts/embed_manifest.py ]; then \
		python3 ../../../../scripts/embed_manifest.py $(TARGET) manifest.json $(TARGET) && \
		echo "  Manifest embedded"; \
	fi
	@echo "Built: $(TARGET)"

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) rom_data.c

.PHONY: all clean
