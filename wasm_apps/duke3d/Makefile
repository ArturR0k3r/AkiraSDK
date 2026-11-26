# Duke Nukem 3D (Chocolate Duke3D, ESP32 port by jkirsons) for AkiraOS.
#
#   make            -> build/duke3d.wasm (manifest embedded)
#   make aot        -> build/duke3d.aot  (xtensa / esp32s3)
#   make host       -> host/out/duke3d_host (native 32-bit test build)
#   make data       -> data/DUKE3D.GRP (shareware episode, checksum-verified)
#
# Game data (DUKE3D.GRP, shareware 1.3D or Atomic 1.4/1.5) goes in the app's
# SD directory: /SD:/apps/duke3d/. It is copyrighted: never commit or ship it.

WASI_SDK  ?= $(HOME)/wasi-sdk
WAMRC     ?= wamrc
CC         = $(WASI_SDK)/bin/clang
# Linear memory: static data + stack 2.11 MB, other heap ~0.45 MB, tile cache.
# On Prod it shares PSRAM with the kept .aot file (1.10 MB) and its code copy
# (~1 MB); Doom fits 5.43 MB in total, and 49 pages keeps Duke3D at ~5.36 MB.
# More pages fail to start with "allocate linear memory failed".
MEMORY    ?= 3211264
# Tile (art) cache in bytes. The shareware art is 6.8 MB; Build streams tiles
# through this cache. Host runs: 2 MB reads 2.6 MB from SD in the first 1000
# frames, 640 KB reads 4.7 MB; both play.
CACHE     ?= 524288
# Code size counts twice in PSRAM (file + code copy): -Os for most of the
# game, -O3 for the renderer and the frame conversion (FAST_SRCS), +39 KB.
OPT       ?= -Os
FAST_OPT  ?= -O3

SDK_ROOT   = ../..
BUILD      = build
WASM       = $(BUILD)/duke3d.wasm
AOT        = $(BUILD)/duke3d.aot
HOST       = host/out/duke3d_host

# mmulti.c is the networked multiplayer layer; dummy_multi.c is the
# single-player one the ESP32 port uses. Both define the same symbols.
# dummy_audiolib.c is commented out upstream; akira_nosound.c replaces it.
GAME_SRCS := $(filter-out duke3d/Engine/mmulti.c duke3d/Game/dummy_audiolib.c,\
               $(wildcard duke3d/Engine/*.c duke3d/Game/*.c))
SRCS       = $(GAME_SRCS) akira_sdl.c akira_nosound.c akira_shim.c
FAST_SRCS  = duke3d/Engine/engine.c duke3d/Engine/draw.c akira_sdl.c
OBJS       = $(patsubst %.c,$(BUILD)/obj/%.o,$(SRCS))

# The vendored code predates C99 strictness (implicit declarations,
# int/pointer mixing); modern clang makes those errors by default.
CFLAGS  = -std=gnu17 -w -Wno-error=int-conversion -Wno-error=implicit-function-declaration
CFLAGS += -Wno-error=incompatible-pointer-types -D_WASI_EMULATED_PROCESS_CLOCKS
CFLAGS += -DDUKE_CACHE_MAX=$(CACHE)
CFLAGS += -include akira_shim.h -I. -Istubs -Iduke3d/Engine -Iduke3d/Game
CFLAGS += -Iduke3d/SDL -Iduke3d/audiolib -I$(SDK_ROOT)/include

WASM_FLAGS = --target=wasm32-wasi -flto
# No WASI on AkiraOS: no crt1 (_start); main is called by the runtime and
# every import must be an AkiraOS native ("env"), listed in natives.syms.
LDFLAGS  = -nostartfiles -Wl,--no-entry -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined-file=natives.syms -Wl,--strip-all -Wl,--gc-sections
LDFLAGS += -z stack-size=65536
# Fixed-size memory: WAMR copies linear memory on every memory.grow.
LDFLAGS += -Wl,--initial-memory=$(MEMORY) -Wl,--max-memory=$(MEMORY)

all: $(WASM)

# make DIAG=1: function-entry ring for finding a loop that never draws
# (see akira_sdl.c). Game logic only: the platform layer and Build
# (Engine) is not instrumented. Own BUILD dir: make DIAG=1 BUILD=build-diag
ifeq ($(DIAG),1)
CFLAGS    += -DDUKE_DIAG
DIAG_SKIP  = $(wildcard duke3d/Engine/*.c) akira_sdl.c akira_shim.c akira_nosound.c
endif

$(BUILD)/obj/%.o: %.c akira_shim.h akira_natives.h
	@mkdir -p $(dir $@)
	$(CC) $(WASM_FLAGS) $(if $(filter $<,$(FAST_SRCS)),$(FAST_OPT),$(OPT)) $(CFLAGS) \
		$(if $(and $(filter 1,$(DIAG)),$(filter-out $(DIAG_SKIP),$<)),-finstrument-functions) -c -o $@ $<

$(WASM): $(OBJS) natives.syms manifest.json | $(BUILD)
	$(CC) $(WASM_FLAGS) -O2 $(LDFLAGS) -o $@ $(OBJS)
	python3 $(SDK_ROOT)/scripts/embed_manifest.py $@ manifest.json $@

aot: $(AOT)

# --stack-bounds-checks=1: AOT frames live on the app's native thread stack
# (11 KB, manifest "stack_kb"); Build's recursion (shoot/checkhitsprite, CON
# blocks) plus a native call at the deep end can exceed it. With the check,
# WAMR stops the app instead of the overflow halting the whole system.
$(AOT): $(WASM)
	$(WAMRC) --target=xtensa --cpu=esp32s3 --opt-level=3 --size-level=0 \
		--bounds-checks=0 --stack-bounds-checks=1 \
		--emit-custom-sections=.akira.manifest -o $@ $<

host: $(HOST)

# host_natives.c is plain host code: built without the shim.
$(HOST): $(SRCS) host/host_natives.c akira_shim.h akira_natives.h
	mkdir -p host/out
	gcc -m32 -O2 -g -c host/host_natives.c -o host/out/host_natives.o
	gcc -m32 $(CFLAGS) -fpermissive -O1 -g -o $@ $(SRCS) host/out/host_natives.o

# Game data: the shareware episode (v1.3D), from the Internet Archive copy of
# the original 3dduke13.zip, checksum-verified. The shareware licence lets
# individuals copy the package; do not commit or ship the GRP.
SW_ZIP_URL  = https://archive.org/download/3dduke13SW/Files/v1.3d%20(1998%20Repack)/3dduke13SW.zip
SW_ZIP_MD5  = 04e4ca70b8a2d59ed56c451c5c1d5d39
SW_GRP_SHA1 = a58bdbfaf28416528a0d9a4452f896f46774a806

data: data/DUKE3D.GRP

data/DUKE3D.GRP:
	mkdir -p data
	curl -fL -o data/3dduke13SW.zip "$(SW_ZIP_URL)"
	echo "$(SW_ZIP_MD5)  data/3dduke13SW.zip" | md5sum -c -
	python3 -c "import io, sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); \
		sys.stdout.buffer.write(zipfile.ZipFile(io.BytesIO(z.read('DN3DSW13.SHR'))).read('DUKE3D.GRP'))" \
		data/3dduke13SW.zip > $@.tmp
	echo "$(SW_GRP_SHA1)  $@.tmp" | sha1sum -c -
	mv $@.tmp $@
	rm -f data/3dduke13SW.zip

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) host/out

.PHONY: all aot host data clean
