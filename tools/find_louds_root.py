#!/usr/bin/env python3
"""Find the LOUDS root by searching for degree ~30 pattern in bitvectors.

Root of a Russian dictionary trie should have ~30 children (one per letter).
In LOUDS (1=child, 0=end): 30 ones then 0 = 31 bits.
This pattern should appear at the START of the LOUDS bitvector.
"""
from __future__ import annotations
import struct
import pathlib


def bit_at_lsb(data: bytes, pos: int) -> int:
    byte_idx = pos >> 3
    bit_idx = pos & 7
    if byte_idx >= len(data):
        return 0
    return (data[byte_idx] >> bit_idx) & 1


def read_degree_lsb(data: bytes, start_bit: int, max_bits: int) -> tuple[int, int]:
    """Read one LOUDS degree starting at start_bit. Returns (degree, next_bit_pos)."""
    pos = start_bit
    degree = 0
    while pos < max_bits and bit_at_lsb(data, pos) == 1:
        degree += 1
        pos += 1
    if pos < max_bits:
        pos += 1  # skip 0
    return degree, pos


def read_degrees_lsb(data: bytes, start_bit: int, count: int) -> list[int]:
    degrees = []
    pos = start_bit
    max_bits = len(data) * 8
    for _ in range(count):
        d, pos = read_degree_lsb(data, pos, max_bits)
        degrees.append(d)
    return degrees


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\louds_root_search.txt"
    )

    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    body = data[24:payload_hint]
    tail = data[payload_hint:]

    lines = []

    # Known bitvector regions (from rank table analysis)
    bv_regions = [
        ("BV1_before_R1", 5752, 124992),
        ("BV_after_R1", 161992, 124992),
        ("BV2_before_R2", 167752, 124992),
        ("BV3_before_R3", 323944, 124992),
        ("BV4_before_R4", 480136, 124992),
        ("BODY_START", 0, 130744),
        ("BODY_TAIL", 636376, len(body) - 636376),
    ]

    # Also search the raw body from offset 0
    # Search for pattern: >=25 consecutive 1-bits at any bit position
    lines.append("=== SEARCH FOR ROOT-LIKE PATTERN (25-35 consecutive 1-bits) ===\n\n")

    # Scan each BV region for positions where degree is 25-35
    for name, offset, size in bv_regions:
        bv = body[offset:offset + size]
        if len(bv) < 100:
            continue

        candidates = []
        max_check = min(len(bv) * 8, 100000)  # check first 100K bits

        for start_bit in range(max_check):
            d, _ = read_degree_lsb(bv, start_bit, len(bv) * 8)
            if 25 <= d <= 40:
                # Read next few degrees to see if they look trie-like
                degs = read_degrees_lsb(bv, start_bit, 50)
                # A good LOUDS root: first degree 25-35, then mostly non-zero
                nonzero_count = sum(1 for x in degs[1:31] if x > 0)
                total_edges = sum(degs[:50])
                if nonzero_count >= 15 and total_edges > 100:
                    candidates.append((start_bit, degs))

        if candidates:
            lines.append(f"{name} (body+{offset}, {size}B): {len(candidates)} candidates\n")
            for bit, degs in candidates[:5]:
                byte_off = bit // 8
                lines.append(f"  bit {bit} (byte {byte_off}): degrees={degs[:30]}\n")
                lines.append(f"    root_degree={degs[0]} sum50={sum(degs[:50])}\n")
        else:
            lines.append(f"{name}: no root-like pattern in first 100K bits\n")

    # --- Also search the ENTIRE body byte-by-byte for the root pattern ---
    lines.append(f"\n=== BROAD SEARCH (first 10K byte positions, LSB) ===\n")
    broad_hits = []
    for byte_start in range(min(10000, len(body) - 100)):
        bv = body[byte_start:]
        d, next_pos = read_degree_lsb(bv, 0, min(len(bv) * 8, 10000))
        if 28 <= d <= 34:  # expect ~30 for Russian
            degs = read_degrees_lsb(bv, 0, 35)
            nonzero = sum(1 for x in degs[1:d+1] if x > 0)
            if nonzero >= d * 0.6:  # at least 60% of root's children have children
                broad_hits.append((byte_start, degs))

    lines.append(f"Found {len(broad_hits)} hits with root degree 28-34\n")
    for off, degs in broad_hits[:10]:
        abs_off = 24 + off
        lines.append(f"  body+{off} (abs {abs_off}): degrees={degs[:30]}\n")
        lines.append(f"    root={degs[0]} sum35={sum(degs[:35])}\n")

    # --- Try also searching at bit-level within first BV ---
    lines.append(f"\n=== BIT-LEVEL SEARCH IN BV3 (first 50K bits) ===\n")
    bv3 = body[323944:323944 + 124992]
    bv3_hits = []
    for start_bit in range(min(50000, len(bv3) * 8)):
        d, _ = read_degree_lsb(bv3, start_bit, len(bv3) * 8)
        if 28 <= d <= 34:
            degs = read_degrees_lsb(bv3, start_bit, 35)
            nonzero = sum(1 for x in degs[1:d+1] if x > 0)
            if nonzero >= d * 0.6:
                bv3_hits.append((start_bit, degs))

    lines.append(f"Found {len(bv3_hits)} hits in BV3\n")
    for bit, degs in bv3_hits[:10]:
        lines.append(f"  BV3 bit {bit}: degrees={degs[:30]}\n")

    # --- Check body header (first 5752 bytes) for section index ---
    lines.append(f"\n=== BODY HEADER (first 5752 bytes) ===\n")
    hdr = body[:5752]
    # Look for u32 values that could be offsets or sizes
    interesting_u32s = []
    for i in range(0, min(len(hdr) - 3, 200), 4):
        v = struct.unpack_from("<I", hdr, i)[0]
        if v in (124992, 125000, 31248, 31250, 999936, 198733, 801071, 369743, 402824):
            interesting_u32s.append((i, v))
        # Also check for values close to our bitvector offsets
        for known in [5752, 130744, 161992, 167752, 292744, 323944, 448936, 480136, 605128, 636376]:
            if abs(v - known) < 100:
                interesting_u32s.append((i, v, f"~{known}"))

    lines.append(f"Interesting u32s in header: {interesting_u32s}\n")

    # First 200 bytes as u32
    header_u32s = [struct.unpack_from("<I", hdr, i)[0] for i in range(0, min(200, len(hdr)), 4)]
    lines.append(f"First 50 u32s: {[hex(v) for v in header_u32s[:50]]}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")

    # Print key results
    if broad_hits:
        print(f"\nBroad search: {len(broad_hits)} root candidates")
        for off, degs in broad_hits[:3]:
            print(f"  body+{off}: root_degree={degs[0]} next_5={degs[1:6]}")
    if bv3_hits:
        print(f"\nBV3 search: {len(bv3_hits)} root candidates")
        for bit, degs in bv3_hits[:3]:
            print(f"  bit {bit}: root_degree={degs[0]} next_5={degs[1:6]}")


if __name__ == "__main__":
    main()
