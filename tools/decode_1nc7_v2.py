#!/usr/bin/env python3
"""1nc7 decoder v2 — using exact verified offsets.

Verified structure:
  file+8:       DataSection (pre-data bitvector bits, 124944B, f0=999284)
  file+124952:  Gap (5792B — structure/mapping data)
  file+130744:  BV1 header [15614, 3904, 369743, 0]
  file+130768:  BV1 rank (~31240B)
  file+162008:  BV1 bits DataSection (f0=999283, 124944B)
  file+286952:  Gap (5792B)
  file+292744:  BV2 header [15614, 3904, 369743, 0]
  file+292768:  BV2 rank
  file+324008:  BV2 bits DataSection
  file+448936:  BV3 header [15614, 3904, 801071, 0]
  file+448960:  BV3 rank
  file+480200:  BV3 bits DataSection
  file+605128:  BV4 header [15614, 3904, 402824, 0]
  file+605152:  BV4 rank
  file+636392+: Metadata [8, 255, 3222592, 50353] + labels + hash
  file+999284:  Tail
"""
import struct
import pathlib
import collections


LABEL_MAP = {}
for i in range(32): LABEL_MAP[i] = chr(0x0410 + i)      # А-Я
for i in range(16): LABEL_MAP[32 + i] = chr(0x0430 + i)  # а-п
for i in range(16): LABEL_MAP[96 + i] = chr(0x0440 + i)  # р-я
LABEL_MAP[112] = 'Ё'; LABEL_MAP[113] = 'ё'


def popcount64(x):
    x -= (x >> 1) & 0x5555555555555555
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f
    return ((x * 0x0101010101010101) >> 56) & 0xff


def bit_at(bits, pos):
    bi = pos >> 3
    if bi >= len(bits): return 0
    return (bits[bi] >> (pos & 7)) & 1


def rank1_brute(bits, pos):
    """Brute-force rank1 — count 1-bits in bits[0..pos)."""
    if pos == 0: return 0
    count = 0
    full = pos >> 6
    for i in range(full):
        off = i * 8
        if off + 8 <= len(bits):
            w = struct.unpack_from("<Q", bits, off)[0]
            count += popcount64(w)
    rem = pos & 63
    if rem > 0:
        off = full * 8
        if off + 8 <= len(bits):
            w = struct.unpack_from("<Q", bits, off)[0]
            count += popcount64(w & ((1 << rem) - 1))
    return count


def main():
    path = pathlib.Path(
        r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"
    )
    data = path.read_bytes()
    N = 15614  # num_words
    TOTAL_BITS = N * 64  # 999296

    # Extract DataSection bits (data starts at offset+24, size=N*8+8=124920)
    def get_bits(ds_offset):
        return data[ds_offset + 24 : ds_offset + 24 + N * 8]

    # 5 bitvector bit arrays
    bv0_bits = get_bits(8)        # pre-data
    bv1_bits = get_bits(162008)   # after BV1 rank
    bv2_bits = get_bits(324008)   # after BV2 rank
    bv3_bits = get_bits(480200)   # after BV3 rank

    # BV4 bits: need to find. Scan from 636000.
    bv4_bits = None
    for off in range(636000, 700000, 8):
        f0 = struct.unpack_from("<Q", data, off)[0]
        cnt = struct.unpack_from("<Q", data, off + 8)[0]
        if f0 == 999283 and cnt == 15614:
            bv4_bits = get_bits(off)
            print(f"BV4 bits found at file+{off}")
            break

    # Popcounts
    bvs = {"BV0": bv0_bits, "BV1": bv1_bits, "BV2": bv2_bits, "BV3": bv3_bits}
    if bv4_bits: bvs["BV4"] = bv4_bits

    for name, bits in bvs.items():
        pc = sum(popcount64(struct.unpack_from("<Q", bits, i*8)[0])
                 for i in range(min(N, len(bits)//8)))
        print(f"{name}: {len(bits)}B, popcount={pc}")

    # Label data: after BV4 metadata, starting around file+636424
    # metadata: [8, 255(alphabet), 3222592(tail_off), 50353(hash_cnt)]
    label_start = 636424  # after 6 u64s of metadata
    label_end = 999284    # payload_hint = tail start
    label_data = data[label_start:label_end]
    print(f"\nLabel data: {len(label_data)}B (file+{label_start} to file+{label_end})")

    # Label byte distribution
    dist = collections.Counter(label_data[:10000])
    cyrillic = sum(dist.get(b, 0) for b in list(range(32, 48)) + list(range(96, 114)))
    print(f"First 10K: CP866 cyrillic bytes = {cyrillic}/10000")
    print(f"Top bytes: {dist.most_common(10)}")

    # Try decoding labels
    print(f"\nFirst 50 label bytes -> chars:")
    chars = []
    for b in label_data[:50]:
        ch = LABEL_MAP.get(b, f"[{b:02x}]")
        chars.append(ch)
    print(" ".join(chars))

    # --- Try DFS traversal ---
    # For each BV pair assignment, try walking the trie
    # BV0 and BV1 have same structure (from same rank pop).
    # Try: BV0=has_child, BV1=terminal (or vice versa)

    print(f"\n=== TRAVERSAL ATTEMPTS ===")
    for name, hc_bits, term_bits in [
        ("BV0=hc BV1=term", bv0_bits, bv1_bits),
        ("BV1=hc BV0=term", bv1_bits, bv0_bits),
        ("BV0=hc BV2=term", bv0_bits, bv2_bits),
        ("BV3=hc BV0=term", bv3_bits, bv0_bits),
    ]:
        words = []
        stack = [(0, "")]
        visited = 0

        while stack and len(words) < 500 and visited < 100000:
            nid, word = stack.pop()
            visited += 1

            if nid >= TOTAL_BITS or nid >= len(label_data):
                continue

            # Label
            if nid > 0 and nid < len(label_data):
                lb = label_data[nid]
                ch = LABEL_MAP.get(lb)
                if ch:
                    word = word + ch
                else:
                    continue

            # Terminal check
            if bit_at(term_bits, nid) and len(word) >= 2:
                words.append(word.lower())

            # Children via rank
            if bit_at(hc_bits, nid):
                cs = rank1_brute(hc_bits, nid)
                ce = rank1_brute(hc_bits, nid + 1)
                for cid in range(cs, min(ce, TOTAL_BITS)):
                    if len(word) < 25:
                        stack.append((cid, word))

        # Check results
        real_words = [w for w in words if len(w) >= 3 and w.isalpha()]
        known = {"привет", "слово", "мама", "папа", "дом", "кот", "мир", "день",
                 "год", "вода", "рука", "город", "время", "жизнь", "работа"}
        found_known = [w for w in real_words if w in known]

        print(f"\n{name}: visited={visited} words={len(words)} real={len(real_words)} "
              f"known={found_known}")
        if real_words:
            print(f"  first_10: {real_words[:10]}")


if __name__ == "__main__":
    main()
