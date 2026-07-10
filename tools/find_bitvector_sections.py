#!/usr/bin/env python3
"""Find bitvector sections by detecting rank structure patterns.

The Yandex rank structure (from decompilation):
  - Separate from bitvector bits
  - Layout: every 16 bytes = [L1:u64][L2_packed:u64]
  - L1 = cumulative popcount (monotonically increasing)
  - One entry per 512-bit superblock

Strategy:
  1. Search body for monotonically increasing u64 sequences (L1 tables)
  2. Each L1 table indicates a bitvector's rank structure
  3. Use L1 values to determine bitvector size and popcount
  4. Match bitvectors with tail label data
"""
from __future__ import annotations
import struct
import pathlib
import collections


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\bitvector_sections.txt"
    )

    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    body = data[24:payload_hint]
    tail = data[payload_hint:]

    lines = []
    lines.append(f"body={len(body)}B tail={len(tail)}B\n")

    # --- Total body popcount ---
    total_pop = sum(bin(b).count('1') for b in body)
    lines.append(f"body_total_popcount={total_pop} ({total_pop/len(body)/8*100:.1f}% density)\n")
    lines.append(f"body_total_bits={len(body)*8}\n")

    # --- Search for rank L1 tables ---
    # L1 table: sequence of u64 values where each is >= previous
    # Stored at 16-byte stride (L1 at offset 0, L2 at offset 8, repeat)
    # L1 values should be: 0, pop(first 512 bits), pop(first 1024 bits), ...
    lines.append(f"\n=== RANK L1 TABLE SEARCH ===\n")

    rank_tables = []

    # Scan body for 16-byte aligned monotonically increasing u64 sequences
    for start in range(0, len(body) - 64, 8):
        # Read potential L1 values at 16-byte stride
        vals = []
        pos = start
        while pos + 8 <= len(body) and len(vals) < 2000:
            v = struct.unpack_from("<Q", body, pos)[0]
            if v > 10_000_000:
                break
            if vals and v < vals[-1]:
                break
            vals.append(v)
            pos += 16  # skip L2

        if len(vals) >= 10 and vals[0] <= 1 and vals[-1] > vals[0]:
            # Check if increments are reasonable (each L1 += popcount of 512 bits, so 0-512)
            increments = [vals[i+1] - vals[i] for i in range(len(vals)-1)]
            max_inc = max(increments)
            min_inc = min(increments)
            if max_inc <= 512 and min_inc >= 0:
                total_pc = vals[-1]
                total_bits = len(vals) * 512
                density = total_pc / total_bits if total_bits > 0 else 0
                rank_tables.append({
                    "body_offset": start,
                    "abs_offset": 24 + start,
                    "entries": len(vals),
                    "total_popcount": total_pc,
                    "total_bits": total_bits,
                    "total_bytes": total_bits // 8,
                    "density": density,
                    "first_5_vals": vals[:5],
                    "last_5_vals": vals[-5:],
                    "mean_increment": sum(increments) / len(increments),
                })

    # Deduplicate (many overlapping starts)
    seen_offsets = set()
    unique_tables = []
    for rt in sorted(rank_tables, key=lambda r: r["entries"], reverse=True):
        # Check if we already have a table within 64 bytes
        if any(abs(rt["body_offset"] - s) < 64 for s in seen_offsets):
            continue
        seen_offsets.add(rt["body_offset"])
        unique_tables.append(rt)

    lines.append(f"Found {len(unique_tables)} unique rank L1 tables\n")
    for rt in unique_tables[:30]:
        lines.append(f"\n  body+{rt['body_offset']} (abs {rt['abs_offset']}):\n")
        lines.append(f"    entries={rt['entries']} total_bits={rt['total_bits']} "
                     f"total_bytes={rt['total_bytes']}\n")
        lines.append(f"    total_popcount={rt['total_popcount']} density={rt['density']:.4f}\n")
        lines.append(f"    mean_increment={rt['mean_increment']:.1f}\n")
        lines.append(f"    first_5: {rt['first_5_vals']}\n")
        lines.append(f"    last_5: {rt['last_5_vals']}\n")

    # --- For each rank table, find the corresponding bitvector bits ---
    # The bits should be somewhere in the body too.
    # The rank structure might be BEFORE or AFTER the bits.
    # Total rank table size = entries * 16 bytes
    # Total bitvector size = entries * 64 bytes (512 bits)
    lines.append(f"\n=== RANK TABLE -> BITVECTOR MAPPING ===\n")
    for rt in unique_tables[:10]:
        rank_size = rt["entries"] * 16
        bv_size = rt["entries"] * 64  # 512 bits = 64 bytes per superblock
        rank_end = rt["body_offset"] + rank_size

        lines.append(f"\n  Rank at body+{rt['body_offset']}, size={rank_size}B:\n")
        lines.append(f"    Expected bitvector size: {bv_size}B\n")

        # Check if bitvector is right after rank table
        if rank_end + bv_size <= len(body):
            bv_candidate = body[rank_end:rank_end + bv_size]
            bv_pop = sum(bin(b).count('1') for b in bv_candidate)
            lines.append(f"    After rank: body+{rank_end} pop={bv_pop} "
                         f"(expected {rt['total_popcount']})\n")
            if abs(bv_pop - rt["total_popcount"]) < rt["total_popcount"] * 0.01 + 10:
                lines.append(f"    *** MATCH! bitvector at body+{rank_end} ***\n")

        # Check if bitvector is right before rank table
        bv_before_start = rt["body_offset"] - bv_size
        if bv_before_start >= 0:
            bv_candidate = body[bv_before_start:rt["body_offset"]]
            bv_pop = sum(bin(b).count('1') for b in bv_candidate)
            lines.append(f"    Before rank: body+{bv_before_start} pop={bv_pop} "
                         f"(expected {rt['total_popcount']})\n")
            if abs(bv_pop - rt["total_popcount"]) < rt["total_popcount"] * 0.01 + 10:
                lines.append(f"    *** MATCH! bitvector at body+{bv_before_start} ***\n")

    # --- Cross-reference with label groups ---
    # We know: has_child popcount = number of internal nodes = number of label groups
    # terminal popcount = number of words
    # louds total 1-bits = total edges = total labels
    lines.append(f"\n=== CROSS-REFERENCE WITH LABELS ===\n")
    lines.append(f"  Expected has_child popcount = num label groups = ~500K+\n")
    lines.append(f"  Expected terminal popcount = num words = ~100K-200K\n")
    lines.append(f"  Expected louds 1-bits = total labels = ~1.1M+\n")

    for rt in unique_tables[:15]:
        pc = rt["total_popcount"]
        nb = rt["total_bits"]
        interp = ""
        if 80000 <= pc <= 250000:
            interp = "POSSIBLE terminal"
        elif 300000 <= pc <= 800000:
            interp = "POSSIBLE has_child"
        elif 800000 <= pc <= 2000000:
            interp = "POSSIBLE louds"
        lines.append(f"  body+{rt['body_offset']}: pop={pc} bits={nb} "
                     f"density={rt['density']:.3f} -> {interp}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")

    # Print summary
    print(f"\nFound {len(unique_tables)} rank tables")
    for rt in unique_tables[:15]:
        pc = rt["total_popcount"]
        interp = ""
        if 80000 <= pc <= 250000:
            interp = "TERMINAL?"
        elif 300000 <= pc <= 800000:
            interp = "HAS_CHILD?"
        elif 800000 <= pc <= 2000000:
            interp = "LOUDS?"
        print(f"  body+{rt['body_offset']:6d}: {rt['entries']:5d} entries, "
              f"pop={pc:7d}, bits={rt['total_bits']:8d}, "
              f"density={rt['density']:.3f} {interp}")


if __name__ == "__main__":
    main()
