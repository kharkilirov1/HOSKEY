#!/usr/bin/env python3
from __future__ import annotations
import struct
from pathlib import Path
from collections import deque, defaultdict

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_prefix_ambiguity_probe_2026-04-09.txt")
LEVEL0 = {'has_child':0x8,'louds':0x278D8,'terminal':0x753C8,'labels':0xFDB98}
MAX_LABELS = 250000
MAX_DEPTH = 4
MAX_BRANCH_KIDS = 64


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from('<Q', data, off)[0]


class QWords:
    def __init__(self, data: bytes, off: int):
        self.count = u64(data, off + 8)
        self.ptr = off + 0x10
        self.buf = data[self.ptr:self.ptr + self.count * 8]

    def qword(self, idx: int) -> int:
        return struct.unpack_from('<Q', self.buf, idx * 8)[0] if 0 <= idx < self.count else 0

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
        return '?'
    ch = bytes([buf[idx]]).decode('cp1251', errors='replace')
    return ch if ord(ch) >= 0x20 else '.'


def is_cyr(ch: str) -> bool:
    return ('а' <= ch <= 'я') or ch == 'ё'


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


def main() -> None:
    data = TRIE_PATH.read_bytes()
    has_child = QWords(data, LEVEL0['has_child'])
    louds = QWords(data, LEVEL0['louds'])
    term = QWords(data, LEVEL0['terminal'])
    labels = data[LEVEL0['labels']:LEVEL0['labels'] + MAX_LABELS]

    by_prefix = defaultdict(list)
    q = deque((cp1251_char(labels, nid), nid, 1) for nid in children(has_child, louds, 0) if is_cyr(cp1251_char(labels, nid)))
    seen = set()
    while q:
        pref, nid, depth = q.popleft()
        if (pref, nid, depth) in seen:
            continue
        seen.add((pref, nid, depth))
        by_prefix[pref].append((nid, term.bit(nid), has_child.bit(nid)))
        if depth < MAX_DEPTH:
            for k in children(has_child, louds, nid):
                ch = cp1251_char(labels, k)
                if is_cyr(ch):
                    q.append((pref + ch, k, depth + 1))

    ambiguous = []
    for pref, items in by_prefix.items():
        uniq = sorted(set(items))
        if len(uniq) > 1:
            term_mix = len({t for _, t, _ in uniq}) > 1
            ambiguous.append((len(pref), pref, term_mix, uniq))
    ambiguous.sort(key=lambda x: (x[0], not x[2], x[1]))

    lines = []
    lines.append(f'TRIE_PATH={TRIE_PATH}')
    lines.append('Prefix ambiguity probe inside corrected root-phase graph')
    lines.append('Goal: find same spelled prefixes that map to multiple node ids / terminal statuses, which likely mark the first real branch into terminal-derived handoff.')
    lines.append('')
    lines.append(f'Ambiguous prefixes: {len(ambiguous)}')
    lines.append('')
    lines.append('Shortest ambiguous prefixes:')
    for depth, pref, term_mix, uniq in ambiguous[:120]:
        details = ', '.join(f'node={nid}:term={t}:child={hc}' for nid, t, hc in uniq)
        lines.append(f'  len={depth} pref={pref:<8s} term_mix={term_mix} {details}')
    lines.append('')
    lines.append('Shortest prefixes with terminal/nonterminal split:')
    shown = 0
    for depth, pref, term_mix, uniq in ambiguous:
        if term_mix:
            details = ', '.join(f'node={nid}:term={t}:child={hc}' for nid, t, hc in uniq)
            lines.append(f'  len={depth} pref={pref:<8s} {details}')
            shown += 1
            if shown >= 80:
                break

    report = '\n'.join(lines) + '\n'
    OUT_PATH.write_text(report, encoding='utf-8')
    print(report)
    print(f'written to {OUT_PATH}')


if __name__ == '__main__':
    main()
