#!/usr/bin/env python3
"""
Extract words with frequency weights from Yandex dict_ru_full container.

The proprietary DFS trie format resists exact traversal (unknown backtrack
encoding, ~91K push vs ~5K pop markers), so we combine:
1. Consecutive UTF-8 Cyrillic sequence extraction (robust, catches all words)
2. Control byte proximity analysis (0x02-0x07 near words = weight tier)
3. Word quality filtering

The control byte nearest to a word's position is used as its frequency tier.
"""

import os
import sys
import re
import time
from collections import Counter, defaultdict

INPUT_FILE = r"C:\Users\Kharki\Desktop\yandex_extracted\dict_ru_full"
OUTPUT_DIR = r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools"

# Dense trie region: first ~375KB after header
TRIE_SIZE_LIMIT = 380_000

TIER_WEIGHTS = {
    0x02: 100, 0x03: 80, 0x04: 60,
    0x05: 40,  0x06: 20, 0x07: 10,
}
DEFAULT_WEIGHT = 5

RUSSIAN_LOWER = set("абвгдежзийклмнопрстуфхцчшщъыьэюя")


def find_binary_offset(data: bytes) -> int:
    """Find where binary trie data starts (after JSON header)."""
    try:
        json_start = data.index(b'{')
    except ValueError:
        return 0
    depth = 0
    for i in range(json_start, min(len(data), json_start + 20000)):
        if data[i] == ord('{'):
            depth += 1
        elif data[i] == ord('}'):
            depth -= 1
            if depth == 0:
                j = i + 1
                while j < len(data) and data[j] in (0x00, 0x20):
                    j += 1
                return j
    return 0


def is_valid_russian_word(word, min_len=2, max_len=32):
    if not (min_len <= len(word) <= max_len):
        return False
    return all(c in RUSSIAN_LOWER for c in word)


def has_vowel(word):
    return any(c in "аеиоуыэюя" for c in word)


def has_consonant(word):
    return any(c in "бвгджзйклмнпрстфхцчшщ" for c in word)


def max_consecutive(word, chars):
    mx = 0
    run = 0
    for c in word:
        if c in chars:
            run += 1
            mx = max(mx, run)
        else:
            run = 0
    return mx


def filter_word(word):
    """Quality filter for extracted words."""
    if not is_valid_russian_word(word):
        return False
    if len(set(word)) == 1 and len(word) > 1:
        return False
    if len(word) >= 3:
        if not has_vowel(word) or not has_consonant(word):
            return False
    if max_consecutive(word, "бвгджзйклмнпрстфхцчшщъь") > 4:
        return False
    if len(word) == 2 and not has_vowel(word):
        return False
    if word[0] in ('ь', 'ъ'):
        return False
    if len(word) > 18:
        return False
    # Validate hard sign
    if 'ъ' in word:
        consonants = set("бвгджзклмнпрстфхцчшщ")
        soft_vowels = set("еёюя")
        for idx, c in enumerate(word):
            if c == 'ъ':
                if idx == 0 or idx == len(word) - 1:
                    return False
                if word[idx-1] not in consonants or word[idx+1] not in soft_vowels:
                    return False
    return True


def is_cyrillic_letter(b1, b2):
    """Check if two bytes form a valid Russian lowercase UTF-8 char."""
    # а-п: D0 B0-BF
    if b1 == 0xD0 and 0xB0 <= b2 <= 0xBF:
        return bytes([b1, b2]).decode('utf-8')
    # р-я: D1 80-8F
    if b1 == 0xD1 and 0x80 <= b2 <= 0x8F:
        return bytes([b1, b2]).decode('utf-8')
    # Uppercase А-П: D0 90-9F -> lowercase
    if b1 == 0xD0 and 0x90 <= b2 <= 0xAF:
        return bytes([b1, b2]).decode('utf-8').lower()
    return None


def extract_words_and_weights(filepath):
    """Extract words via UTF-8 sequences and assign weights from nearby control bytes."""
    file_size = os.path.getsize(filepath)
    print(f"File: {file_size:,} bytes", flush=True)

    with open(filepath, 'rb') as f:
        data = f.read(TRIE_SIZE_LIMIT + 20000)

    offset = find_binary_offset(data)
    print(f"Binary offset: 0x{offset:X}", flush=True)

    blob = data[offset:offset + TRIE_SIZE_LIMIT]
    blob_size = len(blob)
    print(f"Trie blob: {blob_size:,} bytes", flush=True)

    # === Phase 1: Build control byte position map ===
    # Map each position to the nearest control byte value
    control_positions = []  # (position, ctrl_byte)
    i = 0
    while i < blob_size:
        b = blob[i]
        if 0x02 <= b <= 0x07:
            control_positions.append((i, b))
        i += 1

    print(f"Control bytes found: {len(control_positions):,}", flush=True)

    # === Phase 2: Extract consecutive Cyrillic UTF-8 sequences ===
    words_with_pos = []  # (word, start_pos, end_pos)
    i = 0
    current_chars = []
    start_pos = 0

    while i < blob_size - 1:
        ch = is_cyrillic_letter(blob[i], blob[i + 1])
        if ch:
            if not current_chars:
                start_pos = i
            current_chars.append(ch)
            i += 2
        else:
            if current_chars:
                word = ''.join(current_chars)
                if filter_word(word):
                    words_with_pos.append((word, start_pos, i))
                current_chars = []
            i += 1

    if current_chars:
        word = ''.join(current_chars)
        if filter_word(word):
            words_with_pos.append((word, start_pos, blob_size))

    print(f"Filtered word occurrences: {len(words_with_pos):,}", flush=True)

    # === Phase 3: Assign weights based on nearest control byte ===
    # For each word, find the closest control byte within a window
    SEARCH_WINDOW = 16  # bytes before/after the word

    control_counts = Counter()
    words_result = []  # (word, ctrl_byte_or_None, raw_weight_value)

    ctrl_idx = 0
    for word, wstart, wend in words_with_pos:
        # Find nearest control byte in [wstart - WINDOW, wend + WINDOW]
        search_lo = wstart - SEARCH_WINDOW
        search_hi = wend + SEARCH_WINDOW
        best_ctrl = None
        best_dist = SEARCH_WINDOW + 1

        # Advance ctrl_idx to be near our position
        while ctrl_idx < len(control_positions) and control_positions[ctrl_idx][0] < search_lo:
            ctrl_idx += 1

        # Scan forward from ctrl_idx
        j = max(0, ctrl_idx - 5)
        while j < len(control_positions):
            cpos, cval = control_positions[j]
            if cpos > search_hi:
                break
            if search_lo <= cpos <= search_hi:
                dist = min(abs(cpos - wstart), abs(cpos - wend))
                if dist < best_dist:
                    best_dist = dist
                    best_ctrl = cval
            j += 1

        if best_ctrl is not None:
            control_counts[best_ctrl] += 1

        words_result.append((word, best_ctrl))

    # === Phase 4: Deduplicate - keep best tier per word ===
    best = {}
    for word, ctrl in words_result:
        norm = TIER_WEIGHTS.get(ctrl, DEFAULT_WEIGHT) if ctrl is not None else DEFAULT_WEIGHT
        if word not in best or norm > best[word]:
            best[word] = norm

    result = [(w, best[w]) for w in sorted(best.keys())]
    print(f"Unique words: {len(result):,}", flush=True)

    return result, control_counts


def write_outputs(filtered, control_counts):
    tsv_path = os.path.join(OUTPUT_DIR, "words_with_weights.tsv")
    analysis_path = os.path.join(OUTPUT_DIR, "weight_analysis.txt")

    # TSV
    with open(tsv_path, 'w', encoding='utf-8') as f:
        f.write("word\traw_weight\tnormalized_weight\n")
        for word, norm in filtered:
            f.write(f"{word}\t{norm}\t{norm}\n")
    print(f"\nWrote {len(filtered):,} words -> {tsv_path}", flush=True)

    # Analysis
    with open(analysis_path, 'w', encoding='utf-8') as f:
        f.write("=== Weight Analysis for Yandex DFS Trie ===\n\n")

        f.write("--- Control Byte Proximity Distribution ---\n")
        total_ctrl = sum(control_counts.values())
        for c in sorted(control_counts.keys()):
            pct = control_counts[c] / total_ctrl * 100 if total_ctrl else 0
            f.write(f"  0x{c:02X} (tier {TIER_WEIGHTS.get(c, '?')}): "
                    f"{control_counts[c]:,} words ({pct:.1f}%)\n")

        f.write("\n--- Tier Mapping ---\n")
        for c in sorted(TIER_WEIGHTS.keys()):
            f.write(f"  0x{c:02X} -> weight {TIER_WEIGHTS[c]}\n")
        f.write(f"  (none) -> weight {DEFAULT_WEIGHT}\n")

        wd = Counter(n for _, n in filtered)
        f.write("\n--- Output Weight Distribution ---\n")
        for w in sorted(wd.keys(), reverse=True):
            f.write(f"  weight {w}: {wd[w]:,} words\n")
        f.write(f"\nTotal unique words (2-32 chars): {len(filtered):,}\n")

        # Length distribution
        len_dist = Counter(len(w) for w, _ in filtered)
        f.write("\n--- Word Length Distribution ---\n")
        for l in sorted(len_dist.keys()):
            f.write(f"  {l:2d} chars: {len_dist[l]:,}\n")

    print(f"Wrote analysis -> {analysis_path}", flush=True)

    # Console summary
    wd = Counter(n for _, n in filtered)
    print("\n=== Weight Distribution ===", flush=True)
    for w in sorted(wd.keys(), reverse=True):
        print(f"  weight {w}: {wd[w]:,} words")

    print("\n=== Samples per tier ===", flush=True)
    by_tier = defaultdict(list)
    for word, norm in filtered:
        if len(by_tier[norm]) < 10:
            by_tier[norm].append(word)
    for w in sorted(by_tier.keys(), reverse=True):
        print(f"  weight {w}: {', '.join(by_tier[w])}")

    # Length stats
    len_dist = Counter(len(w) for w, _ in filtered)
    print("\n=== Length Distribution ===", flush=True)
    for l in sorted(len_dist.keys()):
        bar = '#' * min(40, len_dist[l] // 100)
        print(f"  {l:2d}: {len_dist[l]:>5,} {bar}")


def main():
    print("=== Yandex dict_ru_full: Word + Weight Extraction ===", flush=True)
    start = time.time()
    filtered, cc = extract_words_and_weights(INPUT_FILE)
    write_outputs(filtered, cc)
    elapsed = time.time() - start
    print(f"\nCompleted in {elapsed:.1f}s", flush=True)


if __name__ == '__main__':
    main()
