#!/usr/bin/env python3
"""
embed_manifest.py — Inject a JSON manifest as a WASM custom section.

AkiraOS reads the ".akira.manifest" custom section at runtime to determine
the app's capability mask and memory quota. Without it, cap_mask = 0 and
every security check fails.

Usage:
    python3 embed_manifest.py <input.wasm> <manifest.json> <output.wasm>

The section is appended at the end of the WASM binary. WASM validators
accept custom sections at any position, so this is spec-compliant.
"""

import sys
import os


def encode_leb128(n: int) -> bytes:
    """Encode a non-negative integer as unsigned LEB128."""
    result = []
    while True:
        byte = n & 0x7F
        n >>= 7
        if n != 0:
            byte |= 0x80
        result.append(byte)
        if n == 0:
            break
    return bytes(result)


def build_custom_section(name: str, payload: bytes) -> bytes:
    """
    Build a WASM custom section:
        byte  0x00              ; section id = 0 (custom)
        LEB128(section_size)    ; total byte count below
        LEB128(name_len)        ; length of name string
        name_bytes              ; UTF-8 name
        payload_bytes           ; raw section content
    """
    name_bytes = name.encode("utf-8")
    name_len_leb = encode_leb128(len(name_bytes))
    section_data = name_len_leb + name_bytes + payload
    section_size_leb = encode_leb128(len(section_data))
    return bytes([0x00]) + section_size_leb + section_data


def embed_manifest(wasm_path: str, json_path: str, output_path: str) -> None:
    with open(wasm_path, "rb") as f:
        wasm = f.read()

    # Sanity-check: must start with WASM magic + version
    if len(wasm) < 8 or wasm[:4] != b"\x00asm":
        print(f"ERROR: {wasm_path} is not a valid WASM binary", file=sys.stderr)
        sys.exit(1)

    with open(json_path, "rb") as f:
        json_data = f.read()

    section = build_custom_section(".akira.manifest", json_data)
    out = wasm + section

    with open(output_path, "wb") as f:
        f.write(out)

    print(
        f"  Embedded .akira.manifest ({len(json_data)} bytes JSON, "
        f"{len(section)} bytes section) → {output_path}"
    )


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(
            f"Usage: {sys.argv[0]} <input.wasm> <manifest.json> <output.wasm>",
            file=sys.stderr,
        )
        sys.exit(1)

    wasm_in, json_in, wasm_out = sys.argv[1], sys.argv[2], sys.argv[3]

    for path in (wasm_in, json_in):
        if not os.path.isfile(path):
            print(f"ERROR: file not found: {path}", file=sys.stderr)
            sys.exit(1)

    embed_manifest(wasm_in, json_in, wasm_out)
