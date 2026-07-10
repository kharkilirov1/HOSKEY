#!/usr/bin/env python3
"""
package_asset_gate.py — strict packaged-dictionary gate.

This catches regressions back to loose text dictionaries:
  - no words.txt / TSV / standalone predictor artifact in HAP;
  - .hosdict may contain binary lexicon.predictor;
  - .hosdict does not contain lexicon.words.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HAP = ROOT / "entry/build/default/outputs/default/app/entry-default.hap"
DEFAULT_PREDICTOR = ROOT / "tools/lexicon.predictor.hpt"


def fail(message: str) -> None:
    print(f"PACKAGE ASSET GATE FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_hosdict(data: bytes) -> list[tuple[str, str, int]]:
    if len(data) < 16:
        fail(".hosdict too small")
    magic, version, count, manifest_size = struct.unpack_from("<IIII", data, 0)
    if magic != 0x444B5348:  # HSKD
        fail(".hosdict magic mismatch")
    if version != 1:
        fail(f".hosdict unsupported version: {version}")
    off = 16 + manifest_size
    assets: list[tuple[str, str, int]] = []
    for _ in range(count):
        if off + 4 > len(data):
            fail(".hosdict index truncated at key length")
        key_len = struct.unpack_from("<I", data, off)[0]
        off += 4
        key = data[off : off + key_len].decode("utf-8")
        off += key_len

        if off + 4 > len(data):
            fail(".hosdict index truncated at format length")
        fmt_len = struct.unpack_from("<I", data, off)[0]
        off += 4
        fmt = data[off : off + fmt_len].decode("utf-8")
        off += fmt_len

        if off + 16 > len(data):
            fail(".hosdict index truncated at asset span")
        _, size = struct.unpack_from("<QQ", data, off)
        off += 16
        assets.append((key, fmt, size))
    return assets


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--hap", type=Path, default=DEFAULT_HAP)
    parser.add_argument(
        "--hosdict",
        type=Path,
        help="check a raw .hosdict directly instead of reading it from a HAP",
    )
    args = parser.parse_args()

    source_label = ""
    if args.hosdict:
        if not args.hosdict.exists():
            fail(f".hosdict not found: {args.hosdict}")
        source_label = f"hosdict={args.hosdict}"
        hosdict_data = args.hosdict.read_bytes()
    else:
        if not args.hap.exists():
            fail(f"HAP not found: {args.hap}")

        forbidden_suffixes = (
            "words.txt",
            "words_with_weights.tsv",
            "lexicon.predictor.hpt",
        )
        with zipfile.ZipFile(args.hap) as zf:
            names = zf.namelist()
            forbidden = [
                name
                for name in names
                if any(name.endswith(suffix) for suffix in forbidden_suffixes)
            ]
            if forbidden:
                fail("forbidden loose dictionary assets in HAP: " + ", ".join(forbidden))

            hosdict_entries = [name for name in names if name.endswith(".hosdict")]
            if len(hosdict_entries) != 1:
                fail(f"expected exactly one .hosdict in HAP, got {hosdict_entries}")
            hosdict_data = zf.read(hosdict_entries[0])
        source_label = f"hap={args.hap}"

    assets = parse_hosdict(hosdict_data)
    by_key = {key: (fmt, size) for key, fmt, size in assets}
    if "lexicon.words" in by_key:
        fail(".hosdict still contains lexicon.words")
    if "lexicon.default" not in by_key:
        fail(".hosdict missing lexicon.default")

    lex_default = by_key["lexicon.default"]
    print("PACKAGE ASSET GATE OK")
    print(f"  {source_label}")
    print(f"  assets={len(assets)}")
    print(f"  lexicon.default=succinct_lexicon_v1, {lex_default[1]} bytes")

    # lexicon.predictor is optional now (1nc7 is primary).
    if "lexicon.predictor" in by_key:
        predictor = by_key["lexicon.predictor"]
        print(f"  lexicon.predictor={predictor[0]}, {predictor[1]} bytes (fallback)")
    else:
        print("  lexicon.predictor: not present (1nc7 primary)")


if __name__ == "__main__":
    main()
