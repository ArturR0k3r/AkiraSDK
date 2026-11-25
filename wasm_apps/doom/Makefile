# Doom (doomgeneric) for AkiraOS.
#
#   make            -> build/doom.wasm (manifest embedded)
#   make aot        -> build/doom.aot  (xtensa / esp32s3)
#   make wad        -> build/freedoom1.wad (Freedoom Phase 1, checksum-verified)
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
# Host measurement (all 36 Freedoom maps): zone + ~0.16 MB other heap, plus
# ~0.6 MB static data/BSS and stack.
LDFLAGS += -Wl,--initial-memory=$(MEMORY) -Wl,--max-memory=$(MEMORY)

all: $(WASM)

$(WASM): $(SRCS) akira_shim.h akira_natives.h natives.syms manifest.json | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)
	python3 $(SDK_ROOT)/scripts/embed_manifest.py $@ manifest.json $@

aot: $(AOT)

$(AOT): $(WASM)
	$(WAMRC) --target=xtensa --cpu=esp32s3 --opt-level=3 --size-level=0 \
		--bounds-checks=0 --emit-custom-sections=.akira.manifest -o $@ $<

# Game data: Freedoom (BSD-licensed), from the official release.
FREEDOOM_VER    = 0.13.0
FREEDOOM_ZIP    = freedoom-$(FREEDOOM_VER).zip
FREEDOOM_URL    = https://github.com/freedoom/freedoom/releases/download/v$(FREEDOOM_VER)/$(FREEDOOM_ZIP)
FREEDOOM_SHA256 = 3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59

wad: $(BUILD)/freedoom1.wad

$(BUILD)/freedoom1.wad: | $(BUILD)
	curl -fL -o $(BUILD)/$(FREEDOOM_ZIP) $(FREEDOOM_URL)
	echo "$(FREEDOOM_SHA256)  $(BUILD)/$(FREEDOOM_ZIP)" | sha256sum -c -
	python3 -c "import sys, zipfile; sys.stdout.buffer.write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))" \
		$(BUILD)/$(FREEDOOM_ZIP) freedoom-$(FREEDOOM_VER)/freedoom1.wad > $@.tmp
	mv $@.tmp $@
	rm -f $(BUILD)/$(FREEDOOM_ZIP)

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all aot wad clean
