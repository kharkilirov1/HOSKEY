#!/usr/bin/env python3
"""
Yandex Keyboard Trie Word Extractor
====================================
Extracts Russian words from Yandex Keyboard dict_ru_full container
and the extracted trie section.

The binary data uses a proprietary interleaved DFS format with:
- 0x40 (NODE), 0x48 (TERM), 0x50 (LEAF), 0x58 (XTND) markers
- 0x20 (SPACE) for backtracking
- 0xD0/0xD1 + 0x80-0xBF for UTF-8 Cyrillic characters
- 0x02-0x07 control/weight bytes with variable trailing data

Since the exact control byte lengths are unknown (proprietary),
we use a multi-strategy approach:
1. Extract all consecutive UTF-8 Cyrillic sequences
2. DFS parsing with heuristic control byte skipping
3. Merge and filter results
"""

import os
import sys
import struct
import re
from collections import Counter, defaultdict
from typing import Set, List, Tuple, Optional


# ── Paths ──────────────────────────────────────────────────────────
DICT_RU_FULL = r"C:\Users\Kharki\Desktop\yandex_extracted\dict_ru_full"
TRIE_FILE = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"
KB_TRIE = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\KEYBOARD-13421_trie"
OUTPUT_FILE = r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\extracted_words.txt"

# ── Constants ──────────────────────────────────────────────────────
RUSSIAN_LOWER = set("абвгдежзийклмнопрстуфхцчшщъыьэюя")
# Note: ё is excluded because Yandex dict stores ё as е


def is_valid_russian_word(word: str, min_len: int = 2, max_len: int = 32) -> bool:
    """Check if a word is a plausible Russian word."""
    if not (min_len <= len(word) <= max_len):
        return False
    return all(c in RUSSIAN_LOWER for c in word)


def is_cyrillic_letter(b1: int, b2: int) -> Optional[str]:
    """Check if two bytes form a valid Russian UTF-8 character.
    Returns the character or None."""
    # Lowercase а-п: D0 B0-BF
    if b1 == 0xD0 and 0xB0 <= b2 <= 0xBF:
        return bytes([b1, b2]).decode('utf-8')
    # Lowercase р-я: D1 80-8F
    if b1 == 0xD1 and 0x80 <= b2 <= 0x8F:
        return bytes([b1, b2]).decode('utf-8')
    # ё: D1 91 -- Yandex dict stores ё as е, skip to avoid artifacts
    # if b1 == 0xD1 and b2 == 0x91:
    #     return 'ё'
    # Uppercase А-Я: D0 90-AF -> lowercase
    if b1 == 0xD0 and 0x90 <= b2 <= 0xAF:
        return bytes([b1, b2]).decode('utf-8').lower()
    # Ё: D0 81 -> ё -- Yandex dict stores ё as е, skip
    # if b1 == 0xD0 and b2 == 0x81:
    #     return 'ё'
    return None


# ── Strategy 1: Consecutive UTF-8 sequences ───────────────────────

def extract_utf8_sequences(data: bytes, start_offset: int = 0) -> Set[str]:
    """Extract all consecutive runs of Cyrillic UTF-8 characters."""
    words = set()
    i = start_offset
    current = []

    while i < len(data) - 1:
        ch = is_cyrillic_letter(data[i], data[i + 1])
        if ch:
            current.append(ch)
            i += 2
        else:
            if current:
                word = ''.join(current)
                if is_valid_russian_word(word):
                    words.add(word)
                current = []
            i += 1

    if current:
        word = ''.join(current)
        if is_valid_russian_word(word):
            words.add(word)

    return words


# ── Strategy 2: DFS with marker-aware parsing ─────────────────────

MARKERS = {0x40, 0x48, 0x50, 0x58}  # NODE, TERM, LEAF, XTND


def extract_dfs_words(data: bytes, start_offset: int = 0,
                      max_bytes: int = 0) -> Set[str]:
    """Parse interleaved DFS format with sticky D0/D1 hi-byte state.

    Each trie node is: MARKER [D0|D1] lo_byte [control_data...]
    SPACE (0x20) = backtrack to parent.
    TERM (0x48) = current stack forms a word.
    """
    if max_bytes <= 0:
        max_bytes = len(data) - start_offset

    words = set()
    stack = []
    hi_byte = 0xD0
    i = start_offset
    end = min(start_offset + max_bytes, len(data))

    while i < end:
        b = data[i]

        if b == 0x20:  # SPACE = backtrack
            if stack:
                stack.pop()
            i += 1
            continue

        if b in MARKERS:
            is_term = (b == 0x48)
            is_leaf = (b == 0x50)
            i += 1

            # Read the character for this node
            got_char = False
            while i < end:
                nb = data[i]

                if nb == 0xD0 or nb == 0xD1:
                    hi_byte = nb
                    i += 1
                    continue

                if 0x80 <= nb <= 0xBF:
                    # This is a letter low byte
                    try:
                        ch = bytes([hi_byte, nb]).decode('utf-8')
                        # Only accept valid Russian letters
                        lc = ch.lower()
                        if lc in RUSSIAN_LOWER:
                            stack.append(lc)
                            got_char = True
                    except (UnicodeDecodeError, ValueError):
                        pass
                    i += 1
                    break

                if nb in MARKERS or nb == 0x20:
                    # Next marker without a letter (e.g., SPACE after TERM)
                    break

                # Control/weight byte -- skip
                i += 1
                # Skip remaining control data until next known byte
                while i < end:
                    peek = data[i]
                    if peek in MARKERS or peek == 0x20 or peek == 0xD0 or peek == 0xD1:
                        break
                    if 0x80 <= peek <= 0xBF:
                        # Could be letter -- check if preceded by D0/D1
                        break
                    i += 1
                break

            # Emit word at TERM nodes
            if is_term and stack:
                word = ''.join(stack)
                if is_valid_russian_word(word):
                    words.add(word)

            # LEAF nodes: character added, then pop (no children)
            if is_leaf and got_char and stack:
                word = ''.join(stack)
                if is_valid_russian_word(word):
                    words.add(word)
                stack.pop()

            continue

        if b == 0xD0 or b == 0xD1:
            hi_byte = b
            i += 1
            continue

        # Unknown byte -- skip
        i += 1

    return words


# ── Strategy 3: Regex scan for word-like patterns ──────────────────

def extract_regex_words(data: bytes, start_offset: int = 0) -> Set[str]:
    """Scan for plausible Russian words using regex on decoded text.
    Handles cases where words span across minor non-letter gaps."""
    words = set()

    # Decode as UTF-8 with error replacement
    text = data[start_offset:].decode('utf-8', errors='replace')

    # Find sequences of Russian letters (including across replacement chars)
    # Standard pattern: 2+ consecutive Russian lowercase letters
    pattern = re.compile(r'[а-яё]{2,32}')
    for m in pattern.finditer(text.lower()):
        word = m.group()
        if is_valid_russian_word(word):
            words.add(word)

    return words


# ── Main extraction pipeline ──────────────────────────────────────

def find_binary_offset(data: bytes) -> int:
    """Find where binary trie data starts in dict_ru_full."""
    # JSON starts at offset 32, find its end
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
                # Skip padding after JSON
                j = i + 1
                while j < len(data) and data[j] in (0x00, 0x20):
                    j += 1
                return j
    return 0


def extract_from_file(filepath: str, label: str) -> Set[str]:
    """Extract words from a single file using all strategies."""
    if not os.path.exists(filepath):
        print(f"  [{label}] File not found: {filepath}")
        return set()

    size = os.path.getsize(filepath)
    print(f"  [{label}] Loading {filepath} ({size:,} bytes)...")

    with open(filepath, 'rb') as f:
        data = f.read()

    # Determine start offset
    if filepath == DICT_RU_FULL:
        offset = find_binary_offset(data)
        print(f"  [{label}] Binary data starts at offset {offset} (0x{offset:x})")
    else:
        offset = 0

    all_words = set()

    # Strategy 1: UTF-8 sequences
    print(f"  [{label}] Strategy 1: UTF-8 consecutive sequences...")
    words1 = extract_utf8_sequences(data, offset)
    print(f"  [{label}]   -> {len(words1):,} words")
    all_words |= words1

    # Strategy 2: DFS parsing (only for dict_ru_full, first 50MB)
    if filepath == DICT_RU_FULL:
        print(f"  [{label}] Strategy 2: DFS marker-aware parsing (first 50MB)...")
        words2 = extract_dfs_words(data, offset, max_bytes=50_000_000)
        print(f"  [{label}]   -> {len(words2):,} words")
        all_words |= words2

    # Strategy 3: Regex
    print(f"  [{label}] Strategy 3: Regex scan...")
    words3 = extract_regex_words(data, offset)
    print(f"  [{label}]   -> {len(words3):,} words")
    all_words |= words3

    print(f"  [{label}] Total unique: {len(all_words):,}")
    return all_words


def has_vowel(word: str) -> bool:
    """Check if word has at least one vowel."""
    vowels = set("аеиоуыэюя")
    return any(c in vowels for c in word)


def has_consonant(word: str) -> bool:
    """Check if word has at least one consonant."""
    consonants = set("бвгджзйклмнпрстфхцчшщ")
    return any(c in consonants for c in word)


def max_consecutive_consonants(word: str) -> int:
    """Count maximum consecutive consonants in a word."""
    consonants = set("бвгджзйклмнпрстфхцчшщъь")
    max_run = 0
    run = 0
    for c in word:
        if c in consonants:
            run += 1
            max_run = max(max_run, run)
        else:
            run = 0
    return max_run


def filter_words(words: Set[str]) -> List[str]:
    """Apply quality filters to extracted words."""
    filtered = set()

    for word in words:
        # Length filter
        if len(word) < 2 or len(word) > 30:
            continue

        # Must be all Russian lowercase
        if not all(c in RUSSIAN_LOWER for c in word):
            continue

        # Skip words that are just repeated characters (ааа, ббб)
        if len(set(word)) == 1 and len(word) > 1:
            continue

        # Words of 3+ chars should have both vowels and consonants
        if len(word) >= 3:
            if not has_vowel(word):
                continue
            if not has_consonant(word):
                continue

        # Skip words with too many consecutive consonants (> 4)
        if max_consecutive_consonants(word) > 4:
            continue

        # Skip words that are full alphabet runs
        if len(word) > 15:
            unique_ratio = len(set(word)) / len(word)
            if unique_ratio > 0.85:
                continue

        # Skip 2-letter words that aren't plausible
        if len(word) == 2:
            if not has_vowel(word):
                continue

        # Validate ъ usage: must be between consonant and soft vowel (е, ё, ю, я)
        if 'ъ' in word:
            valid_hard_sign = True
            for idx, c in enumerate(word):
                if c == 'ъ':
                    consonants = set("бвгджзклмнпрстфхцчшщ")
                    soft_vowels = set("еёюя")
                    if idx == 0 or idx == len(word) - 1:
                        valid_hard_sign = False
                        break
                    if word[idx - 1] not in consonants or word[idx + 1] not in soft_vowels:
                        valid_hard_sign = False
                        break
            if not valid_hard_sign:
                continue

        # ё words are not expected (Yandex stores ё as е)
        if 'ё' in word:
            continue

        # Additional garbage filter: reject words with unlikely bigrams
        # (consecutive ъ, ь at start, etc.)
        if word[0] == 'ь' or word[0] == 'ъ':
            continue

        # Reject words that have 3+ vowels in a row (extremely rare in Russian)
        vowels = set("аеиоуыэюя")
        vowel_run = 0
        max_vowel_run = 0
        for c in word:
            if c in vowels:
                vowel_run += 1
                max_vowel_run = max(max_vowel_run, vowel_run)
            else:
                vowel_run = 0
        if max_vowel_run >= 3 and len(word) < 6:
            continue

        # Reject 2-letter words that are two vowels (except known: ау, ею, её, ая, ия, etc.)
        VALID_2VOWEL = {"ау", "ею", "её", "ая", "ия", "ою", "ею", "ии", "яи"}
        if len(word) == 2 and word[0] in vowels and word[1] in vowels:
            if word not in VALID_2VOWEL:
                continue

        # Reject long words (>15 chars) with very low "naturalness" score
        # Real Russian words rarely exceed 18 chars
        if len(word) > 18:
            continue

        # (ё-related filter removed -- ё is not extracted)

        filtered.add(word)

    return sorted(filtered)


def print_stats(words: List[str]):
    """Print extraction statistics."""
    print(f"\n{'='*60}")
    print(f"EXTRACTION STATISTICS")
    print(f"{'='*60}")
    print(f"Total unique words: {len(words):,}")

    # Length distribution
    len_dist = Counter(len(w) for w in words)
    print(f"\nLength distribution:")
    for l in sorted(len_dist.keys()):
        bar = '#' * min(50, len_dist[l] // 500)
        print(f"  {l:2d}: {len_dist[l]:>6,} {bar}")

    # First/last letter distribution
    first_letters = Counter(w[0] for w in words)
    print(f"\nFirst letter distribution (top 15):")
    for letter, cnt in first_letters.most_common(15):
        print(f"  {letter}: {cnt:>6,}")

    # Sample words
    print(f"\nSample words (every ~{max(1, len(words)//20)}th):")
    step = max(1, len(words) // 20)
    for i in range(0, len(words), step):
        print(f"  {words[i]}")

    print(f"\nFirst 20: {', '.join(words[:20])}")
    print(f"Last 20: {', '.join(words[-20:])}")


def main():
    print("=" * 60)
    print("Yandex Keyboard Dictionary Word Extractor")
    print("=" * 60)

    all_words = set()

    # Extract from dict_ru_full
    print(f"\n[1] Processing dict_ru_full...")
    words1 = extract_from_file(DICT_RU_FULL, "dict_ru_full")
    all_words |= words1

    # Extract from extracted trie section
    print(f"\n[2] Processing extracted trie...")
    words2 = extract_from_file(TRIE_FILE, "trie")
    all_words |= words2

    # Extract from KEYBOARD-13421_trie
    print(f"\n[3] Processing KEYBOARD-13421_trie...")
    words3 = extract_from_file(KB_TRIE, "kb_trie")
    all_words |= words3

    # Filter
    print(f"\n[4] Filtering...")
    print(f"  Before filter: {len(all_words):,}")
    filtered = filter_words(all_words)
    print(f"  After filter:  {len(filtered):,}")

    # Statistics
    print_stats(filtered)

    # Save
    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        for word in filtered:
            f.write(word + '\n')

    print(f"\nSaved to: {OUTPUT_FILE}")
    print(f"Total words: {len(filtered):,}")
    print("Done.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
