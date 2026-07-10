#!/usr/bin/env python3
"""Sequential decoder for 1nc7 succinct trie format.

Based on Ghidra decompilation of:
  FUN_0065f2d8 — main 1nc7 parser
  FUN_00660496 — recursive section loader
  FUN_00661806 — bitvector section reader
  FUN_006615a4 — data section reader

File is read SEQUENTIALLY, section by section.
"""
import struct
import pathlib
import sys


class StreamReader:
    def __init__(self, data: bytes, offset: int = 0):
        self.data = data
        self.pos = offset

    def read_u32(self) -> int:
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_u64(self) -> int:
        v = struct.unpack_from("<Q", self.data, self.pos)[0]
        self.pos += 8
        return v

    def read_bytes(self, n: int) -> bytes:
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def peek_u64(self) -> int:
        return struct.unpack_from("<Q", self.data, self.pos)[0]

    def remaining(self) -> int:
        return len(self.data) - self.pos


class BitvectorSection:
    def __init__(self):
        self.num_words = 0  # u64 words count (or superblock count)
        self.field1 = 0
        self.total_popcount = 0
        self.rank_data = b""
        self.bits = None  # DataSection for actual bits

    def __repr__(self):
        return (f"BV(words={self.num_words}, pop={self.total_popcount}, "
                f"rank={len(self.rank_data)}B, bits={self.bits})")


class DataSection:
    def __init__(self):
        self.field0 = 0
        self.count = 0
        self.field2 = 0
        self.data = b""

    def __repr__(self):
        return f"Data(f0={self.field0}, count={self.count}, f2={self.field2}, data={len(self.data)}B)"


class TrieLevel:
    def __init__(self, name=""):
        self.name = name
        self.bv1 = None  # BitvectorSection
        self.data1 = None  # DataSection (via virtual read)
        self.bv2 = None  # BitvectorSection
        self.data2 = None  # DataSection
        self.labels1 = None  # DataSection
        self.data3 = None  # DataSection
        self.labels2 = None  # DataSection
        self.data4 = None  # DataSection (final)
        self.section_ptr1 = 0
        self.section_ptr2 = 0
        self.sub_trie = None  # Recursive TrieLevel


def read_data_section(reader: StreamReader, label: str) -> DataSection:
    """FUN_006615a4: read [3*u64 header][data: count*8+8 bytes]"""
    ds = DataSection()
    start = reader.pos
    ds.field0 = reader.read_u64()
    ds.count = reader.read_u64()
    ds.field2 = reader.read_u64()
    # Data starts at offset 24, size = count * 8 + 8
    data_size = ds.count * 8 + 8
    ds.data = reader.read_bytes(data_size)
    print(f"  {label}: f0={ds.field0} count={ds.count} f2={ds.field2} "
          f"data={len(ds.data)}B at file+{start}")
    return ds


def read_bitvector_section(reader: StreamReader, label: str) -> BitvectorSection:
    """FUN_00661806: read [3*u64 header][rank][bits via data_section]"""
    bv = BitvectorSection()
    start = reader.pos
    bv.num_words = reader.read_u64()
    bv.field1 = reader.read_u64()
    bv.total_popcount = reader.read_u64()

    # Rank data size: num_words * 4 + alignment + extra
    # From decompiled: uVar1 * 4 + ((uVar1 & 1) == 0) * 4 + 0x1c
    # But we already read 24 bytes of header, so remaining rank size:
    rank_size = bv.num_words * 4 + (4 if (bv.num_words & 1) == 0 else 0) + 0x1c - 0x18
    # = num_words * 4 + pad + 4
    bv.rank_data = reader.read_bytes(rank_size)

    print(f"  {label}: words={bv.num_words} pop={bv.total_popcount} "
          f"rank={len(bv.rank_data)}B at file+{start}")

    # Read bitvector bits as data section
    bv.bits = read_data_section(reader, f"{label}.bits")
    return bv


def read_trie_level(reader: StreamReader, depth: int = 0) -> TrieLevel:
    """FUN_00660496: read all sections for one trie level (recursive)."""
    prefix = "  " * depth
    level = TrieLevel(f"level_{depth}")
    print(f"{prefix}=== TRIE LEVEL {depth} at file+{reader.pos} ===")

    # 1. Bitvector 1 (FUN_00661806)
    level.bv1 = read_bitvector_section(reader, f"{prefix}BV1")

    # 2. Data section (virtual read — same as FUN_006615a4)
    level.data1 = read_data_section(reader, f"{prefix}DATA1")

    # 3. Bitvector 2 (FUN_00661806)
    level.bv2 = read_bitvector_section(reader, f"{prefix}BV2")

    # 4. Data section
    level.data2 = read_data_section(reader, f"{prefix}DATA2")

    # 5. Labels/weights 1 (FUN_006615a4)
    level.labels1 = read_data_section(reader, f"{prefix}LABELS1")

    # 6. Data section
    level.data3 = read_data_section(reader, f"{prefix}DATA3")

    # 7. Labels/weights 2 (FUN_006615a4)
    level.labels2 = read_data_section(reader, f"{prefix}LABELS2")

    # 8. Final data section
    level.data4 = read_data_section(reader, f"{prefix}DATA4")

    # 9. Section pointers (from FUN_00660496 post-section reads)
    # Read first pointer structure
    ptr_count = reader.peek_u64()
    print(f"{prefix}  PTR_PEEK: {ptr_count}")

    # The parent reads: lVar8 = *(long*)*param_1 → size, then advances
    # Then reads another similar block
    # Then reads 8 bytes (2 x u32?)
    # Then if remaining count > 0: recursive

    # Read section data 1
    section_size1 = reader.read_u64()
    level.section_ptr1 = section_size1
    if section_size1 > 0 and section_size1 < 10000000:
        reader.read_bytes(section_size1 * 8 + 8)  # array + header
        print(f"{prefix}  SECTION_DATA1: size={section_size1}")

    # Read section data 2
    section_size2 = reader.read_u64()
    level.section_ptr2 = section_size2
    if section_size2 > 0 and section_size2 < 10000000:
        reader.read_bytes(section_size2 * 8 + 8)
        print(f"{prefix}  SECTION_DATA2: size={section_size2}")

    # Read 8 bytes (some state)
    if reader.remaining() >= 8:
        state = reader.read_bytes(8)

    # Check for recursive sub-trie
    # From decompiled: reads a count, if > 0, creates sub-trie and recurses
    if reader.remaining() >= 8:
        sub_count = reader.peek_u64()
        if sub_count > 0 and sub_count < 1000000 and reader.remaining() > 100:
            print(f"{prefix}  SUB-TRIE detected (count={sub_count})")
            try:
                level.sub_trie = read_trie_level(reader, depth + 1)
            except Exception as e:
                print(f"{prefix}  SUB-TRIE parse error: {e}")

    return level


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\1nc7_parsed_structure.txt"
    )

    data = trie_path.read_bytes()
    print(f"File size: {len(data)}")

    # Header: 24 bytes (6 x u32)
    header = struct.unpack_from("<6I", data, 0)
    magic_str = data[0:4]
    version = header[1]
    print(f"Magic: {magic_str} Version: {version}")
    print(f"Header: {[hex(h) for h in header]}")

    # Stream starts after header
    # The parser validates magic (4 bytes "1nc7") then version (4 bytes = 1)
    # Then creates stream from offset after magic+version check
    # But our header is 24 bytes. Let me try starting at offset 24 (after full header)

    for start_offset in [8, 24]:
        print(f"\n{'='*60}")
        print(f"Trying stream start at offset {start_offset}")
        print(f"{'='*60}")

        reader = StreamReader(data, start_offset)
        try:
            level = read_trie_level(reader, 0)
            print(f"\nParsed successfully up to file+{reader.pos}")
            print(f"Remaining: {reader.remaining()} bytes")

            # Summary
            print(f"\n=== STRUCTURE SUMMARY ===")
            print(f"BV1: words={level.bv1.num_words} pop={level.bv1.total_popcount}")
            print(f"BV2: words={level.bv2.num_words} pop={level.bv2.total_popcount}")
            if level.bv1.bits:
                print(f"BV1.bits: count={level.bv1.bits.count}")
            if level.bv2.bits:
                print(f"BV2.bits: count={level.bv2.bits.count}")
            print(f"LABELS1: count={level.labels1.count}")
            print(f"LABELS2: count={level.labels2.count}")
            if level.sub_trie:
                st = level.sub_trie
                print(f"SUB-TRIE BV1: words={st.bv1.num_words} pop={st.bv1.total_popcount}")
                print(f"SUB-TRIE BV2: words={st.bv2.num_words} pop={st.bv2.total_popcount}")

            break  # Success
        except Exception as e:
            print(f"Failed at offset {start_offset}: {e}")
            import traceback
            traceback.print_exc()

    print(f"\nDone.")


if __name__ == "__main__":
    main()
