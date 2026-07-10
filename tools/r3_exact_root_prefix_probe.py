#!/usr/bin/env python3
from __future__ import annotations
import struct
from pathlib import Path
from collections import deque, defaultdict

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_exact_root_prefix_probe_2026-04-09.txt")

LEVEL0 = {
    "raw0": 0x8,
    "raw1": 0x278D8,
    "term": 0x753C8,
    "labels": 0xFDB98,
}
MAX_LABELS = 200000


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from('<Q', data, off)[0]


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
    return list(siblings(louds, fc))


def main() -> None:
    data = TRIE_PATH.read_bytes()
    raw0 = QWords(data, LEVEL0['raw0'])
    raw1 = QWords(data, LEVEL0['raw1'])
    term = QWords(data, LEVEL0['term'])
    labels = data[LEVEL0['labels']:LEVEL0['labels'] + MAX_LABELS]

    root_ids = children(raw0, raw1, 0)
    clean_roots = [nid for nid in root_ids if is_cyr(cp1251_char(labels, nid))]

    lines: list[str] = []
    lines.append(f'TRIE_PATH={TRIE_PATH}')
    lines.append('Exact root-phase prefix probe using char-filtered traversal only')
    lines.append('Semantics: first_child = select1(rank1(has_child,node))+1 ; sibling stepping via raw1 bit')
    lines.append('')
    lines.append(f'Clean root letters ({len(clean_roots)}): ' + ''.join(cp1251_char(labels, n) for n in clean_roots[:64]))
    lines.append('')

    bigrams = []
    trigrams = []
    frontier = []
    for nid in clean_roots:
        ch = cp1251_char(labels, nid)
        kids = children(raw0, raw1, nid)
        clean_kids = [k for k in kids if is_cyr(cp1251_char(labels, k))]
        bigrams.extend(ch + cp1251_char(labels, k) for k in clean_kids)
        frontier.extend((ch + cp1251_char(labels, k), k) for k in clean_kids)
    lines.append(f'Clean bigrams count={len(bigrams)}')
    lines.append('Sample bigrams: ' + ', '.join(sorted(bigrams)[:120]))
    lines.append('')

    by_root = defaultdict(list)
    for pref, nid in frontier:
        if len(pref) != 2:
            continue
        by_root[pref[0]].append(pref[1])
        kids = children(raw0, raw1, nid)
        clean_kids = [k for k in kids if is_cyr(cp1251_char(labels, k))]
        for k in clean_kids:
            trigrams.append(pref + cp1251_char(labels, k))
    lines.append('Per-root clean child alphabets:')
    for root in sorted(by_root):
        uniq = ''.join(sorted(set(by_root[root])))
        lines.append(f'  {root} -> {uniq}')
    lines.append('')
    lines.append(f'Clean trigrams count={len(trigrams)}')
    lines.append('Sample trigrams: ' + ', '.join(sorted(trigrams)[:180]))
    lines.append('')

    examples = ['аб','ав','ал','ба','бо','ве','за','из','ка','ко','на','не','по','пр','ра','ст']
    lines.append('Manual prefix existence checks:')
    for pref in examples:
        node_ids = [0]
        ok = True
        path = []
        for ch in pref:
            next_nodes = []
            for nid in node_ids:
                for kid in children(raw0, raw1, nid):
                    if cp1251_char(labels, kid) == ch:
                        next_nodes.append(kid)
            if not next_nodes:
                ok = False
                break
            node_ids = sorted(set(next_nodes))
            path.append(node_ids[:8])
        lines.append(f'  {pref}: {"YES" if ok else "NO"} path={path}')

    report = '\n'.join(lines) + '\n'
    OUT_PATH.write_text(report, encoding='utf-8')
    print(report)
    print(f'written to {OUT_PATH}')


if __name__ == '__main__':
    main()
