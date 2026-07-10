#!/usr/bin/env python3
"""Probe real FUN_00661462 outputs from the parsed trie intvector sections.

Uses the confirmed level offsets from r3_emulate_fun_00660496.py and candidate
terminal bitvectors to compute actual handoff keys `uVar5 = FUN_00661462(...)`.
"""

from __future__ import annotations

import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_fun_00661462_probe_2026-04-08.txt")

LEVELS = {
    0: {"bv1_ds": 0x1FEB8, "bv2_ds": 0x47788, "ds3": 0x6D9A8, "ds4": 0x93BC8, "blob1": 0xFDB98, "intv": 0x9B5E8},
    1: {"bv1_ds": 0x1F3118, "bv2_ds": 0x1F4C60, "ds3": 0x1F6668, "ds4": 0x1F8070, "blob1": 0x1FA530, "intv": 0x1F85C0},
    2: {"bv1_ds": 0x204CE8, "bv2_ds": 0x205048, "ds3": 0x205370, "ds4": 0x205698, "blob1": 0x205A20, "intv": 0x205758},
}
ROOT_IDS = [3, 4, 5, 6, 7, 8]


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def cp1251_char(b: int) -> str:
    try:
        ch = bytes([b]).decode("cp1251")
    except UnicodeDecodeError:
        return "?"
    return ch if ord(ch) >= 0x20 else "."


class BitVector:
    def __init__(self, buf: bytes, shift: int = 64):
        self.buf = buf
        self.shift = shift
        self.total_bits = len(buf) * 8 - shift

    def bit(self, pos: int) -> int:
        pos += self.shift
        if pos < 0 or pos >= len(self.buf) * 8:
            return 0
        return (self.buf[pos >> 3] >> (pos & 7)) & 1

    def rank1_exclusive(self, pos: int) -> int:
        return sum(self.bit(i) for i in range(pos))


def get_ds_bits(data: bytes, ds_off: int) -> bytes:
    count = u64(data, ds_off + 8)
    return data[ds_off + 24 : ds_off + 24 + count * 8]


def parse_intvector(data: bytes, off: int) -> dict[str, int | list[int]]:
    bitwidth = u64(data, off)
    mask = u64(data, off + 8)
    total_bits = u64(data, off + 16)
    word_count = u64(data, off + 24)
    data_off = off + 0x20
    words = [u64(data, data_off + i * 8) for i in range(word_count)]
    return {
        "bitwidth": bitwidth,
        "mask": mask,
        "total_bits": total_bits,
        "word_count": word_count,
        "data_off": data_off,
        "words": words,
    }


def intvector_get(iv: dict[str, int | list[int]], idx: int) -> int:
    bw = int(iv["bitwidth"])
    mask = int(iv["mask"])
    words = iv["words"]
    assert isinstance(words, list)
    if bw == 0:
        return 0
    bitpos = idx * bw
    wi = bitpos >> 6
    bo = bitpos & 63
    if wi >= len(words):
        return 0
    v = (words[wi] >> bo) & ((1 << min(bw, 64)) - 1)
    if bo + bw > 64 and wi + 1 < len(words):
        v |= words[wi + 1] << (64 - bo)
    return v & mask


def fun_00661462(iv: dict[str, int | list[int]], blob1: bytes, node_id: int, rank_index: int) -> tuple[int, int, int]:
    low = blob1[node_id]
    upper_src = intvector_get(iv, rank_index)
    return (upper_src << 8) | low, upper_src, low


def main() -> None:
    data = TRIE_PATH.read_bytes()
    lines: list[str] = []
    lines.append(f"TRIE_PATH={TRIE_PATH}")
    lines.append("Probe target: real FUN_00661462 outputs using parsed intvector sections")
    lines.append("")

    for level, cfg in LEVELS.items():
        blob1 = data[cfg["blob1"] : cfg["blob1"] + 128]
        iv = parse_intvector(data, cfg["intv"])
        lines.append(f"LEVEL {level}")
        lines.append(
            f"  intvector: bitwidth={iv['bitwidth']} mask=0x{int(iv['mask']):X} total_bits={iv['total_bits']} word_count={iv['word_count']} data_off=0x{int(iv['data_off']):X}"
        )
        lines.append("  sample upper entries: " + ", ".join(f"{i}:{intvector_get(iv, i)}" for i in range(12)))
        for term_name in ("bv1_ds", "bv2_ds", "ds3", "ds4"):
            bv = BitVector(get_ds_bits(data, cfg[term_name]), shift=64)
            rows = []
            for node_id in ROOT_IDS if level == 0 else range(1, 9):
                t = bv.bit(node_id)
                rank = bv.rank1_exclusive(node_id)
                key, upper, low = fun_00661462(iv, blob1, node_id, rank)
                rows.append(
                    f"id={node_id} term={t} rank={rank} upper={upper} low=0x{low:02X}({cp1251_char(low)}) key=0x{key:X}"
                )
            lines.append(f"  terminal={term_name}")
            lines.extend("    " + r for r in rows)
        lines.append("")

    lines.append("Immediate reading:")
    lines.append("- if `upper` is non-zero for real terminal nodes, the recursive handoff key is widened even though the low byte still looks cp1251-like")
    lines.append("- this script narrows the next task to choosing the correct terminal bitvector family and then using these real `key=0x...` values in recursive probes")

    report = "\n".join(lines) + "\n"
    OUT_PATH.write_text(report, encoding="utf-8")
    print(report)
    print(f"written to {OUT_PATH}")


if __name__ == "__main__":
    main()
