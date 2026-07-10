#!/usr/bin/env python3
from __future__ import annotations
import struct
from pathlib import Path
from collections import deque

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_nonterminal_root_phase_probe_2026-04-08.txt")

LEVEL0 = {
    "raw0": 0x8,
    "bv1_ds": 0x1FEB8,
    "raw1": 0x278D8,
    "raw3": 0x753C8,
    "labels": 0xFDB98,
}


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from('<Q', data, off)[0]


class QWords:
    def __init__(self, data: bytes, off: int):
        self.off = off
        self.field0 = u64(data, off)
        self.count = u64(data, off + 8)
        self.ptr = off + 0x10
        self.buf = data[self.ptr:self.ptr + self.count * 8]

    def bit(self, pos: int) -> int:
        if pos < 0 or pos >= self.count * 64:
            return 0
        return (self.buf[pos >> 3] >> (pos & 7)) & 1

    def rank1_exclusive(self, pos: int) -> int:
        if pos <= 0:
            return 0
        total = 0
        full = min(pos >> 6, self.count)
        for i in range(full):
            total += self.buf[i*8:(i+1)*8].count(1)  # placeholder wrong? replaced below
        return total

    def qword(self, idx: int) -> int:
        if idx < 0 or idx >= self.count:
            return 0
        return struct.unpack_from('<Q', self.buf, idx * 8)[0]

    def rank1(self, pos: int) -> int:
        if pos <= 0:
            return 0
        full_words = pos >> 6
        rem = pos & 63
        total = 0
        for i in range(min(full_words, self.count)):
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


def first_child(raw0: QWords, raw1: QWords, node_id: int) -> tuple[int,int]:
    if raw0.bit(node_id) == 0:
        return -1, 0
    r = raw0.rank1(node_id)
    pos = raw1.select1(r)
    if pos < 0:
        return -1, 0
    child = pos + 1
    return child, 1


def siblings(raw1: QWords, start_node: int):
    node = start_node
    alive = 1
    while alive:
        yield node
        alive = 1 if raw1.bit(node) == 0 else 0
        if alive:
            node += 1


def main() -> None:
    data = TRIE_PATH.read_bytes()
    raw0 = QWords(data, LEVEL0['raw0'])
    raw1 = QWords(data, LEVEL0['raw1'])
    term = QWords(data, LEVEL0['raw3'])
    labels = data[LEVEL0['labels']:LEVEL0['labels'] + 200000]

    roots = list(siblings(raw1, first_child(raw0, raw1, 0)[0]))
    lines: list[str] = []
    lines.append(f'TRIE_PATH={TRIE_PATH}')
    lines.append('Probe: non-terminal depth-0 progression via FUN_00661412 / FUN_00662502 semantics')
    lines.append('Assumptions: raw0 = child-existence bitvector, raw1 = LOUDS/select+sibling bitvector, raw3 = terminal bits')
    lines.append('')
    lines.append('Root children:')
    for nid in roots[:16]:
        lines.append(f'  id={nid:3d} label={cp1251_char(labels, nid)} terminal={term.bit(nid)} raw1_bit={raw1.bit(nid)}')

    lines.append('')
    lines.append('Non-terminal expansions (depth <= 3):')
    q = deque((cp1251_char(labels, nid), nid, 1) for nid in roots[:16])
    seen = set()
    shown = 0
    while q and shown < 120:
        pref, nid, depth = q.popleft()
        key = (nid, depth)
        if key in seen:
            continue
        seen.add(key)
        if depth >= 3:
            continue
        if term.bit(nid):
            continue
        child, ok = first_child(raw0, raw1, nid)
        if not ok or child < 0:
            continue
        kids = list(siblings(raw1, child))
        kid_labels = ''.join(cp1251_char(labels, k) for k in kids[:24])
        lines.append(f'  {pref:<6s} node={nid:4d} -> first_child={child:4d} kids={kid_labels}')
        shown += 1
        for k in kids[:12]:
            ch = cp1251_char(labels, k)
            if ch != '?':
                q.append((pref + ch, k, depth + 1))

    report = '\n'.join(lines) + '\n'
    OUT_PATH.write_text(report, encoding='utf-8')
    print(report)
    print(f'written to {OUT_PATH}')


if __name__ == '__main__':
    main()
