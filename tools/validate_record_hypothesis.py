#!/usr/bin/env python3
"""Validate record-based trie hypothesis.

Hypothesis:
  - Header: 6 x uint32 (24 bytes)
  - Body (24..payload_hint): 64-byte rows, each containing 4 x 16-byte records
  - Each record: [u64:8][tail_offset:u32][padding:u32]
  - Tail (payload_hint..EOF): cp1251 text fragments separated by operator bytes
  - Words are reconstructed from consecutive tail fragments within a record range
"""
from __future__ import annotations
import struct
import sys
import pathlib
import collections


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def decode_cp1251_run(data: bytes, start: int) -> tuple[str, int]:
    """Extract a cp1251 Cyrillic run starting at `start`. Returns (text, end_pos)."""
    i = start
    while i < len(data) and is_cp1251_byte(data[i]):
        i += 1
    if i == start:
        return "", start
    return data[start:i].decode("cp1251", errors="replace"), i


def extract_tail_text(tail: bytes, offset: int, next_offset: int) -> str:
    """Extract all cp1251 text from a tail slice, concatenating runs."""
    chunk = tail[offset:next_offset]
    parts = []
    i = 0
    while i < len(chunk):
        if is_cp1251_byte(chunk[i]):
            text, end = decode_cp1251_run(chunk, i)
            if text:
                parts.append(text)
            i = end
        else:
            i += 1
    return "".join(parts)


def extract_first_cp_run(tail: bytes, offset: int, max_len: int = 200) -> str:
    """Extract just the first cp1251 run from a tail offset."""
    end = min(offset + max_len, len(tail))
    i = offset
    while i < end and is_cp1251_byte(tail[i]):
        i += 1
    if i == offset:
        return ""
    return tail[offset:i].decode("cp1251", errors="replace")


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    if not trie_path.exists():
        print(f"ERROR: {trie_path} not found")
        sys.exit(1)

    data = trie_path.read_bytes()
    print(f"file_size={len(data)}")

    # --- Header ---
    header = struct.unpack_from("<6I", data, 0)
    magic, version, payload_hint, _, item_count, _ = header
    print(f"magic=0x{magic:08X} version={version} payload_hint={payload_hint} item_count={item_count}")

    if magic != 0x37636E31:
        print("ERROR: not a 1nc7 trie file")
        sys.exit(1)

    # --- Body analysis ---
    body = data[24:payload_hint]
    tail = data[payload_hint:]
    body_rows = len(body) // 64
    print(f"body_size={len(body)} body_rows={body_rows} tail_size={len(tail)}")

    # --- Parse ALL records ---
    all_records = []  # list of (row_idx, slot, u64_val, tail_offset, padding)
    for row_idx in range(body_rows):
        row_data = body[row_idx * 64 : (row_idx + 1) * 64]
        for slot in range(4):
            rec = row_data[slot * 16 : (slot + 1) * 16]
            u64_val = struct.unpack_from("<Q", rec, 0)[0]
            tail_off = struct.unpack_from("<I", rec, 8)[0]
            padding = struct.unpack_from("<I", rec, 12)[0]
            all_records.append((row_idx, slot, u64_val, tail_off, padding))

    print(f"total_records={len(all_records)}")

    # --- Basic statistics on records ---
    offsets = [r[3] for r in all_records]
    paddings = [r[4] for r in all_records]
    valid_offsets = [o for o in offsets if 0 < o < len(tail)]
    zero_paddings = sum(1 for p in paddings if p == 0)

    print(f"valid_tail_offsets={len(valid_offsets)}/{len(offsets)}")
    print(f"zero_paddings={zero_paddings}/{len(paddings)}")
    if valid_offsets:
        print(f"offset_range=[{min(valid_offsets)}, {max(valid_offsets)}]")

    # --- Check what's at tail offsets ---
    first_byte_counts = collections.Counter()
    cp_leading_count = 0
    for off in valid_offsets[:5000]:
        fb = tail[off]
        first_byte_counts[fb] += 1
        if is_cp1251_byte(fb):
            cp_leading_count += 1

    print(f"\nfirst_byte_at_offsets (sample {min(len(valid_offsets), 5000)}):")
    print(f"  cp1251_leading={cp_leading_count}")
    print(f"  top_first_bytes={first_byte_counts.most_common(15)}")

    # --- Strategy 1: consecutive offsets as word boundaries ---
    print("\n=== STRATEGY 1: consecutive offset pairs -> full cp1251 text ===")
    words_s1 = []
    for i in range(min(len(all_records) - 1, 50000)):
        _, _, _, off_a, _ = all_records[i]
        _, _, _, off_b, _ = all_records[i + 1]
        if 0 < off_a < len(tail) and off_b > off_a and off_b - off_a < 500:
            text = extract_tail_text(tail, off_a, off_b)
            if 2 <= len(text) <= 30:
                words_s1.append(text)

    print(f"  extracted={len(words_s1)}")
    if words_s1:
        print(f"  first_30: {words_s1[:30]}")
        # Check if they look like real words
        real_looking = sum(1 for w in words_s1 if len(w) >= 3 and w.isalpha())
        print(f"  alpha_3plus={real_looking}/{len(words_s1)}")

    # --- Strategy 2: first cp1251 run at each offset ---
    print("\n=== STRATEGY 2: first cp1251 run at each unique offset ===")
    seen_offsets = set()
    words_s2 = []
    for _, _, _, off, _ in all_records:
        if off in seen_offsets or off <= 0 or off >= len(tail):
            continue
        seen_offsets.add(off)
        text = extract_first_cp_run(tail, off)
        if 2 <= len(text) <= 30:
            words_s2.append((off, text))

    print(f"  unique_offsets={len(seen_offsets)} extracted={len(words_s2)}")
    if words_s2:
        # Sort by offset to see the natural order
        words_s2.sort()
        print(f"  first_30: {[w for _, w in words_s2[:30]]}")
        real_looking = sum(1 for _, w in words_s2 if len(w) >= 3)
        print(f"  len_3plus={real_looking}/{len(words_s2)}")

    # --- Strategy 3: examine record ranges (known from AnalyzeBasinIR) ---
    print("\n=== STRATEGY 3: known record ranges from C++ code ===")
    record_ranges = [
        (2043, 2531, "R1"),
        (4574, 5062, "R2"),
        (7015, 7503, "R3"),
        (9455, 9943, "R4"),
    ]
    for start_row, end_row, label in record_ranges:
        if end_row > body_rows:
            print(f"  {label}: out of range (need {end_row}, have {body_rows})")
            continue
        range_records = []
        for row_idx in range(start_row, end_row):
            row_data = body[row_idx * 64 : (row_idx + 1) * 64]
            for slot in range(4):
                rec = row_data[slot * 16 : (slot + 1) * 16]
                tail_off = struct.unpack_from("<I", rec, 8)[0]
                range_records.append(tail_off)

        valid = [o for o in range_records if 0 < o < len(tail)]
        print(f"  {label}: rows [{start_row},{end_row}) records={len(range_records)} valid_offsets={len(valid)}")
        if valid:
            print(f"    offset_range=[{min(valid)}, {max(valid)}]")
            # Extract first cp1251 run for first 20 valid offsets
            samples = []
            for off in valid[:40]:
                text = extract_first_cp_run(tail, off)
                if text:
                    samples.append(text)
            if samples:
                print(f"    first_cp_runs: {samples[:20]}")

    # --- Strategy 4: u64 field analysis ---
    print("\n=== STRATEGY 4: u64 field structure ===")
    # Check if u64 encodes length, flags, or character data
    sample_u64s = [(r[2], r[3]) for r in all_records[:20] if r[3] > 0 and r[3] < len(tail)]
    for u64_val, off in sample_u64s[:10]:
        b = u64_val.to_bytes(8, "little")
        text = extract_first_cp_run(tail, off, 40)
        print(f"  u64={u64_val:#018x} bytes={b.hex()} off={off} text='{text}'")

    # --- Strategy 5: look for monotonically increasing offsets within rows ---
    print("\n=== STRATEGY 5: row-local offset patterns ===")
    monotonic_rows = 0
    for row_idx in range(min(body_rows, 15000)):
        row_data = body[row_idx * 64 : (row_idx + 1) * 64]
        offs = []
        for slot in range(4):
            rec = row_data[slot * 16 : (slot + 1) * 16]
            offs.append(struct.unpack_from("<I", rec, 8)[0])
        if all(offs[i] <= offs[i + 1] for i in range(3)):
            monotonic_rows += 1

    print(f"  monotonic_offset_rows={monotonic_rows}/{min(body_rows, 15000)}")

    # --- Strategy 6: try to interpret body differently ---
    # Maybe the body is NOT 4x16-byte records per row
    # Maybe it's bitvector data and the record structure is elsewhere
    print("\n=== STRATEGY 6: alternative body interpretations ===")
    # Check byte value distribution in first 1000 rows of body
    sample_body = body[:min(64000, len(body))]
    byte_dist = collections.Counter(sample_body)
    zero_pct = byte_dist[0] / len(sample_body) * 100
    cp_pct = sum(byte_dist[b] for b in range(0xC0, 0x100)) / len(sample_body) * 100
    low_pct = sum(byte_dist[b] for b in range(0, 32)) / len(sample_body) * 100
    print(f"  body_byte_dist: zeros={zero_pct:.1f}% cp1251_range={cp_pct:.1f}% low_control={low_pct:.1f}%")
    print(f"  top_bytes={byte_dist.most_common(20)}")

    # Check bit density
    bits_set = sum(bin(b).count('1') for b in sample_body)
    density = bits_set / (len(sample_body) * 8)
    print(f"  bit_density={density:.4f}")

    print("\nDone.")


if __name__ == "__main__":
    main()
