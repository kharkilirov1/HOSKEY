#!/usr/bin/env python3
"""
1nc7 succinct trie decoder with weight extraction.

Verified binary layout (trie file, 4,498,484 bytes):
  [0..7]       Magic 0x37636E31 ("1nc7") + version=1
  [8..23]      DataSection header: f0=999284 (payload_hint), count=15614 (num u64 words)
  [24..124935] BV_A bits: 15614 u64 words, pop=369743  (rank@130768 verified)
  [124936..124951] Alignment [1445, 256]
  [124952..130743] Gap1: structure mapping (5792 bytes, packed u32 pairs)
  [130744..130767] BV1 header: [15614, 3904, 369743] (24-byte header, NOT 32)
  [130768..161999] BV_A rank: 1952 entries * 16 bytes = 31232 bytes
  [162008..162023] DataSection header: f0=999283, count=15614
  [162024..286935] BV_B bits: 15614 u64 words, pop=369743  (rank@292768 verified)
  [286936..286951] Alignment [1445, 256]
  [286952..292743] Gap2: structure mapping (5792 bytes)
  [292744..292767] BV2 header: [15614, 3904, 369743]
  [292768..323999] BV_B rank: 31232 bytes
  [324008..324023] DataSection header: f0=999283, count=15614
  [324024..448935] BV_C bits: 15614 u64 words, pop=801071  (rank@448960 verified)
  [448936..448959] BV3 header: [15614, 3904, 801071]
  [448960..480191] BV_C rank: 31232 bytes
  [480200..480215] DataSection header: f0=999283, count=15614
  [480216..605127] BV_D bits: 15614 u64 words, pop=402824  (rank@605152 verified)
  [605128..605151] BV4 header: [15614, 3904, 402824]
  [605152..636383] BV_D rank: 31232 bytes
  [636384..636423] Metadata: [402824, 8, 255, 3222592, 50353]
                   = [label_count, bitwidth, max_val, total_bits, u64_words]
  [636424..999283] Label body: 362860 bytes (first part of 402824-byte IntVector)
  [999284..1039247] Label tail part: remaining 39964 bytes of IntVector
  [1039248..]      Tail: alphabet tables (CP1251), weight data, footer metadata

Tail footer: _ORDER=3, _VERSION=1, _LANG_ID=1, _MIN_FREQ=3

Bitvector roles (verified by rank<->bits cross-validation):
  BV_A: rank@130768, bits@24,     pop=369743  -- Level-1 LOUDS tree structure
  BV_B: rank@292768, bits@162024, pop=369743  -- Level-1 has_child/terminal markers
  BV_C: rank@448960, bits@324024, pop=801071  -- Level-2 (sub-trie) LOUDS
  BV_D: rank@605152, bits@480216, pop=402824  -- Level-2 (sub-trie) has_child/terminal

Key insight: Labels are packed IntVector indices (bitwidth=8, 402824 entries)
that reference per-node child character sets in the tail section.
The label-to-character mapping requires the runtime charset table from
libjni_ykeyboard3.so (FUN_006622a7: charset[+0xF8 + label_id*4]).
Without the runtime table, direct label->character decoding is not possible.

Strategy: Combine words from dict_ru_full decoder (words_final.csv, 108K words)
with frequency data (ru_freq.txt, 50K entries) to produce a weighted word list.
"""
import struct
import sys
import os
import re
from collections import Counter


def popcount64(x):
    x = x - ((x >> 1) & 0x5555555555555555)
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f
    return ((x * 0x0101010101010101) >> 56) & 0xFF


class BitVector:
    """Bitvector with precomputed rank support (L1/L2 superblock structure)."""

    def __init__(self, file_data, bits_offset, rank_offset, num_words, expected_pop):
        self.data = file_data
        self.bits_off = bits_offset
        self.rank_off = rank_offset
        self.num_words = num_words
        self.total_bits = num_words * 64
        self.expected_pop = expected_pop
        self.n_rank = (num_words + 7) // 8

    def bit(self, pos):
        if pos < 0 or pos >= self.total_bits:
            return 0
        abs_off = self.bits_off + (pos >> 3)
        return (self.data[abs_off] >> (pos & 7)) & 1

    def word(self, idx):
        off = self.bits_off + idx * 8
        return struct.unpack_from("<Q", self.data, off)[0]

    def rank1(self, pos):
        """Count 1-bits in positions 0..pos-1 using L1/L2 index."""
        if pos <= 0:
            return 0
        if pos >= self.total_bits:
            return self.expected_pop

        sb_idx = pos >> 9
        if sb_idx >= self.n_rank:
            return self.expected_pop

        l1 = struct.unpack_from("<Q", self.data, self.rank_off + sb_idx * 16)[0]
        l2_packed = struct.unpack_from("<Q", self.data, self.rank_off + sb_idx * 16 + 8)[0]

        word_in_sb = (pos >> 6) & 7
        l2 = 0
        if word_in_sb > 0:
            l2 = (l2_packed >> ((word_in_sb - 1) * 9)) & 0x1FF

        word_idx = pos >> 6
        bit_in_word = pos & 63
        pc = 0
        if bit_in_word > 0:
            w = self.word(word_idx)
            mask = (1 << bit_in_word) - 1
            pc = popcount64(w & mask)

        return l1 + l2 + pc

    def select1(self, target):
        """Position of the target-th 1-bit (0-indexed). Binary search on rank."""
        if target < 0 or target >= self.expected_pop:
            return -1
        lo, hi = 0, self.total_bits - 1
        while lo < hi:
            mid = (lo + hi) >> 1
            if self.rank1(mid + 1) <= target:
                lo = mid + 1
            else:
                hi = mid
        return lo

    def verify(self):
        """Verify rank index matches actual bits (first superblock)."""
        l1_1 = struct.unpack_from("<Q", self.data, self.rank_off + 16)[0]
        actual = 0
        for i in range(min(8, self.num_words)):
            actual += popcount64(self.word(i))
        return l1_1 == actual


def parse_trie_header(data):
    """Parse the 1nc7 file header."""
    magic, version = struct.unpack_from("<II", data, 0)
    if magic != 0x37636E31:
        raise ValueError(f"Bad magic: 0x{magic:08x} (expected 0x37636E31)")
    f0, count = struct.unpack_from("<QQ", data, 8)
    return {
        "magic": magic,
        "version": version,
        "payload_hint": f0,
        "num_words": count,
    }


def parse_tail_footer(data, payload_hint):
    """Parse key-value metadata from the tail footer."""
    tail = data[payload_hint:]
    footer = {}
    known_keys = [b"_VERSION", b"_ORDER", b"_LANG_ID", b"_MIN_FREQ",
                  b"_MAX_FREQ", b"_TOTAL_WORDS"]
    for key_bytes in known_keys:
        idx = tail.rfind(key_bytes)
        if idx >= 0:
            val_off = idx + len(key_bytes)
            if val_off + 4 <= len(tail):
                val = struct.unpack_from("<I", tail, val_off)[0]
                footer[key_bytes.decode("ascii")] = val
    return footer


def load_bitvectors(data, num_words):
    """Load the 4 verified bitvector structures."""
    bv_a = BitVector(data, bits_offset=24, rank_offset=130768,
                     num_words=num_words, expected_pop=369743)
    bv_b = BitVector(data, bits_offset=162024, rank_offset=292768,
                     num_words=num_words, expected_pop=369743)
    bv_c = BitVector(data, bits_offset=324024, rank_offset=448960,
                     num_words=num_words, expected_pop=801071)
    bv_d = BitVector(data, bits_offset=480216, rank_offset=605152,
                     num_words=num_words, expected_pop=402824)
    return bv_a, bv_b, bv_c, bv_d


def extract_louds_degrees(bv, max_nodes=500000):
    """Extract degree sequence from a LOUDS bitvector."""
    degrees = []
    d = 0
    for pos in range(bv.total_bits):
        if bv.bit(pos):
            d += 1
        else:
            degrees.append(d)
            d = 0
            if len(degrees) >= max_nodes:
                break
    return degrees


def load_words_final(path):
    """Load words from words_final.csv (dict_ru_full decoder output)."""
    words = set()
    if not os.path.exists(path):
        return words
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("word"):
                parts = line.split(",")
                w = parts[0].strip().lower()
                if re.match(r"^[а-яё\-]+$", w) and 2 <= len(w) <= 32:
                    words.add(w)
    return words


def load_freq_data(path):
    """Load word frequency data from ru_freq.txt."""
    freq = {}
    if not os.path.exists(path):
        return freq
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 2:
                w = parts[0].lower()
                try:
                    freq[w] = int(parts[1])
                except ValueError:
                    pass
    return freq


def attempt_trie_decode(data, bv_a, bv_b, num_words):
    """
    Attempt direct trie traversal using BV_A (LOUDS) + BV_B (markers).
    Labels are at offset 636424 but mapping is unknown without runtime charset.
    Returns (words_found, label_stats) for diagnostic purposes.
    """
    labels = data[636424:636424 + 402824]
    degrees = extract_louds_degrees(bv_a, max_nodes=400000)
    total_nodes = len(degrees)

    # Build cumulative degree sum for child lookup
    cum_deg = [0] * (total_nodes + 1)
    for i in range(total_nodes):
        cum_deg[i + 1] = cum_deg[i] + degrees[i]

    # Count BV_B terminal marks in first 1000 nodes
    terminal_count = sum(bv_b.bit(i) for i in range(min(1000, total_nodes)))

    # Label frequency analysis
    label_freq = Counter(labels[:min(total_nodes, len(labels))])

    return {
        "total_nodes": total_nodes,
        "root_degree": degrees[0] if degrees else 0,
        "first_degrees": degrees[:20],
        "terminal_in_first_1000": terminal_count,
        "label_unique_values": len(label_freq),
        "label_top_10": label_freq.most_common(10),
        "cum_deg_sample": cum_deg[:10],
    }


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    # Paths
    trie_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"
    words_csv = r"C:\Users\Kharki\clawd\v600_analysis\words_final.csv"
    freq_path = r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\ru_freq.txt"
    out_path = r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\words_with_weights.tsv"

    if not os.path.exists(trie_path):
        print(f"ERROR: trie file not found at {trie_path}")
        sys.exit(1)

    data = open(trie_path, "rb").read()
    print(f"Trie file: {len(data)} bytes")

    # 1. Parse header
    header = parse_trie_header(data)
    print(f"Header: magic=1nc7, version={header['version']}, "
          f"payload_hint={header['payload_hint']}, num_words={header['num_words']}")

    num_words = header["num_words"]

    # 2. Load and verify bitvectors
    bv_a, bv_b, bv_c, bv_d = load_bitvectors(data, num_words)
    print(f"\nBitvector verification:")
    for name, bv in [("BV_A", bv_a), ("BV_B", bv_b), ("BV_C", bv_c), ("BV_D", bv_d)]:
        ok = bv.verify()
        print(f"  {name}: pop={bv.expected_pop}, rank verified={'OK' if ok else 'FAIL'}")

    # 3. Parse tail footer
    footer = parse_tail_footer(data, header["payload_hint"])
    print(f"\nTail footer: {footer}")

    # 4. Attempt direct trie decode (diagnostic)
    print("\nDirect trie analysis:")
    diag = attempt_trie_decode(data, bv_a, bv_b, num_words)
    print(f"  Total nodes: {diag['total_nodes']}")
    print(f"  Root degree: {diag['root_degree']}")
    print(f"  First degrees: {diag['first_degrees']}")
    print(f"  BV_B terminals in first 1000: {diag['terminal_in_first_1000']}")
    print(f"  Label unique values: {diag['label_unique_values']}")
    print(f"  Label top 10: {diag['label_top_10']}")
    print(f"\n  NOTE: Direct label->character decoding requires the runtime")
    print(f"  charset table from libjni_ykeyboard3.so (not available in trie file).")
    print(f"  Labels are packed IntVector indices, NOT direct character codes.")
    print(f"  Using dict_ru_full word list + frequency data instead.")

    # 5. Load word sources
    print(f"\nLoading word sources...")
    words_set = load_words_final(words_csv)
    print(f"  words_final.csv: {len(words_set)} valid Russian words")

    freq_data = load_freq_data(freq_path)
    print(f"  ru_freq.txt: {len(freq_data)} frequency entries")

    if not words_set and not freq_data:
        print("ERROR: No word sources available.")
        sys.exit(1)

    # 6. Merge: all words from both sources with best available weight
    result = {}
    for w in words_set:
        result[w] = freq_data.get(w, 1)

    added = 0
    for w, f in freq_data.items():
        if w not in result and re.match(r"^[а-яё\-]+$", w) and 2 <= len(w) <= 32:
            result[w] = f
            added += 1

    print(f"  Added from freq only: {added}")
    print(f"  Total unique words: {len(result)}")

    # Count words with real weights vs default
    with_weight = sum(1 for v in result.values() if v > 1)
    print(f"  Words with frequency data: {with_weight}")
    print(f"  Words with default weight (1): {len(result) - with_weight}")

    # 7. Sort by weight descending, write TSV
    sorted_words = sorted(result.items(), key=lambda x: (-x[1], x[0]))

    with open(out_path, "w", encoding="utf-8") as f:
        for w, weight in sorted_words:
            f.write(f"{w}\t{weight}\n")

    print(f"\nOutput: {out_path}")
    print(f"  Total entries: {len(sorted_words)}")
    print(f"\n  Top 20:")
    for w, weight in sorted_words[:20]:
        print(f"    {w}\t{weight}")
    print(f"\n  Sample from middle:")
    mid = len(sorted_words) // 2
    for w, weight in sorted_words[mid:mid + 5]:
        print(f"    {w}\t{weight}")

    print(f"\nDone.")


if __name__ == "__main__":
    main()
