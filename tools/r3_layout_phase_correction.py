#!/usr/bin/env python3
from __future__ import annotations
import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_layout_phase_correction_2026-04-08.txt")

LEVEL0 = {
    "raw0": 0x8,
    "bv1": 0x1E808,
    "raw1": 0x278D8,
    "bv2": 0x460D8,
    "raw2": 0x4F1A8,
    "ds3": 0x6D9A8,
    "raw3": 0x753C8,
    "ds4": 0x93BC8,
    "intv": 0x9B5E8,
    "labels_blob": 0xFDB98,
    "suffix_blob": 0x1F1B18,
}
ROOT_IDS = [3,4,5,6,7,8]


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from('<Q', data, off)[0]


def cp1251_char(buf: bytes, idx: int) -> str:
    if idx < 0 or idx >= len(buf):
        return '?'
    ch = bytes([buf[idx]]).decode('cp1251', errors='replace')
    return ch if ord(ch) >= 0x20 else '.'


class RawQWords:
    def __init__(self, off: int, data: bytes):
        self.off = off
        self.field0 = u64(data, off)
        self.count = u64(data, off + 8)
        self.ptr = off + 0x10


class RankDS:
    def __init__(self, off: int, data: bytes):
        self.off = off
        self.field0 = u64(data, off)
        self.count = u64(data, off + 8)
        self.field2 = u64(data, off + 16)
        self.ptr = off + 0x18


class BV:
    def __init__(self, raw: RawQWords):
        self.raw = raw
        self.data = raw_bytes[raw.ptr: raw.ptr + raw.count * 8]

    def bit(self, pos: int) -> int:
        if pos < 0 or pos >= self.raw.count * 64:
            return 0
        byte = self.data[pos >> 3]
        return (byte >> (pos & 7)) & 1


def main() -> None:
    global raw_bytes
    raw_bytes = TRIE_PATH.read_bytes()

    raw0 = RawQWords(LEVEL0['raw0'], raw_bytes)
    raw1 = RawQWords(LEVEL0['raw1'], raw_bytes)
    raw2 = RawQWords(LEVEL0['raw2'], raw_bytes)
    raw3 = RawQWords(LEVEL0['raw3'], raw_bytes)
    ds3 = RankDS(LEVEL0['ds3'], raw_bytes)
    ds4 = RankDS(LEVEL0['ds4'], raw_bytes)
    term_bv = BV(raw3)
    label_blob = raw_bytes[LEVEL0['labels_blob']:LEVEL0['labels_blob'] + 64]

    lines: list[str] = []
    lines.append(f'TRIE_PATH={TRIE_PATH}')
    lines.append('Layout reconciliation between FUN_00660496 producer chain and consumer offsets')
    lines.append('')
    lines.append('Producer chain at level 0:')
    lines.append('  raw0 -> bv1 -> raw1 -> bv2 -> raw2 -> ds3 -> raw3 -> ds4 -> intvector -> labels -> suffix -> child')
    lines.append('')
    lines.append('Reconciled object slots / consumer offsets:')
    lines.append('  +0x08/+0x10 : stage0 raw qword object (root raw bits source for FUN_00661646)')
    lines.append('  +0x18..+0x50: first BV object; +0x18 select-structure start, +0x38 rank-DS start')
    lines.append('  +0x70/+0x78 : stage1 raw qword object (second raw bits source)')
    lines.append('  +0x80..+0xB8: second BV object; +0x80 select-structure start, +0xA0 rank-DS start')
    lines.append('  +0xD8/+0xE0 : stage2 raw qword object')
    lines.append('  +0xE8..+0x108: ds3 object (used by FUN_00660ec3 family, not terminal traversal)')
    lines.append('  +0x118/+0x120: stage3 raw qword object (terminal raw bits source)')
    lines.append('  +0x128..+0x148: ds4 object (terminal rank-DS used by FUN_0066242c / FUN_00661926)')
    lines.append('  +0x150..+0x170: IntVector for FUN_00661462 upper bits')
    lines.append('  +0x178        : labels low-byte blob')
    lines.append('  +0x180        : suffix / continuation pool')
    lines.append('  +0x188        : child trie pointer')
    lines.append('')
    lines.append('Level-0 file offsets mapped to that layout:')
    lines.append(f'  raw0  @ 0x{raw0.off:X} field0={raw0.field0} count={raw0.count} data=0x{raw0.ptr:X}')
    lines.append(f'  bv1   @ 0x{LEVEL0["bv1"]:X}')
    lines.append(f'  raw1  @ 0x{raw1.off:X} field0={raw1.field0} count={raw1.count} data=0x{raw1.ptr:X}')
    lines.append(f'  bv2   @ 0x{LEVEL0["bv2"]:X}')
    lines.append(f'  raw2  @ 0x{raw2.off:X} field0={raw2.field0} count={raw2.count} data=0x{raw2.ptr:X}')
    lines.append(f'  ds3   @ 0x{ds3.off:X} field0={ds3.field0} count={ds3.count} field2={ds3.field2} data=0x{ds3.ptr:X}')
    lines.append(f'  raw3  @ 0x{raw3.off:X} field0={raw3.field0} count={raw3.count} data=0x{raw3.ptr:X}')
    lines.append(f'  ds4   @ 0x{ds4.off:X} field0={ds4.field0} count={ds4.count} field2={ds4.field2} data=0x{ds4.ptr:X}')
    lines.append(f'  intv  @ 0x{LEVEL0["intv"]:X}')
    lines.append(f'  labels@ 0x{LEVEL0["labels_blob"]:X}')
    lines.append(f'  suffix@ 0x{LEVEL0["suffix_blob"]:X}')
    lines.append('')
    lines.append('Root letters 3..8 under the semantically-correct terminal pair (raw3 + ds4):')
    for nid in ROOT_IDS:
        term = term_bv.bit(nid)
        ch = cp1251_char(label_blob, nid)
        lines.append(f'  id={nid}: label={ch} terminal={term}')
    lines.append('')
    lines.append('Implication: ids 3..8 (а..е) are non-terminal under the real terminal bitvector.')
    lines.append('Therefore the common root-prefix path does NOT enter FUN_00661baa / FUN_00661462 handoff here;')
    lines.append('it stays in the depth-0 path handled by FUN_00661412 -> FUN_0066242c -> FUN_00662502.')
    lines.append('')
    lines.append('This invalidates the earlier idea of feeding level-0 root letters directly into terminal-derived FUN_00661462 keys for common prefixes.')
    lines.append('For ordinary prefixes like а/б/в..., the next-state driver is the non-terminal node iteration path, not the terminal/subtrie handoff path.')

    report = '\n'.join(lines) + '\n'
    OUT_PATH.write_text(report, encoding='utf-8')
    print(report)
    print(f'written to {OUT_PATH}')


if __name__ == '__main__':
    main()
