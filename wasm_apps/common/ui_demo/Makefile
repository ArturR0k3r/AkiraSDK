WASI_SDK ?= /opt/wasi-sdk
CC        = $(WASI_SDK)/bin/clang
TARGET    = ui_demo.wasm
SRCS      = main.c          # akira_ui is header-only — no separate TU

CFLAGS  = -O2 -nostdlib -Wall -Wextra -Wno-unused-parameter
CFLAGS += -I.. -I../../../include

LDFLAGS  = -Wl,--no-entry -Wl,--export=main -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all -z stack-size=8192
LDFLAGS += -Wl,--initial-memory=131072 -Wl,--max-memory=131072

all: $(TARGET)

$(TARGET): $(SRCS) ../akira_ui.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
