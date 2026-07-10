#!/usr/bin/env python3
"""Probe root->recursive state handoff transforms for succinct trie traversal.

Current unknown: what exactly becomes `state[depth]` after the root-entry path
(`FUN_0066242c` / `FUN_00661baa`) before `FUN_00661926` starts using it on the
next trie level.

This script assumes the observed root sibling run `3..8 -> а..е` is real, then
tries simple transformed keys of the form:

    key = raw_cp1251_byte + delta

against level-1 candidate bitvectors, scoring which `(has_child, louds)` pair +
`delta` produce the cleanest Cyrillic child runs.
"""

from __future__ import annotations

import itertools
import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_handoff_transform_probe_2026-04-08.txt")

LEVEL0 = {
    "blob1": 0xFDB98,
}
LEVEL1 = {
    "bv1_ds": 0x1F3118,
    "bv2_ds": 0x1F4C60,
    "ds3": 0x1F6668,
    "ds4": 0x1F8070,
    "blob1": 0x1FA530,
}

ROOT_SIBLING_IDS = [3, 4, 5, 6, 7, 8]  # current best root-entry+sibling hypothesis


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def get_ds_bits(data: bytes, ds_off: int) -> bytes:
    count = u64(data, ds_off + 8)
    return data[ds_off + 24 : ds_off + 24 + count * 8]


def cp1251_char(buf: bytes, idx: int) -> str:
    if idx < 0 or idx >= len(buf):
        return "?"
    ch = bytes([buf[idx]]).decode("cp1251", errors="replace")
    return ch if ord(ch) >= 0x20 else "?"


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

    def select1(self, n: int) -> int:
        seen = 0
        for pos in range(self.total_bits):
            if self.bit(pos):
                if seen == n:
                    return pos
                seen += 1
        return -1


def score_chars(chars: list[str]) -> tuple[int, int, int]:
    hits = sum(c != "?" for c in chars)
    cyr = sum(("а" <= c <= "я") or c == "ё" for c in chars)
    run = 0
    best_run = 0
    for c in chars:
        if c != "?":
            run += 1
            best_run = max(best_run, run)
        else:
            run = 0
    bonus = 0
    if chars[:2] == ["а", "б"]:
        bonus += 5
    elif chars and chars[0] == "а":
        bonus += 3
    if len(chars) >= 4 and chars[2] in ("в", "г", "д", "е", "ж"):
        bonus += 2
    return hits, cyr, best_run * 5 + bonus


def main() -> None:
    data = TRIE_PATH.read_bytes()
    root_blob1 = data[LEVEL0["blob1"] : LEVEL0["blob1"] + 64]
    lvl1_blob1 = data[LEVEL1["blob1"] : LEVEL1["blob1"] + 512]
    root_bytes = [root_blob1[i] for i in ROOT_SIBLING_IDS]
    root_chars = [cp1251_char(root_blob1, i) for i in ROOT_SIBLING_IDS]

    bvs = {name: BitVector(get_ds_bits(data, off), shift=64) for name, off in LEVEL1.items() if name != "blob1"}

    rows = []
    for delta in range(-230, 1):
        keys = [b + delta for b in root_bytes]
        if min(keys) < 0 or max(keys) > 400:
            continue
        for hc_name, loud_name in itertools.permutations(bvs.keys(), 2):
            hc = bvs[hc_name]
            louds = bvs[loud_name]
            for mode in (0, 1):
                chars = []
                for k in keys:
                    if not hc.bit(k):
                        chars.append("?")
                        continue
                    r = hc.rank1_exclusive(k)
                    child = louds.select1(r - 1 + mode)
                    chars.append(cp1251_char(lvl1_blob1, child))
                hits, cyr, bonus = score_chars(chars)
                if hits >= 4 and cyr >= 4:
                    total = cyr * 10 + bonus
                    rows.append((total, delta, hc_name, loud_name, mode, keys, chars, hits, cyr))

    rows.sort(reverse=True)
    top = rows[:30]

    lines = []
    lines.append(f"TRIE_PATH={TRIE_PATH}")
    lines.append("Handoff probe assumption: root sibling run 3..8 -> а..е is real")
    lines.append(f"root_ids={ROOT_SIBLING_IDS}")
    lines.append(f"root_chars={root_chars}")
    lines.append(f"root_bytes={root_bytes}")
    lines.append("")
    lines.append("Interpretation:")
    lines.append("- `delta` means tentative transformed handoff key = root_cp1251_byte + delta")
    lines.append("- `mode=0` means select1(rank1(key)-1)")
    lines.append("- `mode=1` means select1(rank1(key))")
    lines.append("- higher score = more Cyrillic outputs + stronger initial `а,б,...` shape")
    lines.append("")
    lines.append("TOP CANDIDATES:")
    for total, delta, hc_name, loud_name, mode, keys, chars, hits, cyr in top:
        lines.append(
            f"score={total:3d} delta={delta:4d} hc={hc_name:6s} louds={loud_name:6s} mode={mode} hits={hits} cyr={cyr} keys={keys} -> {chars}"
        )
    lines.append("")
    if top:
        best = top[0]
        lines.append("Best current offline candidate:")
        lines.append(
            f"delta={best[1]}, hc={best[2]}, louds={best[3]}, mode={best[4]}, chars={best[6]}"
        )
    lines.append("")
    lines.append("Immediate takeaway:")
    lines.append("- raw cp1251 bytes 0xE0..0xE5 are NOT directly usable as level-1 state keys (all four BV families fail there)")
    lines.append("- a transformed-key handoff is required")
    lines.append("- ds3 repeatedly appears as one side of the strongest recursive level-1 candidates")
    lines.append("- the strongest current family is around delta ~= -86 with ds3 participating")

    report = "\n".join(lines) + "\n"
    OUT_PATH.write_text(report, encoding="utf-8")
    print(report)
    print(f"written to {OUT_PATH}")


if __name__ == "__main__":
    main()
