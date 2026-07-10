
#!/usr/bin/env python3
"""Probe level-2 trie traversal using the current R3 model.

Goals:
- stop using the obsolete CP866-only assumption;
- test level-2 `blob1` as direct codepage-0 / cp1251-like label bytes;
- brute-force small label index biases and LOUDS candidates;
- combine leaf paths with `blob2` continuation pool and see whether outputs become less garbage.
"""

from __future__ import annotations

import struct
from pathlib import Path
from collections import Counter

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_level2_probe_id0_2026-04-08.txt")

LEVEL2 = {
    "bv1_ds": 0x204CE8,
    "bv2_ds": 0x205048,
    "ds3": 0x205370,
    "ds4": 0x205698,
    "blob1": 0x205A20,
    "blob1_len": 4752,
    "blob2": 0x206CB8,
    "blob2_len": 3208,
}


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def cp1251_char(b: int) -> str:
    try:
        return bytes([b]).decode("cp1251")
    except UnicodeDecodeError:
        return "?"


def is_wordlike(s: str) -> bool:
    return len(s) >= 3 and all(("а" <= ch <= "я") or ch in "ё-" for ch in s)


class BitVector:
    def __init__(self, buf: bytes, bit_shift: int = 64):
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

    def rank0(self, pos: int) -> int:
        if pos <= 0:
            return 0
        return sum(1 for i in range(pos) if self.bit(i) == 0)

    def rank1(self, pos: int) -> int:
        if pos <= 0:
            return 0
        return sum(1 for i in range(pos) if self.bit(i) == 1)

    def select0(self, n: int) -> int:
        seen = 0
        for pos in range(self.total_bits):
            if self.bit(pos) == 0:
                if seen == n:
                    return pos
                seen += 1
        return -1


def get_ds_bits(data: bytes, ds_off: int) -> bytes:
    count = u64(data, ds_off + 8)
    return data[ds_off + 24 : ds_off + 24 + count * 8]


def label_at(blob1: bytes, node_id: int, bias: int) -> str | None:
    idx = node_id + bias
    if idx < 0 or idx >= len(blob1):
        return None
    ch = cp1251_char(blob1[idx])
    if ch == "?" or ord(ch) < 0x20:
        return None
    return ch.lower()


def suffix_at(blob2: bytes, node_id: int) -> str:
    if node_id < 0 or node_id >= len(blob2):
        return ""
    end = blob2.find(b"\x00", node_id)
    if end < 0:
        end = len(blob2)
    chunk = blob2[node_id:end]
    if not chunk:
        return ""
    try:
        return chunk.decode("cp1251", errors="ignore").lower()
    except Exception:
        return ""


def get_children_pure_louds(bv: BitVector, node_id: int) -> list[int]:
    sep = bv.select0(node_id)
    if sep < 0:
        return []
    if node_id == 0:
        start = 0
    else:
        prev_sep = bv.select0(node_id - 1)
        start = prev_sep + 1
    degree = sep - start
    if degree <= 0:
        return []
    first_child = 1 + bv.rank1(start)
    return list(range(first_child, first_child + degree))


def run_probe(data: bytes) -> str:
    blob1 = data[LEVEL2["blob1"] : LEVEL2["blob1"] + LEVEL2["blob1_len"]]
    blob2 = data[LEVEL2["blob2"] : LEVEL2["blob2"] + LEVEL2["blob2_len"]]

    bv_candidates = {
        name: BitVector(get_ds_bits(data, off), bit_shift=64)
        for name, off in (("bv1", LEVEL2["bv1_ds"]), ("bv2", LEVEL2["bv2_ds"]), ("ds3", LEVEL2["ds3"]), ("ds4", LEVEL2["ds4"]))
    }

    out: list[str] = []
    out.append(f"TRIE_PATH={TRIE_PATH}")
    out.append("Using codepage_id=0 hypothesis -> direct cp1251 decode")
    out.append(f"level2 blob1 len={len(blob1)} blob2 len={len(blob2)}")
    out.append("")

    for name, bv in bv_candidates.items():
        root_children = get_children_pure_louds(bv, 0)
        out.append(f"=== louds_candidate={name} shift={bv.bit_shift} pop={bv.total_pop} root_children={root_children[:16]} count={len(root_children)} ===")
        for bias in range(0, 6):
            labels = [label_at(blob1, nid, bias) or "?" for nid in root_children[:12]]
            joined = " ".join(f"{nid}:{lab}" for nid, lab in zip(root_children[:12], labels))
            out.append(f"  bias={bias}: {joined}")
        out.append("")

    # Heuristic DFS only on the most plausible LOUDS candidates.
    # We keep the search small and compare biases objectively.
    for name in ("bv1", "bv2"):
        bv = bv_candidates[name]
        root_children = get_children_pure_louds(bv, 0)
        if not root_children:
            continue
        out.append(f"=== DFS probe louds={name} ===")
        for bias in range(0, 6):
            words: list[str] = []
            seen = set()
            stack = [(0, "", 0)]
            visits = 0
            while stack and visits < 50000 and len(words) < 200:
                node_id, prefix, depth = stack.pop()
                visits += 1
                if depth > 8:
                    continue
                if node_id in seen and depth > 0:
                    continue
                if depth > 0:
                    seen.add(node_id)
                children = get_children_pure_louds(bv, node_id)
                cur = prefix
                if node_id > 0:
                    ch = label_at(blob1, node_id, bias)
                    if not ch:
                        continue
                    cur += ch
                if not children:
                    tail = suffix_at(blob2, node_id)
                    cand = (cur + tail).strip()
                    if cand:
                        words.append(cand)
                else:
                    for child in reversed(children[:32]):
                        if child > node_id:
                            stack.append((child, cur, depth + 1))
            wordlike = [w for w in words if is_wordlike(w)]
            sample = wordlike[:25] if wordlike else words[:25]
            common_prefixes = Counter(w[:3] for w in wordlike if len(w) >= 3).most_common(10)
            out.append(
                f"  bias={bias}: visits={visits} total={len(words)} wordlike={len(wordlike)} sample={sample} prefixes={common_prefixes}"
            )
        out.append("")

    return "\n".join(out) + "\n"


def main() -> None:
    data = TRIE_PATH.read_bytes()
    report = run_probe(data)
    OUT_PATH.write_text(report, encoding="utf-8")
    print(report)
    print(f"written to {OUT_PATH}")


if __name__ == "__main__":
    main()
