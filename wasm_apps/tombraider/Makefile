# Tomb Raider (OpenLara fixed-point engine) for AkiraOS.
#
#   make            -> build/tombraider.wasm (manifest embedded)
#   make aot        -> build/tombraider.aot  (xtensa / esp32s3)
#   make host       -> host/out/tr_host      (native 32-bit test build)
#
# Level data (packed GBA levels: TITLE/GYM/LEVEL1/LEVEL2.PKD and TITLE.SCR)
# goes in the app's SD directory: /SD:/apps/tombraider/data/. It is
# copyrighted game data: never commit or ship it.

WASI_SDK  ?= $(HOME)/wasi-sdk
WAMRC     ?= wamrc
CXX        = $(WASI_SDK)/bin/clang++
# Frame size; the panel is 320x240 and the picture is centred.
FRAME_W   ?= 320
FRAME_H   ?= 240
MEMORY    ?= 3735552

SDK_ROOT   = ../..
BUILD      = build
WASM       = $(BUILD)/tombraider.wasm
AOT        = $(BUILD)/tombraider.aot
HOST       = host/out/tr_host

SRCS       = akira_main.cpp openlara/common.cpp openlara/render.cpp

DEFS       = -D__AKIRA__ -DFRAME_WIDTH=$(FRAME_W) -DFRAME_HEIGHT=$(FRAME_H)
CXXFLAGS   = $(DEFS) -Iopenlara -I. -O3 -fno-exceptions -fno-rtti \
             -fno-threadsafe-statics -w

WASM_FLAGS = --target=wasm32-wasi -flto
# No WASI on AkiraOS: no crt1 (_start); main is called by the runtime and
# every import must be an AkiraOS native ("env"), listed in natives.syms.
LDFLAGS    = -nostartfiles -nostdlib++ -Wl,--no-entry -Wl,--export=main
LDFLAGS   += -Wl,--allow-undefined-file=natives.syms -Wl,--strip-all -Wl,--gc-sections
LDFLAGS   += -z stack-size=65536
# Fixed-size memory: WAMR copies linear memory on every memory.grow.
LDFLAGS   += -Wl,--initial-memory=$(MEMORY) -Wl,--max-memory=$(MEMORY)

all: $(WASM)

$(WASM): $(SRCS) akira_natives.h natives.syms manifest.json $(wildcard openlara/*.h) | $(BUILD)
	$(CXX) $(WASM_FLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SRCS)
	python3 $(SDK_ROOT)/scripts/embed_manifest.py $@ manifest.json $@

aot: $(AOT)

$(AOT): $(WASM)
	$(WAMRC) --target=xtensa --cpu=esp32s3 --opt-level=3 --size-level=0 \
		--bounds-checks=0 --emit-custom-sections=.akira.manifest -o $@ $<

host: $(HOST)

$(HOST): $(SRCS) host/host_natives.c akira_natives.h $(wildcard openlara/*.h)
	mkdir -p host/out
	g++ -m32 $(CXXFLAGS) -g -c akira_main.cpp -o host/out/akira_main.o
	g++ -m32 $(CXXFLAGS) -g -c openlara/common.cpp -o host/out/common.o
	g++ -m32 $(CXXFLAGS) -g -c openlara/render.cpp -o host/out/render.o
	gcc -m32 -O2 -g -c host/host_natives.c -o host/out/host_natives.o
	g++ -m32 -o $@ host/out/*.o

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) host/out

.PHONY: all aot host clean
