#!/usr/bin/env python3
"""
gen_catalogue.py — Generate catalogue.json from a directory of signed .akpkg files.

For each .akpkg file the script:
  1. Extracts manifest.json from the gzip-compressed tar archive
  2. Reads metadata fields (name, version, capabilities, min_akiraos_version, …)
  3. Derives the category from the package slug (prefix before the first '-')
  4. Computes the file size in bytes
  5. Constructs download_url and icon_url based on the GitHub release tag

The resulting catalogue.json has the schema:
{
  "generated_at": "<ISO-8601 UTC>",
  "tag": "<release-tag>",
  "apps": [
    {
      "id":                  "<category>/<name>",
      "name":                "<name>",
      "display_name":        "<display_name or name>",
      "category":            "<generic|console_apps|retro_games>",
      "version":             "1.2.3",
      "size_bytes":          12345,
      "capabilities":        ["display.write", "input.read"],
      "min_akiraos_version": "1.0.0",
      "download_url":        "https://github.com/ArturR0k3r/AkiraOS/releases/download/<tag>/<slug>.akpkg",
      "icon_url":            "https://…/<slug>-icon.png" | null
    },
    …
  ]
}

Usage:
    python3 gen_catalogue.py --dir dist/ --tag v1.2.3 [--repo <owner/repo>] [--out catalogue.json]

The script exits with a non-zero status if any .akpkg is missing required manifest fields.
"""

import argparse
import gzip
import json
import os
import sys
import tarfile
from datetime import datetime, timezone
from pathlib import Path


REQUIRED_MANIFEST_FIELDS = ("name", "version", "capabilities", "min_akiraos_version")
GITHUB_RELEASES_BASE = "https://github.com/{repo}/releases/download/{tag}"


def extract_manifest(akpkg_path: str) -> dict:
    """Extract and parse manifest.json from a .akpkg archive."""
    with gzip.open(akpkg_path, "rb") as gz:
        with tarfile.open(fileobj=gz, mode="r:") as tar:
            try:
                member = tar.getmember("manifest.json")
            except KeyError:
                raise ValueError(f"manifest.json not found inside {akpkg_path}")
            fh = tar.extractfile(member)
            if fh is None:
                raise ValueError(f"Cannot read manifest.json from {akpkg_path}")
            return json.loads(fh.read().decode("utf-8"))


def slug_from_path(akpkg_path: str) -> str:
    """Return the filename stem, e.g. 'console_apps-tetris' from 'console_apps-tetris.akpkg'."""
    return Path(akpkg_path).stem


def category_and_name_from_slug(slug: str, manifest: dict) -> tuple[str, str]:
    """
    Derive (category, name) from the package slug.

    Slug convention: <category>-<name>  (dashes separate category from name)
    Category is the first segment; the rest forms the name.
    Fall back to manifest["name"] and manifest.get("category", "generic") if the
    slug doesn't match the expected pattern.
    """
    parts = slug.split("-", 1)
    if len(parts) == 2:
        category = parts[0]
        name = parts[1]
    else:
        name = manifest.get("name", slug)
        category = manifest.get("category", "generic")
    return category, name


def build_catalogue_entry(
    akpkg_path: str,
    tag: str,
    repo: str,
    icon_slugs: set[str],
) -> dict:
    manifest = extract_manifest(akpkg_path)

    # Validate required fields
    missing = [f for f in REQUIRED_MANIFEST_FIELDS if f not in manifest]
    if missing:
        raise ValueError(
            f"{akpkg_path}: manifest is missing required fields: {missing}"
        )

    slug = slug_from_path(akpkg_path)
    category, name = category_and_name_from_slug(slug, manifest)
    size_bytes = os.path.getsize(akpkg_path)
    base_url = GITHUB_RELEASES_BASE.format(repo=repo, tag=tag)

    icon_url = (
        f"{base_url}/{slug}-icon.png" if slug in icon_slugs else None
    )

    return {
        "id": f"{category}/{name}",
        "name": name,
        "display_name": manifest.get("display_name", manifest.get("name", name)),
        "category": category,
        "version": manifest["version"],
        "size_bytes": size_bytes,
        "capabilities": manifest["capabilities"],
        "min_akiraos_version": manifest["min_akiraos_version"],
        "download_url": f"{base_url}/{slug}.akpkg",
        "icon_url": icon_url,
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate catalogue.json from a directory of .akpkg files"
    )
    parser.add_argument(
        "--dir", required=True, help="Directory containing .akpkg (and optionally *-icon.png) files"
    )
    parser.add_argument(
        "--tag", required=True, help="Release tag name (e.g. v1.2.3)"
    )
    parser.add_argument(
        "--repo",
        default="ArturR0k3r/AkiraOS",
        help="GitHub owner/repo (default: ArturR0k3r/AkiraOS)",
    )
    parser.add_argument(
        "--out",
        default="-",
        help="Output path for catalogue.json (default: stdout)",
    )
    args = parser.parse_args()

    dist_dir = Path(args.dir)
    if not dist_dir.is_dir():
        print(f"Error: --dir '{args.dir}' is not a directory", file=sys.stderr)
        sys.exit(1)

    akpkg_files = sorted(dist_dir.glob("*.akpkg"))
    if not akpkg_files:
        print(f"Warning: no .akpkg files found in '{args.dir}'", file=sys.stderr)

    # Collect which slugs have an accompanying icon PNG in the same dir
    icon_slugs: set[str] = {
        p.stem.removesuffix("-icon")
        for p in dist_dir.glob("*-icon.png")
    }

    apps = []
    errors = []
    for akpkg in akpkg_files:
        try:
            entry = build_catalogue_entry(str(akpkg), args.tag, args.repo, icon_slugs)
            apps.append(entry)
        except Exception as exc:
            errors.append(str(exc))
            print(f"Error: {exc}", file=sys.stderr)

    if errors:
        sys.exit(1)

    # Sort by category then name for stable output
    apps.sort(key=lambda e: (e["category"], e["name"]))

    catalogue = {
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "tag": args.tag,
        "apps": apps,
    }

    output_json = json.dumps(catalogue, indent=2) + "\n"

    if args.out == "-":
        sys.stdout.write(output_json)
    else:
        Path(args.out).write_text(output_json, encoding="utf-8")
        print(f"Catalogue written: {args.out} ({len(apps)} apps)", file=sys.stderr)


if __name__ == "__main__":
    main()
