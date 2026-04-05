# macro_pad - AkiraOS WASM Application Makefile

APP_NAME   = macro_pad
WASI_SDK  ?= /opt/wasi-sdk
CC         = $(WASI_SDK)/bin/clang

TARGET  = $(APP_NAME).wasm
SRCS    = main.c

CFLAGS  = -O2 -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -I../../../include

LDFLAGS  = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=4096
LDFLAGS += -Wl,--initial-memory=65536
LDFLAGS += -Wl,--max-memory=65536

all: $(TARGET)
	@if [ -f manifest.json ] && [ -f ../../../scripts/embed_manifest.py ]; then \
		python3 ../../../scripts/embed_manifest.py $(TARGET) manifest.json $(TARGET) && \
		echo "  ✓ Manifest embedded"; \
	fi

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	@echo "✓ Built: $(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all clean
