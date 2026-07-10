#!/usr/bin/env python3
"""Full 1nc7 succinct trie decoder.

Based on Ghidra decompilation:
  FUN_0065f2d8 — main 1nc7 parser
  FUN_00660496 — recursive section loader
  FUN_00661806 — bitvector section reader
  FUN_006615a4 — data section reader

File structure (verified):
  BV header: [num_words:u64=15614][field1:u64=3904][popcount:u64][zero:u64=0]
  BV sections at: file+130744, +292744, +448936, +605128
  Bits data section: [f0:u64][count:u64=15614][f2:u64][data: count*8+8 bytes]
  Two-level recursive structure: main trie (BV1+BV2) + sub-trie (BV3+BV4)
"""
import struct
import pathlib
import sys


# CP866 label_id → Unicode
LABEL_MAP = {}
for i in range(32):
    LABEL_MAP[i] = chr(0x0410 + i)  # А-Я
for i in range(16):
    LABEL_MAP[32 + i] = chr(0x0430 + i)  # а-п
for i in range(16):
    LABEL_MAP[96 + i] = chr(0x0440 + i)  # р-я
LABEL_MAP[112] = 'Ё'
LABEL_MAP[113] = 'ё'


def popcount64(x):
    x -= (x >> 1) & 0x5555555555555555
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f
    return ((x * 0x0101010101010101) >> 56) & 0xff


class Bitvector:
    """Parsed bitvector with rank support."""
    def __init__(self, num_words, popcount, rank_data, bits_data):
        self.num_words = num_words
        self.total_pop = popcount
        self.rank_data = rank_data  # bytes, 16-byte stride L1/L2
        self.bits = bits_data       # bytes, raw u64 words
        self.total_bits = num_words * 64

    def bit_at(self, pos):
        if pos >= self.total_bits:
            return 0
        byte_idx = pos >> 3
        if byte_idx >= len(self.bits):
            return 0
        return (self.bits[byte_idx] >> (pos & 7)) & 1

    def rank1(self, pos):
        if pos == 0:
            return 0
        if pos >= self.total_bits:
            return self.total_pop

        # Use L1/L2 rank structure
        sb_idx = pos // 512
        entry_off = sb_idx * 16
        if entry_off + 16 <= len(self.rank_data):
            l1 = struct.unpack_from("<Q", self.rank_data, entry_off)[0]
            l2_packed = struct.unpack_from("<Q", self.rank_data, entry_off + 8)[0]
            word_in_sb = (pos >> 6) & 7
            l2 = 0
            if word_in_sb > 0:
                l2 = (l2_packed >> ((word_in_sb - 1) * 9)) & 0x1FF
        else:
            # Fallback: brute force
            l1 = 0
            l2 = 0

        word_idx = pos >> 6
        bit_in_word = pos & 63
        pc = 0
        if bit_in_word > 0:
            word_off = word_idx * 8
            if word_off + 8 <= len(self.bits):
                word = struct.unpack_from("<Q", self.bits, word_off)[0]
                mask = (1 << bit_in_word) - 1
                pc = popcount64(word & mask)

        return l1 + l2 + pc

    def select0(self, target):
        """Find position of (target+1)-th 0-bit."""
        count = 0
        for i in range(self.num_words):
            off = i * 8
            if off + 8 > len(self.bits):
                break
            w = struct.unpack_from("<Q", self.bits, off)[0]
            zeros = 64 - popcount64(w)
            if count + zeros > target:
                rem = target - count
                for b in range(64):
                    if not ((w >> b) & 1):
                        if rem == 0:
                            return i * 64 + b
                        rem -= 1
            count += zeros
        return -1

    def select1(self, target):
        """Find position of (target+1)-th 1-bit."""
        count = 0
        for i in range(self.num_words):
            off = i * 8
            if off + 8 > len(self.bits):
                break
            w = struct.unpack_from("<Q", self.bits, off)[0]
            ones = popcount64(w)
            if count + ones > target:
                rem = target - count
                for b in range(64):
                    if (w >> b) & 1:
                        if rem == 0:
                            return i * 64 + b
                        rem -= 1
            count += ones
        return -1


def parse_bv_section(data, offset):
    """Parse a bitvector section at given file offset."""
    # Header: [num_words:u64][field1:u64][popcount:u64][zero:u64] = 32 bytes
    num_words, field1, popcount, zero = struct.unpack_from("<4Q", data, offset)

    # Rank data starts at offset+24 (after first 3 u64s), 16-byte stride
    rank_start = offset + 24

    # Find bits data section: scan for count == num_words
    # Bits section header: [f0:u64][count:u64=num_words][f2:u64] at some offset after rank
    bits_header_off = None
    for scan in range(rank_start, min(offset + 200000, len(data) - 24), 8):
        candidate_count = struct.unpack_from("<Q", data, scan + 8)[0]
        if candidate_count == num_words:
            f0 = struct.unpack_from("<Q", data, scan)[0]
            if f0 < 2000000:  # reasonable f0
                bits_header_off = scan
                break

    if bits_header_off is None:
        return None, offset + 32

    rank_data = data[rank_start:bits_header_off]

    # Bits data: [f0:u64][count:u64][f2:u64][data: count*8+8]
    bits_f0, bits_count, bits_f2 = struct.unpack_from("<3Q", data, bits_header_off)
    bits_data_start = bits_header_off + 24
    bits_data_size = bits_count * 8 + 8
    bits_data = data[bits_data_start:bits_data_start + bits_data_size]

    section_end = bits_data_start + bits_data_size

    bv = Bitvector(num_words, popcount, rank_data, bits_data)
    return bv, section_end


def parse_data_section(data, offset):
    """Parse a generic data section (FUN_006615a4 format)."""
    if offset + 24 > len(data):
        return None, None, offset
    f0, count, f2 = struct.unpack_from("<3Q", data, offset)
    data_size = count * 8 + 8
    section_data = data[offset + 24:offset + 24 + data_size]
    return (f0, count, f2), section_data, offset + 24 + data_size


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\1nc7_decoded_words.txt"
    )

    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    print(f"File: {len(data)} bytes")
    print(f"Header: magic=0x{header[0]:08X} ver={header[1]} payload={header[2]} items={header[4]}")

    # Parse 4 BV sections at known offsets
    bv_offsets = [130744, 292744, 448936, 605128]
    bvs = []
    for i, off in enumerate(bv_offsets):
        bv, end = parse_bv_section(data, off)
        if bv:
            print(f"BV{i+1} at file+{off}: words={bv.num_words} pop={bv.total_pop} "
                  f"rank={len(bv.rank_data)}B bits={len(bv.bits)}B end={end}")
            bvs.append(bv)
        else:
            print(f"BV{i+1} at file+{off}: PARSE FAILED")
            bvs.append(None)

    if len(bvs) < 4 or any(b is None for b in bvs):
        print("ERROR: not all bitvectors parsed")
        return

    # The data BETWEEN and AROUND BV sections contains labels and other data
    # Body data before BV1: file+24 to file+130744
    pre_bv = data[24:130744]
    print(f"\nPre-BV data: {len(pre_bv)} bytes (file+24 to file+130744)")

    # Data between sections (gaps)
    # These gaps contain the label data, weight data, etc.
    # Gap between BV1 end and BV2 start
    bv1_end = 130744 + 32 + len(bvs[0].rank_data) + 24 + len(bvs[0].bits)
    gap1 = data[bv1_end:292744]
    print(f"Gap after BV1: {len(gap1)} bytes (file+{bv1_end} to file+292744)")

    # Remaining data after BV4
    bv4_end = 605128 + 32 + len(bvs[3].rank_data) + 24 + len(bvs[3].bits)
    remaining = data[bv4_end:header[2]]  # up to payload_hint
    tail = data[header[2]:]
    print(f"Remaining body: {len(remaining)} bytes (file+{bv4_end} to file+{header[2]})")
    print(f"Tail: {len(tail)} bytes")

    # --- Try to identify label data ---
    # Labels should be a byte array where labels[node_id] gives a CP866 label
    # With num_words=15614, total_bits = 999,296
    # Node count depends on BV interpretation

    # For the main trie level (BV1+BV2):
    # BV1 pop=369,743 BV2 pop=369,743
    # The pre-BV data (130,720 bytes) might contain labels

    # Check if pre-BV data has label-like bytes
    pre_bv_dist = {}
    for b in pre_bv[:10000]:
        pre_bv_dist[b] = pre_bv_dist.get(b, 0) + 1

    # Check for CP866 cyrillic range (label values 32-47 and 96-113)
    label_bytes = sum(pre_bv_dist.get(b, 0) for b in range(32, 48))
    label_bytes += sum(pre_bv_dist.get(b, 0) for b in range(96, 114))
    print(f"\nPre-BV first 10K: CP866 label bytes = {label_bytes}/10000")

    # Check remaining body
    rem_dist = {}
    for b in remaining[:10000]:
        rem_dist[b] = rem_dist.get(b, 0) + 1
    rem_label = sum(rem_dist.get(b, 0) for b in range(32, 48))
    rem_label += sum(rem_dist.get(b, 0) for b in range(96, 114))
    print(f"Remaining first 10K: CP866 label bytes = {rem_label}/10000")

    # --- Attempt word extraction ---
    # Try: pre-BV data as label_low array, BV1 as has_child, BV2 as terminal
    # Or: BV2 as has_child, BV1 as terminal
    # Node traversal: LOUDS-like using has_child for child navigation

    # First, check what the label array looks like
    print(f"\nPre-BV first 100 bytes as labels:")
    sample_labels = []
    for b in pre_bv[:100]:
        ch = LABEL_MAP.get(b, f"[{b}]")
        sample_labels.append(ch)
    print("  " + " ".join(sample_labels[:50]))

    # Check remaining body as labels
    print(f"\nRemaining first 100 bytes as labels:")
    sample_labels2 = []
    for b in remaining[:100]:
        ch = LABEL_MAP.get(b, f"[{b}]")
        sample_labels2.append(ch)
    print("  " + " ".join(sample_labels2[:50]))

    # --- LOUDS traversal attempt ---
    # Use BV1 for structure, BV2 for terminal marking
    # Or try both assignments

    for bv_name, has_child_bv, terminal_bv, label_source, label_name in [
        ("BV1=struct BV2=term pre_bv=labels", bvs[0], bvs[1], pre_bv, "pre_bv"),
        ("BV2=struct BV1=term pre_bv=labels", bvs[1], bvs[0], pre_bv, "pre_bv"),
        ("BV1=struct BV2=term remain=labels", bvs[0], bvs[1], remaining, "remaining"),
        ("BV3=struct BV4=term pre_bv=labels", bvs[2], bvs[3], pre_bv, "pre_bv"),
    ]:
        print(f"\n--- Trying: {bv_name} ---")

        # Simple traversal: for each node, check terminal, get label
        words = []
        total_nodes = has_child_bv.total_bits

        # Count terminal nodes to sanity check
        term_sample = sum(terminal_bv.bit_at(i) for i in range(min(1000, total_nodes)))
        hc_sample = sum(has_child_bv.bit_at(i) for i in range(min(1000, total_nodes)))
        print(f"  First 1000: terminal={term_sample} has_child={hc_sample}")

        # DFS using has_child for child navigation
        # In this encoding, has_child[i]=1 means node i has children
        # Children range: [rank1(has_child, i), rank1(has_child, i+1))
        # Each child's label: label_source[child_node_id]

        stack = [(0, "")]  # (node_id, current_word)
        visited = 0
        max_visit = 200000

        while stack and len(words) < 5000 and visited < max_visit:
            node_id, word = stack.pop()
            visited += 1

            if node_id >= total_nodes or node_id >= len(label_source):
                continue

            # Get label for this node
            if node_id > 0:
                label_byte = label_source[node_id]
                ch = LABEL_MAP.get(label_byte, "")
                if ch:
                    word = word + ch
                else:
                    continue  # skip non-cyrillic

            # Check terminal
            if terminal_bv.bit_at(node_id) and len(word) >= 2:
                words.append(word.lower())

            # Get children
            if has_child_bv.bit_at(node_id):
                child_start = has_child_bv.rank1(node_id)
                child_end = has_child_bv.rank1(node_id + 1)
                for child_id in range(child_start, min(child_end, total_nodes)):
                    if len(word) < 25:
                        stack.append((child_id, word))

        print(f"  Visited {visited}, found {len(words)} words")
        if words:
            # Check for real Russian words
            print(f"  First 20: {words[:20]}")
            # Check unique
            unique = sorted(set(words))
            print(f"  Unique: {len(unique)}")

    # Write all results
    lines = ["# 1nc7 decode attempt results\n\n"]
    lines.append(f"BV1: pop={bvs[0].total_pop}\n")
    lines.append(f"BV2: pop={bvs[1].total_pop}\n")
    lines.append(f"BV3: pop={bvs[2].total_pop}\n")
    lines.append(f"BV4: pop={bvs[3].total_pop}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"\nWritten to {out_path}")


if __name__ == "__main__":
    main()
