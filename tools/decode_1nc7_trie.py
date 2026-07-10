#!/usr/bin/env python3
"""
decode_1nc7_trie.py — Decode Yandex 1nc7 succinct trie and extract words.
Empirical approach: try multiple traversal strategies until words emerge.
"""
import struct, sys

TRIE_PATH = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"

# CP866 label to Unicode
def build_cp866():
    t = [0xFFFD] * 256
    for i in range(32): t[i] = 0x0410 + i      # А-Я
    for i in range(32, 48): t[i] = 0x0430 + (i - 32)  # а-п
    for i in range(96, 112): t[i] = 0x0440 + (i - 96)  # р-я
    t[112] = 0x0401; t[113] = 0x0451  # Ё, ё
    return t

CP866 = build_cp866()

def label_to_char(label_id):
    cp = CP866[label_id]
    return chr(cp) if cp != 0xFFFD else '?'

def read_u32(data, off): return struct.unpack_from('<I', data, off)[0]
def read_u64(data, off): return struct.unpack_from('<Q', data, off)[0]

def popcount64(x):
    x = x - ((x >> 1) & 0x5555555555555555)
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0F
    return ((x * 0x0101010101010101) >> 56) & 0xFF

class Bitvector:
    def __init__(self, data, bits_offset, rank_offset, num_words):
        self.data = data
        self.bits_off = bits_offset
        self.rank_off = rank_offset
        self.num_words = num_words
        self.total_bits = num_words * 64

    def get_bit(self, pos):
        if pos >= self.total_bits: return 0
        word_idx = pos // 64
        bit_idx = pos % 64
        word = read_u64(self.data, self.bits_off + word_idx * 8)
        return (word >> bit_idx) & 1

    def rank1(self, pos):
        """Count 1-bits in [0, pos) — exclusive."""
        if pos == 0: return 0
        if pos >= self.total_bits:
            return read_u64(self.data, self.rank_off)  # header popcount
        word_idx = pos // 64
        sb_idx = word_idx // 8
        word_in_sb = word_idx % 8

        L1 = read_u64(self.data, self.rank_off + sb_idx * 16)
        L2 = 0
        if word_in_sb > 0:
            packed = read_u64(self.data, self.rank_off + sb_idx * 16 + 8)
            L2 = (packed >> ((word_in_sb - 1) * 9)) & 0x1FF

        bit_in_word = pos % 64
        mask = (1 << bit_in_word) - 1 if bit_in_word > 0 else 0
        word = read_u64(self.data, self.bits_off + word_idx * 8)
        pc = popcount64(word & mask)

        return L1 + L2 + pc

    def select1(self, n):
        """Find position of n-th 1-bit (0-based)."""
        if n < 0: return None
        # Linear scan (slow but correct)
        count = 0
        for w in range(self.num_words):
            word = read_u64(self.data, self.bits_off + w * 8)
            wpc = popcount64(word)
            if count + wpc > n:
                rem = n - count
                for b in range(64):
                    if (word >> b) & 1:
                        if rem == 0:
                            return w * 64 + b
                        rem -= 1
            count += wpc
        return None

    def select0(self, n):
        """Find position of n-th 0-bit (0-based)."""
        if n < 0: return None
        count = 0
        for w in range(self.num_words):
            word = read_u64(self.data, self.bits_off + w * 8)
            zeros = 64 - popcount64(word)
            if count + zeros > n:
                rem = n - count
                for b in range(64):
                    if not ((word >> b) & 1):
                        if rem == 0:
                            return w * 64 + b
                        rem -= 1
            count += zeros
        return None


def main():
    with open(TRIE_PATH, 'rb') as f:
        data = f.read()

    size = len(data)
    print(f"File size: {size}")

    # Header
    magic = read_u32(data, 0)
    version = read_u32(data, 4)
    payload_hint = read_u32(data, 8)
    item_count = read_u32(data, 16)
    print(f"magic=0x{magic:08X} version={version} payload_hint={payload_hint} item_count={item_count}")

    # Find rank sections (BV headers with pattern item_count, 3904)
    rank_offsets = []
    off = 24
    while off + 16 <= size:
        v0 = read_u64(data, off)
        v1 = read_u64(data, off + 8)
        if v0 == item_count and v1 == 3904:
            pop = read_u64(data, off + 16)
            rank_offsets.append((off, pop))
            off += 32
            continue
        off += 8

    print(f"\nFound {len(rank_offsets)} rank sections:")
    for i, (off, pop) in enumerate(rank_offsets):
        print(f"  Rank[{i}] at {off}: pop={pop}")

    # Compute bits offsets: bits are immediately before rank entries
    # Rank entries start at rank_offset + 24 (3 x u64 header)
    # Bits section = 124992 bytes before rank entries
    bits_section_size = 124992
    num_words = item_count  # 15614

    bvs = []
    for i, (roff, pop) in enumerate(rank_offsets):
        rank_entries_start = roff + 24  # rank header is 24 bytes
        bits_start = rank_entries_start - bits_section_size
        print(f"  BV[{i}]: bits@{bits_start} rank@{roff} pop={pop}")
        bv = Bitvector(data, bits_start, rank_entries_start, num_words)

        # Quick verification
        qpop = 0
        for w in range(min(8, num_words)):
            qpop += popcount64(read_u64(data, bits_start + w * 8))
        print(f"    qpop(first8)={qpop}")

        # Show first 30 bits
        bits_str = ''.join(str(bv.get_bit(b)) for b in range(30))
        print(f"    first30: {bits_str}")

        bvs.append(bv)

    # Label array: after metadata at 636384+40 = 636424
    label_offset = 636424
    label_count = payload_hint - label_offset  # = 999284 - 636424 = 362860
    print(f"\nLabels: {label_count} bytes at offset {label_offset}")

    # Show first 20 labels
    labels_str = ' '.join(f"{data[label_offset+i]}({label_to_char(data[label_offset+i])})"
                          for i in range(min(20, label_count)))
    print(f"  First 20: {labels_str}")

    # === Try different traversal strategies ===

    louds = bvs[2]   # BV2 (sub-trie LOUDS, pop=801071)
    hc = bvs[3]      # BV3 (sub-trie has_child, pop=402824)

    print(f"\n=== Strategy 1: LOUDS DFS with child_node = rank1(louds, pos+1) ===")
    words = []

    def dfs1(node_id, path, depth=0):
        if depth > 40 or len(words) > 100: return

        # Label (skip root)
        if node_id > 0 and node_id < label_count:
            label = data[label_offset + node_id]
            ch = label_to_char(label)
            if ch != '?':
                path.append(ch)
            else:
                return

        # Terminal: leaf
        if not hc.get_bit(node_id):
            if len(path) > 0:
                words.append(''.join(path))

        # Children
        if hc.get_bit(node_id):
            child_rank = hc.rank1(node_id)
            if child_rank == 0:
                # Root case: start from select1(louds, 0)
                first = louds.select1(0)
                if first is not None:
                    pos = first
                    while pos is not None and pos < louds.total_bits and louds.get_bit(pos):
                        child_node = louds.rank1(pos + 1)  # NEW FORMULA
                        if child_node != node_id and child_node < label_count:
                            dfs1(child_node, path, depth + 1)
                        pos += 1
            else:
                cs = louds.select1(child_rank - 1)
                if cs is not None:
                    pos = cs
                    while pos < louds.total_bits and louds.get_bit(pos):
                        child_node = louds.rank1(pos + 1)
                        if child_node != node_id and child_node < label_count:
                            dfs1(child_node, path, depth + 1)
                        pos += 1

        if node_id > 0:
            path.pop()

    dfs1(0, [])
    print(f"  Found {len(words)} words")
    for w in words[:20]:
        print(f"    {w}")

    print(f"\n=== Strategy 2: LOUDS DFS with select0-based children ===")
    words2 = []

    def dfs2(node_id, path, depth=0):
        if depth > 40 or len(words2) > 100: return

        if node_id > 0 and node_id < label_count:
            label = data[label_offset + node_id]
            ch = label_to_char(label)
            if ch != '?':
                path.append(ch)
            else:
                return

        # Children: between select0(node_id) + 1 and select0(node_id + 1)
        s0 = louds.select0(node_id)
        s0_next = louds.select0(node_id + 1)

        if s0 is not None and s0_next is not None:
            first_child = s0 + 1
            # Children are 1-bits from first_child to s0_next - 1
            for pos in range(first_child, s0_next):
                if louds.get_bit(pos):
                    # Child node_id: rank1(louds, pos + 1)? or rank0(pos)?
                    child_node = pos + 1 - louds.rank1(pos + 1)  # rank0
                    if child_node != node_id and child_node < label_count and depth < 40:
                        dfs2(child_node, path, depth + 1)

            # Terminal: no children found
            if first_child >= s0_next and len(path) > 0:
                words2.append(''.join(path))
        elif len(path) > 0:
            words2.append(''.join(path))

        if node_id > 0:
            path.pop()

    dfs2(0, [])
    print(f"  Found {len(words2)} words")
    for w in words2[:20]:
        print(f"    {w}")

    print(f"\n=== Strategy 3: Use BV0/BV1 (main level) instead ===")
    louds_m = bvs[0]
    hc_m = bvs[1]
    words3 = []

    def dfs3(node_id, path, depth=0):
        if depth > 40 or len(words3) > 100: return

        if node_id > 0 and node_id < label_count:
            label = data[label_offset + node_id]
            ch = label_to_char(label)
            if ch != '?':
                path.append(ch)
            else:
                return

        if not hc_m.get_bit(node_id):
            if len(path) > 0:
                words3.append(''.join(path))

        if hc_m.get_bit(node_id):
            s0 = louds_m.select0(node_id)
            s0_next = louds_m.select0(node_id + 1)
            if s0 is not None and s0_next is not None:
                for pos in range(s0 + 1, s0_next):
                    if louds_m.get_bit(pos):
                        child_node = pos + 1 - louds_m.rank1(pos + 1)
                        if child_node != node_id and child_node < label_count:
                            dfs3(child_node, path, depth + 1)

        if node_id > 0:
            path.pop()

    dfs3(0, [])
    print(f"  Found {len(words3)} words")
    for w in words3[:20]:
        print(f"    {w}")

    # === Strategy 4: Try ALL BV combinations ===
    print(f"\n=== Strategy 4: Try all BV pairs ===")
    for li in range(4):
        for hi in range(4):
            if li == hi: continue
            l = bvs[li]
            h = bvs[hi]
            words4 = []

            def dfs4(nid, path, depth=0):
                if depth > 30 or len(words4) > 50: return
                if nid > 0 and nid < label_count:
                    label = data[label_offset + nid]
                    ch = label_to_char(label)
                    if ch != '?': path.append(ch)
                    else: return
                if not h.get_bit(nid) and len(path) > 0:
                    words4.append(''.join(path))
                if h.get_bit(nid):
                    cr = h.rank1(nid)
                    if cr > 0:
                        cs = l.select1(cr - 1)
                        if cs is not None:
                            pos = cs
                            while pos < l.total_bits and l.get_bit(pos) and len(words4) < 50:
                                cn = l.rank1(pos + 1)
                                if cn != nid and cn < label_count:
                                    dfs4(cn, path, depth + 1)
                                pos += 1
                    else:
                        # root
                        first = l.select1(0)
                        if first is not None:
                            pos = first
                            while pos < l.total_bits and l.get_bit(pos) and len(words4) < 50:
                                cn = l.rank1(pos + 1)
                                if cn != nid and cn < label_count:
                                    dfs4(cn, path, depth + 1)
                                pos += 1
                if nid > 0: path.pop()

            dfs4(0, [])
            if len(words4) > 2:
                cyrillic = sum(1 for w in words4 if all(0x0400 <= ord(c) <= 0x04FF for c in w))
                print(f"  louds=BV{li} hc=BV{hi}: {len(words4)} words, {cyrillic} Cyrillic")
                for w in words4[:5]:
                    print(f"    {w}")

if __name__ == '__main__':
    main()
