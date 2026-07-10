#!/usr/bin/env python3
"""Test each BV as LOUDS: which gives root degree ~30?
Also test body[0:5752] and inverted conventions."""
import struct, pathlib

def bit_at(data, pos):
    if pos >> 3 >= len(data): return 0
    return (data[pos >> 3] >> (pos & 7)) & 1

def read_degree(data, start_bit, total_bits, one_is_child=True):
    """Read LOUDS degree starting at a 0-bit (node)."""
    pos = start_bit
    if one_is_child:
        # Skip the 0-bit (node marker), then count 1-bits (children)
        if bit_at(data, pos) != 0:
            return -1, pos  # not a node
        pos += 1
        d = 0
        while pos < total_bits and bit_at(data, pos) == 1:
            d += 1; pos += 1
        return d, pos
    else:
        # Inverted: skip 1-bit (node), count 0-bits (children)
        if bit_at(data, pos) != 1:
            return -1, pos
        pos += 1
        d = 0
        while pos < total_bits and bit_at(data, pos) == 0:
            d += 1; pos += 1
        return d, pos

def test_louds(data, total_bits, label, one_is_child):
    """Test data as LOUDS bitvector."""
    conv = "1=child" if one_is_child else "0=child"
    # Find root: first 0-bit (or 1-bit if inverted)
    root_bit = 0 if one_is_child else 1
    root_pos = -1
    for i in range(min(100, total_bits)):
        if bit_at(data, i) == (0 if one_is_child else 1):
            root_pos = i
            break
    if root_pos < 0:
        return

    d, next_pos = read_degree(data, root_pos, total_bits, one_is_child)
    if 25 <= d <= 40:
        # Read next 10 degrees
        degrees = [d]
        pos = next_pos
        for _ in range(30):
            dd, pos = read_degree(data, pos, total_bits, one_is_child)
            if dd < 0: break
            degrees.append(dd)
        print(f"  ** {label} ({conv}): ROOT at bit {root_pos}, degree={d}")
        print(f"     first_20_degrees: {degrees[:20]}")
        return True
    return False

def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    data = trie_path.read_bytes()
    body = data[24:struct.unpack_from("<I", data, 8)[0]]

    bvs = [
        ("BV_header", body[0:5752], 5752*8),
        ("BV1", body[5752:5752+124992], 999936),
        ("BV2", body[167752:167752+124992], 999936),
        ("BV3", body[323992:323992+124992], 999936),
        ("BV4", body[480184:480184+124992], 999936),
        ("BV_header+BV1", body[0:130744], 130744*8),
        ("FULL_BODY", body, len(body)*8),
    ]

    for name, bv_data, total in bvs:
        for one_child in [True, False]:
            test_louds(bv_data, total, name, one_child)

    # Also try starting from various bit offsets within each BV
    print("\n=== OFFSET SCAN (looking for root degree 28-34) ===")
    for name, bv_data, total in bvs[:5]:
        for start_bit in range(min(200, total)):
            for one_child in [True, False]:
                d, _ = read_degree(bv_data, start_bit, total, one_child)
                if 28 <= d <= 34:
                    conv = "1=child" if one_child else "0=child"
                    # Read more degrees
                    degrees = [d]
                    pos = _
                    for _ in range(20):
                        dd, pos = read_degree(bv_data, pos, total, one_child)
                        if dd < 0: break
                        degrees.append(dd)
                    nonzero = sum(1 for x in degrees[1:d+1] if x > 0)
                    if nonzero >= 15:  # at least half of root's children have children
                        print(f"  {name} ({conv}) bit {start_bit}: root={d} "
                              f"nonzero_children={nonzero}/{min(d,len(degrees)-1)}")
                        print(f"    degrees: {degrees[:20]}")

if __name__ == "__main__":
    main()
