#!/usr/bin/env python3
"""Emulate the confirmed control-flow skeleton of FUN_00660496.

This is an offset tracer, not a full semantic decoder.
It follows the now-confirmed handler chain:

  raw -> bv -> raw -> bv -> raw -> ds -> raw -> ds -> intvector -> tail blobs -> recurse

The formulas are derived from current Ghidra decomp on 2026-04-08.
"""

from __future__ import annotations

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
    rank_size = u64(data, off + 8)
    popcount = u64(data, off + 16)
    ds_off = off + word_count * 4 + (4 if (word_count & 1) == 0 else 0) + 0x1C
    ds_count = u64(data, ds_off + 8)
    nxt = ds_off + ds_count * 8 + 0x20
    return {
        "off": off,
        "word_count": word_count,
        "rank_size": rank_size,
        "popcount": popcount,
        "ds_off": ds_off,
        "ds_count": ds_count,
        "next": nxt,
    }


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

    # Matches the current decomp of FUN_0066084e:
    # stream += 0x10
    # read qword at +8 from that advanced stream as span
    # output = advanced_stream + span*8 + 0x10
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


def trace_level(data: bytes, off: int, depth: int, max_depth: int) -> list[str]:
    lines: list[str] = []
    indent = "  " * depth
    lines.append(f"{indent}LEVEL {depth} start=0x{off:X} ({off})")

    raw1 = parse_raw(data, off)
    lines.append(f"{indent}  raw1 off=0x{raw1['off']:X} field0={raw1['field0']} count={raw1['count']} next=0x{raw1['next']:X}")

    bv1 = parse_bv(data, raw1["next"])
    lines.append(
        f"{indent}  bv1  off=0x{bv1['off']:X} words={bv1['word_count']} rank={bv1['rank_size']} pop={bv1['popcount']} "
        f"ds=0x{bv1['ds_off']:X} ds_count={bv1['ds_count']} next=0x{bv1['next']:X}"
    )

    raw2 = parse_raw(data, bv1["next"])
    lines.append(f"{indent}  raw2 off=0x{raw2['off']:X} field0={raw2['field0']} count={raw2['count']} next=0x{raw2['next']:X}")

    bv2 = parse_bv(data, raw2["next"])
    lines.append(
        f"{indent}  bv2  off=0x{bv2['off']:X} words={bv2['word_count']} rank={bv2['rank_size']} pop={bv2['popcount']} "
        f"ds=0x{bv2['ds_off']:X} ds_count={bv2['ds_count']} next=0x{bv2['next']:X}"
    )

    raw3 = parse_raw(data, bv2["next"])
    lines.append(f"{indent}  raw3 off=0x{raw3['off']:X} field0={raw3['field0']} count={raw3['count']} next=0x{raw3['next']:X}")

    ds3 = parse_ds(data, raw3["next"])
    lines.append(
        f"{indent}  ds3   off=0x{ds3['off']:X} field0={ds3['field0']} count={ds3['count']} field2={ds3['field2']} next=0x{ds3['next']:X}"
    )

    raw4 = parse_raw(data, ds3["next"])
    lines.append(f"{indent}  raw4 off=0x{raw4['off']:X} field0={raw4['field0']} count={raw4['count']} next=0x{raw4['next']:X}")

    ds4 = parse_ds(data, raw4["next"])
    lines.append(
        f"{indent}  ds4   off=0x{ds4['off']:X} field0={ds4['field0']} count={ds4['count']} field2={ds4['field2']} next=0x{ds4['next']:X}"
    )

    tail = parse_intvector_tail(data, ds4["next"])
    lines.append(
        f"{indent}  intv  off=0x{tail['off']:X} bw={tail['bitwidth']} hdr1={tail['hdr1']} hdr2={tail['hdr2']} span={tail['span']} tail=0x{tail['tail']:X}"
    )
    lines.append(
        f"{indent}  tail  len1={tail['len1']} blob1=0x{tail['blob1']:X} len2={tail['len2']} blob2=0x{tail['blob2']:X} "
        f"child={tail['child_flag']} child_start=0x{tail['child_start']:X}"
    )

    if depth + 1 < max_depth and tail["child_flag"] != 0:
        lines.extend(trace_level(data, tail["child_start"], depth + 1, max_depth))

    return lines


def main() -> None:
    data = TRIE_PATH.read_bytes()
    print(f"TRIE_PATH={TRIE_PATH}")
    print(f"SIZE={len(data)}")
    print()
    for line in trace_level(data, 8, 0, 4):
        print(line)


if __name__ == "__main__":
    main()
