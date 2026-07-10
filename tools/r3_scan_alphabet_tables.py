#!/usr/bin/env python3
"""Scan x86_64 libjni_ykeyboard3.so for Unicode alphabet tables.

Goals:
- prove that the binary contains both CP866-style and CP1251-style label tables
- show that level-2 trie blob1 decodes plausibly under CP1251-style mapping
- show that the old CP866-only assumption is too narrow for current trie work
"""

from __future__ import annotations

import struct
from pathlib import Path

BIN_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\native_libs\x86_64\libjni_ykeyboard3.so")
TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")

# Empirically derived from matching Ghidra virtual addresses (e.g. 0x009e6b38)
# to file offsets in the x86_64 ELF.
GHIDRA_IMAGE_DELTA = 0x104000


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def utf32_seq(start: int, count: int) -> bytes:
    return b"".join((start + i).to_bytes(4, "little") for i in range(count))


def decode_table(data: bytes, table_off: int, limit: int = 256) -> list[int]:
    return [u32(data, table_off + i * 4) for i in range(limit)]


def safe_chr(codepoint: int) -> str:
    if codepoint == 0:
        return "."
    try:
        ch = chr(codepoint)
    except ValueError:
        return "?"
    if ch.isprintable() and ch not in {"\r", "\n", "\t"}:
        return ch
    return "."


def preview_map(table: list[int], values: bytes) -> str:
    out = []
    for b in values:
        cp = table[b] if b < len(table) else 0
        out.append(safe_chr(cp))
    return "".join(out)


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
    count = u64(data, off + 8)
    nxt = off + count * 8 + 0x20
    return {"off": off, "count": count, "next": nxt}


def parse_intvector_tail(data: bytes, off: int) -> dict[str, int]:
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
    return {"tail": tail, "len1": len1, "blob1": blob1, "len2": len2, "blob2": blob2, "child_flag": child_flag, "child_start": child_start}


def parse_levels(data: bytes, off: int = 8, max_depth: int = 4) -> list[dict[str, int]]:
    out = []
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
        out.append({"depth": depth, "start": off, **tail})
        if tail["child_flag"] == 0:
            break
        off = tail["child_start"]
        depth += 1
    return out


def scan_tables(data: bytes) -> tuple[list[int], list[int]]:
    pat_upper = utf32_seq(0x0410, 32)
    pat_low1 = utf32_seq(0x0430, 16)
    pat_low2 = utf32_seq(0x0440, 16)
    pat_low_full = utf32_seq(0x0430, 32)

    cp866_style: list[int] = []
    cp1251_style: list[int] = []

    max_off = len(data) - 0x400 - len(pat_upper)
    for off in range(0, max_off, 4):
        if (
            data[off : off + len(pat_upper)] == pat_upper
            and data[off + 0x80 : off + 0x80 + len(pat_low1)] == pat_low1
            and data[off + 0x180 : off + 0x180 + len(pat_low2)] == pat_low2
        ):
            cp866_style.append(off)

        if (
            data[off + 0x300 : off + 0x300 + len(pat_upper)] == pat_upper
            and data[off + 0x380 : off + 0x380 + len(pat_low_full)] == pat_low_full
        ):
            cp1251_style.append(off)

    return cp866_style, cp1251_style


def fmt_va(off: int) -> str:
    return f"off=0x{off:X} va=0x{off + GHIDRA_IMAGE_DELTA:08X}"


def main() -> None:
    bin_data = BIN_PATH.read_bytes()
    trie_data = TRIE_PATH.read_bytes()
    levels = parse_levels(trie_data)
    level2 = levels[-1]
    blob1 = trie_data[level2["blob1"] : level2["blob1"] + level2["len1"]]

    cp866_tables, cp1251_tables = scan_tables(bin_data)
    print(f"BIN_PATH={BIN_PATH}")
    print(f"TRIE_PATH={TRIE_PATH}")
    print(f"GHIDRA_IMAGE_DELTA=0x{GHIDRA_IMAGE_DELTA:X}")
    print()

    print(f"CP866-style tables found: {len(cp866_tables)}")
    for off in cp866_tables:
        table = decode_table(bin_data, off)
        print(
            "  "
            + fmt_va(off)
            + f" sample[0,32,96,224]=({safe_chr(table[0])},{safe_chr(table[32])},{safe_chr(table[96])},{safe_chr(table[224])})"
        )
    print()

    print(f"CP1251-style tables found: {len(cp1251_tables)}")
    for off in cp1251_tables:
        table = decode_table(bin_data, off)
        print(
            "  "
            + fmt_va(off)
            + f" sample[192,224,237,255]=({safe_chr(table[192])},{safe_chr(table[224])},{safe_chr(table[237])},{safe_chr(table[255])})"
        )
    print()

    cp866 = decode_table(bin_data, cp866_tables[0])
    cp1251 = decode_table(bin_data, cp1251_tables[0])
    head = blob1[:96]
    print(f"LEVEL2 blob1 off=0x{level2['blob1']:X} len={level2['len1']}")
    print(f"blob1 head bytes={head.hex()}")
    print(f"blob1 raw cp1251={head.decode('cp1251', errors='replace')!r}")
    print(f"blob1 via first CP866-style Unicode table ={preview_map(cp866, head)!r}")
    print(f"blob1 via first CP1251-style Unicode table={preview_map(cp1251, head)!r}")
    print()

    interesting = [0x20, 0x2D, 0x32, 0xC0, 0xCF, 0xD0, 0xDF, 0xE0, 0xE5, 0xED, 0xFF]
    print("Selected byte mapping comparison:")
    for b in interesting:
        print(
            f"  0x{b:02X}: cp866={safe_chr(cp866[b])} U+{cp866[b]:04X} | "
            f"cp1251={safe_chr(cp1251[b])} U+{cp1251[b]:04X}"
        )


if __name__ == "__main__":
    main()
