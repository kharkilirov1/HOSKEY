#!/usr/bin/env python3
"""Try to decode the trie by matching body bitvectors with tail labels.

Strategy:
  1. Parse tail label arena -> degree sequence (cp1251 run lengths)
  2. Build expected LOUDS bitvector from degree sequence
  3. Compare expected LOUDS bits with body bits at various offsets
  4. If match found: walk the trie to extract words
"""
from __future__ import annotations
import struct
import pathlib
import collections
import sys


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def cp1251_to_char(b: int) -> str:
    return bytes([b]).decode("cp1251", errors="replace")


def popcount_byte(b: int) -> int:
    return bin(b).count('1')


def bit_at(data: bytes, pos: int) -> int:
    """Get bit at position pos (LSB-first within each byte)."""
    byte_idx = pos >> 3
    bit_idx = pos & 7
    if byte_idx >= len(data):
        return 0
    return (data[byte_idx] >> bit_idx) & 1


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\louds_decode_result.txt"
    )

    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    body = data[24:payload_hint]
    tail = data[payload_hint:]

    lines = []
    lines.append(f"body={len(body)}B tail={len(tail)}B item_count={item_count}\n")

    # --- Step 1: Extract label runs from tail ---
    # Find where label arena starts (first dense cp1251 block)
    arena_start = None
    BLOCK = 256
    for bi in range(len(tail) // BLOCK):
        chunk = tail[bi * BLOCK : (bi + 1) * BLOCK]
        cp_ratio = sum(1 for b in chunk if is_cp1251_byte(b)) / len(chunk)
        if cp_ratio > 0.40:
            arena_start = bi * BLOCK
            break

    lines.append(f"label_arena_start=tail+{arena_start} (abs {payload_hint + arena_start})\n")

    # Extract label groups from arena
    arena = tail[arena_start:]
    label_groups = []  # list of (offset, [cp1251 bytes])
    i = 0
    while i < len(arena) and len(label_groups) < 500000:
        if is_cp1251_byte(arena[i]):
            start = i
            group = []
            while i < len(arena) and is_cp1251_byte(arena[i]):
                group.append(arena[i])
                i += 1
            label_groups.append((arena_start + start, group))
        else:
            i += 1

    degrees = [len(g) for _, g in label_groups]
    lines.append(f"label_groups={len(label_groups)} total_labels={sum(degrees)}\n")
    lines.append(f"degree_dist: {collections.Counter(degrees).most_common(15)}\n")
    lines.append(f"first_20_degrees: {degrees[:20]}\n")
    lines.append(f"first_group_labels: {''.join(cp1251_to_char(b) for b in label_groups[0][1])}\n")

    # --- Step 2: Build expected LOUDS from degrees ---
    # LOUDS format (1=child, 0=end-of-children):
    #   For root with degree d0: d0 ones, then 0
    #   For next node with degree d1: d1 ones, then 0
    #   etc.
    # Total bits = sum(degrees) + num_nodes

    # But we don't know which groups correspond to internal nodes vs all nodes.
    # In LOUDS, every node (including leaves) has a degree entry.
    # Leaves have degree 0 -> just a single 0-bit.
    #
    # So the full LOUDS includes 0-degree entries for leaves.
    # But our label groups only contain non-zero degree groups (runs >= 1 char).
    #
    # We need to interleave 0-degree entries (leaves) with the non-zero groups.
    # The LOUDS bitvector itself tells us where the leaves are.

    # --- Step 3: Try direct LOUDS decode from body ---
    # Read body as LOUDS bitvector and see what degrees we get
    lines.append(f"\n=== LOUDS DECODE FROM BODY (multiple start offsets) ===\n")

    best_match = None
    best_match_score = 0

    for start_byte in [0, 8, 16, 24, 32, 64, 128, 256, 512, 1024, 2048, 4096]:
        bv = body[start_byte:]
        if len(bv) < 1000:
            continue

        # Read degrees from LOUDS
        decoded_degrees = []
        bit_pos = 0
        max_bits = min(len(bv) * 8, 2000000)

        while bit_pos < max_bits and len(decoded_degrees) < 300000:
            # Count consecutive 1-bits (= degree of this node)
            degree = 0
            while bit_pos < max_bits and bit_at(bv, bit_pos) == 1:
                degree += 1
                bit_pos += 1
            # Skip the terminating 0-bit
            if bit_pos < max_bits:
                bit_pos += 1
            decoded_degrees.append(degree)

            # Safety: if first node has unreasonable degree, skip
            if len(decoded_degrees) == 1 and (degree < 2 or degree > 100):
                break

        if len(decoded_degrees) < 10:
            continue

        # Compare non-zero degrees with label groups
        nonzero_decoded = [d for d in decoded_degrees if d > 0]
        match_count = 0
        compare_len = min(len(nonzero_decoded), len(degrees), 200)
        for j in range(compare_len):
            if nonzero_decoded[j] == degrees[j]:
                match_count += 1

        match_pct = match_count / max(compare_len, 1) * 100

        if match_pct > best_match_score:
            best_match_score = match_pct
            best_match = start_byte

        root_degree = decoded_degrees[0] if decoded_degrees else -1
        total_decoded = len(decoded_degrees)
        lines.append(f"  offset={start_byte}: root_degree={root_degree} "
                     f"total_nodes={total_decoded} "
                     f"nonzero_match={match_count}/{compare_len} ({match_pct:.1f}%)\n")

        if root_degree >= 2 and root_degree <= 50:
            lines.append(f"    first_20_decoded: {decoded_degrees[:20]}\n")
            lines.append(f"    first_20_label_g: {degrees[:20]}\n")

    lines.append(f"\nbest_match: offset={best_match} score={best_match_score:.1f}%\n")

    # --- Step 4: Try inverted LOUDS (0=child, 1=end) ---
    lines.append(f"\n=== INVERTED LOUDS (0=child, 1=end-of-children) ===\n")

    for start_byte in [0, 8, 16, 24, 32, 64, 128, 256, 512, 1024]:
        bv = body[start_byte:]
        if len(bv) < 1000:
            continue

        decoded_degrees = []
        bit_pos = 0
        max_bits = min(len(bv) * 8, 2000000)

        while bit_pos < max_bits and len(decoded_degrees) < 300000:
            degree = 0
            while bit_pos < max_bits and bit_at(bv, bit_pos) == 0:
                degree += 1
                bit_pos += 1
            if bit_pos < max_bits:
                bit_pos += 1
            decoded_degrees.append(degree)

            if len(decoded_degrees) == 1 and (degree < 2 or degree > 100):
                break

        if len(decoded_degrees) < 10:
            continue

        nonzero_decoded = [d for d in decoded_degrees if d > 0]
        match_count = 0
        compare_len = min(len(nonzero_decoded), len(degrees), 200)
        for j in range(compare_len):
            if nonzero_decoded[j] == degrees[j]:
                match_count += 1

        match_pct = match_count / max(compare_len, 1) * 100
        root_degree = decoded_degrees[0] if decoded_degrees else -1

        if root_degree >= 2 and root_degree <= 50:
            lines.append(f"  offset={start_byte}: root_degree={root_degree} "
                         f"match={match_count}/{compare_len} ({match_pct:.1f}%)\n")
            lines.append(f"    first_20_decoded: {decoded_degrees[:20]}\n")

    # --- Step 5: Try LOUDS with MSB-first bit ordering ---
    lines.append(f"\n=== LOUDS MSB-FIRST ===\n")

    def bit_at_msb(data, pos):
        byte_idx = pos >> 3
        bit_idx = 7 - (pos & 7)
        if byte_idx >= len(data):
            return 0
        return (data[byte_idx] >> bit_idx) & 1

    for start_byte in [0, 8, 16, 24, 32, 64, 128, 256, 512, 1024]:
        bv = body[start_byte:]
        if len(bv) < 1000:
            continue

        decoded_degrees = []
        bit_pos = 0
        max_bits = min(len(bv) * 8, 2000000)

        while bit_pos < max_bits and len(decoded_degrees) < 300000:
            degree = 0
            while bit_pos < max_bits and bit_at_msb(bv, bit_pos) == 1:
                degree += 1
                bit_pos += 1
            if bit_pos < max_bits:
                bit_pos += 1
            decoded_degrees.append(degree)

            if len(decoded_degrees) == 1 and (degree < 2 or degree > 100):
                break

        if len(decoded_degrees) < 10:
            continue

        nonzero_decoded = [d for d in decoded_degrees if d > 0]
        match_count = 0
        compare_len = min(len(nonzero_decoded), len(degrees), 200)
        for j in range(compare_len):
            if nonzero_decoded[j] == degrees[j]:
                match_count += 1

        match_pct = match_count / max(compare_len, 1) * 100
        root_degree = decoded_degrees[0] if decoded_degrees else -1

        if root_degree >= 2 and root_degree <= 50:
            lines.append(f"  offset={start_byte}: root_degree={root_degree} "
                         f"match={match_count}/{compare_len} ({match_pct:.1f}%)\n")
            lines.append(f"    first_20_decoded: {decoded_degrees[:20]}\n")
            lines.append(f"    first_20_label_g: {degrees[:20]}\n")

    # --- Step 6: Try using tail preamble as degree sequence directly ---
    lines.append(f"\n=== TAIL PREAMBLE AS DEGREE SEQUENCE ===\n")
    preamble = tail[:arena_start]
    lines.append(f"preamble_size={len(preamble)}\n")

    # Maybe each byte in preamble IS the degree of a node?
    preamble_degrees = list(preamble)
    nonzero_preamble = [d for d in preamble_degrees if d > 0]
    match_count = 0
    compare_len = min(len(nonzero_preamble), len(degrees), 200)
    for j in range(compare_len):
        if j < len(nonzero_preamble) and j < len(degrees):
            if nonzero_preamble[j] == degrees[j]:
                match_count += 1
    match_pct = match_count / max(compare_len, 1) * 100
    lines.append(f"  byte-as-degree match: {match_count}/{compare_len} ({match_pct:.1f}%)\n")
    lines.append(f"  first_30_preamble: {preamble_degrees[:30]}\n")
    lines.append(f"  first_30_nonzero: {nonzero_preamble[:30]}\n")
    lines.append(f"  first_30_label_degrees: {degrees[:30]}\n")

    # Maybe preamble is variable-length encoded degrees
    # Try: pairs of bytes as u16 LE
    preamble_u16 = [struct.unpack_from("<H", preamble, i)[0] for i in range(0, min(len(preamble) - 1, 200), 2)]
    lines.append(f"\n  first_50_u16: {preamble_u16[:50]}\n")

    # --- Step 7: What if labels come before bitvectors in body? ---
    # Check if body has label data at specific offset that matches tail labels
    lines.append(f"\n=== SEARCH BODY FOR LABEL DATA MATCHING TAIL ===\n")
    # The first label group should be the root's children: ~30 cp1251 sorted bytes
    first_group = bytes(label_groups[0][1])
    lines.append(f"first_label_group ({len(first_group)} bytes): "
                 f"{''.join(cp1251_to_char(b) for b in first_group)}\n")
    lines.append(f"  hex: {first_group.hex()}\n")

    # Search for this sequence in the body
    search_bytes = first_group
    found_in_body = []
    for off in range(len(body) - len(search_bytes)):
        if body[off:off + len(search_bytes)] == search_bytes:
            found_in_body.append(off)
    lines.append(f"  found_in_body: {found_in_body[:10]}\n")

    # Also search for a shorter prefix (first 10 bytes of first group)
    if len(search_bytes) > 10:
        short_search = search_bytes[:10]
        found_short = []
        for off in range(len(body) - len(short_search)):
            if body[off:off + len(short_search)] == short_search:
                found_short.append(off)
        lines.append(f"  short_prefix_found: {found_short[:10]}\n")

    # Search in tail too
    found_in_tail = []
    for off in range(len(tail) - len(search_bytes)):
        if tail[off:off + len(search_bytes)] == search_bytes:
            found_in_tail.append(off)
    lines.append(f"  found_in_tail: {found_in_tail[:10]}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")
    # Print key results
    for line in lines:
        if "best_match" in line or "root_degree" in line or "match=" in line:
            print(line.strip())


if __name__ == "__main__":
    main()
