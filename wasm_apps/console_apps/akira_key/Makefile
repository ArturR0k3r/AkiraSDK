APP_NAME  = akira_key
WASI_SDK ?= /opt/wasi-sdk
CC        = $(WASI_SDK)/bin/clang

TARGET = $(APP_NAME).wasm
SRCS   = key_main.c key_crypto.c key_store.c key_ui.c

CFLAGS  = -O2 -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -Wno-implicit-function-declaration
CFLAGS += -Wno-incompatible-library-redeclaration
CFLAGS += -Wno-macro-redefined
CFLAGS += -DAKIRA_NO_PRINTF_INLINE
CFLAGS += -I../../../include

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
