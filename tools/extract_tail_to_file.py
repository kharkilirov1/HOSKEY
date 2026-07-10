#!/usr/bin/env python3
"""Extract tail fragments to UTF-8 file for inspection."""
from __future__ import annotations
import struct
import pathlib
import collections


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\tail_analysis.txt"
    )

    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    tail = data[payload_hint:]

    lines = []
    lines.append(f"tail_size={len(tail)} item_count={item_count}\n")

    # Extract all cp1251 runs with their offsets and what follows
    runs = []
    i = 0
    while i < len(tail):
        if is_cp1251_byte(tail[i]):
            j = i
            while j < len(tail) and is_cp1251_byte(tail[j]):
                j += 1
            text = tail[i:j].decode("cp1251", errors="replace")
            after = tail[j] if j < len(tail) else -1
            runs.append((i, text, after))
            i = j
        else:
            i += 1

    lines.append(f"total_cp1251_runs={len(runs)}\n")

    # --- Show runs by length ---
    by_len = collections.defaultdict(list)
    for off, text, after in runs:
        by_len[len(text)].append((off, text, after))

    lines.append("\n=== RUNS BY LENGTH (samples) ===\n")
    for length in sorted(by_len.keys()):
        samples = by_len[length][:10]
        sample_strs = [f"  {off}: '{text}' after=0x{after:02X}" for off, text, after in samples]
        lines.append(f"\nlen={length} count={len(by_len[length])}:\n")
        lines.extend(s + "\n" for s in sample_strs)

    # --- Show LONG runs (likely actual words or word parts) ---
    lines.append("\n\n=== LONG RUNS (>= 6 chars, first 200) ===\n")
    long_runs = [(off, text, after) for off, text, after in runs if len(text) >= 6]
    for off, text, after in long_runs[:200]:
        lines.append(f"  tail+{off}: '{text}' after=0x{after:02X}\n")

    # --- Try null-delimited words ---
    lines.append("\n\n=== NULL-DELIMITED WORDS (first 200) ===\n")
    words = []
    i = 0
    while i < len(tail):
        if tail[i] == 0:
            i += 1
            continue
        if is_cp1251_byte(tail[i]):
            j = i
            # Read until null or non-text byte (but allow hyphens, soft hyphens)
            while j < len(tail) and tail[j] != 0:
                if is_cp1251_byte(tail[j]) or tail[j] == ord('-'):
                    j += 1
                else:
                    break
            text = tail[i:j].decode("cp1251", errors="replace")
            if 3 <= len(text) <= 30:
                words.append((i, text))
            i = j
        else:
            i += 1

    lines.append(f"total_null_delimited={len(words)}\n")
    for off, text in words[:200]:
        lines.append(f"  tail+{off}: '{text}'\n")

    # --- Analyze the preamble (first 50K of tail) ---
    lines.append("\n\n=== TAIL PREAMBLE STRUCTURE (first 50K) ===\n")
    preamble = tail[:50000]
    byte_freq = collections.Counter(preamble)
    lines.append(f"top_bytes: {byte_freq.most_common(20)}\n")

    # Look for any structure in the preamble
    # It's mostly bytes 0x01, 0x02 with some control codes
    # Maybe it's a bitvector or a variable-length encoded structure?
    lines.append(f"\nbyte value distribution:\n")
    for b in range(256):
        c = byte_freq.get(b, 0)
        if c > 0:
            pct = c / len(preamble) * 100
            label = ""
            if is_cp1251_byte(b):
                label = f" ({bytes([b]).decode('cp1251')})"
            elif 32 <= b < 127:
                label = f" ('{chr(b)}')"
            lines.append(f"  0x{b:02X}{label}: {c} ({pct:.1f}%)\n")

    # --- Try: treat tail as having a header/index structure ---
    lines.append("\n\n=== TAIL INTERNAL STRUCTURE HYPOTHESIS ===\n")
    # The first bytes of tail are 0x0C, 0x02, 0x02, 0x01...
    # 0x0C = 12 decimal. Maybe it's a count or a type tag.
    # Check if the preamble has any u32 values that look like offsets into the string area
    lines.append(f"first_32_bytes: {tail[:32].hex()}\n")
    first_u32s = struct.unpack_from("<8I", tail, 0)
    lines.append(f"first_8_u32: {[hex(v) for v in first_u32s]}\n")

    # Check at the very end of the tail for a footer/index
    lines.append(f"\nlast_64_bytes: {tail[-64:].hex()}\n")
    last_u32s = struct.unpack_from("<16I", tail, len(tail) - 64)
    lines.append(f"last_16_u32: {[hex(v) for v in last_u32s]}\n")

    # Check if there's a section table at the end (yarchive style)
    last_u64 = struct.unpack_from("<Q", tail, len(tail) - 8)[0]
    lines.append(f"last_u64_as_offset: {last_u64} (0x{last_u64:X})\n")
    if 0 < last_u64 < len(tail) - 8:
        lines.append(f"  -> potentially valid section table offset within tail!\n")
        # Read section count
        st = tail[last_u64:]
        if len(st) >= 4:
            sec_count = struct.unpack_from("<I", st, 0)[0]
            lines.append(f"  section_count_at_offset={sec_count}\n")
            if 0 < sec_count < 200:
                lines.append(f"  PROMISING: {sec_count} sections\n")
                for si in range(min(sec_count, 20)):
                    if 4 + si * 20 + 20 <= len(st):
                        key, sec_off, sec_size = struct.unpack_from("<QQI", st, 4 + si * 20)
                        lines.append(f"  section[{si}]: key=0x{key:016X} offset={sec_off} size={sec_size}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")
    print(f"Total runs: {len(runs)}, long runs: {len(long_runs)}, null-words: {len(words)}")


if __name__ == "__main__":
    main()
