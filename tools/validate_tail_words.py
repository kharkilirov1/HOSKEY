#!/usr/bin/env python3
"""Deep analysis of the tail region to find actual Russian words.

Focus: what's in the tail (after payload_hint=999284) and how to extract words.
"""
from __future__ import annotations
import struct
import sys
import pathlib
import collections


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def cp1251_to_utf8(data: bytes) -> str:
    return data.decode("cp1251", errors="replace")


def is_operator_byte(b: int) -> bool:
    """Known operator bytes from reverse analysis."""
    return b in (0x89, 0x0A, 0xAE, 0x94, 0x20, 0xB3, 0x67, 0x57,
                 0x30, 0x7D, 0x0C, 0x23, 0xAD, 0x05, 0x1A, 0x1E, 0x09, 0x08)


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    magic, version, payload_hint, _, item_count, _ = header

    tail = data[payload_hint:]
    body = data[24:payload_hint]
    print(f"tail_size={len(tail)} item_count={item_count}")

    # --- 1. Tail structure overview ---
    print("\n=== TAIL STRUCTURE ===")
    # First 256 bytes of tail
    print(f"tail_first_64_hex: {tail[:64].hex()}")
    print(f"tail_first_64_preview: ", end="")
    for b in tail[:64]:
        if is_cp1251_byte(b):
            print(cp1251_to_utf8(bytes([b])), end="")
        elif 32 <= b < 127:
            print(chr(b), end="")
        elif b == 0:
            print(".", end="")
        else:
            print(f"[{b:02x}]", end="")
    print()

    # Byte distribution in tail
    tail_dist = collections.Counter(tail[:100000])
    cp_count = sum(tail_dist[b] for b in range(256) if is_cp1251_byte(b))
    zero_count = tail_dist[0]
    print(f"tail_100k: cp1251={cp_count} zeros={zero_count} total=100000")
    print(f"tail_top_bytes={tail_dist.most_common(20)}")

    # --- 2. Find where cp1251 text regions start ---
    print("\n=== CP1251 TEXT REGIONS IN TAIL ===")
    BLOCK = 512
    for block_idx in range(min(len(tail) // BLOCK, 200)):
        chunk = tail[block_idx * BLOCK : (block_idx + 1) * BLOCK]
        cp_ratio = sum(1 for b in chunk if is_cp1251_byte(b)) / len(chunk)
        if cp_ratio > 0.40:
            abs_off = payload_hint + block_idx * BLOCK
            preview = ""
            for b in chunk[:80]:
                if is_cp1251_byte(b):
                    preview += cp1251_to_utf8(bytes([b]))
                elif 32 <= b < 127:
                    preview += chr(b)
                else:
                    preview += f"[{b:02x}]"
            print(f"  block[{block_idx}] abs={abs_off} cp_ratio={cp_ratio:.2f} preview={preview}")
            break

    # --- 3. Extract ALL cp1251 runs >= 2 chars from tail ---
    print("\n=== ALL CP1251 RUNS IN TAIL ===")
    runs = []
    i = 0
    while i < len(tail):
        if is_cp1251_byte(tail[i]):
            j = i
            while j < len(tail) and is_cp1251_byte(tail[j]):
                j += 1
            if j - i >= 2:
                text = cp1251_to_utf8(tail[i:j])
                runs.append((i, text))
            i = j
        else:
            i += 1

    print(f"total_runs={len(runs)}")
    if runs:
        lengths = [len(t) for _, t in runs]
        print(f"len_dist: min={min(lengths)} max={max(lengths)} mean={sum(lengths)/len(lengths):.1f}")
        len_dist = collections.Counter(lengths)
        print(f"len_histogram: {sorted(len_dist.items())[:20]}")

    # --- 4. Classify runs by what follows them ---
    print("\n=== WHAT FOLLOWS CP1251 RUNS ===")
    after_run = collections.Counter()
    for off, text in runs:
        end_pos = off + len(text.encode("cp1251"))
        if end_pos < len(tail):
            after_run[tail[end_pos]] += 1

    print(f"byte_after_run top: {after_run.most_common(20)}")
    for b, count in after_run.most_common(20):
        label = f"0x{b:02X}"
        if is_operator_byte(b):
            label += " (OPERATOR)"
        elif b == 0:
            label += " (NULL)"
        elif 32 <= b < 127:
            label += f" ('{chr(b)}')"
        print(f"  {label}: {count}")

    # --- 5. Try word extraction: null-terminated strings ---
    print("\n=== STRATEGY: NULL-TERMINATED CP1251 STRINGS ===")
    words_null = []
    i = 0
    while i < len(tail):
        if is_cp1251_byte(tail[i]):
            j = i
            while j < len(tail) and tail[j] != 0 and (is_cp1251_byte(tail[j]) or tail[j] == ord('-')):
                j += 1
            if j > i:
                text = tail[i:j].decode("cp1251", errors="replace")
                text = text.strip()
                if 2 <= len(text) <= 25 and text.replace("-", "").isalpha():
                    words_null.append(text)
            i = j + 1
        else:
            i += 1

    print(f"extracted={len(words_null)}")
    if words_null:
        # Print first 50
        for w in words_null[:50]:
            print(f"  {w}")

    # --- 6. Try word extraction: runs separated by specific operator bytes ---
    print("\n=== STRATEGY: OPERATOR-DELIMITED FRAGMENTS ===")
    # Group consecutive cp1251 runs that are separated by single operator bytes
    fragments = []
    i = 0
    while i < len(tail):
        if is_cp1251_byte(tail[i]):
            # Start a potential word
            word_bytes = bytearray()
            while i < len(tail):
                if is_cp1251_byte(tail[i]):
                    word_bytes.append(tail[i])
                    i += 1
                elif is_operator_byte(tail[i]) and i + 1 < len(tail) and is_cp1251_byte(tail[i + 1]):
                    # Operator followed by cp1251 = might be part of same entry, or separator
                    # For now, treat operator as word boundary
                    break
                else:
                    break
            if len(word_bytes) >= 2:
                text = bytes(word_bytes).decode("cp1251", errors="replace")
                if text.replace("-", "").isalpha():
                    fragments.append(text)
        else:
            i += 1

    print(f"extracted={len(fragments)}")
    if fragments:
        # Count unique
        unique = sorted(set(fragments), key=len)
        print(f"unique={len(unique)}")
        # Show distribution of lengths
        frag_lens = collections.Counter(len(f) for f in fragments)
        print(f"len_dist: {sorted(frag_lens.items())[:15]}")
        # Print samples at various lengths
        by_len = collections.defaultdict(list)
        for f in fragments:
            by_len[len(f)].append(f)
        for length in sorted(by_len.keys()):
            if length <= 1:
                continue
            samples = list(set(by_len[length]))[:8]
            print(f"  len={length}: {samples}")
            if length > 15:
                break

    # --- 7. Raw tail dump at interesting offsets ---
    print("\n=== RAW TAIL SAMPLES ===")
    interesting_offsets = [0, 100, 500, 1000, 5000, 10000, 50000, 100000, 200000, 500000]
    for off in interesting_offsets:
        if off >= len(tail):
            continue
        chunk = tail[off:off + 40]
        preview = ""
        for b in chunk:
            if is_cp1251_byte(b):
                preview += cp1251_to_utf8(bytes([b]))
            elif 32 <= b < 127:
                preview += chr(b)
            elif b == 0:
                preview += "."
            else:
                preview += f"[{b:02x}]"
        print(f"  tail+{off}: {preview}")

    # --- 8. Check if body itself contains structured sections ---
    print("\n=== BODY SECTION ANALYSIS ===")
    # The body might have different zones: bitvector zone + label zone + record zone
    BBLOCK = 4096
    body_blocks = len(body) // BBLOCK
    zones = []
    for bi in range(body_blocks):
        chunk = body[bi * BBLOCK : (bi + 1) * BBLOCK]
        cp_r = sum(1 for b in chunk if is_cp1251_byte(b)) / len(chunk)
        bit_d = sum(bin(b).count('1') for b in chunk) / (len(chunk) * 8)
        zero_r = chunk.count(0) / len(chunk)
        zones.append((bi, cp_r, bit_d, zero_r))

    # Find transitions between zone types
    prev_type = None
    for bi, cp_r, bit_d, zero_r in zones:
        if cp_r > 0.40:
            zone_type = "CP1251"
        elif bit_d > 0.30 and bit_d < 0.70 and cp_r < 0.20:
            zone_type = "BITVEC"
        elif zero_r > 0.30:
            zone_type = "SPARSE"
        else:
            zone_type = "MIXED"

        if zone_type != prev_type:
            abs_off = 24 + bi * BBLOCK
            print(f"  block[{bi}] abs={abs_off} type={zone_type} cp={cp_r:.2f} bits={bit_d:.2f} zeros={zero_r:.2f}")
            prev_type = zone_type

    print("\nDone.")


if __name__ == "__main__":
    main()
