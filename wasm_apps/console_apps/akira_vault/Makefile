# akira_vault — Hardware Crypto Wallet + Password Manager

APP_NAME  = akira_vault
WASI_SDK ?= /opt/wasi-sdk
CC        = $(WASI_SDK)/bin/clang

TARGET = $(APP_NAME).wasm
SRCS   = main.c

CFLAGS  = -O2 -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -I../../../include

# 16 KB stack (crypto call depth: PBKDF2 → HMAC → SHA-256)
# 256 KB heap (vault body ~2.4 KB, crypto buffers, display)
LDFLAGS  = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=16384
LDFLAGS += -Wl,--initial-memory=262144
LDFLAGS += -Wl,--max-memory=262144

all: $(TARGET)
	@if [ -f manifest.json ] && [ -f ../../../scripts/embed_manifest.py ]; then \
		python3 ../../../scripts/embed_manifest.py $(TARGET) manifest.json $(TARGET) && \
		echo "  Manifest embedded into $(TARGET)"; \
	fi

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	@echo "Built: $(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all clean
