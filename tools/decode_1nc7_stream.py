#!/usr/bin/env python3
"""Precise sequential stream decoder for 1nc7.

Tracks exact stream position like FUN_00660496 does.
Reading order per level:
  1. DataSection (FUN_006615a4): [f0:u64][count:u64][f2:u64][data: count*8+8]
  2. BitvectorSection (FUN_00661806): [words:u64][f1:u64][pop:u64][rank: words*4+pad+4][DataSection for bits]
  3. DataSection (virtual read = IntVector or similar)
  4. BitvectorSection
  5. DataSection (virtual read)
  6. DataSection (FUN_006615a4)
  7. DataSection (virtual read)
  8. DataSection (FUN_006615a4)
  9. DataSection (virtual read → final output)
  Then: labels1 (u64 count + bytes), labels2 (u64 count + bytes)
  Then: 8 bytes state
  Then: if remaining → recursive sub-level
"""
import struct, pathlib, sys


LABEL = {}
for i in range(32): LABEL[i] = chr(0x0430 + i)  # а-я
LABEL[32] = chr(0x0451)  # ё


def popcount64(x):
    x -= (x >> 1) & 0x5555555555555555
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f
    return ((x * 0x0101010101010101) >> 56) & 0xff


class Stream:
    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def u64(self):
        v = struct.unpack_from("<Q", self.data, self.pos)[0]
        self.pos += 8
        return v

    def skip(self, n):
        self.pos += n

    def peek_u64(self):
        return struct.unpack_from("<Q", self.data, self.pos)[0]

    def slice(self, n):
        s = self.data[self.pos:self.pos + n]
        self.pos += n
        return s

    def remaining(self):
        return len(self.data) - self.pos


def read_datasection(s, label=""):
    """FUN_006615a4: [f0:u64][count:u64][f2:u64][data: count*8+8]"""
    start = s.pos
    f0 = s.u64()
    count = s.u64()
    f2 = s.u64()
    data_size = count * 8 + 8
    data = s.slice(data_size)
    total = s.pos - start
    print(f"  {label}: DataSection at {start} f0={f0} count={count} data={len(data)}B total={total}B")
    return f0, count, f2, data


def read_bvsection(s, label=""):
    """FUN_00661806: [words:u64][f1:u64][pop:u64] + rank + DataSection(bits)"""
    start = s.pos
    words = s.u64()
    f1 = s.u64()
    pop = s.u64()

    # Rank size from decompiled: words * 4 + ((words&1)==0)*4 + 0x1c - 0x18
    # = words*4 + pad + 4
    rank_extra = words * 4 + (4 if (words & 1) == 0 else 0) + 4
    rank_data = s.slice(rank_extra)

    # Then bits as DataSection
    bf0, bcount, bf2, bits_data = read_datasection(s, f"{label}.bits")

    total = s.pos - start
    print(f"  {label}: BV at {start} words={words} pop={pop} rank={len(rank_data)}B bits={len(bits_data)}B total={total}B")
    return words, pop, rank_data, bits_data


def read_level(s, depth=0):
    """FUN_00660496 reading sequence."""
    prefix = "  " * depth
    print(f"{prefix}=== LEVEL {depth} at stream pos {s.pos} ===")

    # 1. DataSection (initial virtual read)
    read_datasection(s, f"{prefix}[1] initial")

    # 2. BitvectorSection (LOUDS)
    louds = read_bvsection(s, f"{prefix}[2] LOUDS_BV")

    # 3. DataSection (virtual read / IntVector)
    read_datasection(s, f"{prefix}[3] post-LOUDS")

    # 4. BitvectorSection (has_child)
    hc = read_bvsection(s, f"{prefix}[4] HC_BV")

    # 5. DataSection (virtual read)
    read_datasection(s, f"{prefix}[5] post-HC")

    # 6. DataSection (FUN_006615a4 — labels/weights?)
    read_datasection(s, f"{prefix}[6] data_section")

    # 7. DataSection (virtual read)
    read_datasection(s, f"{prefix}[7] virtual")

    # 8. DataSection (FUN_006615a4)
    read_datasection(s, f"{prefix}[8] data_section")

    # 9. DataSection (final virtual read → output)
    read_datasection(s, f"{prefix}[9] final")

    # Labels1: [count:u64][data: count bytes]
    lab1_count = s.u64()
    lab1_data = s.slice(lab1_count) if lab1_count > 0 and lab1_count < 10000000 else b""
    print(f"{prefix}  LABELS1: count={lab1_count} data={len(lab1_data)}B at {s.pos - len(lab1_data)}")
    if lab1_data:
        print(f"{prefix}    first20: {list(lab1_data[:20])}")

    # Labels2: [count:u64][data: count bytes]
    lab2_count = s.u64()
    lab2_data = s.slice(lab2_count) if lab2_count > 0 and lab2_count < 10000000 else b""
    print(f"{prefix}  LABELS2: count={lab2_count} data={len(lab2_data)}B at {s.pos - len(lab2_data)}")
    if lab2_data:
        print(f"{prefix}    first20: {list(lab2_data[:20])}")
        # Try to decode as Russian
        chars = [LABEL.get(b, f"[{b}]") for b in lab2_data[:30]]
        print(f"{prefix}    as chars: {''.join(chars)}")

    # 8 bytes state / alignment
    if s.remaining() >= 8:
        state = s.u64()
        print(f"{prefix}  STATE: {state}")

    # Recursive sub-level?
    if s.remaining() > 100:
        print(f"{prefix}  REMAINING: {s.remaining()} bytes → recursive sub-level")
        try:
            read_level(s, depth + 1)
        except Exception as e:
            print(f"{prefix}  RECURSIVE ERROR: {e}")


def main():
    path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    data = path.read_bytes()

    # File header: 24 bytes (magic, version, payload_hint, 0, item_count, 0)
    # Stream starts at offset 8 (after magic+version, which FUN_0065f2d8 consumes)
    # Actually the stream covers full data, but sections start after header validation
    # The pre-data DataSection at file+8 IS the first section read

    print(f"File: {len(data)} bytes")
    print(f"Header: {struct.unpack_from('<6I', data, 0)}")

    s = Stream(data, 8)  # Start after magic+version (8 bytes)
    try:
        read_level(s, 0)
    except Exception as e:
        print(f"\nPARSE ERROR at pos {s.pos}: {e}")
        import traceback
        traceback.print_exc()

    print(f"\nFinal stream position: {s.pos}")
    print(f"Remaining: {s.remaining()} bytes")


if __name__ == "__main__":
    main()
