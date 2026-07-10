
#!/usr/bin/env python3
"""Probe multilevel trie sections with codepage_id=0 assumptions.

This focuses on two fresh hypotheses from 2026-04-08:
1. active bitvectors inside each level start after a 64-bit zero pad;
2. `blob1` is direct cp1251-like label bytes, but low node ids may need a tiny bias.
"""

from __future__ import annotations

import struct
from pathlib import Path
from collections import Counter

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_multilevel_id0_probe_2026-04-08.txt")

LEVELS = {
    0: {"bv1_ds": 0x1FEB8, "bv2_ds": 0x47788, "ds3": 0x6D9A8, "ds4": 0x93BC8, "blob1": 0xFDB98, "blob1_len": 999288, "blob2": 0x1F1B18, "blob2_len": 0},
    1: {"bv1_ds": 0x1F3118, "bv2_ds": 0x1F4C60, "ds3": 0x1F6668, "ds4": 0x1F8070, "blob1": 0x1FA530, "blob1_len": 42248, "blob2": 0x204A40, "blob2_len": 0},
    2: {"bv1_ds": 0x204CE8, "bv2_ds": 0x205048, "ds3": 0x205370, "ds4": 0x205698, "blob1": 0x205A20, "blob1_len": 4752, "blob2": 0x206CB8, "blob2_len": 3208},
}


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def cp1251_char(b: int) -> str:
    try:
        return bytes([b]).decode("cp1251")
    except UnicodeDecodeError:
        return "�"


def is_wordlike(s: str) -> bool:
    return len(s) >= 3 and all(("а" <= ch <= "я") or ch in "ё-" for ch in s)


class BitVector:
    def __init__(self, buf: bytes, bit_shift: int = 0):
        self.buf = buf
        self.bit_shift = bit_shift
        self.total_bits = len(buf) * 8 - bit_shift
        self.total_pop = sum(struct.unpack_from("<Q", buf, i * 8)[0].bit_count() for i in range(len(buf) // 8))

    def bit(self, pos: int) -> int:
        pos += self.bit_shift
        if pos < 0 or pos >= len(self.buf) * 8:
            return 0
        w = struct.unpack_from("<Q", self.buf, (pos >> 6) * 8)[0]
        return (w >> (pos & 63)) & 1

    def rank1(self, pos: int) -> int:
        return sum(self.bit(i) for i in range(pos))

    def select0(self, n: int) -> int:
        seen = 0
        for pos in range(self.total_bits):
            if self.bit(pos) == 0:
                if seen == n:
                    return pos
                seen += 1
        return -1

    def children_pure_louds(self, node_id: int) -> list[int]:
        sep = self.select0(node_id)
        if sep < 0:
            return []
        start = 0 if node_id == 0 else self.select0(node_id - 1) + 1
        degree = sep - start
        if degree <= 0:
            return []
        first_child = 1 + self.rank1(start)
        return list(range(first_child, first_child + degree))


def get_ds_bits(data: bytes, ds_off: int) -> bytes:
    count = u64(data, ds_off + 8)
    return data[ds_off + 24 : ds_off + 24 + count * 8]


def label_at(blob1: bytes, node_id: int, bias: int) -> str | None:
    idx = node_id + bias
    if idx < 0 or idx >= len(blob1):
        return None
    ch = cp1251_char(blob1[idx]).lower()
    if ord(ch) < 0x20:
        return None
    return ch


def suffix_at(blob2: bytes, node_id: int) -> str:
    if not blob2 or node_id < 0 or node_id >= len(blob2):
        return ""
    end = blob2.find(b"\x00", node_id)
    if end < 0:
        end = len(blob2)
    return blob2[node_id:end].decode("cp1251", errors="ignore").lower()


def sample_dfs(bv: BitVector, blob1: bytes, blob2: bytes, bias: int, max_visits: int = 50000) -> tuple[int, list[str], list[str]]:
    words: list[str] = []
    raw: list[str] = []
    stack = [(0, "", 0)]
    visits = 0
    seen = set()
    while stack and visits < max_visits and len(raw) < 200:
        node_id, prefix, depth = stack.pop()
        visits += 1
        if depth > 8:
            continue
        key = (node_id, depth)
        if key in seen:
            continue
        seen.add(key)
        cur = prefix
        if node_id > 0:
            ch = label_at(blob1, node_id, bias)
            if not ch:
                continue
            cur += ch
        children = bv.children_pure_louds(node_id)
        if not children:
            cand = (cur + suffix_at(blob2, node_id)).strip()
            if cand:
                raw.append(cand)
                if is_wordlike(cand):
                    words.append(cand)
        else:
            for child in reversed(children[:32]):
                if child > node_id:
                    stack.append((child, cur, depth + 1))
    return visits, raw, words


def probe_level(data: bytes, level_id: int, cfg: dict[str, int]) -> list[str]:
    out: list[str] = []
    blob1 = data[cfg["blob1"] : cfg["blob1"] + cfg["blob1_len"]]
    blob2 = data[cfg["blob2"] : cfg["blob2"] + cfg["blob2_len"]]
    out.append(f"LEVEL {level_id}: blob1_len={len(blob1)} blob2_len={len(blob2)}")
    out.append(f"  blob1_head32={''.join(cp1251_char(b) if b >= 0x20 else '.' for b in blob1[:32])}")
    for shift in (0, 64):
        out.append(f"  -- shift={shift} --")
        bvs = {name: BitVector(get_ds_bits(data, cfg[name]), bit_shift=shift) for name in ("bv1_ds", "bv2_ds", "ds3", "ds4")}
        for name, bv in bvs.items():
            root = bv.children_pure_louds(0)
            preview_lines = []
            for bias in range(0, 6):
                labels = [label_at(blob1, nid, bias) or "?" for nid in root[:8]]
                preview_lines.append(f"bias{bias}=" + ",".join(labels))
            out.append(f"    {name}: root={root[:12]} count={len(root)} {' | '.join(preview_lines)}")
        for name in ("bv1_ds", "bv2_ds"):
            bv = bvs[name]
            root = bv.children_pure_louds(0)
            if not root:
                continue
            for bias in range(0, 6):
                visits, raw, words = sample_dfs(bv, blob1, blob2, bias)
                prefixes = Counter(w[:3] for w in words if len(w) >= 3).most_common(8)
                sample = words[:12] if words else raw[:12]
                out.append(
                    f"      dfs {name} bias={bias}: visits={visits} total={len(raw)} wordlike={len(words)} sample={sample} prefixes={prefixes}"
                )
    out.append("")
    return out


def main() -> None:
    data = TRIE_PATH.read_bytes()
    lines = [f"TRIE_PATH={TRIE_PATH}", "Using codepage_id=0 / direct cp1251 decoding", ""]
    for level_id, cfg in LEVELS.items():
        lines.extend(probe_level(data, level_id, cfg))
    report = "\n".join(lines) + "\n"
    OUT_PATH.write_text(report, encoding="utf-8")
    print(report)
    print(f"written to {OUT_PATH}")


if __name__ == "__main__":
    main()
