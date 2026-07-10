#!/usr/bin/env python3
"""R3 Stream Tracer — follows FUN_00660496 reading order exactly.

Goal: Find precise offsets of BVs, labels, weights for EACH trie level.

FUN_00660496 reading order per level:
  Step 1: FUN_00661806 => BV header (32B) + rank data (field1*8 bytes)
  Step 2: vtable       => DataSection for BV1 bits (24B header + count*8 + 8B pad)
  Step 3: FUN_00661806 => BV header + rank data
  Step 4: vtable       => DataSection for BV2 bits
  Step 5: FUN_006615a4 => DataSection (LABELS)
  Step 6: vtable       => process labels (may consume more stream bytes)
  Step 7: FUN_006615a4 => DataSection (WEIGHTS)
  Step 8: vtable       => process weights
  Step 9: Read 2 u64   => pointers (16B)
  Step 10: Read 4 u32  => metadata (16B)
  Step 11: If remaining => RECURSE
"""
import struct
import sys

TRIE_PATH = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"


def u64(data, off):
    return struct.unpack_from("<Q", data, off)[0]

def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]

def read_bv_header(data, pos, label=""):
    """FUN_00661806: reads BV header (4 x u64) + rank array."""
    num_words = u64(data, pos)
    rank_size = u64(data, pos + 8)   # number of u64s in rank data
    popcount  = u64(data, pos + 16)
    zero      = u64(data, pos + 24)
    header_bytes = 32
    rank_bytes = rank_size * 8
    total = header_bytes + rank_bytes
    print(f"  [{label}] BV @ file+{pos}: num_words={num_words}, rank_u64s={rank_size}, "
          f"pop={popcount}, zero={zero} => consumes {total}B (header 32 + rank {rank_bytes})")
    return pos + total, num_words, popcount

def read_datasection(data, pos, label=""):
    """FUN_006615a4: reads DataSection [f0, count, f2, data, pad]."""
    f0    = u64(data, pos)
    count = u64(data, pos + 8)
    f2    = u64(data, pos + 16)
    # data starts at pos+24, length = count*8
    # advance = count*8 + 0x20 (32)
    consumed = count * 8 + 32
    data_start = pos + 24
    data_end = data_start + count * 8
    print(f"  [{label}] DataSection @ file+{pos}: f0={f0}, count={count}, f2={f2} "
          f"=> data at file+{data_start}..{data_end} ({count*8}B), total consumed={consumed}B")
    return pos + consumed, data_start, count

def read_intvector_header(data, pos, label=""):
    """FUN_0066084e: reads IntVector [bitwidth:u64, count:u64] + packed data."""
    bitwidth = u64(data, pos)
    count    = u64(data, pos + 8)
    header_bytes = 16  # advance_stream(0x10)
    # packed data: count * 8 + 0x10
    packed_bytes = count * 8 + 16
    total = header_bytes + packed_bytes
    data_start = pos + header_bytes
    print(f"  [{label}] IntVector @ file+{pos}: bitwidth={bitwidth}, count={count} "
          f"=> packed data at file+{data_start} ({packed_bytes}B), total consumed={total}B")
    return pos + total, bitwidth, count


def scan_file_header(data):
    """Identify the file header before stream starts."""
    print("=== FILE HEADER ===")
    # First 64 bytes
    for i in range(0, 64, 8):
        val = u64(data, i)
        print(f"  file+{i:3d}: {val:20d}  (0x{val:016X})")

    # Check if offset 0 is magic "1nc7" or similar
    magic = data[0:4]
    print(f"  Magic bytes: {magic} = {magic.hex()}")

    # Check if offset 8 looks like a DataSection
    f0_at_8 = u64(data, 8)
    cnt_at_8 = u64(data, 16)
    print(f"\n  Offset 8: f0={f0_at_8}, count_candidate={cnt_at_8}")
    if f0_at_8 > 900000 and cnt_at_8 < 100000:
        print(f"  >> Looks like DataSection header!")
    return


def try_parse_level(data, stream_pos, level=0, file_size=0):
    """Try parsing one trie level starting at stream_pos."""
    indent = "  " * level
    print(f"\n{'='*60}")
    print(f"{indent}LEVEL {level}: stream starts at file+{stream_pos}")
    print(f"{'='*60}")

    start_pos = stream_pos

    # Step 1: BV1 header + rank
    print(f"\n{indent}Step 1: BV1 (has_child or LOUDS)")
    # Check if this looks like a BV header
    v0 = u64(data, stream_pos)
    v1 = u64(data, stream_pos + 8)
    if v0 > 100000 or v1 > 100000:
        print(f"  WARNING: doesn't look like BV header: [{v0}, {v1}, ...]")
        # Maybe there's a gap. Try scanning ahead.
        return None

    stream_pos, nw1, pop1 = read_bv_header(data, stream_pos, f"L{level}_BV1")

    # Step 2: BV1 bits DataSection
    print(f"\n{indent}Step 2: BV1 bits DataSection")
    stream_pos, bits1_start, bits1_count = read_datasection(data, stream_pos, f"L{level}_BV1_bits")

    # Check for gap between DS end and next structure
    next_v0 = u64(data, stream_pos)
    next_v1 = u64(data, stream_pos + 8)
    print(f"\n{indent}  After BV1 bits DS: file+{stream_pos}, next u64s: [{next_v0}, {next_v1}]")

    # If there's a gap (vtable processing consumed extra bytes), try to detect it
    gap_start = stream_pos
    if next_v0 > 100000 and next_v1 > 100000:
        # This doesn't look like a BV header, might be gap data
        # Scan forward for BV header pattern [small, small, medium, 0]
        print(f"  Scanning for next BV header...")
        for probe in range(stream_pos, min(stream_pos + 20000, file_size), 8):
            pv0 = u64(data, probe)
            pv1 = u64(data, probe + 8)
            pv3 = u64(data, probe + 24)
            if pv0 == nw1 and pv1 < 100000 and pv3 == 0:
                gap_size = probe - stream_pos
                print(f"  Found BV header at file+{probe} (gap={gap_size}B)")
                stream_pos = probe
                break

    # Step 3: BV2 header + rank
    print(f"\n{indent}Step 3: BV2 (LOUDS or has_child)")
    stream_pos, nw2, pop2 = read_bv_header(data, stream_pos, f"L{level}_BV2")

    # Step 4: BV2 bits DataSection
    print(f"\n{indent}Step 4: BV2 bits DataSection")
    stream_pos, bits2_start, bits2_count = read_datasection(data, stream_pos, f"L{level}_BV2_bits")

    # Again check for gap
    next_v0 = u64(data, stream_pos)
    next_v1 = u64(data, stream_pos + 8)
    print(f"\n{indent}  After BV2 bits DS: file+{stream_pos}, next u64s: [{next_v0}, {next_v1}]")

    # Check if this is another BV header (would mean we're still reading BVs)
    # or a DataSection (labels)
    if next_v0 == nw1 and u64(data, stream_pos + 24) == 0:
        # Another BV header! Maybe there are more than 2 BVs per level
        print(f"  => Looks like ANOTHER BV header! Maybe 3+ BVs per level?")

        # Step 3b: BV3 header + rank
        print(f"\n{indent}Step 3b: BV3 (terminal?)")
        stream_pos, nw3, pop3 = read_bv_header(data, stream_pos, f"L{level}_BV3")

        # Step 4b: BV3 bits DataSection
        print(f"\n{indent}Step 4b: BV3 bits DataSection")
        stream_pos, bits3_start, bits3_count = read_datasection(data, stream_pos, f"L{level}_BV3_bits")

        next_v0 = u64(data, stream_pos)
        next_v1 = u64(data, stream_pos + 8)
        print(f"\n{indent}  After BV3 bits DS: file+{stream_pos}, next u64s: [{next_v0}, {next_v1}]")

        # Check for yet another BV
        if next_v0 == nw1 and u64(data, stream_pos + 24) == 0:
            print(f"  => ANOTHER BV header! BV4")
            print(f"\n{indent}Step 3c: BV4")
            stream_pos, nw4, pop4 = read_bv_header(data, stream_pos, f"L{level}_BV4")
            print(f"\n{indent}Step 4c: BV4 bits DataSection")
            stream_pos, bits4_start, bits4_count = read_datasection(data, stream_pos, f"L{level}_BV4_bits")

            next_v0 = u64(data, stream_pos)
            next_v1 = u64(data, stream_pos + 8)
            print(f"\n{indent}  After BV4 bits DS: file+{stream_pos}, next u64s: [{next_v0}, {next_v1}]")

    # Try scanning for gap again
    if next_v0 > 100000:
        # Probably gap data before labels
        print(f"  Scanning for next structure (labels DS or IntVector)...")
        for probe in range(stream_pos, min(stream_pos + 20000, file_size), 8):
            pv0 = u64(data, probe)
            pv1 = u64(data, probe + 8)
            # DataSection: f0 is large, count is reasonable
            if pv0 > 900000 and 0 < pv1 < 1000000:
                gap_size = probe - stream_pos
                print(f"  Found DataSection at file+{probe} (gap={gap_size}B)")
                stream_pos = probe
                break
            # IntVector: bitwidth < 256, count reasonable
            if pv0 < 256 and 0 < pv1 < 10000000:
                gap_size = probe - stream_pos
                print(f"  Found IntVector at file+{probe} (gap={gap_size}B)")
                stream_pos = probe
                break

    # Step 5-6: Labels
    print(f"\n{indent}Step 5-6: LABELS")
    # Detect type: DataSection (f0 large) or IntVector (bitwidth small)
    test_v0 = u64(data, stream_pos)
    if test_v0 < 256:
        print(f"  => IntVector format (bitwidth={test_v0})")
        stream_pos, bw, cnt = read_intvector_header(data, stream_pos, f"L{level}_LABELS")
        # Show first 20 label values
        print(f"  First 20 label values (bitwidth={bw}):")
        packed = data[stream_pos - cnt*8 - 16 + 16:]  # data starts after header
        # Actually let's just read from the data_start
    else:
        print(f"  => DataSection format (f0={test_v0})")
        stream_pos, lbl_start, lbl_count = read_datasection(data, stream_pos, f"L{level}_LABELS")
        # Show first 20 bytes of label data
        print(f"  First 20 label bytes:")
        for i in range(min(20, lbl_count * 8)):
            b = data[lbl_start + i]
            print(f"    [{i}] = {b:3d} (0x{b:02X})")

    # Step 7-8: Weights
    print(f"\n{indent}Step 7-8: WEIGHTS")
    test_v0 = u64(data, stream_pos)
    if test_v0 < 256:
        print(f"  => IntVector format (bitwidth={test_v0})")
        stream_pos, bw, cnt = read_intvector_header(data, stream_pos, f"L{level}_WEIGHTS")
    else:
        print(f"  => DataSection format (f0={test_v0})")
        stream_pos, wt_start, wt_count = read_datasection(data, stream_pos, f"L{level}_WEIGHTS")

    # Step 9: 2 pointers (16 bytes)
    print(f"\n{indent}Step 9: 2 pointers")
    p1 = u64(data, stream_pos)
    p2 = u64(data, stream_pos + 8)
    print(f"  ptr1={p1}, ptr2={p2}")
    stream_pos += 16

    # Step 10: 4 x u32 (16 bytes)
    print(f"\n{indent}Step 10: 4 x u32 metadata")
    m = struct.unpack_from("<4I", data, stream_pos)
    print(f"  metadata: {m}")
    stream_pos += 16

    total_consumed = stream_pos - start_pos
    remaining = file_size - stream_pos
    print(f"\n{indent}Level {level} consumed {total_consumed}B, stream now at file+{stream_pos}, remaining={remaining}B")

    # Step 11: Recurse if remaining
    if remaining > 100:
        print(f"\n{indent}=> RECURSING into sub-level...")
        try_parse_level(data, stream_pos, level + 1, file_size)

    return stream_pos


def scan_all_structures(data):
    """Brute-force scan for known structure signatures."""
    print("\n\n=== BRUTE FORCE STRUCTURE SCAN ===")
    file_size = len(data)

    print("\n--- BV Headers (pattern: [N, small, pop, 0] where N=15614) ---")
    for off in range(0, file_size - 32, 8):
        v0 = u64(data, off)
        v1 = u64(data, off + 8)
        v2 = u64(data, off + 16)
        v3 = u64(data, off + 24)
        if v0 == 15614 and v3 == 0 and 0 < v1 < 100000:
            print(f"  file+{off}: [{v0}, {v1}, {v2}, {v3}]  (rank_bytes={v1*8})")

    print("\n--- DataSections (pattern: [f0>900000, count<100000, f2]) ---")
    for off in range(0, file_size - 24, 8):
        v0 = u64(data, off)
        v1 = u64(data, off + 8)
        v2 = u64(data, off + 16)
        if 900000 < v0 < 10000000 and 0 < v1 < 100000:
            consumed = v1 * 8 + 32
            print(f"  file+{off}: f0={v0}, count={v1}, f2={v2}  (consumes {consumed}B, ends at file+{off+consumed})")

    print("\n--- IntVector Headers (pattern: [bitwidth<64, count<10M]) ---")
    for off in range(0, file_size - 16, 8):
        v0 = u64(data, off)
        v1 = u64(data, off + 8)
        if 0 < v0 < 64 and 100000 < v1 < 10000000:
            consumed = 16 + v1 * 8 + 16
            print(f"  file+{off}: bitwidth={v0}, count={v1}  (consumes {consumed}B)")


def main():
    with open(TRIE_PATH, "rb") as f:
        data = f.read()

    file_size = len(data)
    print(f"Trie file: {file_size} bytes ({file_size/1024/1024:.1f} MB)")

    # 1. Show file header
    scan_file_header(data)

    # 2. Brute-force scan all structures
    scan_all_structures(data)

    # 3. Try sequential parse starting from known BV1 position
    print("\n\n=== SEQUENTIAL PARSE FROM file+130744 ===")
    try:
        try_parse_level(data, 130744, level=0, file_size=file_size)
    except Exception as e:
        print(f"Parse failed: {e}")
        import traceback; traceback.print_exc()

    # 4. Also try from file+8 (after magic)
    print("\n\n=== SEQUENTIAL PARSE FROM file+8 ===")
    # The data at file+8 looks like a DataSection, not a BV header.
    # Maybe the pre-data DS is read BEFORE FUN_00660496 is called.
    # Let's check what's right after it.
    ds_consumed = u64(data, 16) * 8 + 32  # count at offset 16
    after_ds = 8 + ds_consumed
    print(f"Pre-data DS at file+8, count={u64(data,16)}, consumed={ds_consumed}B, ends at file+{after_ds}")
    print(f"Values after pre-data DS:")
    for i in range(8):
        val = u64(data, after_ds + i*8)
        print(f"  file+{after_ds + i*8}: {val} (0x{val:016X})")


if __name__ == "__main__":
    main()
