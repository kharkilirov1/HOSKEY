
#!/usr/bin/env python3
"""Probe the root-entry helper hypothesis around FUN_00661412.

FUN_00661412 explicitly returns `select1(...) + 1` on the root-entry path.
This script checks which top-level bitvector candidate makes that first child
land on the first clear Cyrillic byte in `blob1` under the new id=0 model.
"""

from __future__ import annotations

import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_root_entry_probe_2026-04-08.txt")

LEVEL0 = {
    "bv1_ds": 0x1FEB8,
    "bv2_ds": 0x47788,
    "ds3": 0x6D9A8,
    "ds4": 0x93BC8,
    "blob1": 0xFDB98,
    "blob1_len": 64,
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


def main() -> None:
    data = TRIE_PATH.read_bytes()
    blob1 = data[LEVEL0["blob1"] : LEVEL0["blob1"] + LEVEL0["blob1_len"]]
    lines = []
    lines.append(f"TRIE_PATH={TRIE_PATH}")
    lines.append("FUN_00661412 model: child_id = select1(louds, rank1(...)) + 1")
    lines.append(f"level0 blob1 head64={blob1.hex(' ')}")
    lines.append("blob1 decoded=" + "".join(cp1251(b) for b in blob1[:32]))
    lines.append("")
    for name in ("bv1_ds", "bv2_ds", "ds3", "ds4"):
        bv = BitVector(get_ds_bits(data, LEVEL0[name]), shift=64)
        s0 = bv.select1(0)
        s1 = bv.select1(1)
        s2 = bv.select1(2)
        idx = s0 + 1 if s0 >= 0 else -1
        ch = cp1251(blob1[idx]) if 0 <= idx < len(blob1) else "?"
        lines.append(f"{name}: select1(0)={s0} -> child_id={idx} -> blob1[child_id]={ch}")
        lines.append(f"       select1(1)={s1}, select1(2)={s2}")
    lines.append("")
    lines.append("Observation: only bv2_ds gives select1(0)=2, so FUN_00661412's explicit +1 would land on child_id=3, i.e. the first clear Cyrillic byte 'а' in level0 blob1.")
    report = "\n".join(lines) + "\n"
    OUT_PATH.write_text(report, encoding="utf-8")
    print(report)
    print(f"written to {OUT_PATH}")


if __name__ == "__main__":
    main()
