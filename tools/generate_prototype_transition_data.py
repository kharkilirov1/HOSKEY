#!/usr/bin/env python3
from __future__ import annotations

import re
import struct
from collections import defaultdict, deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
AMBIG_PATH = ROOT / "reverse" / "R3_prefix_ambiguity_probe_2026-04-09.txt"
STATE_MACHINE_PATH = ROOT / "reverse" / "R3_state_machine_prototype_2026-04-09.txt"
TARGET = (
    ROOT
    / "entry"
    / "src"
    / "main"
    / "cpp"
    / "hosdict_engine"
    / "prototype_transition_data.h"
)

OFF = {
    "has_child": 0x8,
    "louds": 0x278D8,
    "terminal": 0x753C8,
    "labels": 0xFDB98,
}

MAX_LABELS = 250000
MAX_BRANCH_KIDS = 64
MAX_PREFIX_RECORDS = 96

AMBIG_RE = re.compile(r"^\s+len=\d+\s+pref=(\S+)\s+term_mix=True\b")
SAMPLE_RE = re.compile(r"^\s{2}(\S+)\s+-> states=")


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
        if depth < 4:
            for kid in children(has_child, louds, nid):
                ch = cp1251_char(labels, kid)
                if is_cyr(ch):
                    q.append((pref + ch, kid, depth + 1))
    return by_prefix


def next_letters_for_nodes(nodes: list[tuple[int, int, int]], has_child: QWords, louds: QWords, labels: bytes):
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


def escape_cpp(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def parse_ambiguous_prefixes() -> list[str]:
    prefixes: list[str] = []
    for line in AMBIG_PATH.read_text(encoding="utf-8").splitlines():
        m = AMBIG_RE.match(line)
        if m:
            prefixes.append(m.group(1))
    return prefixes


def parse_sample_prefixes() -> list[str]:
    prefixes: list[str] = []
    for line in STATE_MACHINE_PATH.read_text(encoding="utf-8").splitlines():
        m = SAMPLE_RE.match(line)
        if m:
            prefixes.append(m.group(1))
    return prefixes


def collect_prefixes(by_prefix: dict[str, list[tuple[int, int, int]]]) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()

    def add(pref: str) -> None:
        if pref in seen or pref not in by_prefix:
            return
        seen.add(pref)
        out.append(pref)

    for pref in parse_sample_prefixes():
        add(pref)

    for pref in parse_ambiguous_prefixes():
        add(pref)
        if len(out) >= MAX_PREFIX_RECORDS:
            break

    return out


def render_header(records: list[tuple[str, str, str]]) -> str:
    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "",
        "namespace hoskey::engine {",
        "",
        "struct PrototypeTransitionRecord {",
        "  const char* prefix_utf8;",
        "  bool filter_direct;",
        "  bool filter_handoff;",
        "  const char* direct_utf8;",
        "  const char* handoff_utf8;",
        "};",
        "",
        "inline constexpr PrototypeTransitionRecord kPrototypeTransitionRecords[] = {",
    ]
    for prefix, direct, handoff in records:
        filter_direct = "true"
        filter_handoff = "true" if handoff else "false"
        lines.append(
            f'    {{"{escape_cpp(prefix)}", {filter_direct}, {filter_handoff}, "{escape_cpp(direct)}", "{escape_cpp(handoff)}"}},'
        )
    lines += [
        "};",
        "",
        "inline constexpr std::size_t kPrototypeTransitionRecordCount =",
        "    sizeof(kPrototypeTransitionRecords) / sizeof(kPrototypeTransitionRecords[0]);",
        "",
        "}  // namespace hoskey::engine",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    data = TRIE_PATH.read_bytes()
    has_child = QWords(data, OFF["has_child"])
    louds = QWords(data, OFF["louds"])
    terminal = QWords(data, OFF["terminal"])
    labels = data[OFF["labels"]:OFF["labels"] + MAX_LABELS]

    by_prefix = build_prefix_graph(has_child, louds, terminal, labels)
    prefixes = collect_prefixes(by_prefix)
    records: list[tuple[str, str, str]] = []
    for pref in prefixes:
        vals = sorted(set(by_prefix[pref]))
        _, nonterm_next, term_next = next_letters_for_nodes(vals, has_child, louds, labels)
        records.append((pref, nonterm_next, term_next))

    TARGET.write_text(render_header(records), encoding="utf-8", newline="\n")
    print(f"wrote {len(records)} records -> {TARGET}")


if __name__ == "__main__":
    main()
