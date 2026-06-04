#!/usr/bin/env python3
"""
embed_manifest_aot.py — Append akira.manifest as an AOT custom section.

WAMR's wamrc strips WASM custom sections when compiling to AOT.
This script appends the manifest as a native AOT custom section so
AkiraOS's manifest_parser can find it in the .aot binary.

AOT custom section layout (from manifest_parser.c):
  [uint32 sec_type=100][uint32 sec_size]
  body:
    [uint32 sub_type=0]          # AOT_CUSTOM_SECTION_RAW
    [uint16 name_len]
    [name bytes (NUL-terminated)]
    [json payload bytes]

Usage:
  python3 embed_manifest_aot.py <input.aot> <manifest.json> <output.aot>
"""

import struct
import json
import sys
from pathlib import Path

AOT_SECTION_TYPE_CUSTOM = 100
AOT_CUSTOM_SECTION_RAW  = 0
SECTION_NAME = b".akira.manifest\x00"  # NUL-terminated


def embed(aot_path: str, manifest_path: str, output_path: str) -> None:
    aot_data = Path(aot_path).read_bytes()

    if not aot_data.startswith(b"\x00aot"):
        raise ValueError(f"{aot_path}: not a WAMR AOT binary (bad magic)")

    # Load and minify JSON
    manifest = json.loads(Path(manifest_path).read_text())
    json_bytes = json.dumps(manifest, separators=(",", ":")).encode("utf-8")

    # Remove any existing .akira.manifest custom section to avoid duplicates
    aot_clean = _strip_akira_section(aot_data)

    # Build new custom section body:
    #   uint32 sub_type + uint16 name_len + name + json
    name_bytes = SECTION_NAME
    name_len   = len(name_bytes)
    body = (
        struct.pack("<I", AOT_CUSTOM_SECTION_RAW) +   # sub_type  (4 bytes)
        struct.pack("<H", name_len) +                  # name_len  (2 bytes)
        name_bytes +                                   # name
        json_bytes                                     # JSON payload
    )

    # Align section start to 4 bytes (WAMR AOT parser aligns before each read)
    pad = (4 - (len(aot_clean) % 4)) % 4
    section = (
        b"\x00" * pad +
        struct.pack("<I", AOT_SECTION_TYPE_CUSTOM) +   # sec_type  (4 bytes)
        struct.pack("<I", len(body)) +                  # sec_size  (4 bytes)
        body
    )

    Path(output_path).write_bytes(aot_clean + section)
    print(f"  Embedded .akira.manifest ({len(json_bytes)} bytes JSON) → {output_path}")


def _strip_akira_section(data: bytes) -> bytes:
    """Remove any existing .akira.manifest AOT custom section."""
    out = bytearray(data[:8])  # preserve magic + version
    pos = 8
    while pos + 8 <= len(data):
        aligned = (pos + 3) & ~3
        if aligned + 8 > len(data):
            out += data[pos:]
            break
        if aligned > pos:
            out += data[pos:aligned]
            pos = aligned

        sec_type, sec_size = struct.unpack_from("<II", data, pos)
        sec_end = pos + 8 + sec_size

        if sec_type == AOT_SECTION_TYPE_CUSTOM and sec_size > 6:
            body = data[pos + 8 : sec_end]
            sub_type = struct.unpack_from("<I", body, 0)[0]
            if sub_type == AOT_CUSTOM_SECTION_RAW and len(body) > 6:
                name_len = struct.unpack_from("<H", body, 4)[0]
                if len(body) >= 6 + name_len:
                    name = body[6 : 6 + name_len]
                    if name.rstrip(b"\x00") == b".akira.manifest":
                        pos = sec_end
                        continue  # skip this section

        out += data[pos : sec_end]
        pos = sec_end

    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.aot> <manifest.json> <output.aot>")
        sys.exit(1)
    embed(sys.argv[1], sys.argv[2], sys.argv[3])
