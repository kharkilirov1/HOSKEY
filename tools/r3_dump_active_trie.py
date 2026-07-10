#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import struct
from collections import defaultdict, deque
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
DEFAULT_OUT_PATH = PROJECT_ROOT / "reverse" / "R3_active_trie_oracle_2026-04-25.json"

OFFSETS = {
    "has_child": 0x000008,
    "louds": 0x0278D8,
    "terminal": 0x0753C8,
    "labels": 0x0FDB98,
}

SAMPLE_PREFIXES = [
    "ла", "лаб", "алв", "бое", "ст", "пр", "не", "она", "что", "яу",
    "шк", "юх", "ама", "анч", "аня", "ася", "бои", "буг", "вак",
    "ниж", "орл", "тюл", "уня", "фог", "цык", "чия", "шты", "эрк",
]

MAX_DEPTH = 4
MAX_BRANCH_KIDS = 64


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


class QWords:
    def __init__(self, data: bytes, off: int):
        self.off = off
        self.field0 = u64(data, off)
        self.count = u64(data, off + 8)
        self.ptr = off + 0x10
        self.buf = data[self.ptr:self.ptr + self.count * 8]

    def qword(self, idx: int) -> int:
        if idx < 0 or idx >= self.count:
            return 0
        return struct.unpack_from("<Q", self.buf, idx * 8)[0]

    def bit(self, pos: int) -> int:
        if pos < 0 or pos >= self.count * 64:
            return 0
        return (self.buf[pos >> 3] >> (pos & 7)) & 1

    def rank1(self, pos: int) -> int:
        if pos <= 0:
            return 0
        full_words = min(pos >> 6, self.count)
        rem = pos & 63
        total = 0
        for i in range(full_words):
            total += self.qword(i).bit_count()
        if full_words < self.count and rem:
            total += (self.qword(full_words) & ((1 << rem) - 1)).bit_count()
        return total

    def select1(self, n: int) -> int:
        if n < 0:
            return -1
        seen = 0
        for wi in range(self.count):
            word = self.qword(wi)
            cnt = word.bit_count()
            if seen + cnt <= n:
                seen += cnt
                continue
            for bi in range(64):
                if (word >> bi) & 1:
                    if seen == n:
                        return wi * 64 + bi
                    seen += 1
        return -1


def cp1251_char(buf: bytes, idx: int) -> str:
    if idx < 0 or idx >= len(buf):
        return "?"
    ch = bytes([buf[idx]]).decode("cp1251", errors="replace")
    return ch if ord(ch) >= 0x20 else "."


def is_cyr(ch: str) -> bool:
    return ("а" <= ch <= "я") or ch == "ё"


def first_child(has_child: QWords, louds: QWords, node_id: int) -> int:
    if has_child.bit(node_id) == 0:
        return -1
    pos = louds.select1(has_child.rank1(node_id))
    return pos + 1 if pos >= 0 else -1


def siblings(louds: QWords, start: int):
    node = start
    while node >= 0:
        yield node
        if louds.bit(node) != 0:
            break
        node += 1


def children(has_child: QWords, louds: QWords, node_id: int):
    fc = first_child(has_child, louds, node_id)
    if fc < 0:
        return []
    out = []
    for i, nid in enumerate(siblings(louds, fc)):
        out.append(nid)
        if i + 1 >= MAX_BRANCH_KIDS:
            break
    return out


def node_state(nid: int, terminal: QWords, has_child: QWords, labels: bytes) -> dict:
    return {
        "node": nid,
        "label": cp1251_char(labels, nid),
        "terminal": bool(terminal.bit(nid)),
        "has_child": bool(has_child.bit(nid)),
    }


def next_letters_for_states(states: list[dict], has_child: QWords, louds: QWords, labels: bytes) -> dict:
    all_chars: set[str] = set()
    nonterminal_chars: set[str] = set()
    terminal_chars: set[str] = set()
    for st in states:
        chars = {
            cp1251_char(labels, kid)
            for kid in children(has_child, louds, st["node"])
            if is_cyr(cp1251_char(labels, kid))
        }
        all_chars |= chars
        if st["terminal"]:
            terminal_chars |= chars
        else:
            nonterminal_chars |= chars
    return {
        "all": "".join(sorted(all_chars)),
        "nonterminal": "".join(sorted(nonterminal_chars)),
        "terminal": "".join(sorted(terminal_chars)),
    }


def build_prefix_graph(has_child: QWords, louds: QWords, terminal: QWords, labels: bytes):
    by_prefix: dict[str, list[dict]] = defaultdict(list)
    root_nodes = [nid for nid in children(has_child, louds, 0) if is_cyr(cp1251_char(labels, nid))]
    q = deque((cp1251_char(labels, nid), nid, 1) for nid in root_nodes)
    seen = set()
    while q:
        pref, nid, depth = q.popleft()
        key = (pref, nid, depth)
        if key in seen:
            continue
        seen.add(key)
        by_prefix[pref].append(node_state(nid, terminal, has_child, labels))
        if depth < MAX_DEPTH:
            for kid in children(has_child, louds, nid):
                ch = cp1251_char(labels, kid)
                if is_cyr(ch):
                    q.append((pref + ch, kid, depth + 1))
    return by_prefix


def main() -> int:
    parser = argparse.ArgumentParser(description="Dump R3 active-state trie oracle as JSON")
    parser.add_argument("--trie", type=Path, default=DEFAULT_TRIE_PATH)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT_PATH)
    args = parser.parse_args()

    data = args.trie.read_bytes()
    has_child = QWords(data, OFFSETS["has_child"])
    louds = QWords(data, OFFSETS["louds"])
    terminal = QWords(data, OFFSETS["terminal"])
    labels = data[OFFSETS["labels"]:]

    by_prefix = build_prefix_graph(has_child, louds, terminal, labels)
    root_states = [
        node_state(nid, terminal, has_child, labels)
        for nid in children(has_child, louds, 0)
        if is_cyr(cp1251_char(labels, nid))
    ]
    root_letters = "".join(st["label"] for st in root_states)
    ambiguous = {
        pref: sorted(states, key=lambda s: s["node"])
        for pref, states in by_prefix.items()
        if len({s["node"] for s in states}) > 1
    }
    samples = []
    for pref in SAMPLE_PREFIXES:
        states = sorted(by_prefix.get(pref, []), key=lambda s: s["node"])
        samples.append({
            "prefix": pref,
            "states": states,
            "next": next_letters_for_states(states, has_child, louds, labels),
        })

    payload = {
        "source": str(args.trie),
        "format": "r3_active_trie_oracle_v1",
        "offsets": OFFSETS,
        "max_depth": MAX_DEPTH,
        "root_letters": root_letters,
        "total_prefixes": len(by_prefix),
        "ambiguous_prefixes": len(ambiguous),
        "shortest_ambiguous": [
            {
                "prefix": pref,
                "states": ambiguous[pref],
                "next": next_letters_for_states(ambiguous[pref], has_child, louds, labels),
            }
            for pref in sorted(ambiguous, key=lambda p: (len(p), p))[:80]
        ],
        "samples": samples,
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.out}")
    print(f"root_letters={root_letters}")
    print(f"total_prefixes={len(by_prefix)} ambiguous_prefixes={len(ambiguous)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
