# GR4V — AkiraConsole WASM game Makefile

APP_NAME  = gr4v
WASI_SDK ?= /opt/wasi-sdk
CC        = $(WASI_SDK)/bin/clang

TARGET = $(APP_NAME).wasm
SRCS   = src/main.c src/physics.c src/renderer.c src/effects.c \
         src/audio.c src/levels.c src/font.c

CFLAGS  = -O2 -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -Wno-incompatible-library-redeclaration
CFLAGS += -Iinclude -I../../../include

LDFLAGS  = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
# akira_api.h defines printf (non-static) in the header; each TU gets a copy.
LDFLAGS += -Wl,--allow-multiple-definition
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=4096
LDFLAGS += -Wl,--initial-memory=65536
LDFLAGS += -Wl,--max-memory=65536

# Desktop stub (SDL2) — for local testing without hardware
CC_NATIVE = gcc
STUB_SRCS = src/main.c src/physics.c src/renderer.c src/effects.c \
            src/audio.c src/levels.c src/font.c stub/hal_stub.c
STUB_CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter \
              -Wno-builtin-declaration-mismatch -Wno-incompatible-pointer-types \
              -DGAME_STUB -Iinclude -I../../../include
STUB_LDFLAGS = $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2") -lm \
               -Wl,--allow-multiple-definition

.PHONY: all clean desktop

all: $(TARGET)
	@if [ -f manifest.json ] && [ -f ../../../scripts/embed_manifest.py ]; then \
		python3 ../../../scripts/embed_manifest.py $(TARGET) manifest.json $(TARGET) && \
		echo "  ✓ Manifest embedded"; \
	fi

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	@echo "✓ Built: $(TARGET) ($$(wc -c < $(TARGET)) bytes)"

desktop: $(STUB_SRCS)
	$(CC_NATIVE) $(STUB_CFLAGS) $(STUB_SRCS) $(STUB_LDFLAGS) -o $(APP_NAME)_desktop
	@echo "✓ Built: $(APP_NAME)_desktop (run ./$(APP_NAME)_desktop)"

clean:
	rm -f $(TARGET) $(APP_NAME)_desktop
