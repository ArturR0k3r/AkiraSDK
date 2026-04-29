# glasses_mystery - AkiraOS WASM Application Makefile

APP_NAME   = glasses_mystery
WASI_SDK  ?= /opt/wasi-sdk
CC         = $(WASI_SDK)/bin/clang

TARGET  = $(APP_NAME).wasm

SRCS    = main.c engine.c world.c entities.c dialogue.c

CFLAGS  = -O2 -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -I../../../include

LDFLAGS  = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=4096
LDFLAGS += -Wl,--initial-memory=131072
LDFLAGS += -Wl,--max-memory=131072

all: $(TARGET)
	@if [ -f manifest.json ] && [ -f ../../../scripts/embed_manifest.py ]; then \
		python3 ../../../scripts/embed_manifest.py $(TARGET) manifest.json $(TARGET) && \
		echo "  ✓ Manifest embedded"; \
	fi

$(TARGET): $(SRCS) engine.h world.h entities.h dialogue.h assets.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)
	@echo "✓ Built: $(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all clean
