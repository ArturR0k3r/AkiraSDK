#!/usr/bin/env python3
"""
py_to_wasm.py — Package a Python script into an AkiraOS WASM app.

Injects the Python source as a WASM custom section ("akira_py_script") into a
prebuilt micropython.wasm runtime, then optionally embeds the manifest.

The MicroPython runtime reads this section at startup and executes the script.
No C compilation is required — only standard Python tools.

Usage:
  python3 py_to_wasm.py app.py -o app.wasm [options]

Options:
  -o, --output <file>      Output WASM file (default: <app_stem>.wasm)
  --manifest <file>        manifest.json to embed (default: manifest.json
                           alongside the script, if it exists)
  --runtime <file>         micropython.wasm to use as base runtime
                           (default: $MICROPYTHON_WASM or
                            AkiraSDK/python/runtime/micropython.wasm)
  --sdk-root <path>        AkiraSDK root (default: auto-detect)
  --no-manifest            Skip manifest embedding
  -v, --verbose            Verbose output

Examples:
  python3 py_to_wasm.py hello_world/main.py -o hello_world.wasm
  python3 py_to_wasm.py app.py --manifest manifest.json -o app.wasm
  MICROPYTHON_WASM=/path/to/micropython.wasm python3 py_to_wasm.py app.py

Custom section format (WASM spec §2.4):
  Section id  : 0x00  (1 byte)
  Section size: varuint32
  Name length : varuint32
  Name bytes  : ASCII "akira_py_script"
  Data bytes  : UTF-8 Python source

Copyright (c) 2025 AkiraOS Contributors
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import os
import struct
import sys

SECTION_NAME = b"akira_py_script"


# ── WASM binary helpers ───────────────────────────────────────────────────────

def _encode_uleb128(value: int) -> bytes:
    """Encode an unsigned integer as LEB128 (WASM varuint32)."""
    result = []
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            byte |= 0x80
        result.append(byte)
        if not value:
            break
    return bytes(result)


def _build_custom_section(name: bytes, data: bytes) -> bytes:
    """Build a WASM custom section (id=0) containing the given name and data."""
    name_encoded = _encode_uleb128(len(name)) + name
    section_content = name_encoded + data
    section_size = _encode_uleb128(len(section_content))
    return b"\x00" + section_size + section_content


def _validate_wasm(data: bytes) -> None:
    """Check that data starts with the WASM magic and version header."""
    if len(data) < 8:
        raise ValueError("File too small to be a valid WASM binary")
    if data[:4] != b"\x00asm":
        raise ValueError(
            f"Not a WASM binary — magic bytes are {data[:4]!r} (expected \\x00asm)"
        )


def inject_custom_section(wasm_bytes: bytes, section_name: bytes, section_data: bytes) -> bytes:
    """Append a new custom section to an existing WASM binary."""
    _validate_wasm(wasm_bytes)
    custom = _build_custom_section(section_name, section_data)
    return wasm_bytes + custom


# ── Tool discovery ────────────────────────────────────────────────────────────

def find_sdk_root(start: str) -> str | None:
    """Walk upward from *start* to find the AkiraSDK root (contains include/akira_api.h)."""
    path = os.path.abspath(start)
    for _ in range(10):
        if os.path.isfile(os.path.join(path, "include", "akira_api.h")):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return None


def find_micropython_wasm(sdk_root: str | None) -> str | None:
    """Locate micropython.wasm: env var → SDK runtime dir."""
    env = os.environ.get("MICROPYTHON_WASM")
    if env and os.path.isfile(env):
        return env
    if sdk_root:
        candidate = os.path.join(sdk_root, "python", "runtime", "micropython.wasm")
        if os.path.isfile(candidate):
            return candidate
    return None


def find_embed_script(sdk_root: str | None) -> str | None:
    if sdk_root:
        p = os.path.join(sdk_root, "scripts", "embed_manifest.py")
        if os.path.isfile(p):
            return p
    return None


# ── Main ─────────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Package a Python script into an AkiraOS WASM app.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Examples:")[1].split("\n\n")[0] if "Examples:" in __doc__ else "",
    )
    parser.add_argument("script", help="Python source file to package")
    parser.add_argument("-o", "--output", help="Output .wasm file")
    parser.add_argument("--manifest", help="manifest.json to embed")
    parser.add_argument("--runtime", help="micropython.wasm base runtime")
    parser.add_argument("--sdk-root", help="AkiraSDK root directory")
    parser.add_argument("--no-manifest", action="store_true", help="Skip manifest embedding")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    verbose = args.verbose

    # ── Locate inputs ─────────────────────────────────────────────────────
    script_path = os.path.abspath(args.script)
    if not os.path.isfile(script_path):
        print(f"ERROR: Script not found: {script_path}", file=sys.stderr)
        return 1

    script_dir = os.path.dirname(script_path)
    script_stem = os.path.splitext(os.path.basename(script_path))[0]

    sdk_root = args.sdk_root or find_sdk_root(script_path)
    if verbose and sdk_root:
        print(f"  SDK root: {sdk_root}")

    # ── Find MicroPython runtime ───────────────────────────────────────────
    runtime_path = args.runtime or find_micropython_wasm(sdk_root)
    if not runtime_path:
        print(
            "ERROR: micropython.wasm not found.\n"
            "Options:\n"
            "  1. Set MICROPYTHON_WASM=/path/to/micropython.wasm\n"
            "  2. Pass --runtime /path/to/micropython.wasm\n"
            "  3. Place it at AkiraSDK/python/runtime/micropython.wasm\n"
            "  4. Build it: see AkiraSDK/python/runtime/README.md",
            file=sys.stderr,
        )
        return 1

    # ── Locate manifest ───────────────────────────────────────────────────
    manifest_path = None
    if not args.no_manifest:
        if args.manifest:
            manifest_path = os.path.abspath(args.manifest)
        else:
            candidate = os.path.join(script_dir, "manifest.json")
            if os.path.isfile(candidate):
                manifest_path = candidate

    # ── Output path ───────────────────────────────────────────────────────
    output_path = os.path.abspath(args.output or f"{script_stem}.wasm")

    # ── Read inputs ───────────────────────────────────────────────────────
    if verbose:
        print(f"  Script  : {script_path}")
        print(f"  Runtime : {runtime_path}")
        print(f"  Manifest: {manifest_path or '(none)'}")
        print(f"  Output  : {output_path}")

    with open(runtime_path, "rb") as f:
        wasm_bytes = f.read()

    try:
        _validate_wasm(wasm_bytes)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    with open(script_path, "rb") as f:
        script_bytes = f.read()

    # ── Inject custom section ─────────────────────────────────────────────
    print(f"  Injecting Python script ({len(script_bytes)} bytes) → {SECTION_NAME.decode()} section")
    result_bytes = inject_custom_section(wasm_bytes, SECTION_NAME, script_bytes)

    # ── Write intermediate WASM ───────────────────────────────────────────
    with open(output_path, "wb") as f:
        f.write(result_bytes)

    # ── Embed manifest ────────────────────────────────────────────────────
    if manifest_path:
        embed_script = find_embed_script(sdk_root)
        if embed_script:
            import subprocess
            result = subprocess.run(
                [sys.executable, embed_script, output_path, manifest_path, output_path],
                capture_output=True,
                text=True,
            )
            if result.returncode == 0:
                print("  Manifest embedded")
            else:
                print(f"  WARNING: Manifest embedding failed: {result.stderr.strip()}")
        else:
            print("  WARNING: embed_manifest.py not found — manifest not embedded")
    elif not args.no_manifest and verbose:
        print("  No manifest.json found — skipping manifest embedding")

    # ── Summary ───────────────────────────────────────────────────────────
    size = os.path.getsize(output_path)
    print(f"  OK: {output_path} ({size:,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
