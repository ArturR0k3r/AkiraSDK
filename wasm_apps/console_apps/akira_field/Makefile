# akira_field — Dual-Radio Field Diagnostic Tool (CC1121 + LR1121)
APP_NAME  = akira_field
WASI_SDK ?= /opt/wasi-sdk
CC        = $(WASI_SDK)/bin/clang

TARGET = $(APP_NAME).wasm
SRCS   = field_main.c field_cc1121.c field_lr1121.c field_config.c \
         field_link.c field_scan.c field_txbench.c field_ui.c

CFLAGS  = -O2 -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -Wno-implicit-function-declaration
CFLAGS += -Wno-incompatible-library-redeclaration
CFLAGS += -DAKIRA_NO_PRINTF_INLINE
CFLAGS += -I../../../include

# 512 KB: waterfall buffer + RX rings + scan buffers
LDFLAGS  = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=16384
LDFLAGS += -Wl,--initial-memory=524288
LDFLAGS += -Wl,--max-memory=524288

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
