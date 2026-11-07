# AkiraConsole shared UI kit (WASM side) — header-only.
#
# akira_ui.h is `static inline`, so there is no library to build: apps just
#   #include "akira_api.h"
#   #include "akira_ui.h"
# in their single main.c. This target is a header syntax check + demo build.

WASI_SDK ?= /opt/wasi-sdk
CC        = $(WASI_SDK)/bin/clang
CFLAGS    = -O2 -nostdlib -Wall -Wextra -Wno-unused-parameter -I. -I../../include

all: check demo

# Compile the header standalone to catch errors without an app.
check:
	echo '#include "akira_api.h"' >  .ui_check.c
	echo '#include "akira_ui.h"'  >> .ui_check.c
	echo 'int main(void){return 0;}' >> .ui_check.c
	$(CC) $(CFLAGS) -c .ui_check.c -o /dev/null
	rm -f .ui_check.c
	@echo "akira_ui.h: OK"

demo:
	$(MAKE) -C ui_demo

clean:
	$(MAKE) -C ui_demo clean
	rm -f .ui_check.c

.PHONY: all check demo clean
