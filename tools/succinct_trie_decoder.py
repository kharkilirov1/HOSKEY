#!/usr/bin/env python3
"""Full succinct trie decoder for 1nc7 format.

Uses 4 identified bitvector+rank sections + label data + CP866 mapping.
Based on Ghidra decompilation of libjni_ykeyboard3.so runtime struct.
"""
import struct
import pathlib
import sys


# CP866 label_id -> Russian character
LABEL_MAP = {}
for i in range(32):
    LABEL_MAP[i] = chr(0x0410 + i)  # А-Я
for i in range(16):
    LABEL_MAP[32 + i] = chr(0x0430 + i)  # а-п
for i in range(16):
    LABEL_MAP[96 + i] = chr(0x0440 + i)  # р-я
LABEL_MAP[112] = 'Ё'
LABEL_MAP[113] = 'ё'

# Simplified: 0-32 → а-я lowercase
SIMPLE_MAP = {}
for i in range(32):
    SIMPLE_MAP[i] = chr(0x0430 + i)  # а-я
SIMPLE_MAP[32] = 'ё'


def popcount64(x):
    x = x - ((x >> 1) & 0x5555555555555555)
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f
    return ((x * 0x0101010101010101) >> 56) & 0xff


def bit_at(data, pos):
    byte_idx = pos >> 3
    bit_idx = pos & 7
    if byte_idx >= len(data):
        return 0
    return (data[byte_idx] >> bit_idx) & 1


def rank1(bv_data, rank_data, pos, total_bits):
    """Rank1 using L1/L2 superblock structure.

    rank_data layout: for each superblock i (0..N):
      [L1: u64 at i*16]   = cumulative popcount up to superblock i
      [L2: u64 at i*16+8] = packed 9-bit local popcounts

    L1[N] (last entry) = total popcount.
    Bitvector bits are in bv_data (separate from rank_data).
    """
    if pos == 0:
        return 0

    num_entries = len(rank_data) // 16
    if num_entries == 0:
        return 0

    if pos >= total_bits:
        # Total popcount = last L1 value
        return struct.unpack_from("<Q", rank_data, (num_entries - 1) * 16)[0]

    sb_idx = pos // 512
    if sb_idx >= num_entries:
        return struct.unpack_from("<Q", rank_data, (num_entries - 1) * 16)[0]

    l1 = struct.unpack_from("<Q", rank_data, sb_idx * 16)[0]
    l2_packed = struct.unpack_from("<Q", rank_data, sb_idx * 16 + 8)[0]

    word_in_sb = (pos >> 6) & 7
    l2 = 0
    if word_in_sb > 0:
        l2 = (l2_packed >> ((word_in_sb - 1) * 9)) & 0x1FF

    word_idx = pos >> 6
    bit_in_word = pos & 63
    pc = 0
    if bit_in_word > 0:
        word_off = word_idx * 8
        if word_off + 8 <= len(bv_data):
            word = struct.unpack_from("<Q", bv_data, word_off)[0]
            mask = (1 << bit_in_word) - 1
            pc = popcount64(word & mask)

    return l1 + l2 + pc


def select0(bv_data, total_bits, target_rank):
    """Find position of the (target_rank+1)-th 0-bit."""
    count = 0
    num_words = (total_bits + 63) // 64
    for i in range(num_words):
        off = i * 8
        if off + 8 > len(bv_data):
            break
        w = struct.unpack_from("<Q", bv_data, off)[0]
        zeros = 64 - popcount64(w)
        if i * 64 + 64 > total_bits:
            valid = total_bits - i * 64
            mask = (1 << valid) - 1 if valid < 64 else 0xFFFFFFFFFFFFFFFF
            zeros = valid - popcount64(w & mask)
            w = w | (~mask & 0xFFFFFFFFFFFFFFFF)

        if count + zeros > target_rank:
            target = target_rank - count
            for b in range(64):
                if not ((w >> b) & 1):
                    if target == 0:
                        return i * 64 + b
                    target -= 1
        count += zeros
    return -1


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\succinct_decoded_words.txt"
    )

    data = trie_path.read_bytes()
    h = struct.unpack_from("<6I", data, 0)
    body = data[24:h[2]]

    TOTAL_BITS = 999936  # 1953 * 512

    # BV+Rank sections (from find_bitvector_sections.py)
    bv1_data = body[5752:5752+124992]
    r1_data = body[130744:130744+31248]

    bv2_data = body[167752:167752+124992]
    r2_data = body[292744:292744+31248]

    bv3_data = body[323992:323992+124992]  # adjusted: R2 end = 323992
    r3_data = body[448936:448936+31248]

    bv4_data = body[480184:480184+124992]  # adjusted: R3 end = 480184
    r4_data = body[605128:605128+31248]

    # Label data: remaining body after BV4+R4
    label_start = 636376
    label_data = body[label_start:]
    print(f"label_data size: {len(label_data)}")

    # Skip label header (3 x u64 = 24 bytes)
    label_array = label_data[24:]
    print(f"label_array size: {len(label_array)}")

    # Assignment: BV1=has_child, BV2=terminal, BV3=LOUDS, BV4=aux
    has_child_bv = bv1_data
    has_child_rank = r1_data
    terminal_bv = bv2_data
    terminal_rank = r2_data
    louds_bv = bv3_data
    louds_rank = r3_data

    # Count nodes (0-bits in LOUDS)
    louds_pop = rank1(louds_bv, louds_rank, TOTAL_BITS, TOTAL_BITS)
    node_count = TOTAL_BITS - louds_pop
    print(f"LOUDS pop (edges): {louds_pop}, nodes (0-bits): {node_count}")

    terminal_pop = rank1(terminal_bv, terminal_rank, TOTAL_BITS, TOTAL_BITS)
    has_child_pop = rank1(has_child_bv, has_child_rank, TOTAL_BITS, TOTAL_BITS)
    print(f"terminal pop: {terminal_pop}, has_child pop: {has_child_pop}")

    # --- Try to traverse the LOUDS trie ---
    # LOUDS traversal:
    # - 0-bit at position p corresponds to node with index rank0(louds, p)
    # - Children of node i: find the i-th 0-bit (select0(louds, i)),
    #   then children are the 1-bits that follow until next 0-bit
    # - Each 1-bit at position p in LOUDS is an edge with label = label_array[rank1(louds, p)]

    # First, let's check what the root looks like
    # Root is node 0. Its position in LOUDS = select0(louds, 0)
    root_pos = select0(louds_bv, TOTAL_BITS, 0)
    print(f"Root position in LOUDS: {root_pos}")

    # Children of root: 1-bits after root_pos until next 0-bit
    if root_pos >= 0:
        child_start = root_pos + 1
        root_children = 0
        pos = child_start
        while pos < TOTAL_BITS and bit_at(louds_bv, pos) == 1:
            root_children += 1
            pos += 1
        print(f"Root children: {root_children}")

        # Get labels for root's children
        root_labels = []
        for j in range(root_children):
            edge_pos = child_start + j
            edge_rank = rank1(louds_bv, louds_rank, edge_pos, TOTAL_BITS)
            if edge_rank < len(label_array):
                label_id = label_array[edge_rank]
                ch = LABEL_MAP.get(label_id, SIMPLE_MAP.get(label_id, f"[{label_id}]"))
                root_labels.append((label_id, ch))
        print(f"Root labels: {root_labels[:40]}")

    # --- DFS traversal to extract words ---
    print("\nStarting DFS traversal...")
    words = []
    max_words = 1000  # Start small

    def get_children(node_idx):
        """Get children of node_idx as list of (child_node_idx, label_char)."""
        node_pos = select0(louds_bv, TOTAL_BITS, node_idx)
        if node_pos < 0:
            return []

        children = []
        pos = node_pos + 1
        while pos < TOTAL_BITS and bit_at(louds_bv, pos) == 1:
            edge_rank = rank1(louds_bv, louds_rank, pos, TOTAL_BITS)
            # Child node index = rank0(louds, pos+1) but since pos is a 1-bit,
            # the child node is identified by finding the next 0-bit after all siblings
            # Actually, in LOUDS: the j-th child of node i occupies position
            # select0(louds, i) + 1 + j. The child's node index is:
            # node_index = rank1(louds, select0(louds, i) + 1) + j - node_index_base
            # This is getting complex. Use simpler mapping:
            # child_node_idx = number of 0-bits before or at the next 0-bit position

            # Standard LOUDS: child j of node i is node number
            # (number of 1-bits before position select0(louds, i)) + j + 1
            # = rank1(louds, select0(louds, i)) + j + 1
            # But this is only true for BFS numbering

            # Simpler: we know the first child position. Count backwards to get node index.
            # Actually, child node index = sum of 0-bits up to the child's own 0-bit
            # For LOUDS position child_1_bit_pos, we need to find the child's 0-bit
            # to get the child's node index.

            # In LOUDS BFS: if parent is node i, children are at positions
            # select0(i)+1 through select0(i+1)-1 (all 1-bits).
            # The child's node index j starts at rank1(louds, select0(i)) + 1

            if edge_rank < len(label_array):
                label_id = label_array[edge_rank]
                ch = LABEL_MAP.get(label_id, SIMPLE_MAP.get(label_id, f"[{label_id}]"))
            else:
                ch = "?"

            # Child node index: the base is rank1(louds, node_pos+1)
            # For child offset j: child_node = base + j
            base = 0
            if len(children) == 0:
                base_rank1 = rank1(louds_bv, louds_rank, node_pos + 1, TOTAL_BITS)
                # In standard LOUDS: first child of node i has node index =
                # rank1(louds, select0(louds, i) + 1)
                # Wait, that counts edges, not nodes. Let me use rank0.
                # Actually: first child node index = rank0(louds, select0(louds,i)+1)
                # = (select0(louds,i)+1) - rank1(louds, select0(louds,i)+1)
                # But select0(louds,i) is a 0-bit, so:
                # first_child_node = i + 1 + rank1(louds, node_pos)
                # Hmm, this isn't standard either.
                #
                # Standard LOUDS: children of node i are nodes
                # y, y+1, ..., y+d-1 where y = rank1(louds, select0(louds,i)+1) + 1
                # No wait: children are numbered sequentially as they appear in BFS
                # child j of node i has node index = num_nodes_before + j
                # where num_nodes_before = total 0-bits before child's group
                #
                # Simplest correct formula:
                # first_child = rank1(louds, select0(louds, i) + 1)
                # (this counts all edges before this child group, which equals
                #  the number of nodes up to and including the parent's BFS position)
                # Wait no. Let me just use: first_child_node = popcount of 0-bits...
                #
                # Actually the simplest way:
                # In LOUDS, node numbering follows 0-bit order.
                # Node i's 0-bit is at position select0(louds, i).
                # The edges (1-bits) after this 0-bit are the children.
                # Each child is itself a node whose 0-bit comes later.
                #
                # The children of node i are nodes:
                #   rank0(louds, p) for p = each 1-bit position after select0(i)
                # But rank0 of a 1-bit position counts 0-bits before it.
                # As we scan left to right, each 1-bit is followed by more structure.
                # The child's node index is determined by where its 0-bit falls.
                #
                # Standard formula: first_child(i) = rank1(louds, select0(louds,i)+1)
                # This gives the ordinal position of the first child's node.
                # But this counts 1-bits, which are edges, not nodes.
                #
                # I think the correct formula is more complex. Let me just compute:
                base = rank1(louds_bv, louds_rank, node_pos + 1, TOTAL_BITS)

            child_idx = base + len(children)
            children.append((child_idx, ch))
            pos += 1

        return children

    def is_terminal(node_idx):
        """Check if node is a terminal (word end)."""
        if node_idx >= TOTAL_BITS:
            return False
        return bit_at(terminal_bv, node_idx) == 1

    # DFS
    stack = [(0, "")]  # (node_idx, current_word)
    visited = 0

    while stack and len(words) < max_words:
        node_idx, word = stack.pop()
        visited += 1

        if visited > 500000:
            break

        if is_terminal(node_idx) and len(word) >= 2:
            words.append(word.lower())

        children = get_children(node_idx)
        for child_idx, ch in reversed(children):
            if len(word) < 25:
                stack.append((child_idx, word + ch))

    print(f"Visited {visited} nodes, found {len(words)} words")

    # Write results
    lines = [f"visited={visited} words={len(words)}\n\n"]
    for w in words[:500]:
        lines.append(f"{w}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")

    if words:
        print(f"\nFirst 30 words:")
        for w in words[:30]:
            print(f"  {w}")


if __name__ == "__main__":
    main()
