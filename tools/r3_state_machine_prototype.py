#!/usr/bin/env python3
from __future__ import annotations

import struct
from collections import defaultdict, deque
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_state_machine_prototype_2026-04-09.txt")

OFF = {
    "has_child": 0x8,
    "louds": 0x278D8,
    "terminal": 0x753C8,
    "labels": 0xFDB98,
}

MAX_LABELS = 250000
MAX_DEPTH = 4
MAX_BRANCH_KIDS = 64
SAMPLE_PREFIXES = [
    "ла", "лаб", "алв", "бое", "ст", "пр", "не", "она", "что", "яу",
]


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


class QWords:
    def __init__(self, data: bytes, off: int):
        self.count = u64(data, off + 8)
        self.ptr = off + 0x10
        self.buf = data[self.ptr:self.ptr + self.count * 8]

    def qword(self, idx: int) -> int:
        return struct.unpack_from("<Q", self.buf, idx * 8)[0] if 0 <= idx < self.count else 0

    def bit(self, pos: int) -> int:
        return ((self.buf[pos >> 3] >> (pos & 7)) & 1) if 0 <= pos < self.count * 64 else 0

    def rank1(self, pos: int) -> int:
        if pos <= 0:
            return 0
        fw = min(pos >> 6, self.count)
        rem = pos & 63
        total = sum(self.qword(i).bit_count() for i in range(fw))
        if fw < self.count and rem:
            total += (self.qword(fw) & ((1 << rem) - 1)).bit_count()
        return total

    def select1(self, n: int) -> int:
        if n < 0:
            return -1
        seen = 0
        for wi in range(self.count):
            w = self.qword(wi)
            c = w.bit_count()
            if seen + c <= n:
                seen += c
                continue
            for bi in range(64):
                if (w >> bi) & 1:
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
    alive = 1
    while alive and node >= 0:
        yield node
        alive = 1 if louds.bit(node) == 0 else 0
        if alive:
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


def build_prefix_graph(has_child: QWords, louds: QWords, terminal: QWords, labels: bytes):
    by_prefix: dict[str, list[tuple[int, int, int]]] = defaultdict(list)
    root_nodes = [nid for nid in children(has_child, louds, 0) if is_cyr(cp1251_char(labels, nid))]
    q = deque((cp1251_char(labels, nid), nid, 1) for nid in root_nodes)
    seen = set()
    while q:
        pref, nid, depth = q.popleft()
        if (pref, nid, depth) in seen:
            continue
        seen.add((pref, nid, depth))
        by_prefix[pref].append((nid, terminal.bit(nid), has_child.bit(nid)))
        if depth < MAX_DEPTH:
            for kid in children(has_child, louds, nid):
                ch = cp1251_char(labels, kid)
                if is_cyr(ch):
                    q.append((pref + ch, kid, depth + 1))
    return by_prefix


def next_letters_for_nodes(nodes: list[int], has_child: QWords, louds: QWords, labels: bytes) -> tuple[str, str, str]:
    all_chars = set()
    nonterm_chars = set()
    term_chars = set()
    for nid, term_flag, _ in nodes:
        kids = children(has_child, louds, nid)
        chars = {cp1251_char(labels, k) for k in kids if is_cyr(cp1251_char(labels, k))}
        all_chars |= chars
        if term_flag:
            term_chars |= chars
        else:
            nonterm_chars |= chars
    return (
        "".join(sorted(all_chars)),
        "".join(sorted(nonterm_chars)),
        "".join(sorted(term_chars)),
    )


def main() -> None:
    data = TRIE_PATH.read_bytes()
    has_child = QWords(data, OFF["has_child"])
    louds = QWords(data, OFF["louds"])
    terminal = QWords(data, OFF["terminal"])
    labels = data[OFF["labels"]:OFF["labels"] + MAX_LABELS]

    by_prefix = build_prefix_graph(has_child, louds, terminal, labels)

    lines: list[str] = []
    lines.append(f"TRIE_PATH={TRIE_PATH}")
    lines.append("Prototype active-state model for the corrected root-phase graph")
    lines.append("Model idea: visible prefix maps to a SET of node states, not a single node_id.")
    lines.append("")

    ambiguous = [(p, v) for p, v in by_prefix.items() if len({tuple(x) for x in v}) > 1]
    ambiguous.sort(key=lambda x: (len(x[0]), x[0]))
    lines.append(f"Total prefixes in graph: {len(by_prefix)}")
    lines.append(f"Ambiguous visible prefixes: {len(ambiguous)}")
    lines.append("")
    lines.append("Shortest ambiguous prefixes:")
    for pref, vals in ambiguous[:40]:
        parts = ", ".join(f"node={nid}:term={t}:child={hc}" for nid, t, hc in sorted(set(vals)))
        lines.append(f"  {pref:<8s} -> {parts}")
    lines.append("")

    lines.append("Sample active-state views:")
    for pref in SAMPLE_PREFIXES:
        vals = sorted(set(by_prefix.get(pref, [])))
        if not vals:
            lines.append(f"  {pref:<8s} -> MISSING")
            continue
        all_next, nonterm_next, term_next = next_letters_for_nodes(vals, has_child, louds, labels)
        lines.append(f"  {pref:<8s} -> states={vals}")
        lines.append(f"             next(all)     = {all_next}")
        lines.append(f"             next(nonterm) = {nonterm_next}")
        lines.append(f"             next(term)    = {term_next}")
    lines.append("")

    lines.append("Engine implication:")
    lines.append("- traversal state should be modeled as `ActivePrefixState = set[node_state]`")
    lines.append("- each node_state must at minimum preserve `(node_id, terminal_here, has_child)` semantics")
    lines.append("- terminal nodes should be treated as epsilon-handoff candidates, not as ordinary consumed-character siblings")
    lines.append("- candidate generation must merge visible continuation from non-terminal states with terminal-handoff states for the same prefix spelling")

    report = "\n".join(lines) + "\n"
    OUT_PATH.write_text(report, encoding="utf-8")
    print(report)
    print(f"written to {OUT_PATH}")


if __name__ == "__main__":
    main()
