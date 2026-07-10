#!/usr/bin/env python3
"""R3: Pure LOUDS traversal using DS0 only.

LOUDS encoding: for each node in BFS order, write degree(node) 1-bits then a 0-bit.
Navigation without has_child bitvector.

Node k's children:
  start_pos = select0(LOUDS, k) + 1
  while LOUDS[start_pos] == 1:
    child_id = rank0(LOUDS, start_pos) + 1   # BFS order
    start_pos += 1
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

# LSB-first within 64-bit words (verified from Ghidra decompilation)
def bit_at(data, base_off, pos):
    w = u64(data, base_off + (pos >> 6) * 8)
    return (w >> (pos & 63)) & 1

# Precompute rank0 and select0 using block-based approach for speed
class BitVector:
    def __init__(self, data, base_off, num_words, total_bits):
        self.data = data
        self.base = base_off
        self.num_words = num_words
        self.total_bits = total_bits
        # Build rank0 blocks (per 64-bit word)
        self.rank0_blocks = [0] * (num_words + 1)
        for i in range(num_words):
            w = u64(data, base_off + i * 8)
            pc = popcount64(w)
            self.rank0_blocks[i + 1] = self.rank0_blocks[i] + (64 - pc)

    def bit(self, pos):
        if pos >= self.total_bits:
            return 0
        w = u64(self.data, self.base + (pos >> 6) * 8)
        return (w >> (pos & 63)) & 1

    def rank0(self, pos):
        """Count 0-bits in [0, pos)."""
        if pos <= 0:
            return 0
        if pos >= self.total_bits:
            return self.rank0_blocks[self.num_words]
        word_idx = pos >> 6
        rem = pos & 63
        count = self.rank0_blocks[word_idx]
        if rem > 0:
            w = u64(self.data, self.base + word_idx * 8)
            ones = popcount64(w & ((1 << rem) - 1))
            count += rem - ones
        return count

    def rank1(self, pos):
        """Count 1-bits in [0, pos)."""
        return pos - self.rank0(pos) if pos > 0 else 0

    def select0(self, n):
        """Find position of n-th 0-bit (0-indexed). Returns -1 if not found."""
        # Binary search on rank0_blocks
        lo, hi = 0, self.num_words
        while lo < hi:
            mid = (lo + hi) // 2
            if self.rank0_blocks[mid + 1] <= n:
                lo = mid + 1
            else:
                hi = mid
        # Now lo is the word containing the n-th 0-bit
        if lo >= self.num_words:
            return -1
        w = u64(self.data, self.base + lo * 8)
        target = n - self.rank0_blocks[lo]  # need target-th 0-bit in this word
        for b in range(64):
            if not ((w >> b) & 1):
                if target == 0:
                    return lo * 64 + b
                target -= 1
        return -1

    def select1(self, n):
        """Find position of n-th 1-bit (0-indexed). Returns -1 if not found."""
        # Binary search: find word where cumulative 1-count crosses n
        # rank1_blocks[i] = i*64 - rank0_blocks[i]
        lo, hi = 0, self.num_words
        while lo < hi:
            mid = (lo + hi) // 2
            r1 = (mid + 1) * 64 - self.rank0_blocks[mid + 1]
            if r1 <= n:
                lo = mid + 1
            else:
                hi = mid
        if lo >= self.num_words:
            return -1
        w = u64(self.data, self.base + lo * 8)
        r1_before = lo * 64 - self.rank0_blocks[lo]
        target = n - r1_before
        for b in range(64):
            if (w >> b) & 1:
                if target == 0:
                    return lo * 64 + b
                target -= 1
        return -1


def main():
    with open(TRIE_PATH, "rb") as f:
        data = f.read()

    NUM_WORDS = 15614
    TOTAL_BITS = 999284  # f0 of DS0
    LABELS_START = 636424
    LABELS_END = 999284
    TOTAL_LABELS = LABELS_END - LABELS_START  # 362860

    # Build CP866 label map
    LABEL_MAP = {}
    for i in range(32): LABEL_MAP[i] = chr(0x0410 + i)      # A-Ya uppercase
    for i in range(16): LABEL_MAP[32 + i] = chr(0x0430 + i)  # a-p lowercase
    for i in range(16): LABEL_MAP[96 + i] = chr(0x0440 + i)  # r-ya lowercase
    LABEL_MAP[112] = chr(0x0401)  # Yo
    LABEL_MAP[113] = chr(0x0451)  # yo

    # DS offsets (data start = DS header + 24)
    DS0_OFF = 8 + 24   # = 32
    DS1_OFF = 162008 + 24  # = 162032
    DS2_OFF = 324008 + 24  # = 324032
    DS3_OFF = 480200 + 24  # = 480224

    print("Building bitvectors...")
    bv_ds0 = BitVector(data, DS0_OFF, NUM_WORDS, TOTAL_BITS)
    bv_ds1 = BitVector(data, DS1_OFF, NUM_WORDS, TOTAL_BITS)
    bv_ds2 = BitVector(data, DS2_OFF, NUM_WORDS, TOTAL_BITS)
    bv_ds3 = BitVector(data, DS3_OFF, NUM_WORDS, TOTAL_BITS)

    # Verify DS0 LOUDS structure
    print(f"\n=== DS0 LOUDS ANALYSIS ===")
    root_sep = bv_ds0.select0(0)
    root_degree = root_sep  # bits 0..root_sep-1 are all 1s
    print(f"Root separator at pos {root_sep}, degree={root_degree}")

    # Show first few nodes' degrees
    print(f"\nFirst 10 nodes (BFS order):")
    for node in range(10):
        sep = bv_ds0.select0(node)
        if node == 0:
            start = 0
        else:
            prev_sep = bv_ds0.select0(node - 1)
            start = prev_sep + 1
        degree = sep - start
        # Label for this node
        if node < TOTAL_LABELS:
            lbl_byte = data[LABELS_START + node]
            lbl_char = LABEL_MAP.get(lbl_byte, f"[{lbl_byte}]")
        else:
            lbl_char = "?"
        print(f"  Node {node}: label={lbl_char} (byte={data[LABELS_START+node] if node<TOTAL_LABELS else -1}), "
              f"degree={degree}, LOUDS[{start}..{sep}]")

    # Pure LOUDS traversal: BFS to find children, DFS to build words
    print(f"\n=== PURE LOUDS DFS TRAVERSAL ===")

    words = []
    known = {"привет", "слово", "мама", "папа", "дом", "кот", "мир", "день",
             "год", "вода", "рука", "город", "время", "жизнь", "работа",
             "человек", "земля", "книга", "школа", "друг", "дело", "место",
             "если", "что", "как", "при", "для", "все", "они", "она", "его"}

    def get_children(node_id):
        """Get children of node_id using pure LOUDS."""
        sep = bv_ds0.select0(node_id)
        if sep < 0:
            return []
        if node_id == 0:
            start = 0
        else:
            prev_sep = bv_ds0.select0(node_id - 1)
            start = prev_sep + 1
        degree = sep - start
        if degree <= 0:
            return []
        # Children node_ids = rank0(start_pos) + 1, rank0(start_pos+1) + 1, ...
        # Actually: i-th child at LOUDS pos (start + i) has node_id = rank0(start + i) + 1
        # Because rank0 counts 0-bits (=nodes) before this position, and +1 for the child itself
        # Wait, let me think again...
        # The children of node k are the nodes whose parent is k
        # In BFS LOUDS, node k's first child id = rank1(select0(k) + 1) ... no
        #
        # Simpler: first_child_id = 1 + sum(degrees of nodes 0..k-1)
        #        = 1 + rank1(LOUDS, select0(k-1) + 1)  for k > 0
        #        = 1  for k = 0
        #
        # Actually: first_child_id = 1 + total edges before this node's edges
        # = 1 + rank1(LOUDS, start)
        first_child = 1 + bv_ds0.rank1(start)
        return list(range(first_child, first_child + degree))

    # Also try: with terminal detection from DS2 (pop=801071, ~80% of nodes)
    # or DS3 (pop=402824, ~64% of nodes)
    # For now, try both: treat EVERY node as terminal (collect all paths)

    # DFS
    visited = 0
    MAX_VISIT = 500000
    MAX_WORDS = 200

    def dfs(node_id, prefix, depth):
        nonlocal visited
        if depth > 20 or visited > MAX_VISIT or len(words) > MAX_WORDS:
            return
        visited += 1

        # Get label
        if node_id > 0 and node_id < TOTAL_LABELS:
            lbl = data[LABELS_START + node_id]
            ch = LABEL_MAP.get(lbl)
            if ch is None:
                return  # non-Cyrillic label, skip this branch
            prefix = prefix + ch.lower()

        children = get_children(node_id)

        # Check if this could be a word (using various terminal BVs)
        if len(prefix) >= 2 and node_id > 0:
            # Try DS2 as terminal
            if bv_ds2.bit(node_id):
                words.append(("DS2", prefix))
            # Try DS3 as terminal
            elif bv_ds3.bit(node_id):
                words.append(("DS3", prefix))
            # Try DS1 as terminal
            elif bv_ds1.bit(node_id):
                words.append(("DS1", prefix))
            # Also: leaf node = word
            elif len(children) == 0:
                words.append(("leaf", prefix))

        for c in children:
            dfs(c, prefix, depth + 1)

    print("Running DFS...")
    dfs(0, "", 0)

    print(f"\nVisited {visited} nodes, found {len(words)} words")

    # Analyze results
    for term_bv in ["DS1", "DS2", "DS3", "leaf"]:
        bv_words = [w for _, w in words if _ == term_bv]
        real = [w for w in bv_words if len(w) >= 3 and all(c in "abcdefghijklmnopqrstuvwxyz" or ord(c) >= 0x0400 for c in w)]
        found_known = [w for w in real if w in known]
        if bv_words:
            print(f"\n  {term_bv}: {len(bv_words)} words, {len(real)} valid, {len(found_known)} known")
            print(f"    First 20: {bv_words[:20]}")
            if found_known:
                print(f"    KNOWN: {found_known}")

    # Also show raw counts
    all_words = [w for _, w in words]
    all_known = [w for w in all_words if w in known]
    if all_known:
        print(f"\n  === KNOWN WORDS FOUND: {all_known} ===")

    # Show first children and their labels
    print(f"\n=== ROOT'S CHILDREN LABELS ===")
    root_children = get_children(0)
    print(f"Root has {len(root_children)} children: {root_children}")
    for c in root_children[:10]:
        if c < TOTAL_LABELS:
            lbl = data[LABELS_START + c]
            ch = LABEL_MAP.get(lbl, f"[{lbl}]")
            print(f"  Node {c}: label byte={lbl}, char={ch}")
            # Show this child's children
            gc = get_children(c)
            print(f"    Has {len(gc)} children")
            for g in gc[:5]:
                if g < TOTAL_LABELS:
                    gl = data[LABELS_START + g]
                    gch = LABEL_MAP.get(gl, f"[{gl}]")
                    print(f"      Node {g}: label byte={gl}, char={gch}")


    # Dump all words to file for inspection
    with open("tools/r3_words_dump.txt", "w", encoding="utf-8") as f:
        f.write(f"Visited {visited} nodes, found {len(words)} words\n\n")
        for term_bv, w in words:
            f.write(f"{term_bv}\t{w}\n")
        f.write(f"\nRoot children:\n")
        for c in root_children:
            if c < TOTAL_LABELS:
                lbl = data[LABELS_START + c]
                ch = LABEL_MAP.get(lbl, f"[{lbl}]")
                gc = get_children(c)
                f.write(f"  Node {c}: byte={lbl} char={ch} children={len(gc)}\n")
                for g in gc[:30]:
                    if g < TOTAL_LABELS:
                        gl = data[LABELS_START + g]
                        gch = LABEL_MAP.get(gl, f"[{gl}]")
                        ggc = get_children(g)
                        f.write(f"    Node {g}: byte={gl} char={gch} children={len(ggc)}\n")
    print("Dumped to tools/r3_words_dump.txt")

if __name__ == "__main__":
    main()
