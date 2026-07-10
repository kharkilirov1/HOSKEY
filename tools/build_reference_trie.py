#!/usr/bin/env python3
"""Build a reference trie from extracted_words.txt and compare metrics with the binary file.

If the trie metrics match the file structure, we can reverse-engineer the encoding.
"""
from __future__ import annotations
import struct
import pathlib
import collections
import sys


class TrieNode:
    __slots__ = ("children", "is_terminal", "depth")
    def __init__(self, depth=0):
        self.children: dict[str, TrieNode] = {}
        self.is_terminal = False
        self.depth = depth


def build_trie(words):
    root = TrieNode(0)
    for word in words:
        node = root
        for i, ch in enumerate(word):
            if ch not in node.children:
                node.children[ch] = TrieNode(i + 1)
            node = node.children[ch]
        node.is_terminal = True
    return root


def trie_stats(root):
    """Compute comprehensive trie statistics."""
    total_nodes = 0
    terminal_nodes = 0
    internal_nodes = 0
    leaf_nodes = 0
    total_edges = 0
    max_depth = 0
    degree_dist = collections.Counter()
    depth_dist = collections.Counter()
    label_chars = collections.Counter()

    stack = [root]
    while stack:
        node = stack.pop()
        total_nodes += 1
        degree = len(node.children)
        degree_dist[degree] += 1
        depth_dist[node.depth] += 1
        total_edges += degree
        if node.depth > max_depth:
            max_depth = node.depth

        if node.is_terminal:
            terminal_nodes += 1

        if degree == 0:
            leaf_nodes += 1
        else:
            internal_nodes += 1

        for ch, child in node.children.items():
            label_chars[ch] += 1
            stack.append(child)

    return {
        "total_nodes": total_nodes,
        "terminal_nodes": terminal_nodes,
        "internal_nodes": internal_nodes,
        "leaf_nodes": leaf_nodes,
        "total_edges": total_edges,
        "max_depth": max_depth,
        "degree_dist": degree_dist,
        "depth_dist": depth_dist,
        "label_chars": label_chars,
    }


def louds_sizes(stats):
    """Estimate LOUDS-encoded sizes."""
    n = stats["total_nodes"]
    e = stats["total_edges"]

    # LOUDS bitvector: for each node, degree 1-bits then one 0-bit = e + n bits
    louds_bits = e + n
    louds_bytes = (louds_bits + 7) // 8

    # has_child bitvector: n bits
    has_child_bytes = (n + 7) // 8

    # terminal bitvector: n bits
    terminal_bytes = (n + 7) // 8

    # labels: one byte per edge
    label_bytes = e

    # rank structure: 16 bytes per 512-bit superblock
    def rank_size(bits):
        superblocks = (bits + 511) // 512
        return superblocks * 16

    return {
        "louds_bits": louds_bits,
        "louds_bytes": louds_bytes,
        "louds_rank": rank_size(louds_bits),
        "has_child_bytes": has_child_bytes,
        "has_child_rank": rank_size(n),
        "terminal_bytes": terminal_bytes,
        "terminal_rank": rank_size(n),
        "label_bytes": label_bytes,
        "total_bitvector_bytes": louds_bytes + has_child_bytes + terminal_bytes,
        "total_rank_bytes": rank_size(louds_bits) + rank_size(n) * 2,
        "total_estimated": louds_bytes + has_child_bytes + terminal_bytes +
                          rank_size(louds_bits) + rank_size(n) * 2 + label_bytes,
    }


def main():
    words_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\extracted_words.txt"
    )
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )

    # Load words
    words_text = words_path.read_text(encoding="utf-8")
    words = [w.strip() for w in words_text.split("\n") if w.strip()]
    print(f"Loaded {len(words)} words")
    print(f"Word length stats: min={min(len(w) for w in words)} max={max(len(w) for w in words)} "
          f"mean={sum(len(w) for w in words)/len(words):.1f}")

    # Build trie
    root = build_trie(words)
    stats = trie_stats(root)

    print(f"\n=== REFERENCE TRIE STATS ===")
    print(f"  total_nodes:    {stats['total_nodes']}")
    print(f"  terminal_nodes: {stats['terminal_nodes']}")
    print(f"  internal_nodes: {stats['internal_nodes']}")
    print(f"  leaf_nodes:     {stats['leaf_nodes']}")
    print(f"  total_edges:    {stats['total_edges']}")
    print(f"  max_depth:      {stats['max_depth']}")

    print(f"\n  Degree distribution (top 15):")
    for degree, count in sorted(stats["degree_dist"].items()):
        if count >= 10 or degree <= 5:
            print(f"    degree {degree:3d}: {count:7d} nodes")

    print(f"\n  Depth distribution:")
    for depth in sorted(stats["depth_dist"].keys()):
        count = stats["depth_dist"][depth]
        print(f"    depth {depth:2d}: {count:7d} nodes")

    print(f"\n  Unique label chars: {len(stats['label_chars'])}")
    print(f"  Top labels: {stats['label_chars'].most_common(15)}")

    # Estimate LOUDS sizes
    sizes = louds_sizes(stats)
    print(f"\n=== ESTIMATED LOUDS SIZES ===")
    for key, val in sizes.items():
        print(f"  {key}: {val}")

    # Compare with file
    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    body_size = payload_hint - 24
    tail_size = len(data) - payload_hint

    print(f"\n=== FILE COMPARISON ===")
    print(f"  body_size:  {body_size} bytes")
    print(f"  tail_size:  {tail_size} bytes")
    print(f"  item_count: {item_count}")
    print(f"  ")
    print(f"  Ref total_nodes:  {stats['total_nodes']}")
    print(f"  Ref total_edges:  {stats['total_edges']}")
    print(f"  Ref terminal:     {stats['terminal_nodes']}")
    print(f"  ")
    print(f"  Est total_bv:     {sizes['total_bitvector_bytes']} bytes")
    print(f"  Est total_rank:   {sizes['total_rank_bytes']} bytes")
    print(f"  Est labels:       {sizes['label_bytes']} bytes")
    print(f"  Est total:        {sizes['total_estimated']} bytes")
    print(f"  ")
    print(f"  body / est_bv+rank = {body_size / max(sizes['total_bitvector_bytes'] + sizes['total_rank_bytes'], 1):.2f}")
    print(f"  tail / est_labels  = {tail_size / max(sizes['label_bytes'], 1):.2f}")
    print(f"  file / est_total   = {len(data) / max(sizes['total_estimated'], 1):.2f}")

    # Root degree check
    root_degree = len(root.children)
    root_labels = sorted(root.children.keys())
    print(f"\n=== ROOT NODE ===")
    print(f"  degree: {root_degree}")
    print(f"  labels: {''.join(root_labels)}")

    # LOUDS bitvector first bytes
    # Root has root_degree children, so LOUDS starts with root_degree 1-bits then a 0
    louds_first_bits = [1] * root_degree + [0]
    # Then for each of root's children (in order), their children count 1-bits + 0
    for ch in root_labels:
        child = root.children[ch]
        d = len(child.children)
        louds_first_bits.extend([1] * d + [0])

    # Convert to bytes
    louds_bytes_preview = []
    for i in range(0, min(len(louds_first_bits), 64), 8):
        byte_val = 0
        for j in range(8):
            if i + j < len(louds_first_bits) and louds_first_bits[i + j]:
                byte_val |= (1 << j)  # LSB first
        louds_bytes_preview.append(byte_val)

    print(f"\n  Expected LOUDS first {len(louds_bytes_preview)} bytes (LSB-first):")
    print(f"    hex: {''.join(f'{b:02x}' for b in louds_bytes_preview)}")
    print(f"    vals: {[hex(b) for b in louds_bytes_preview]}")

    # Compare with body first bytes
    body = data[24:payload_hint]
    print(f"\n  Actual body first {len(louds_bytes_preview)} bytes:")
    print(f"    hex: {body[:len(louds_bytes_preview)].hex()}")
    print(f"    vals: {[hex(b) for b in body[:len(louds_bytes_preview)]]}")

    # Try MSB-first too
    louds_bytes_msb = []
    for i in range(0, min(len(louds_first_bits), 64), 8):
        byte_val = 0
        for j in range(8):
            if i + j < len(louds_first_bits) and louds_first_bits[i + j]:
                byte_val |= (1 << (7 - j))  # MSB first
        louds_bytes_msb.append(byte_val)

    print(f"\n  Expected LOUDS first bytes (MSB-first):")
    print(f"    hex: {''.join(f'{b:02x}' for b in louds_bytes_msb)}")

    # Try has_child: 1 for every node that has children
    # In BFS order, the first node (root) has children, then each child of root...
    # This would start with a dense block of 1s
    print(f"\n  has_child first pattern: all 1s for first {stats['internal_nodes']} nodes")
    print(f"  Expected: 0xFF... for first {stats['internal_nodes'] // 8} bytes")


if __name__ == "__main__":
    main()
