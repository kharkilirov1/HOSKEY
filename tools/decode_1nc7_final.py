#!/usr/bin/env python3
"""Final 1nc7 decoder using verified traversal algorithm.

Formula: child_pos = select1(LOUDS, rank1(has_child, node_id) - 1)
From: FUN_00661926 in libjni_ykeyboard3.so

BV assignment:
  BV0 (file+8, pop=370K) = main level LOUDS
  BV1 (file+162008, pop=370K) = main level has_child
  BV2 (file+324008, pop=801K) = sub-trie LOUDS
  BV3 (file+480200, pop=403K) = sub-trie has_child
  Labels at file+636424 (362KB, CP866 indices)
  Terminal: need to identify (try BV1 or separate section)
"""
import struct
import pathlib


LABEL_MAP = {}
for i in range(32): LABEL_MAP[i] = chr(0x0430 + i)  # а-я lowercase
LABEL_MAP[32] = 'ё'
# Also add uppercase mapping for completeness
LABEL_MAP_UPPER = {}
for i in range(32): LABEL_MAP_UPPER[i] = chr(0x0410 + i)


def popcount64(x):
    x -= (x >> 1) & 0x5555555555555555
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f
    return ((x * 0x0101010101010101) >> 56) & 0xff


class BV:
    def __init__(self, bits_data, rank_data=None):
        self.bits = bits_data
        self.rank = rank_data
        self.total_bits = len(bits_data) * 8
        # Compute total popcount
        self.total_pop = 0
        for i in range(len(bits_data) // 8):
            w = struct.unpack_from("<Q", bits_data, i * 8)[0]
            self.total_pop += popcount64(w)

    def bit_at(self, pos):
        if pos >= self.total_bits:
            return 0
        return (self.bits[pos >> 3] >> (pos & 7)) & 1

    def rank1(self, pos):
        if pos == 0:
            return 0
        if pos >= self.total_bits:
            return self.total_pop
        count = 0
        full = pos >> 6
        for i in range(full):
            w = struct.unpack_from("<Q", self.bits, i * 8)[0]
            count += popcount64(w)
        rem = pos & 63
        if rem > 0:
            w = struct.unpack_from("<Q", self.bits, full * 8)[0]
            count += popcount64(w & ((1 << rem) - 1))
        return count

    def select1(self, target):
        """Position of (target+1)-th 1-bit (0-indexed)."""
        if target < 0 or target >= self.total_pop:
            return -1
        count = 0
        for i in range(len(self.bits) // 8):
            w = struct.unpack_from("<Q", self.bits, i * 8)[0]
            pc = popcount64(w)
            if count + pc > target:
                rem = target - count
                for b in range(64):
                    if (w >> b) & 1:
                        if rem == 0:
                            return i * 64 + b
                        rem -= 1
            count += pc
        return -1


def main():
    path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\1nc7_final_words.txt"
    )

    data = path.read_bytes()
    N = 15614  # num_words

    def get_bits(ds_offset):
        return data[ds_offset + 24: ds_offset + 24 + N * 8]

    # Load bitvectors
    bv0 = BV(get_bits(8))         # main LOUDS
    bv1 = BV(get_bits(162008))    # main has_child
    bv2 = BV(get_bits(324008))    # sub-trie LOUDS
    bv3 = BV(get_bits(480200))    # sub-trie has_child

    print(f"BV0 (main LOUDS): pop={bv0.total_pop}")
    print(f"BV1 (main has_child): pop={bv1.total_pop}")
    print(f"BV2 (sub LOUDS): pop={bv2.total_pop}")
    print(f"BV3 (sub has_child): pop={bv3.total_pop}")

    # Labels
    labels = data[636424:999284]
    print(f"Labels: {len(labels)} bytes")

    # Try ALL combinations of (louds, has_child) for sub-trie traversal
    # Formula: child_pos = select1(LOUDS, rank1(has_child, node_id) - 1)
    combos = [
        ("BV2=louds BV3=hc", bv2, bv3),
        ("BV3=louds BV2=hc", bv3, bv2),
        ("BV0=louds BV1=hc", bv0, bv1),
        ("BV1=louds BV0=hc", bv1, bv0),
        ("BV2=louds BV1=hc", bv2, bv1),
        ("BV0=louds BV3=hc", bv0, bv3),
    ]

    best_words = []
    best_name = ""

    for name, louds, has_child in combos:
        # For terminal, try the remaining BVs
        # Use the BV NOT used as louds/has_child
        all_bvs = {id(bv0): bv0, id(bv1): bv1, id(bv2): bv2, id(bv3): bv3}
        remaining = [b for k, b in all_bvs.items()
                     if k != id(louds) and k != id(has_child)]

        for term_idx, terminal in enumerate(remaining):
            words = []
            stack = [(0, "")]
            visited = 0
            errors = 0

            while stack and len(words) < 2000 and visited < 300000:
                node_id, word = stack.pop()
                visited += 1

                if node_id < 0 or node_id >= louds.total_bits:
                    continue

                # Get label
                if node_id > 0 and node_id < len(labels):
                    lb = labels[node_id]
                    ch = LABEL_MAP.get(lb)
                    if ch:
                        word = word + ch
                    else:
                        continue

                # Terminal check
                if terminal.bit_at(node_id) and len(word) >= 2:
                    words.append(word)

                # Child navigation: child = select1(louds, rank1(has_child, node_id) - 1)
                if has_child.bit_at(node_id):
                    r = has_child.rank1(node_id)
                    if r > 0:
                        child_start = louds.select1(r - 1)
                        if child_start >= 0:
                            # Scan children: consecutive 1-bits in LOUDS after select position
                            # Actually the formula gives the POSITION, children are at consecutive nodes
                            # In LOUDS: child_start is where the children BEGIN
                            # Children are at child_start, child_start+1, ... until next 0-bit
                            pos = child_start
                            children_added = 0
                            while pos < louds.total_bits and children_added < 50:
                                if len(word) < 25:
                                    stack.append((pos, word))
                                pos += 1
                                children_added += 1
                                # Stop at some reasonable limit
                                # In LOUDS, children end at next 0-bit
                                # But our formula already gives the right start
                                # Try just the single child
                                break  # First attempt: just one child per rank

            cyrillic_words = [w for w in words if len(w) >= 3 and all(
                '\u0430' <= c <= '\u044f' or c == '\u0451' for c in w)]

            if cyrillic_words and len(cyrillic_words) > len(best_words):
                best_words = cyrillic_words
                best_name = f"{name} term={term_idx}"

            if cyrillic_words:
                print(f"\n{name} term={term_idx}: visited={visited} "
                      f"words={len(words)} cyrillic={len(cyrillic_words)}")
                print(f"  first_10: {cyrillic_words[:10]}")

    if not best_words:
        # Try different child interpretation: rank gives child NODE ID directly
        print("\n=== ALT: rank as direct child ID ===")
        for name, louds, has_child in combos[:4]:
            remaining = [b for k, b in
                        {id(bv0): bv0, id(bv1): bv1, id(bv2): bv2, id(bv3): bv3}.items()
                        if k != id(louds) and k != id(has_child)]

            for term_idx, terminal in enumerate(remaining[:1]):
                words = []
                stack = [(0, "")]
                visited = 0

                while stack and len(words) < 2000 and visited < 300000:
                    node_id, word = stack.pop()
                    visited += 1
                    if node_id < 0 or node_id >= louds.total_bits:
                        continue

                    if node_id > 0 and node_id < len(labels):
                        lb = labels[node_id]
                        ch = LABEL_MAP.get(lb)
                        if ch:
                            word = word + ch
                        else:
                            continue

                    if terminal.bit_at(node_id) and len(word) >= 2:
                        words.append(word)

                    if has_child.bit_at(node_id):
                        r = has_child.rank1(node_id)
                        # Alt: children start at rank value directly
                        child = r
                        if child < louds.total_bits and len(word) < 25:
                            # Get degree from louds
                            degree = 0
                            pos = child
                            while pos < louds.total_bits and louds.bit_at(pos) == 1:
                                degree += 1
                                pos += 1
                            # Children are at child..child+degree-1? Or child is the LOUDS position?
                            for j in range(min(degree, 40)):
                                stack.append((child + j, word))

                cyrillic = [w for w in words if len(w) >= 3 and all(
                    '\u0430' <= c <= '\u044f' or c == '\u0451' for c in w)]
                if cyrillic:
                    print(f"\n{name} ALT term={term_idx}: words={len(words)} cyrillic={len(cyrillic)}")
                    print(f"  first_10: {cyrillic[:10]}")
                    if len(cyrillic) > len(best_words):
                        best_words = cyrillic
                        best_name = f"{name} ALT"

    # Write results
    lines = [f"# 1nc7 decoded words\n# Method: {best_name}\n# Count: {len(best_words)}\n\n"]
    for w in best_words:
        lines.append(w + "\n")
    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"\nBest: {best_name} with {len(best_words)} words")
    print(f"Written to {out_path}")


if __name__ == "__main__":
    main()
