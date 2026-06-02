"""
hello_world — display API test for AkiraOS.

Build:
  python3 scripts/py_to_wasm.py python/apps/hello_world/main.py -o wasm_apps/bin/hello_world_py.wasm
"""

import _akira as akira

WHITE  = 0xFFFF
BLACK  = 0x0000
RED    = 0xF800
GREEN  = 0x07E0
BLUE   = 0x001F
YELLOW = 0xFFE0
CYAN   = 0x07FF

def main():
    akira.display_clear(WHITE)
    akira.display_text(10, 10, "Hello Python!", BLACK)
    akira.display_rect(10, 30, 100, 20, RED)
    akira.display_circle(120, 80, 30, GREEN)
    akira.display_line(0, 120, 240, 120, BLUE)
    akira.display_text(10, 130, "Display works!", YELLOW)
    akira.printf_native("Display test done")

    while True:
        akira.sleep(1000)

main()
