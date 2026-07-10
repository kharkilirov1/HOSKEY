#!/usr/bin/env python3
"""
pack_hosdict.py — Packs asset files into a .hosdict binary container.

Usage:
  python pack_hosdict.py <manifest.json> <asset_dir> <output.hosdict>
  python pack_hosdict.py --generate-manifest <asset_dir> <output_manifest.json>

Binary format:
  [4]  magic "HSKD" (little-endian 0x484B5344 -> bytes b'HSKD')
  [4]  version (1)
  [4]  asset_count
  [4]  manifest_size
  [manifest_size] JSON manifest (UTF-8)
  For each asset:
    [4]  key_length
    [key_length] key (UTF-8)
    [4]  format_length
    [format_length] format string (UTF-8)
    [8]  data_offset (from file start)
    [8]  data_size
  [padding to 16-byte alignment]
  Asset data blobs (concatenated, each 16-byte aligned)
"""

import argparse
import json
import os
import struct
import sys

MAGIC = b"HSKD"
VERSION = 1
ALIGNMENT = 16


def align_up(value: int, alignment: int = ALIGNMENT) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def pad_to_alignment(data: bytearray, alignment: int = ALIGNMENT) -> None:
    remainder = len(data) % alignment
    if remainder != 0:
        data.extend(b"\x00" * (alignment - remainder))


def pack_hosdict(manifest_path: str, asset_dir: str, output_path: str) -> None:
    with open(manifest_path, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    package_name = manifest.get("package_name", "")
    locale = manifest.get("locale", "")
    version = manifest.get("version", VERSION)
    assets = manifest.get("assets", [])

    if not package_name:
        print("ERROR: manifest missing 'package_name'", file=sys.stderr)
        sys.exit(1)

    # Re-serialize manifest JSON compactly for embedding.
    manifest_bytes = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode("utf-8")

    # Read all asset files.
    asset_data_list = []
    for asset in assets:
        key = asset["key"]
        fmt = asset["format"]
        rel_file = asset["file"]
        file_path = os.path.join(asset_dir, rel_file)

        if not os.path.isfile(file_path):
            print(f"WARNING: asset file not found, skipping: {file_path}", file=sys.stderr)
            continue

        with open(file_path, "rb") as f:
            data = f.read()

        asset_data_list.append({
            "key": key,
            "format": fmt,
            "data": data,
        })

    asset_count = len(asset_data_list)
    print(f"Packing {asset_count} assets from '{asset_dir}' into '{output_path}'")
    print(f"  package_name: {package_name}")
    print(f"  locale: {locale}")
    print(f"  version: {version}")

    # Build the output buffer.
    buf = bytearray()

    # Header: magic + version + asset_count + manifest_size
    buf.extend(MAGIC)
    buf.extend(struct.pack("<I", version))
    buf.extend(struct.pack("<I", asset_count))
    buf.extend(struct.pack("<I", len(manifest_bytes)))

    # Manifest JSON
    buf.extend(manifest_bytes)

    # We need to compute data offsets, which depend on the index size.
    # First, compute the total size of the index section.
    index_size = 0
    for ad in asset_data_list:
        key_bytes = ad["key"].encode("utf-8")
        fmt_bytes = ad["format"].encode("utf-8")
        # key_length(4) + key + format_length(4) + format + offset(8) + size(8)
        index_size += 4 + len(key_bytes) + 4 + len(fmt_bytes) + 8 + 8

    # After index, pad to alignment, then asset data starts.
    header_and_manifest_size = len(buf)
    index_end = header_and_manifest_size + index_size
    data_start = align_up(index_end)

    # Compute per-asset offsets.
    current_data_offset = data_start
    asset_offsets = []
    for ad in asset_data_list:
        size = len(ad["data"])
        asset_offsets.append((current_data_offset, size))
        current_data_offset = align_up(current_data_offset + size)

    # Write asset index.
    for i, ad in enumerate(asset_data_list):
        key_bytes = ad["key"].encode("utf-8")
        fmt_bytes = ad["format"].encode("utf-8")
        offset, size = asset_offsets[i]

        buf.extend(struct.pack("<I", len(key_bytes)))
        buf.extend(key_bytes)
        buf.extend(struct.pack("<I", len(fmt_bytes)))
        buf.extend(fmt_bytes)
        buf.extend(struct.pack("<Q", offset))
        buf.extend(struct.pack("<Q", size))

    # Pad to alignment before data section.
    pad_to_alignment(buf)

    assert len(buf) == data_start, f"Index section size mismatch: {len(buf)} != {data_start}"

    # Write asset data blobs.
    for ad in asset_data_list:
        buf.extend(ad["data"])
        pad_to_alignment(buf)

    # Write output file.
    with open(output_path, "wb") as f:
        f.write(buf)

    print(f"Written {len(buf)} bytes to '{output_path}'")
    for i, ad in enumerate(asset_data_list):
        offset, size = asset_offsets[i]
        print(f"  [{i}] key='{ad['key']}' format='{ad['format']}' "
              f"offset={offset} size={size}")


def generate_default_ru_manifest(asset_dir: str, output_path: str) -> None:
    """Generate default manifest for Russian keyboard dictionary."""
    manifest = {
        "package_name": "ru.smart.default",
        "locale": "ru-RU",
        "version": 1,
        "assets": [
            {"key": "config", "format": "feature_json", "file": "config"},
            {"key": "lexicon.default", "format": "succinct_lexicon_v1", "file": "trie"},
            {"key": "lexicon.experiments.KEYBOARD-13421_v2", "format": "succinct_lexicon_k13421",
             "file": "KEYBOARD-13421_trie"},
            {"key": "rules.transfeme_default", "format": "tfmtrie_v1", "file": "rules.bin"},
            {"key": "rules.transfeme_experiments.KEYBOARD-13421_v2", "format": "tfmtrie_k13421",
             "file": "KEYBOARD-13421_rules.bin"},
            {"key": "normdict", "format": "normdict_v2", "file": "normdict"},
            {"key": "lemma.hash", "format": "chd_hash", "file": "lemmer_mhash"},
            {"key": "lemma.rules", "format": "lemma_rules_program", "file": "lemmer_rules"},
            {"key": "policy.blacklist", "format": "policy_blob", "file": "blacklist"},
            {"key": "policy.blacklist_suggest", "format": "policy_blob", "file": "blacklist_suggest"},
            {"key": "policy.autocorrect_blocker", "format": "policy_blob",
             "file": "autocorrect_blocker"},
            {"key": "policy.abbr", "format": "policy_blob", "file": "abbr"},
            {"key": "features.autocc", "format": "feature_json",
             "file": "features_autocc.json"},
            {"key": "features.ranker", "format": "feature_json",
             "file": "features_ranker.json"},
            {"key": "features.tap_model", "format": "feature_json",
             "file": "features_tap_model.json"},
        ]
    }

    # Filter to only assets that actually exist in asset_dir.
    if os.path.isdir(asset_dir):
        existing = []
        for asset in manifest["assets"]:
            path = os.path.join(asset_dir, asset["file"])
            if os.path.isfile(path):
                existing.append(asset)
            else:
                print(f"  NOTE: '{asset['file']}' not found in '{asset_dir}', "
                      f"included in manifest anyway")

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)

    print(f"Generated manifest with {len(manifest['assets'])} assets: '{output_path}'")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Pack asset files into a .hosdict binary container.")
    subparsers = parser.add_subparsers(dest="command")

    # Pack command
    pack_parser = subparsers.add_parser("pack", help="Pack assets into .hosdict")
    pack_parser.add_argument("manifest", help="Path to manifest JSON file")
    pack_parser.add_argument("asset_dir", help="Directory containing asset files")
    pack_parser.add_argument("output", help="Output .hosdict file path")

    # Generate manifest command
    gen_parser = subparsers.add_parser("generate-manifest",
                                        help="Generate default RU manifest")
    gen_parser.add_argument("asset_dir", help="Directory containing asset files")
    gen_parser.add_argument("output", help="Output manifest JSON path")

    args = parser.parse_args()

    if args.command == "pack":
        pack_hosdict(args.manifest, args.asset_dir, args.output)
    elif args.command == "generate-manifest":
        generate_default_ru_manifest(args.asset_dir, args.output)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
