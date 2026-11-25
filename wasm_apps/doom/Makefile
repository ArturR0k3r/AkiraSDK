# Doom (doomgeneric) for AkiraOS.
#
#   make            -> build/doom.wasm (manifest embedded)
#   make aot        -> build/doom.aot  (xtensa / esp32s3)
#
# Put the IWAD (freedoom1.wad) in the app's SD directory: /SD:/apps/doom/.

WASI_SDK  ?= $(HOME)/wasi-sdk
WAMRC     ?= wamrc
CC         = $(WASI_SDK)/bin/clang
# Zone in KiB: 2.75 MiB. With static data/stack (0.57 MB) and the rest of the
# heap (~0.16 MB) it fits 57 pages; 3 MiB (61 pages) is ~50 KB too big for
# the PSRAM left on AkiraConsole Prod once the app file and code are loaded.
ZONE_KB   ?= 2816
MEMORY    ?= 3735552

SDK_ROOT   = ../..
BUILD      = build
WASM       = $(BUILD)/doom.wasm
AOT        = $(BUILD)/doom.aot

DOOM_SRCS := $(wildcard doomgeneric/*.c)
SRCS       = $(DOOM_SRCS) doomgeneric_akira.c akira_shim.c

CFLAGS  = --target=wasm32-wasi -O3 -flto
CFLAGS += -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
CFLAGS += -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOM_ZONE_KB=\"$(ZONE_KB)\"
CFLAGS += -include akira_shim.h -I. -Idoomgeneric -I$(SDK_ROOT)/include
CFLAGS += -w

# No WASI on AkiraOS: no crt1 (_start); main is called by the runtime and
# every import must be an AkiraOS native ("env"), listed in natives.syms;
# any other undefined symbol is a link error.
LDFLAGS  = -nostartfiles -Wl,--no-entry -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined-file=natives.syms -Wl,--strip-all -Wl,--gc-sections
LDFLAGS += -z stack-size=65536
# Fixed-size memory: WAMR reallocates (copies) linear memory on every
# memory.grow, which would briefly need twice the ~4 MB Doom uses.
# Host measurement (all 36 Freedoom maps, -mb 3): heap peak ~3.3 MB, plus
# at most 0.64 MB static data/BSS and stack (~3.96 MB in total).
LDFLAGS += -Wl,--initial-memory=$(MEMORY) -Wl,--max-memory=$(MEMORY)

all: $(WASM)

$(WASM): $(SRCS) akira_shim.h akira_natives.h natives.syms manifest.json | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)
	python3 $(SDK_ROOT)/scripts/embed_manifest.py $@ manifest.json $@

aot: $(AOT)

$(AOT): $(WASM)
	$(WAMRC) --target=xtensa --cpu=esp32s3 --opt-level=3 --size-level=0 \
		--bounds-checks=0 --emit-custom-sections=.akira.manifest -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all aot clean
