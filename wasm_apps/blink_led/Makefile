# Blink LED WASM Application
# Builds a GPIO/LED control app for AkiraOS
# Uses libc-builtin mode with env module (NOT WASI)

WASI_SDK ?= /opt/wasi-sdk
CC = $(WASI_SDK)/bin/clang
OBJDUMP = $(WASI_SDK)/bin/wasm-objdump

# Include path for akira_api.h
INCLUDE = -I../include

# Compiler flags for embedded WAMR
# -Os: Optimize for size (critical for 64KB memory limit)
# -nostdlib: Avoid WASI imports, only use env module
# -Wall -Wextra: Enable warnings for quality
# -Wno-unknown-attributes: Some attributes from akira_api.h may be unknown
CFLAGS = -Os -nostdlib $(INCLUDE)
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes

# Linker flags for WAMR embedded mode
# --no-entry: No default _start entry point
# --export=main: Export main function (OCRE runtime entry point)
# --allow-undefined: Allow imports from env module
# --strip-all: Remove all symbols for minimal size
# -z stack-size: Set stack to 4KB (leaving 60KB for heap)
# --initial-memory/--max-memory: Limit to 64KB (one WebAssembly page)
LDFLAGS = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=4096
LDFLAGS += -Wl,--initial-memory=65536
LDFLAGS += -Wl,--max-memory=65536

# Target
TARGET = blink_led.wasm
SRCS = main.c

all: $(TARGET)

$(TARGET): $(SRCS)
	# Prefer the SDK helper script which will include SDK stubs when available
ifeq ($(wildcard ../../build_wasm_app.sh),)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
else
	../../build_wasm_app.sh -o $@ $(SRCS)
endif
	@echo "✓ Built: $(TARGET)"
	@ls -la $(TARGET)

info: $(TARGET)
	@echo "=== WASM Module Info ==="
	$(OBJDUMP) -x $(TARGET) | head -60

clean:
	rm -f $(TARGET)

.PHONY: all clean info
