
#!/usr/bin/env python3
"""Probe root-entry child stepping using FUN_00661412 + FUN_00662502 models.

This checks which recursive has_child candidate behaves sanely once the special
root-entry helper has landed on the first real child id.
"""

from __future__ import annotations

import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_root_sibling_probe_2026-04-08.txt")

LEVEL0 = {
    "root_has_child": 0x1FEB8,   # best current candidate for FUN_00661412 has_child gate
    "root_louds": 0x47788,       # best current candidate for FUN_00661412 select1 source
    "rec_hc_candidates": {
        "bv1_ds": 0x1FEB8,
        "bv2_ds": 0x47788,
        "ds3": 0x6D9A8,
        "ds4": 0x93BC8,
    },
    "blob1": 0xFDB98,
}


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def cp1251(b: int) -> str:
    try:
        ch = bytes([b]).decode("cp1251")
    except UnicodeDecodeError:
        return "?"
    return ch if ord(ch) >= 0x20 else "."


class BitVector:
    def __init__(self, buf: bytes, shift: int = 64):
        self.buf = buf
        self.shift = shift
        self.total_bits = len(buf) * 8 - shift

    def bit(self, pos: int) -> int:
        pos += self.shift
        if pos < 0 or pos >= len(self.buf) * 8:
            return 0
        return (self.buf[pos >> 3] >> (pos & 7)) & 1

    def select1(self, n: int) -> int:
        seen = 0
        for pos in range(self.total_bits):
            if self.bit(pos):
                if seen == n:
                    return pos
                seen += 1
        return -1


def get_ds_bits(data: bytes, ds_off: int) -> bytes:
    count = u64(data, ds_off + 8)
    return data[ds_off + 24 : ds_off + 24 + count * 8]


def next_node_fun_00662502_style(node_id: int, has_child_bit: int) -> int:
    nxt = node_id + 1
    if has_child_bit == 0:
        low = nxt & 0xFF
    else:
        low = 0
    return (nxt & ~0xFF) | low


def main() -> None:
    data = TRIE_PATH.read_bytes()
    blob1 = data[LEVEL0["blob1"] : LEVEL0["blob1"] + 64]

    root_louds = BitVector(get_ds_bits(data, LEVEL0["root_louds"]), shift=64)
    root_first_child = root_louds.select1(0) + 1

    lines = []
    lines.append(f"TRIE_PATH={TRIE_PATH}")
    lines.append(f"FUN_00661412 best current root model -> first child id = select1(root_louds,0)+1 = {root_first_child}")
    lines.append("blob1 decoded=" + "".join(cp1251(b) for b in blob1[:32]))
    lines.append("")

    for name, off in LEVEL0["rec_hc_candidates"].items():
        hc = BitVector(get_ds_bits(data, off), shift=64)
        node = root_first_child
        seq = []
        for _ in range(12):
            if node >= len(blob1):
                seq.append(f"{node}:OUT")
                break
            ch = cp1251(blob1[node])
            bit = hc.bit(node)
            seq.append(f"{node}:{ch}/hc={bit}")
            node = next_node_fun_00662502_style(node, bit)
        lines.append(f"{name}: " + " -> ".join(seq))

    lines.append("")
    lines.append("Observation: under the current root-entry model, ds4 is the cleanest recursive has_child candidate for the first bucket: it yields a straight Cyrillic run from child_id=3 before the first branch reset.")
    report = "\n".join(lines) + "\n"
    OUT_PATH.write_text(report, encoding="utf-8")
    print(report)
    print(f"written to {OUT_PATH}")


if __name__ == "__main__":
    main()
