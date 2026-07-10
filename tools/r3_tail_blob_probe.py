#!/usr/bin/env python3
"""Probe candidate tail layouts for FUN_00660496.

Confirmed from decomp:
  [len1:u64][blob1:len1 bytes][len2:u64][blob2:len2 bytes][child:u64][...]

This script does not claim the offsets are correct. It helps inspect hypotheses.
"""

from __future__ import annotations

import struct
from collections import Counter
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")

# Current hypotheses worth checking after the 2026-04-08 decomp breakthrough.
CANDIDATE_OFFSETS = [
    636392,
    636400,
    636408,
    636416,
    636424,
]


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def score_blob(blob: bytes) -> dict[str, object]:
    top = Counter(blob[: min(len(blob), 1024)]).most_common(12)
    low_ascii = sum(b < 0x80 for b in blob[: min(len(blob), 1024)])
    return {
        "sample_len": min(len(blob), 1024),
        "low_ascii_ratio": round(low_ascii / max(1, min(len(blob), 1024)), 4),
        "top12": top,
        "first32_hex": blob[:32].hex(),
    }


def inspect_offset(data: bytes, off: int) -> dict[str, object]:
    out: dict[str, object] = {"offset": off}
    if off < 0 or off + 8 > len(data):
        out["error"] = "out_of_range"
        return out

    len1 = u64(data, off)
    out["len1"] = len1
    off_blob1 = off + 8
    off2 = off_blob1 + len1
    out["blob1_offset"] = off_blob1
    out["off2"] = off2

    if off2 + 8 > len(data):
        out["error"] = "len1_runs_past_eof"
        return out

    len2 = u64(data, off2)
    out["len2"] = len2
    off_blob2 = off2 + 8
    off3 = off_blob2 + len2
    out["blob2_offset"] = off_blob2
    out["off3"] = off3

    if off3 + 8 > len(data):
        out["error"] = "len2_runs_past_eof"
        return out

    child = u64(data, off3)
    out["child"] = child

    blob1 = data[off_blob1:off_blob1 + min(len1, len(data) - off_blob1)]
    blob2 = data[off_blob2:off_blob2 + min(len2, len(data) - off_blob2)]
    out["blob1_stats"] = score_blob(blob1)
    out["blob2_stats"] = score_blob(blob2)
    return out


def main() -> None:
    data = TRIE_PATH.read_bytes()
    print(f"TRIE_PATH={TRIE_PATH}")
    print(f"SIZE={len(data)}")
    print()

    for off in CANDIDATE_OFFSETS:
        info = inspect_offset(data, off)
        print("=" * 80)
        print(f"offset={off}")
        if "error" in info:
            print(f"  error={info['error']}")
            print(f"  len1={info.get('len1')}")
            print(f"  off2={info.get('off2')}")
            continue
        print(f"  len1={info['len1']} blob1_offset={info['blob1_offset']}")
        print(f"  len2={info['len2']} blob2_offset={info['blob2_offset']}")
        print(f"  child={info['child']} off3={info['off3']}")
        b1 = info["blob1_stats"]
        b2 = info["blob2_stats"]
        print(f"  blob1 low_ascii_ratio={b1['low_ascii_ratio']} top12={b1['top12']}")
        print(f"  blob1 first32={b1['first32_hex']}")
        print(f"  blob2 low_ascii_ratio={b2['low_ascii_ratio']} top12={b2['top12']}")
        print(f"  blob2 first32={b2['first32_hex']}")


if __name__ == "__main__":
    main()
