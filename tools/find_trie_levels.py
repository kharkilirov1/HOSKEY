#!/usr/bin/env python3
"""Phase 1: Find 3 trie level boundaries in the body.

Strategy:
  1. Classify body at 64-byte row granularity
  2. Group consecutive same-type rows into sections
  3. For each BITVEC section, compute popcount
  4. For each LABEL section, compute byte count
  5. Find bitvec-label pairs where popcount ≈ label_byte_count
  6. Look for 3 repeating level structures

Key insight: for a LOUDS trie level, has_child bitvector's popcount should equal
the number of label bytes in the corresponding label section (one label per child edge).
"""
from __future__ import annotations
import struct
import pathlib
import collections
import sys


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def popcount_bytes(data: bytes) -> int:
    return sum(bin(b).count('1') for b in data)


def classify_row(row: bytes) -> str:
    """Classify a 64-byte row."""
    cp = sum(1 for b in row if is_cp1251_byte(b))
    zeros = row.count(0)
    bits = popcount_bytes(row)
    density = bits / (64 * 8)
    cp_ratio = cp / 64
    zero_ratio = zeros / 64

    if cp_ratio > 0.45:
        return "LABEL"
    if zero_ratio > 0.35:
        return "SPARSE"
    if density > 0.20 and density < 0.80 and cp_ratio < 0.12:
        return "BITVEC"
    return "MIXED"


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    body = data[24:payload_hint]
    tail = data[payload_hint:]
    body_rows = len(body) // 64

    print(f"body={len(body)}B rows={body_rows} tail={len(tail)}B item_count={item_count}")

    # --- Step 1: Classify every row ---
    row_classes = []
    for i in range(body_rows):
        row = body[i * 64 : (i + 1) * 64]
        row_classes.append(classify_row(row))

    type_counts = collections.Counter(row_classes)
    print(f"\nRow type counts: {type_counts}")

    # --- Step 2: Group consecutive same-type rows into sections ---
    sections = []
    if row_classes:
        cur_type = row_classes[0]
        cur_start = 0
        for i, rtype in enumerate(row_classes):
            if rtype != cur_type:
                sections.append((cur_start, i, cur_type))
                cur_type = rtype
                cur_start = i
        sections.append((cur_start, len(row_classes), cur_type))

    print(f"Total sections: {len(sections)}")

    # --- Step 3: For each section, compute key statistics ---
    section_stats = []
    for start, end, stype in sections:
        sec_data = body[start * 64 : end * 64]
        size = len(sec_data)
        pc = popcount_bytes(sec_data)
        cp_bytes = sum(1 for b in sec_data if is_cp1251_byte(b))

        section_stats.append({
            "start_row": start,
            "end_row": end,
            "rows": end - start,
            "type": stype,
            "abs_start": 24 + start * 64,
            "abs_end": 24 + end * 64,
            "size": size,
            "popcount": pc,
            "cp_bytes": cp_bytes,
            "bit_density": pc / (size * 8),
        })

    # --- Step 4: Find BITVEC sections and potential matching LABEL sections ---
    bitvec_secs = [s for s in section_stats if s["type"] == "BITVEC"]
    label_secs = [s for s in section_stats if s["type"] == "LABEL"]

    print(f"\nBITVEC sections: {len(bitvec_secs)}")
    print(f"LABEL sections: {len(label_secs)}")

    # Print largest BITVEC and LABEL sections
    bitvec_sorted = sorted(bitvec_secs, key=lambda s: s["size"], reverse=True)
    label_sorted = sorted(label_secs, key=lambda s: s["size"], reverse=True)

    print("\nTop 15 BITVEC sections (by size):")
    for s in bitvec_sorted[:15]:
        print(f"  [{s['abs_start']:7d}, {s['abs_end']:7d}) {s['size']:6d}B  "
              f"popcount={s['popcount']:6d}  density={s['bit_density']:.3f}  "
              f"rows=[{s['start_row']},{s['end_row']})")

    print("\nTop 15 LABEL sections (by size):")
    for s in label_sorted[:15]:
        print(f"  [{s['abs_start']:7d}, {s['abs_end']:7d}) {s['size']:6d}B  "
              f"cp_bytes={s['cp_bytes']:6d}  rows=[{s['start_row']},{s['end_row']})")

    # --- Step 5: Look for bitvec-label pairs ---
    # For each BITVEC section, look for a nearby LABEL section whose cp_bytes ≈ popcount
    print("\n=== BITVEC-LABEL PAIR SEARCH ===")
    print("Looking for BITVEC.popcount ~ LABEL.cp_bytes (+/-20%)")
    matches = []
    for bv in bitvec_secs:
        for lb in label_secs:
            # Only look at sections within reasonable distance (< 100KB)
            if abs(bv["abs_start"] - lb["abs_start"]) > 200000:
                continue
            pc = bv["popcount"]
            cp = lb["cp_bytes"]
            if pc > 0 and cp > 0:
                ratio = cp / pc
                if 0.8 < ratio < 1.2:  # within 20%
                    matches.append((bv, lb, ratio))

    matches.sort(key=lambda m: abs(1.0 - m[2]))
    print(f"Found {len(matches)} potential pairs")
    for bv, lb, ratio in matches[:20]:
        dist = lb["abs_start"] - bv["abs_end"]
        print(f"  BV[{bv['abs_start']},{bv['abs_end']}) pop={bv['popcount']}  <->  "
              f"LB[{lb['abs_start']},{lb['abs_end']}) cp={lb['cp_bytes']}  "
              f"ratio={ratio:.3f}  dist={dist}B")

    # --- Step 6: Analyze the FIRST part of body carefully ---
    # Level 0 (root) should be small and easy to identify
    print("\n=== FIRST 200 ROWS DETAIL ===")
    for i in range(min(200, body_rows)):
        row = body[i * 64 : (i + 1) * 64]
        cp = sum(1 for b in row if is_cp1251_byte(b))
        pc = popcount_bytes(row)
        zeros = row.count(0)
        rtype = row_classes[i]
        # Only print interesting rows
        if i < 20 or rtype == "LABEL" or (rtype != row_classes[max(0, i-1)]):
            # For LABEL rows, show the cp1251 content
            cp_text = ""
            if cp > 10:
                cp_bytes = bytes(b for b in row if is_cp1251_byte(b))
                cp_text = cp_bytes.decode("cp1251", errors="replace")
            abs_off = 24 + i * 64
            print(f"  row[{i:4d}] abs={abs_off:7d} {rtype:7s} pop={pc:3d} cp={cp:2d} z={zeros:2d}"
                  + (f"  text='{cp_text}'" if cp_text else ""))

    # --- Step 7: Check if body has section headers at level boundaries ---
    # In some trie formats, each level starts with a small header (node_count, etc.)
    print("\n=== POTENTIAL LEVEL HEADERS (small SPARSE blocks) ===")
    for s in section_stats:
        if s["type"] == "SPARSE" and s["rows"] <= 3:
            sec_data = body[s["start_row"] * 64 : s["end_row"] * 64]
            u32s = struct.unpack_from(f"<{min(16, len(sec_data)//4)}I", sec_data, 0)
            abs_off = s["abs_start"]
            print(f"  [{abs_off:7d}] {s['rows']} rows: u32s={[hex(v) for v in u32s[:8]]}")

    # --- Step 8: Tail preamble — is it a degree sequence? ---
    print("\n=== TAIL PREAMBLE AS DEGREE SEQUENCE ===")
    preamble = tail[:40000]
    # If it's degree sequence, sum should ≈ total number of child edges
    degree_sum = sum(preamble)
    nonzero = sum(1 for b in preamble if b > 0)
    print(f"  preamble_bytes={len(preamble)} degree_sum={degree_sum} nonzero={nonzero}")
    print(f"  max_byte={max(preamble)} mean={degree_sum/len(preamble):.2f}")

    # Check first 100 values as potential degree sequence
    print(f"  first_100_vals: {list(preamble[:100])}")

    # If it's a LOUDS bitvector in byte form (1=child, 0=boundary):
    # Count runs of 1s (each run = one node's children)
    # But 0x02 is the most common value, not 0/1...

    # Maybe it's variable-length encoded? Check if pairs of bytes make sense
    # Or maybe the preamble IS part of the body structure (just after payload_hint boundary)
    # and the real label data starts later

    # Find where cp1251 heavy data actually starts in tail
    BLOCK = 256
    for bi in range(len(tail) // BLOCK):
        chunk = tail[bi * BLOCK : (bi + 1) * BLOCK]
        cp_ratio = sum(1 for b in chunk if is_cp1251_byte(b)) / len(chunk)
        if cp_ratio > 0.40:
            print(f"\n  First LABEL block in tail: block[{bi}] at tail+{bi*BLOCK} "
                  f"(abs {payload_hint + bi*BLOCK}), cp_ratio={cp_ratio:.2f}")
            # Show content
            sample = bytes(b for b in chunk[:80] if is_cp1251_byte(b))
            print(f"  sample: '{sample.decode('cp1251', errors='replace')}'")
            break

    # --- Step 9: Correlation between body and tail sizes ---
    print("\n=== SIZE CORRELATIONS ===")
    total_bitvec = sum(s["size"] for s in bitvec_secs)
    total_label_body = sum(s["cp_bytes"] for s in label_secs)
    total_bitvec_pop = sum(s["popcount"] for s in bitvec_secs)

    print(f"  Total BITVEC bytes in body: {total_bitvec}")
    print(f"  Total BITVEC popcount: {total_bitvec_pop}")
    print(f"  Total LABEL cp_bytes in body: {total_label_body}")
    print(f"  Total tail label runs (from prev analysis): ~516K runs")
    print(f"  Ratio bitvec_pop/label_cp: {total_bitvec_pop/max(total_label_body,1):.2f}")
    print(f"  item_count={item_count}")
    print(f"  bitvec_pop / item_count = {total_bitvec_pop / item_count:.2f}")

    print("\nDone.")


if __name__ == "__main__":
    main()
