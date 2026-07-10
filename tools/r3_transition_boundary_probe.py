#!/usr/bin/env python3
from __future__ import annotations
import struct
from pathlib import Path
from collections import deque

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_transition_boundary_probe_2026-04-09.txt")

LEVEL0 = {
    "has_child": 0x8,
    "louds": 0x278D8,
    "terminal": 0x753C8,
    "labels": 0xFDB98,
}
MAX_LABELS = 250000
MAX_DEPTH = 4
MAX_BRANCH_KIDS = 64


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from('<Q', data, off)[0]


class QWords:
    def __init__(self, data: bytes, off: int):
        self.off = off
        self.count = u64(data, off + 8)
        self.ptr = off + 0x10
        self.buf = data[self.ptr:self.ptr + self.count * 8]

    def qword(self, idx: int) -> int:
        if idx < 0 or idx >= self.count:
            return 0
        return struct.unpack_from('<Q', self.buf, idx * 8)[0]

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
            c = word.bit_count()
            if seen + c <= n:
                seen += c
                continue
            for bi in range(64):
                if (word >> bi) & 1:
                    if seen == n:
                        return wi * 64 + bi
                    seen += 1
        return -1


def cp1251_char(buf: bytes, idx: int) -> str:
    if idx < 0 or idx >= len(buf):
        return '?'
    ch = bytes([buf[idx]]).decode('cp1251', errors='replace')
    return ch if ord(ch) >= 0x20 else '.'


def is_cyr(ch: str) -> bool:
    return ('а' <= ch <= 'я') or ch == 'ё'


def first_child(has_child: QWords, louds: QWords, node_id: int) -> int:
    if has_child.bit(node_id) == 0:
        return -1
    r = has_child.rank1(node_id)
    pos = louds.select1(r)
    return pos + 1 if pos >= 0 else -1


def siblings(louds: QWords, start_node: int):
    if start_node < 0:
        return
    node = start_node
    alive = 1
    while alive:
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


def main() -> None:
    data = TRIE_PATH.read_bytes()
    has_child = QWords(data, LEVEL0['has_child'])
    louds = QWords(data, LEVEL0['louds'])
    term = QWords(data, LEVEL0['terminal'])
    labels = data[LEVEL0['labels']:LEVEL0['labels'] + MAX_LABELS]

    root_ids = [nid for nid in children(has_child, louds, 0) if is_cyr(cp1251_char(labels, nid))]
    q = deque((cp1251_char(labels, nid), nid, 1) for nid in root_ids)
    seen = set()
    terminal_prefixes = []
    nonterminal_samples = []

    while q:
        pref, nid, depth = q.popleft()
        if (nid, depth) in seen:
            continue
        seen.add((nid, depth))

        t = term.bit(nid)
        hc = has_child.bit(nid)
        kids = [k for k in children(has_child, louds, nid) if is_cyr(cp1251_char(labels, k))]
        kid_chars = ''.join(cp1251_char(labels, k) for k in kids[:40])

        if t:
            terminal_prefixes.append((len(pref), pref, nid, hc, len(kids), kid_chars))
        elif depth <= 3 and len(nonterminal_samples) < 80:
            nonterminal_samples.append((pref, nid, hc, len(kids), kid_chars))

        if depth < MAX_DEPTH:
            for k in kids:
                q.append((pref + cp1251_char(labels, k), k, depth + 1))

    terminal_prefixes.sort(key=lambda x: (x[0], x[1]))

    lines = []
    lines.append(f'TRIE_PATH={TRIE_PATH}')
    lines.append('Transition-boundary probe for corrected root-phase traversal')
    lines.append('Goal: find the earliest real prefixes where FUN_0066242c would switch from non-terminal walk to terminal-derived FUN_00661baa state handoff.')
    lines.append('')
    lines.append(f'Visited states: {len(seen)}')
    lines.append(f'Terminal prefixes found: {len(terminal_prefixes)}')
    lines.append('')
    lines.append('Shortest terminal prefixes:')
    for depth, pref, nid, hc, kid_count, kid_chars in terminal_prefixes[:120]:
        lines.append(f'  len={depth} pref={pref:<8s} node={nid:<5d} has_child={hc} clean_kids={kid_count:<2d} kids={kid_chars}')
    lines.append('')
    lines.append('Sample non-terminal prefixes:')
    for pref, nid, hc, kid_count, kid_chars in nonterminal_samples:
        lines.append(f'  pref={pref:<8s} node={nid:<5d} has_child={hc} clean_kids={kid_count:<2d} kids={kid_chars}')
    lines.append('')
    if terminal_prefixes:
        min_len = terminal_prefixes[0][0]
        mins = [x for x in terminal_prefixes if x[0] == min_len][:60]
        lines.append(f'First terminal boundary appears at prefix length {min_len}')
        lines.append('Candidates at first boundary: ' + ', '.join(p for _, p, *_ in mins))
    else:
        lines.append('No terminal prefixes found within current depth budget.')

    report = '\n'.join(lines) + '\n'
    OUT_PATH.write_text(report, encoding='utf-8')
    print(report)
    print(f'written to {OUT_PATH}')


if __name__ == '__main__':
    main()
