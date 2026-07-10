#!/usr/bin/env python3
"""Probe charset registry entries from Ghidra-dumped codepage_ptrs.

Goals:
- resolve runtime charset IDs for the first 32 codepage registry entries
- classify which IDs look CP866-like vs direct Cyrillic (CP1251-like) vs other families
- narrow the most likely parser codepage_id values for Russian trie traversal
"""

from __future__ import annotations

import re
import struct
from pathlib import Path

BIN_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\native_libs\x86_64\libjni_ykeyboard3.so")
ALPHABET_DUMP_PATH = Path(r"C:\Users\Kharki\clawd\v600_analysis\phase2_3_results\alphabet_data.txt")
GHIDRA_IMAGE_DELTA = 0x104000


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def read_codepage_ptrs(text: str) -> list[int]:
    lines: list[str] = []
    capture = False
    for line in text.splitlines():
        if "codepage_ptrs @ 0x00a09840" in line:
            capture = True
            continue
        if capture:
            if line.startswith("// As uint16"):
                break
            if line.startswith("// ") and ":" in line:
                lines.append(line)
    hexbytes: list[str] = []
    for line in lines:
        hexbytes.extend(re.findall(r"\b[0-9a-fA-F]{2}\b", line.split(":", 1)[1]))
    raw = bytes(int(b, 16) for b in hexbytes)
    return [struct.unpack_from("<Q", raw, i)[0] for i in range(0, len(raw), 8)]


def safe_chr(cp: int) -> str:
    if cp == 0:
        return "."
    try:
        ch = chr(cp)
    except ValueError:
        return "?"
    if ch.isprintable() and ch not in {"\r", "\n", "\t"}:
        return ch
    return "."


def family_label(c0: int, e0: int, ed: int, ff: int) -> str:
    if (c0, e0, ed, ff) == (0x0420, 0x0440, 0x044D, 0x045F):
        return "direct Cyrillic / CP1251-like"
    if (c0, e0, ed, ff) == (0x2514, 0x0440, 0x044D, 0x00A0):
        return "CP866-like / box-drawing Cyrillic"
    if (c0, e0, ed, ff) == (0x00C0, 0x00E0, 0x00ED, 0x00FF):
        return "direct byte identity / Latin-1-like"
    if e0 == 0x0640:
        return "Arabic family"
    if e0 in {0x03B1, 0x03C9}:
        return "Greek family"
    if e0 in {0x0E40, 0x0E20}:
        return "Thai/Lao family"
    if e0 == 0x00D3:
        return "Central/Eastern European family"
    if c0 == 0 and e0 == 0 and ed == 0 and ff == 0:
        return "empty / placeholder"
    return "other"


def main() -> None:
    bin_data = BIN_PATH.read_bytes()
    dump_text = ALPHABET_DUMP_PATH.read_text(encoding="utf-8", errors="ignore")
    ptrs = read_codepage_ptrs(dump_text)

    print(f"BIN_PATH={BIN_PATH}")
    print(f"ALPHABET_DUMP_PATH={ALPHABET_DUMP_PATH}")
    print(f"GHIDRA_IMAGE_DELTA=0x{GHIDRA_IMAGE_DELTA:X}")
    print(f"pointer_count={len(ptrs)}")
    print()

    likely_russian: list[tuple[int, int]] = []
    legacy_cyr: list[tuple[int, int]] = []

    for idx, ptr in enumerate(ptrs):
        off = ptr - GHIDRA_IMAGE_DELTA
        entry_id = u32(bin_data, off)
        c0 = u32(bin_data, off + 0xF8 + 0xC0 * 4)
        e0 = u32(bin_data, off + 0xF8 + 0xE0 * 4)
        ed = u32(bin_data, off + 0xF8 + 0xED * 4)
        ff = u32(bin_data, off + 0xF8 + 0xFF * 4)
        label = family_label(c0, e0, ed, ff)
        sample = f"C0={safe_chr(c0)} U+{c0:04X}, E0={safe_chr(e0)} U+{e0:04X}, ED={safe_chr(ed)} U+{ed:04X}, FF={safe_chr(ff)} U+{ff:04X}"
        print(f"[{idx:02d}] ptr=0x{ptr:08X} file_off=0x{off:X} id={entry_id:>3} family={label} | {sample}")
        if label == "direct Cyrillic / CP1251-like":
            likely_russian.append((idx, entry_id))
        elif label == "CP866-like / box-drawing Cyrillic":
            legacy_cyr.append((idx, entry_id))

    print()
    print("Likely Russian direct-Cyrillic entries (inference):")
    for idx, entry_id in likely_russian:
        print(f"  registry_index={idx} codepage_id={entry_id}")
    print()
    print("Legacy CP866-like Cyrillic entries:")
    for idx, entry_id in legacy_cyr:
        print(f"  registry_index={idx} codepage_id={entry_id}")


if __name__ == "__main__":
    main()
