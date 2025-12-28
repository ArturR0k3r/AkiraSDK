Logic Analyzer sample

This simple example demonstrates how a WASM app can sample GPIO pins using the OCRE API `ocre_gpio_pin_get` and visualize the result on the display.

Features
- Periodically sample a small set of GPIO pins
- Log transitions with `akira_log` (visible in serial console)
- Draw a rolling waveform on the display: each column represents a sample

Usage
1. Build: `make`
2. Install the produced `logic-analyzer.wasm` into your AkiraOS environment
3. Run on the device or simulator

Notes
- The sample pins are hardcoded in `main.c` and intended as an example; adapt to your board and wiring.
- Uses `gpio` and `display` permissions in `manifest.json`.
