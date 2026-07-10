#!/usr/bin/env python3
"""R3: Identify all bitvectors by computing popcounts and matching to BV headers.
Then try trie traversal with correct LSB-first 64-bit word bit ordering.
"""
import struct
import sys

TRIE_PATH = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"

def u64(data, off):
    return struct.unpack_from("<Q", data, off)[0]

def popcount64(x):
    x -= (x >> 1) & 0x5555555555555555
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f
    return ((x * 0x0101010101010101) >> 56) & 0xff

def compute_popcount(data, ds_offset, num_words=15614):
    """Compute popcount of a bitvector stored as DataSection at ds_offset."""
    # DS header: [f0:u64, count:u64, f2:u64] then data
    data_start = ds_offset + 24
    total = 0
    for i in range(num_words):
        w = u64(data, data_start + i * 8)
        total += popcount64(w)
    return total

# Bit access: LSB-first within 64-bit words (matches Ghidra decompilation)
def bit_at(bits_data, bits_offset, pos):
    """Read bit at position pos. LSB-first within 64-bit words."""
    word_idx = pos >> 6
    bit_idx = pos & 63
    word_off = bits_offset + word_idx * 8
    if word_off + 8 > len(bits_data):
        return 0
    w = u64(bits_data, word_off)
    return (w >> bit_idx) & 1

def rank1(data, bits_offset, pos, num_words=15614):
    """Count 1-bits in positions [0, pos). LSB-first 64-bit words."""
    if pos <= 0:
        return 0
    full_words = pos >> 6
    rem = pos & 63
    count = 0
    for i in range(min(full_words, num_words)):
        w = u64(data, bits_offset + i * 8)
        count += popcount64(w)
    if rem > 0 and full_words < num_words:
        w = u64(data, bits_offset + full_words * 8)
        count += popcount64(w & ((1 << rem) - 1))
    return count

def select1(data, bits_offset, n, num_words=15614):
    """Find position of the (n+1)-th 1-bit (0-based n). Returns -1 if not found."""
    # n is 0-based: select1(bv, 0) returns position of first 1-bit
    count = 0
    for i in range(num_words):
        w = u64(data, bits_offset + i * 8)
        pc = popcount64(w)
        if count + pc > n:
            # The target bit is in this word
            target = n - count  # we need the (target)-th 1-bit in this word (0-based)
            for b in range(64):
                if (w >> b) & 1:
                    if target == 0:
                        return i * 64 + b
                    target -= 1
        count += pc
    return -1

def main():
    with open(TRIE_PATH, "rb") as f:
        data = f.read()

    file_size = len(data)
    print(f"File size: {file_size} bytes")

    # Known DataSection offsets (bitvector bits)
    ds_offsets = [8, 162008, 324008, 480200]
    ds_names = ["DS0(file+8)", "DS1(file+162008)", "DS2(file+324008)", "DS3(file+480200)"]

    # Known BV headers
    bv_headers = [
        (130744, "BV1"), (292744, "BV2"), (448936, "BV3"), (605128, "BV4")
    ]

    print("\n=== BV HEADER POPCOUNTS ===")
    for off, name in bv_headers:
        nw = u64(data, off)
        rs = u64(data, off + 8)
        pop = u64(data, off + 16)
        z = u64(data, off + 24)
        print(f"  {name} @ {off}: num_words={nw}, rank_size={rs}, popcount={pop}, zero={z}")

    print("\n=== DATASECTION COMPUTED POPCOUNTS ===")
    for i, (off, name) in enumerate(zip(ds_offsets, ds_names)):
        f0 = u64(data, off)
        count = u64(data, off + 8)
        f2 = u64(data, off + 16)
        pc = compute_popcount(data, off, count)
        print(f"  {name}: f0={f0}, count={count}, computed_pop={pc}")

    # Check if DS0's f2 has a special meaning
    print(f"\n=== DS0 DETAILS ===")
    print(f"  f0={u64(data,8)}, count={u64(data,16)}, f2={u64(data,24)} (0x{u64(data,24):016X})")
    # f2 for DS0 = 0xFFFFFFFFFFFFFFFB = -5 as signed. Interesting.

    print(f"\n=== METADATA AT 636392 ===")
    for i in range(8):
        off = 636392 + i * 8
        val = u64(data, off)
        print(f"  file+{off}: {val} (0x{val:016X})")

    # Read first 50 bytes of label data
    print(f"\n=== LABEL DATA AT 636424 ===")
    LABEL_MAP = {}
    for i in range(32): LABEL_MAP[i] = chr(0x0410 + i)      # A-Ya
    for i in range(16): LABEL_MAP[32 + i] = chr(0x0430 + i)  # a-p
    for i in range(16): LABEL_MAP[96 + i] = chr(0x0440 + i)  # r-ya
    LABEL_MAP[112] = 'Yo'; LABEL_MAP[113] = 'yo'

    print("  First 50 label bytes:")
    for i in range(50):
        b = data[636424 + i]
        ch = LABEL_MAP.get(b, f"[{b}]")
        print(f"    node[{i}] = {b:3d} => {ch}")

    # Count how many bytes in 636424..999284 are valid CP866 Cyrillic
    total_labels = 999284 - 636424
    cyrillic_count = 0
    for i in range(total_labels):
        b = data[636424 + i]
        if b in LABEL_MAP:
            cyrillic_count += 1
    print(f"\n  Total label bytes: {total_labels}")
    print(f"  Valid CP866 Cyrillic: {cyrillic_count} ({100*cyrillic_count/total_labels:.1f}%)")

    # Byte distribution
    from collections import Counter
    dist = Counter(data[636424:999284])
    print(f"  Top 15 byte values:")
    for val, cnt in dist.most_common(15):
        ch = LABEL_MAP.get(val, f"[{val}]")
        print(f"    byte {val:3d} ({ch:>4s}): {cnt:6d} ({100*cnt/total_labels:.1f}%)")

    # Now try traversal
    # From R1_ANALYSIS: child = select1(LOUDS, rank1(has_child, node_id) - 1)
    # Terminal check: bit in terminal BV

    # We need to figure out which BVs are LOUDS, has_child, terminal
    # BV1 and BV2 have same popcount (369743) - likely LOUDS and has_child
    # BV3 (801071) and BV4 (402824) are different

    # Total nodes estimate:
    # LOUDS has one 1-bit per non-root node + one 0-bit per node = total_nodes + total_nodes - 1 = 2*N-1
    # has_child has one bit per node, popcount = number of internal nodes
    # terminal has one bit per node, popcount = number of terminal (word-ending) nodes

    # With 999296 total bits (15614 * 64):
    # If LOUDS pop = 369743, then total_nodes ~ 369743 (each node contributes one 1-bit to LOUDS)
    # Nah, LOUDS: for each node, put as many 1s as children, then 0.
    # So LOUDS popcount = sum of children = total_nodes - 1 (every node except root has a parent)
    # Total LOUDS bits = popcount + total_nodes (one 0 per node)
    # If pop = 369743, total LOUDS bits = 369743 + total_nodes
    # But we have 999296 bits, so 369743 + total_nodes = 999296 => total_nodes = 629553
    #
    # has_child pop = 369743 means 369743 out of 629553 nodes have children
    # => 629553 - 369743 = 259810 leaf nodes

    # BV3 pop=801071, BV4 pop=402824 don't fit this model well
    # BV3 pop > total bits? No, 801071 < 999296. But 801071 > 629553 (estimated nodes)
    # If BV3 is terminal, it would mean 801071 terminal nodes out of 629553 total. Impossible!

    # Unless BV3 and BV4 belong to a different trie level (sub-trie)

    print(f"\n=== TRAVERSAL TEST ===")
    print(f"Estimated structure:")
    print(f"  If LOUDS pop=369743: total_nodes = 999296 - 369743 = {999296-369743}")
    print(f"  If has_child pop=369743: leaf_nodes = {999296-369743} - 369743 = {999296-2*369743}")
    print(f"  If terminal pop from DS0 computed above")

    # Let's identify: which DS is LOUDS? which is has_child?
    # Try DS1 as LOUDS, DS2 as has_child (both from BV1/BV2 with same pop=369743)

    # Data offsets for bits (after 24-byte DS header)
    bits_offsets = {
        "DS0": 8 + 24,
        "DS1": 162008 + 24,
        "DS2": 324008 + 24,
        "DS3": 480200 + 24,
    }

    NUM_WORDS = 15614
    TOTAL_BITS = NUM_WORDS * 64  # 999296
    LABELS_START = 636424

    # Try each pair of (LOUDS, has_child) from DS0..DS3
    # For terminal, try each remaining DS

    # Diagnostic: print bit[0] and first u64 of each DS
    print(f"\n=== BIT[0] AND FIRST WORD OF EACH DS ===")
    for dsname, dsoff in bits_offsets.items():
        first_word = u64(data, dsoff)
        b0 = first_word & 1
        leading_ones = 0
        for b in range(64):
            if (first_word >> b) & 1:
                leading_ones += 1
            else:
                break
        print(f"  {dsname} @ {dsoff}: first_u64=0x{first_word:016X}, bit[0]={b0}, leading_1s={leading_ones}")

    # Generate ALL 4*3*2 = 24 permutations of (LOUDS, HC, TERM) from 4 DSes
    import itertools
    ds_keys = list(bits_offsets.keys())
    combos = []
    for perm in itertools.permutations(ds_keys, 3):
        louds_name, hc_name, term_name = perm
        combos.append((
            f"{louds_name}=LOUDS, {hc_name}=HC, {term_name}=TERM",
            bits_offsets[louds_name], bits_offsets[hc_name], bits_offsets[term_name]
        ))

    for name, louds_off, hc_off, term_off in combos:
        print(f"\n--- {name} ---")

        # Check root: node 0 in LOUDS should start with some 1s then a 0
        root_children = 0
        pos = 0
        while bit_at(data, louds_off, pos) == 1:
            root_children += 1
            pos += 1
            if root_children > 100:
                break
        print(f"  Root children (LOUDS leading 1s): {root_children}")

        if root_children == 0 or root_children > 64:
            print(f"  SKIP: unreasonable root children count")
            continue

        # Try DFS traversal
        # LOUDS navigation:
        #   To find children of node_id:
        #   1. Check has_child[node_id]
        #   2. child_rank = rank1(has_child, node_id)  (count of 1s before this node)
        #   3. child_start = select1(LOUDS, child_rank) + 1  (NOTE: +1 to skip the 0 separator? Or not?)
        #      Actually in LOUDS: select0(LOUDS, child_rank) + 1
        #      No wait, LOUDS encodes: for each node, children count 1s then a 0
        #      So select0(LOUDS, node_id+1) gives the position just before node_id's children

        # Actually, for LOUDS-based succinct trie:
        # - LOUDS bit string: for each node in BFS order, write degree 1-bits then a 0-bit
        # - To find first child of node v: select0(LOUDS, v) + 1
        #   (select0 gives the position of v's own 0-bit, +1 is first child's 1-bit)
        # - Children run until the next 0-bit
        # - Child node index = rank1(LOUDS, position_of_child_1bit)

        # But R1_ANALYSIS says: child = select1(LOUDS, rank1(has_child, node_id) - 1)
        # This is a different formula! Let me re-read.

        # From R1_ANALYSIS:
        # rank1(has_child_bv, node_id) => gets child rank
        # select1(louds_bv, child_rank - 1) => gets child position

        # Wait this doesn't match standard LOUDS. Let me just try both formulas.

        words = []
        visited = 0
        max_visit = 200000

        def dfs_r1(node_id, prefix, depth):
            """Using R1_ANALYSIS formula: select1(LOUDS, rank1(has_child, node_id) - 1)"""
            nonlocal visited
            if depth > 20 or visited > max_visit or len(words) > 50:
                return
            visited += 1

            # Get label
            if node_id > 0 and node_id < total_labels:
                b = data[LABELS_START + node_id]
                ch = LABEL_MAP.get(b)
                if ch is None:
                    return  # non-cyrillic label, skip
                prefix = prefix + ch

            # Terminal check
            if bit_at(data, term_off, node_id) and len(prefix) >= 2:
                words.append(prefix.lower())

            # Children check
            if not bit_at(data, hc_off, node_id):
                return  # leaf

            # R1 formula: child_start = select1(LOUDS, rank1(has_child, node_id) - 1)
            hc_rank = rank1(data, hc_off, node_id + 1, NUM_WORDS)  # rank1 up to and including node_id
            if hc_rank == 0:
                return

            first_child_pos = select1(data, louds_off, hc_rank - 1, NUM_WORDS)
            if first_child_pos < 0:
                return

            # Enumerate children: starting from first_child_pos, each consecutive 1-bit is a child
            # The child's node_id = rank1(LOUDS, child_pos)
            pos = first_child_pos
            while bit_at(data, louds_off, pos) == 1:
                child_id = rank1(data, louds_off, pos + 1, NUM_WORDS)
                if child_id > node_id and child_id < TOTAL_BITS:
                    dfs_r1(child_id, prefix, depth + 1)
                pos += 1
                if pos >= TOTAL_BITS:
                    break

        try:
            visited = 0
            words = []
            dfs_r1(0, "", 0)
        except Exception as e:
            print(f"  ERROR: {e}")
            continue

        real_words = [w for w in words if len(w) >= 3]
        known = {"привет", "слово", "мама", "папа", "дом", "кот", "мир", "день",
                 "год", "вода", "рука", "город", "время", "жизнь", "работа",
                 "человек", "земля", "книга", "школа", "друг", "дело", "место"}
        found_known = [w for w in real_words if w in known]

        print(f"  visited={visited}, total_words={len(words)}, real_words={len(real_words)}, "
              f"known_matches={len(found_known)}")
        if found_known:
            print(f"  KNOWN WORDS FOUND: {found_known[:10]}")
        if real_words:
            print(f"  First 15: {real_words[:15]}")
        elif words:
            print(f"  Raw words: {words[:10]}")


if __name__ == "__main__":
    main()
