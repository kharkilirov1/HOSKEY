#!/usr/bin/env python3
"""Decode words from LOUDS bitvector (BV3) + tail labels.

BV3 at body+323944 (125KB), pop=801071, density=0.80
This is likely the LOUDS bitvector where:
  - 1-bit = child edge
  - 0-bit = end of children for a node
  - nodes = 0-bits = ~199K
  - edges = 1-bits = ~801K

Labels are in tail arena starting at tail+39936.
Each consecutive 1-bit group matches a label group from the arena.
"""
from __future__ import annotations
import struct
import pathlib
import sys


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def cp1251_to_char(b: int) -> str:
    return bytes([b]).decode("cp1251", errors="replace")


def bit_at_lsb(data: bytes, pos: int) -> int:
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
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\decoded_words.txt"
    )

    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    body = data[24:payload_hint]
    tail = data[payload_hint:]

    # --- BV3: LOUDS bitvector ---
    bv3_offset = 323944
    bv3_size = 124992
    louds = body[bv3_offset : bv3_offset + bv3_size]
    total_bits = bv3_size * 8

    # --- BV4: terminal bitvector candidate (pop=402K) ---
    bv4_offset = 480136
    bv4_size = 124992
    terminal_bv = body[bv4_offset : bv4_offset + bv4_size]

    # --- Extract labels from tail arena ---
    arena_start = 39936
    arena = tail[arena_start:]

    # Build flat label array: concatenate all cp1251 bytes in order
    labels = bytearray()
    i = 0
    while i < len(arena):
        if is_cp1251_byte(arena[i]):
            labels.append(arena[i])
            i += 1
        else:
            i += 1

    print(f"LOUDS bits: {total_bits}, labels: {len(labels)}")

    # --- Decode LOUDS degree sequence ---
    degrees = []
    bit_pos = 0
    while bit_pos < total_bits and len(degrees) < 500000:
        degree = 0
        while bit_pos < total_bits and bit_at_lsb(louds, bit_pos) == 1:
            degree += 1
            bit_pos += 1
        if bit_pos < total_bits:
            bit_pos += 1  # skip 0-bit
        degrees.append(degree)

    total_edges = sum(degrees)
    print(f"Nodes: {len(degrees)}, Edges: {total_edges}")
    print(f"First 30 degrees: {degrees[:30]}")
    print(f"Root degree: {degrees[0]}")

    if total_edges > len(labels):
        print(f"WARNING: more edges ({total_edges}) than labels ({len(labels)})")
        print("Labels might not include all edges. Proceeding with available labels.")

    # --- BFS walk to extract words ---
    lines = []
    lines.append(f"nodes={len(degrees)} edges={total_edges} labels={len(labels)}\n")
    lines.append(f"root_degree={degrees[0]}\n")
    lines.append(f"first_30_degrees: {degrees[:30]}\n\n")

    # Determine which nodes are terminal
    # Option A: use BV4 as terminal indicator (1 bit per node)
    # Check terminal for first few nodes
    term_sample = [bit_at_lsb(terminal_bv, i) for i in range(min(50, len(degrees)))]
    lines.append(f"BV4 terminal sample (first 50): {term_sample}\n")
    term_count = sum(bit_at_lsb(terminal_bv, i) for i in range(len(degrees)))
    lines.append(f"BV4 terminal count (in first {len(degrees)} bits): {term_count}\n\n")

    # LOUDS BFS order: node 0 is virtual super-root or real root
    # The children of node i start at label_offset[i] and have degree[i] labels
    label_offset = [0]  # cumulative label offset per node
    cum = 0
    for d in degrees:
        cum += d
        label_offset.append(cum)

    # BFS queue: (node_id, path)
    words_found = []
    queue = [(0, "")]  # start from root

    # In LOUDS, the children of node_id are nodes numbered as:
    # child_base = 1 + sum of degrees of nodes 0..node_id-1
    # Actually in LOUDS: children of node i are at LOUDS positions
    # select0(i) + 1 through select0(i+1) - 1 (the 1-bits between two 0-bits)
    # And these correspond to nodes:
    # first_child(i) = rank0(select0(i) + 1) = i + 1 + sum of degrees before
    #
    # Simpler: in our degree sequence, the children of node 0 are nodes 1..1+degrees[0]-1
    # Children of node 1 are nodes (1+degrees[0]) .. (1+degrees[0]+degrees[1]-1)
    # etc.
    #
    # child_start[i] = 1 + sum(degrees[0:i])

    child_start = [0] * (len(degrees) + 1)
    child_start[0] = 1  # root's children start at node 1
    for i in range(1, len(degrees)):
        child_start[i] = child_start[i - 1] + degrees[i - 1]

    # DFS to extract words
    def dfs(node_id, path, depth):
        if depth > 25:
            return
        if node_id >= len(degrees):
            return

        # Check if terminal
        is_term = bit_at_lsb(terminal_bv, node_id) == 1

        if is_term and len(path) >= 2:
            words_found.append(path)

        degree = degrees[node_id]
        if degree == 0:
            return

        lo = label_offset[node_id]
        for j in range(degree):
            child_node = child_start[node_id] + j
            if child_node >= len(degrees):
                break
            label_idx = lo + j
            if label_idx >= len(labels):
                break
            ch = cp1251_to_char(labels[label_idx])
            dfs(child_node, path + ch, depth + 1)

    # Start DFS from root (node 0) or node 1 if node 0 is super-root
    print("Starting DFS from node 0...")
    dfs(0, "", 0)

    if len(words_found) < 10:
        # Maybe node 0 is a virtual super-root with degree 1
        # Try starting from node 1
        print(f"Only {len(words_found)} words from node 0, trying node 1...")
        words_found.clear()
        # Re-assign: skip the super-root
        for child_id in range(1, 1 + degrees[0]):
            if child_id < len(degrees):
                lo = label_offset[0]
                label_idx = lo + (child_id - 1)
                if label_idx < len(labels):
                    ch = cp1251_to_char(labels[label_idx])
                    dfs(child_id, ch, 1)

    print(f"Words found: {len(words_found)}")

    # Write results
    lines.append(f"\n=== WORDS FOUND: {len(words_found)} ===\n")
    for w in words_found[:500]:
        lines.append(f"  {w}\n")

    # Also try with BV2 as terminal
    bv2_offset = 167752
    bv2 = body[bv2_offset : bv2_offset + bv3_size]
    term2_count = sum(bit_at_lsb(bv2, i) for i in range(min(len(degrees), total_bits)))
    lines.append(f"\n\nBV2 as terminal: count={term2_count}\n")

    # Check if any BV gives ~100K-200K terminal nodes (matching word count)
    for bv_name, bv_off in [("BV1", 5752), ("BV2", 167752), ("BV3_louds", 323944), ("BV4", 480136)]:
        bv_data = body[bv_off : bv_off + bv3_size]
        tc = sum(bit_at_lsb(bv_data, i) for i in range(min(len(degrees), total_bits)))
        lines.append(f"  {bv_name} as terminal over {len(degrees)} nodes: {tc}\n")

    # Show first few words sample regardless
    lines.append(f"\n=== FIRST 50 WORDS ===\n")
    for w in words_found[:50]:
        lines.append(f"  '{w}'\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")


if __name__ == "__main__":
    main()
