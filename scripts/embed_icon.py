#!/usr/bin/env python3
"""
embed_icon.py — Resize an icon image and inject it into an app manifest.

The icon is resized to 32×32 RGBA, the raw pixel bytes are base64-encoded,
and the result is stored as an "icon" field in the manifest JSON.
AkiraOS reads this field at install time to display the app icon in the launcher.

Usage:
    python3 embed_icon.py <icon.png> <manifest.json> [output_manifest.json]

If output_manifest.json is omitted the input file is updated in-place.

Dependencies:
    pip install Pillow
"""

import base64
import json
import sys
from pathlib import Path


def embed_icon(icon_path: str, manifest_path: str, output_path: str) -> None:
    try:
        from PIL import Image
    except ImportError:
        print(
            "Error: Pillow is required.  Install with: pip install Pillow",
            file=sys.stderr,
        )
        sys.exit(1)

    icon_path_obj = Path(icon_path)
    if not icon_path_obj.exists():
        print(f"Error: icon file not found: {icon_path}", file=sys.stderr)
        sys.exit(1)

    manifest_path_obj = Path(manifest_path)
    if not manifest_path_obj.exists():
        print(f"Error: manifest not found: {manifest_path}", file=sys.stderr)
        sys.exit(1)

    # Load and resize icon to 32×32 RGBA
    with Image.open(icon_path_obj) as img:
        img = img.convert("RGBA")
        img = img.resize((32, 32), Image.LANCZOS)
        raw_bytes = img.tobytes()  # RGBA, row-major, 32×32×4 = 4096 bytes

    icon_b64 = base64.b64encode(raw_bytes).decode("ascii")

    with open(manifest_path_obj, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)

    manifest["icon"] = icon_b64

    with open(output_path, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")

    print(
        f"Icon embedded: {icon_path} ({len(raw_bytes)} raw bytes → {len(icon_b64)} b64 chars) → {output_path}"
    )


def main() -> None:
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print(f"Usage: {sys.argv[0]} <icon.png> <manifest.json> [output_manifest.json]")
        sys.exit(1)

    icon_path = sys.argv[1]
    manifest_path = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) == 4 else manifest_path

    embed_icon(icon_path, manifest_path, output_path)


if __name__ == "__main__":
    main()
