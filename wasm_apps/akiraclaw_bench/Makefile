APP_NAME  = akiraclaw_bench
WASI_SDK ?= /opt/wasi-sdk
CC        = $(WASI_SDK)/bin/clang

# ── Build variants ────────────────────────────────────────────────────────
# make                 → wasm_claw (CG enabled, ai.infer in manifest)
# make VARIANT=noguard → wasm_noguard  (cap check bypassed by CG-disabled build)
# make VARIANT=mpu     → wasm_claw_mpu (mock — ARM MPU unavailable on ESP32-S3)
VARIANT ?= claw

ifeq ($(VARIANT),noguard)
  TARGET   = $(APP_NAME)_noguard.wasm
  CONFIG   = wasm_noguard
else ifeq ($(VARIANT),mpu)
  TARGET   = $(APP_NAME)_mpu.wasm
  CONFIG   = wasm_claw_mpu
  MPU_FLAG = -DBENCH_MPU=1
else
  TARGET   = $(APP_NAME)_claw.wasm
  CONFIG   = wasm_claw
  MPU_FLAG =
endif

SRCS    = main.c

CFLAGS  = -O2 -nostdlib
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CFLAGS += -I../../include
CFLAGS += -DBENCH_CONFIG=\"$(CONFIG)\"
CFLAGS += $(MPU_FLAG)

LDFLAGS  = -Wl,--no-entry
LDFLAGS += -Wl,--export=main
LDFLAGS += -Wl,--allow-undefined
LDFLAGS += -Wl,--strip-all
LDFLAGS += -z stack-size=8192
# 128 KB: 4 KB sample array + 64 KB for WASM runtime stacks + headroom
LDFLAGS += -Wl,--initial-memory=131072
LDFLAGS += -Wl,--max-memory=131072

all: $(TARGET)

$(TARGET): $(SRCS) ../aiinfer_test/model_data.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)
	@echo "  Built: $(TARGET)"
	@if [ -f manifest.json ] && [ -f ../../scripts/embed_manifest.py ]; then \
		python3 ../../scripts/embed_manifest.py $(TARGET) manifest.json $(TARGET) && \
		echo "  Manifest embedded in $(TARGET)"; \
	fi

# Generate C header for embedding in the native Zephyr benchmark app.
# The native app (bench_b1_wasm.c) includes wasm_data_*.h.
wasm_data_claw.h: $(APP_NAME)_claw.wasm
	xxd -i $< | sed 's/unsigned char/static const unsigned char/g; \
	                 s/unsigned int/static const unsigned int/g' > $@
	@echo "  Generated: $@"

wasm_data_noguard.h: $(APP_NAME)_noguard.wasm
	xxd -i $< | sed 's/unsigned char/static const unsigned char/g; \
	                 s/unsigned int/static const unsigned int/g' > $@
	@echo "  Generated: $@"

wasm_data_mpu.h: $(APP_NAME)_mpu.wasm
	xxd -i $< | sed 's/unsigned char/static const unsigned char/g; \
	                 s/unsigned int/static const unsigned int/g' > $@
	@echo "  Generated: $@"

# Build all three variants and generate all headers (run before west build)
headers: wasm_data_claw.h wasm_data_noguard.h wasm_data_mpu.h
	@echo "  All WASM headers generated — copy to bench/akiraclaw_bench/src/"

all_variants:
	$(MAKE) VARIANT=claw
	$(MAKE) VARIANT=noguard
	$(MAKE) VARIANT=mpu
	$(MAKE) headers

clean:
	rm -f $(APP_NAME)_*.wasm wasm_data_*.h

.PHONY: all clean headers all_variants
