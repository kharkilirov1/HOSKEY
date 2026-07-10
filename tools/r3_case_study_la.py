#!/usr/bin/env python3
from __future__ import annotations
import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_case_study_la_2026-04-09.txt")

OFF = {
    'has_child': 0x8,
    'louds': 0x278D8,
    'terminal_raw': 0x753C8,
    'terminal_ds': 0x93BC8,
    'labels': 0xFDB98,
    'intv': 0x9B5E8,
}
TARGETS = [14, 308, 325]


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from('<Q', data, off)[0]


class QWords:
    def __init__(self, data: bytes, off: int):
        self.off = off
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


def children(has_child: QWords, louds: QWords, node_id: int, limit=40):
    fc = first_child(has_child, louds, node_id)
    if fc < 0:
        return []
    out = []
    for i, nid in enumerate(siblings(louds, fc)):
        out.append(nid)
        if i + 1 >= limit:
            break
    return out


def parse_intvector(data: bytes, off: int):
    bitwidth = u64(data, off)
    mask = u64(data, off + 8)
    span = u64(data, off + 24)
    data_off = off + 0x20
    words = [u64(data, data_off + i * 8) for i in range(span)]
    return bitwidth, mask, words


def iv_get(iv, idx: int) -> int:
    bitwidth, mask, words = iv
    bitpos = idx * bitwidth
    wi = bitpos >> 6
    bo = bitpos & 63
    if wi >= len(words):
        return 0
    val = words[wi] >> bo
    if bo + bitwidth > 64 and wi + 1 < len(words):
        val |= words[wi + 1] << (64 - bo)
    return val & mask


def main() -> None:
    data = TRIE_PATH.read_bytes()
    has_child = QWords(data, OFF['has_child'])
    louds = QWords(data, OFF['louds'])
    term_raw = QWords(data, OFF['terminal_raw'])
    labels = data[OFF['labels']:OFF['labels'] + 300000]
    iv = parse_intvector(data, OFF['intv'])

    lines = []
    lines.append(f'TRIE_PATH={TRIE_PATH}')
    lines.append('Case study: shortest mixed prefix "ла"')
    lines.append('')
    lines.append('Relevant decomp semantics:')
    lines.append('- FUN_0066242c on non-terminal node writes label|0x100 and returns without FUN_00661baa')
    lines.append('- FUN_0066242c on terminal node does not emit a label byte; it computes terminal rank, calls FUN_00661462, then FUN_00661baa')
    lines.append('- FUN_0066268c sees that empty emitted char and recurses with the SAME input character on the new state')
    lines.append('- Therefore terminal nodes act like epsilon-transitions across prefix states')
    lines.append('')

    for nid in TARGETS:
        label = cp1251_char(labels, nid)
        term = term_raw.bit(nid)
        hc = has_child.bit(nid)
        kids = children(has_child, louds, nid)
        kid_chars = ''.join(cp1251_char(labels, k) for k in kids)
        lines.append(f'node={nid} label={label} terminal={term} has_child={hc} first_child={first_child(has_child,louds,nid)} kids={kid_chars}')
        if term:
            rank = term_raw.rank1(nid)
            upper = iv_get(iv, rank)
            key = (upper << 8) | labels[nid]
            lines.append(f'  terminal_rank={rank} upper={upper} key=0x{key:X}')
        lines.append('')

    lines.append('Observed prefix split:')
    lines.append('  л  -> node 14')
    lines.append('  ла -> node 308 (term=0) and node 325 (term=1)')
    lines.append('')
    lines.append('Interpretation:')
    lines.append('- node 308 is the ordinary non-terminal continuation branch')
    lines.append('- node 325 is a terminal boundary for the same visible prefix, and runtime should treat it as an epsilon handoff candidate rather than a plain sibling-equivalent node')
    lines.append('- this is exactly why a simple `prefix -> single node_id` engine model would be wrong')

    report = '\n'.join(lines) + '\n'
    OUT_PATH.write_text(report, encoding='utf-8')
    print(report)
    print(f'written to {OUT_PATH}')


if __name__ == '__main__':
    main()
