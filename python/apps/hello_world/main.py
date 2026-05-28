"""
hello_world — minimal Python application for AkiraOS.

Demonstrates console output and basic display drawing using the akira module.

Build this into a WASM app with:
  python3 AkiraSDK/scripts/py_to_wasm.py main.py -o hello_world.wasm

Or via the AkiraSDK build script:
  cd AkiraSDK/wasm_apps && ./build.sh python/hello_world

Copyright (c) 2025 AkiraOS Contributors
SPDX-License-Identifier: Apache-2.0
"""

import akira

def main():
    # ── Console output ─────────────────────────────────────────────────────
    akira.print("=================================")
    akira.print("  Hello from AkiraOS Python!    ")
    akira.print("=================================")
    akira.print("")
    akira.print("[INFO]  This is an info message")
    akira.print("[WARN]  This is a warning message")
    akira.print("")
    akira.print("WASM app executed successfully!")
    akira.print("Runtime: MicroPython/AkiraOS 1.0.0")

    # ── Display ────────────────────────────────────────────────────────────
    akira.display_clear(akira.COLOR_BLACK)
    akira.display_text(10, 10, "Hello from",    akira.COLOR_YELLOW)
    akira.display_text(10, 30, "Python WASM!",  akira.COLOR_WHITE)
    akira.display_rounded_rect(5, 5, 130, 50, 6, akira.COLOR_YELLOW)
    akira.display_flush()

main()
