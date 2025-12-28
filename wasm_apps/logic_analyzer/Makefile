WASI_SDK ?= /opt/wasi-sdk
CC = $(WASI_SDK)/bin/clang
OBJDUMP = $(WASI_SDK)/bin/wasm-objdump

# Include path for akira_api.h
INCLUDE = -I../include

CFLAGS = -Os -nostdlib $(INCLUDE)
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes

LDFLAGS = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=4096
LDFLAGS += -Wl,--initial-memory=65536
LDFLAGS += -Wl,--max-memory=65536

TARGET = logic_analyzer.wasm
SRCS = main.c

all: $(TARGET)

$(TARGET): $(SRCS)
	# Prefer the SDK helper script which will include SDK stubs when available
ifeq ($(wildcard ../../build_wasm_app.sh),)
ifeq ($(wildcard ../../build.sh),)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
else
	../../build.sh -o $@ $(SRCS)
endif
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
