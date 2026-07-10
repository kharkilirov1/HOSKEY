#!/usr/bin/env python3
"""Try to decode the ENTIRE tail as a DFS-encoded trie traversal.

Hypothesis: the tail encodes a DFS walk of the trie where:
- cp1251 bytes (0xC0-0xFF, 0xA8, 0xB8) = character labels
- Various control bytes = navigation (push/pop/sibling)
- The preamble (bytes 0x00-0x20) = structural control for upper trie levels

Try multiple control byte interpretations to find one that produces Russian words.
"""
from __future__ import annotations
import pathlib
import struct
import collections


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def cp1251_char(b: int) -> str:
    return bytes([b]).decode("cp1251", errors="replace")


def is_cyrillic_word(s: str) -> bool:
    if len(s) < 2 or len(s) > 25:
        return False
    return all(c.isalpha() for c in s) and any('\u0400' <= c <= '\u04FF' for c in s)


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\dfs_decode_result.txt"
    )

    data = trie_path.read_bytes()
    h = struct.unpack_from("<6I", data, 0)
    tail = data[h[2]:]

    lines = []
    lines.append(f"tail_size={len(tail)}\n")

    # --- Strategy A: 0x00=pop, cp1251=label+push, everything else=skip ---
    lines.append("\n=== STRATEGY A: 0x00=pop, cp1251=push+label ===\n")
    path = []
    words_a = []
    # Also try: 0x00=word_end(terminal), then pop to restore
    for start_off in [0, 39936, 39975]:
        path.clear()
        words_a.clear()
        for i in range(start_off, min(start_off + 500000, len(tail))):
            b = tail[i]
            if is_cp1251_byte(b):
                path.append(cp1251_char(b))
            elif b == 0x00:
                word = "".join(path)
                if is_cyrillic_word(word):
                    words_a.append(word)
                if path:
                    path.pop()
            elif b < 0x20:
                # Small control byte: maybe pop N levels?
                for _ in range(min(b, len(path))):
                    path.pop()

        lines.append(f"  start={start_off}: found {len(words_a)} words\n")
        if words_a:
            lines.append(f"  first_30: {words_a[:30]}\n")

    # --- Strategy B: null-terminated, sequential ---
    lines.append("\n=== STRATEGY B: null-terminated sequential strings ===\n")
    for start_off in [0, 39936, 39975]:
        words_b = []
        buf = []
        for i in range(start_off, min(start_off + 2000000, len(tail))):
            b = tail[i]
            if is_cp1251_byte(b):
                buf.append(cp1251_char(b))
            elif b == 0x00 and buf:
                word = "".join(buf)
                if is_cyrillic_word(word):
                    words_b.append(word)
                buf.clear()
            elif not is_cp1251_byte(b):
                if buf:
                    word = "".join(buf)
                    if is_cyrillic_word(word):
                        words_b.append(word)
                    buf.clear()

        lines.append(f"  start={start_off}: found {len(words_b)} cyrillic words\n")
        if words_b:
            unique = sorted(set(words_b), key=len, reverse=True)
            lines.append(f"  unique={len(unique)}\n")
            lines.append(f"  longest_20: {unique[:20]}\n")
            # Check if any are real Russian words
            known_words = {"привет", "слово", "мама", "папа", "дом", "кот", "мир",
                          "день", "ночь", "год", "время", "жизнь", "работа", "дело",
                          "рука", "глаз", "город", "страна", "земля", "вода",
                          "человек", "ребенок", "женщина", "мужчина"}
            found_known = [w for w in words_b if w.lower() in known_words]
            lines.append(f"  known_words_found: {found_known[:20]}\n")

    # --- Strategy C: the entire tail as interleaved level data ---
    # Maybe each "level" of the trie is encoded sequentially
    # Level 0: root children labels (30 chars)
    # Level 1: all children of level 0 nodes
    # etc.
    lines.append("\n=== STRATEGY C: level-wise label extraction ===\n")
    arena_start = 39975  # where first full alphabet appears
    arena = tail[arena_start:]

    # Read sequential cp1251 blocks as levels
    levels = []
    i = 0
    while i < len(arena) and len(levels) < 20:
        if is_cp1251_byte(arena[i]):
            block = []
            while i < len(arena) and is_cp1251_byte(arena[i]):
                block.append(cp1251_char(arena[i]))
                i += 1
            levels.append(block)
        else:
            i += 1

    for li, level in enumerate(levels[:10]):
        lines.append(f"  level[{li}]: {len(level)} labels: {''.join(level[:50])}\n")

    # --- Strategy D: interpret the body header (5752 bytes) as trie structure ---
    lines.append("\n=== STRATEGY D: body header as trie node table ===\n")
    body = data[24:h[2]]
    hdr = body[:5752]
    # Maybe it's a table of (child_count:u8, label_offset:u32) per node?
    # Or a compact table of node pointers?
    # Check: 5752 / 2 = 2876 entries (u16 per entry)
    # 5752 / 4 = 1438 entries (u32 per entry)
    # 5752 / 8 = 719 entries (u64 per entry)

    # Check if it's popcount-like (monotonically increasing)
    u16s = [struct.unpack_from("<H", hdr, i)[0] for i in range(0, 200, 2)]
    lines.append(f"  header as u16 (first 100): {u16s[:50]}\n")

    # Check pattern
    hdr_bytes_dist = collections.Counter(hdr[:200])
    lines.append(f"  header byte dist (first 200): {hdr_bytes_dist.most_common(10)}\n")

    # --- Strategy E: try treating body+636376 (remaining data) as labels ---
    lines.append("\n=== STRATEGY E: remaining body data (after BV4+R4) ===\n")
    rest = body[636376:]
    lines.append(f"  size={len(rest)}B\n")
    lines.append(f"  first_32_hex: {rest[:32].hex()}\n")

    cp_count = sum(1 for b in rest[:10000] if is_cp1251_byte(b))
    zero_count = rest[:10000].count(0)
    lines.append(f"  first_10KB: cp1251={cp_count} zeros={zero_count}\n")

    # Extract any cp1251 strings
    words_e = []
    buf = []
    for b in rest[:100000]:
        if is_cp1251_byte(b):
            buf.append(cp1251_char(b))
        else:
            if len(buf) >= 2:
                word = "".join(buf)
                if is_cyrillic_word(word):
                    words_e.append(word)
            buf.clear()

    lines.append(f"  cyrillic words in first 100KB: {len(words_e)}\n")
    if words_e:
        lines.append(f"  longest_20: {sorted(set(words_e), key=len, reverse=True)[:20]}\n")

    # --- Strategy F: try reading body header as label_low byte array ---
    # The header (5752 bytes) is all 0xFx bytes (very dense bitvector)
    # But what if it's actually a LABEL array encoded in cp1251?
    lines.append("\n=== STRATEGY F: body header as cp1251 label sequence ===\n")
    hdr_chars = [cp1251_char(b) for b in hdr if is_cp1251_byte(b)]
    lines.append(f"  cp1251 chars in header: {len(hdr_chars)}\n")
    lines.append(f"  first_100: {''.join(hdr_chars[:100])}\n")
    # Check if sorted
    if hdr_chars:
        sorted_count = sum(1 for i in range(len(hdr_chars)-1) if hdr_chars[i] <= hdr_chars[i+1])
        lines.append(f"  sorted_ratio: {sorted_count/(len(hdr_chars)-1):.3f}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")

    # Print summary
    for line in lines:
        if "found" in line or "unique" in line or "known" in line or "longest" in line:
            print(line.rstrip())


if __name__ == "__main__":
    main()
