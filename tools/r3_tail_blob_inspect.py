#!/usr/bin/env python3
"""Inspect tail blobs reached by the current FUN_00660496 emulator.

Goal:
- confirm whether blob1 looks like a node-aligned label byte stream
- confirm whether blob2 looks like a leaf continuation/suffix pool
"""

from __future__ import annotations

import collections
import re
import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def parse_raw(data: bytes, off: int) -> dict[str, int]:
    field0 = u64(data, off)
    count = u64(data, off + 8)
    nxt = off + count * 8 + 0x10
    return {"off": off, "field0": field0, "count": count, "next": nxt}


def parse_bv(data: bytes, off: int) -> dict[str, int]:
    word_count = u64(data, off)
    ds_off = off + word_count * 4 + (4 if (word_count & 1) == 0 else 0) + 0x1C
    ds_count = u64(data, ds_off + 8)
    nxt = ds_off + ds_count * 8 + 0x20
    return {"off": off, "word_count": word_count, "ds_off": ds_off, "ds_count": ds_count, "next": nxt}


def parse_ds(data: bytes, off: int) -> dict[str, int]:
    field0 = u64(data, off)
    count = u64(data, off + 8)
    field2 = u64(data, off + 16)
    nxt = off + count * 8 + 0x20
    return {"off": off, "field0": field0, "count": count, "field2": field2, "next": nxt}


def parse_intvector_tail(data: bytes, off: int) -> dict[str, int]:
    bitwidth = u64(data, off)
    hdr1 = u64(data, off + 8)
    hdr2 = u64(data, off + 16)
    span = u64(data, off + 24)
    tail = off + 0x10 + span * 8 + 0x10

    len1 = u64(data, tail)
    blob1 = tail + 8
    off2 = blob1 + len1
    len2 = u64(data, off2)
    blob2 = off2 + 8
    off3 = blob2 + len2
    child_flag = u64(data, off3)
    child_start = off3 + 8
    return {
        "off": off,
        "bitwidth": bitwidth,
        "hdr1": hdr1,
        "hdr2": hdr2,
        "span": span,
        "tail": tail,
        "len1": len1,
        "blob1": blob1,
        "len2": len2,
        "blob2": blob2,
        "child_flag": child_flag,
        "child_start": child_start,
    }


def parse_levels(data: bytes, off: int = 8, max_depth: int = 4) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    depth = 0
    while depth < max_depth:
        raw1 = parse_raw(data, off)
        bv1 = parse_bv(data, raw1["next"])
        raw2 = parse_raw(data, bv1["next"])
        bv2 = parse_bv(data, raw2["next"])
        raw3 = parse_raw(data, bv2["next"])
        ds3 = parse_ds(data, raw3["next"])
        raw4 = parse_raw(data, ds3["next"])
        ds4 = parse_ds(data, raw4["next"])
        tail = parse_intvector_tail(data, ds4["next"])
        out.append(
            {
                "depth": depth,
                "start": off,
                "raw1": raw1,
                "raw2": raw2,
                "raw3": raw3,
                "raw4": raw4,
                "ds3": ds3,
                "ds4": ds4,
                "tail": tail,
            }
        )
        if tail["child_flag"] == 0:
            break
        off = tail["child_start"]
        depth += 1
    return out


def clean_text(text: str) -> str:
    text = text.replace("\r", " ").replace("\n", " ")
    text = "".join(ch if (ch.isprintable() and ch != "\ufffd") else "." for ch in text)
    return re.sub(r"\s+", " ", text)


def inspect_blob(name: str, blob: bytes) -> list[str]:
    lines: list[str] = []
    lines.append(f"{name}: len={len(blob)}")
    lines.append(f"  head16={blob[:16].hex()}")
    lines.append(f"  head64={blob[:64].hex()}")
    if not blob:
        lines.append("  empty")
        return lines

    ctr = collections.Counter(blob)
    zero_ratio = ctr.get(0, 0) / len(blob)
    ascii_ratio = sum(32 <= x < 127 for x in blob) / len(blob)
    lines.append(f"  unique_bytes={len(ctr)} zero_ratio={zero_ratio:.4f} ascii_ratio={ascii_ratio:.4f}")
    lines.append(
        "  top_bytes="
        + " ".join(f"{b:02x}:{n}" for b, n in ctr.most_common(12))
    )

    cp = blob[:256].decode("cp1251", errors="replace")
    lines.append(f"  cp1251_head={clean_text(cp)!r}")

    parts = [p.decode("cp1251", errors="replace") for p in blob.split(b'\x00') if p]
    nonempty = [clean_text(p) for p in parts if p]
    if nonempty and ctr.get(0, 0) > 0:
        avg_len = sum(len(p) for p in nonempty) / len(nonempty)
        lines.append(f"  nul_parts={len(nonempty)} avg_part_len={avg_len:.2f}")
        lines.append("  sample_parts=" + ", ".join(repr(p) for p in nonempty[:20]))
    else:
        lines.append("  nul_parts=0")
    return lines


def main() -> None:
    data = TRIE_PATH.read_bytes()
    print(f"TRIE_PATH={TRIE_PATH}")
    print(f"SIZE={len(data)}")
    print()
    for level in parse_levels(data):
        depth = level["depth"]
        raw2 = level["raw2"]
        raw3 = level["raw3"]
        raw4 = level["raw4"]
        tail = level["tail"]
        print(f"LEVEL {depth} start=0x{level['start']:X}")
        print(
            f"  tail blob1=0x{tail['blob1']:X} len1={tail['len1']} "
            f"blob2=0x{tail['blob2']:X} len2={tail['len2']} child={tail['child_flag']}"
        )
        print(
            f"  raw field0 comparison: raw2={raw2['field0']} raw3={raw3['field0']} raw4={raw4['field0']} "
            f"delta(len1-raw4)={tail['len1'] - raw4['field0']}"
        )
        blob1 = data[tail["blob1"]:tail["blob1"] + tail["len1"]]
        blob2 = data[tail["blob2"]:tail["blob2"] + tail["len2"]]
        for line in inspect_blob("blob1", blob1):
            print(" ", line)
        for line in inspect_blob("blob2", blob2):
            print(" ", line)
        print()


if __name__ == "__main__":
    main()
