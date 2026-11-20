# kws_demo — AkiraClaw keyword spotting demo

APP_NAME   = kws_demo
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
LDFLAGS += -z stack-size=8192
LDFLAGS += -Wl,--initial-memory=196608
LDFLAGS += -Wl,--max-memory=196608

# Model binary — obtain a DS-CNN quantized KWS model and place it here.
# Reference: tensorflow/lite/micro/examples/micro_speech (hey_you, yes/no dataset)
# or train with the Google Speech Commands dataset and quantize to INT8.
MODEL ?= kws_model.tflite

all: $(TARGET)
	@if [ -f manifest.json ] && [ -f ../../../scripts/embed_manifest.py ]; then \
		python3 ../../../scripts/embed_manifest.py $(TARGET) manifest.json $(TARGET) && \
		echo "  Manifest embedded into $(TARGET)"; \
	fi

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	@echo "Built: $(TARGET)"

# Pack + sign workflow (requires akira-cli and MODEL to exist)
pack: all
	@if [ ! -f "$(MODEL)" ]; then \
		echo "Error: model file '$(MODEL)' not found."; \
		echo "Provide a quantized TFLite model: make pack MODEL=path/to/kws.tflite"; \
		exit 1; \
	fi
	akira-cli pack $(TARGET) manifest.json --model $(MODEL) -o $(APP_NAME).akpkg
	@echo "Created: $(APP_NAME).akpkg"

sign: pack
	@if [ -z "$(KEY)" ]; then \
		echo "Usage: make sign KEY=privkey.pem"; exit 1; \
	fi
	akira-cli sign $(APP_NAME).akpkg --key $(KEY)
	@echo "Signed: $(APP_NAME).akpkg"

clean:
	rm -f $(TARGET) $(APP_NAME).akpkg

.PHONY: all pack sign clean
