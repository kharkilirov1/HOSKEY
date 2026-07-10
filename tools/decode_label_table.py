#!/usr/bin/env python3
"""Decode label index table from tail at offset 3222592.

Discovery: remaining body (363KB) uses small byte values (0-20) as label INDICES.
First record: alphabet_size=255, label_table_offset=3222592 (in tail).
"""
from __future__ import annotations
import struct
import pathlib


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def cp1251_char(b: int) -> str:
    return bytes([b]).decode("cp1251", errors="replace")


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\label_table_decode.txt"
    )

    data = trie_path.read_bytes()
    h = struct.unpack_from("<6I", data, 0)
    body = data[24:h[2]]
    tail = data[h[2]:]
    rest = body[636376:]

    lines = []

    # --- Header of remaining body ---
    lines.append("=== REMAINING BODY HEADER ===\n")
    # u64[0] = 0xFF = 255 (alphabet size)
    # u64[1] = 0x312C40 = 3222592 (tail offset)
    # u64[2] = 0xC4B1 = 50353
    alphabet_size = struct.unpack_from("<Q", rest, 0)[0]
    label_table_off = struct.unpack_from("<Q", rest, 8)[0]
    val3 = struct.unpack_from("<Q", rest, 16)[0]
    lines.append(f"alphabet_size = {alphabet_size}\n")
    lines.append(f"label_table_offset = {label_table_off}\n")
    lines.append(f"val3 = {val3}\n")

    # --- Read tail at offset 3222592 ---
    lines.append(f"\n=== TAIL AT OFFSET {label_table_off} ===\n")
    label_area = tail[label_table_off:]
    remaining_tail = len(tail) - label_table_off
    lines.append(f"remaining_tail = {remaining_tail} bytes\n")
    lines.append(f"first_256_hex: {label_area[:256].hex()}\n")
    lines.append(f"first_256_bytes: {list(label_area[:256])}\n")

    # Check if it's a label mapping table: index -> character
    lines.append(f"\n=== INTERPRET AS LABEL TABLE ===\n")

    # Maybe it's: [label_count:u32] [label_bytes...]
    label_count = struct.unpack_from("<I", label_area, 0)[0]
    lines.append(f"first_u32 = {label_count}\n")

    # Or direct: first 256 bytes are the label characters
    lines.append(f"\nDirect interpretation (first 64 bytes as label chars):\n")
    for i in range(64):
        b = label_area[i]
        ch = cp1251_char(b) if is_cp1251_byte(b) else f"0x{b:02x}"
        lines.append(f"  [{i:2d}] = 0x{b:02x} = {ch}\n")

    # Check if there's a cp1251 alphabet stored contiguously
    lines.append(f"\nSearch for alphabet pattern in label area:\n")
    for off in range(min(1000, len(label_area) - 32)):
        sample = label_area[off:off+32]
        cp_count = sum(1 for b in sample if is_cp1251_byte(b))
        if cp_count >= 25:
            chars = "".join(cp1251_char(b) for b in sample if is_cp1251_byte(b))
            lines.append(f"  offset {label_table_off}+{off}: {cp_count} cp1251 chars: {chars}\n")
            break

    # --- Maybe the label table is at the BEGINNING of the tail label arena ---
    lines.append(f"\n=== CHECK TAIL LABEL ARENA START ===\n")
    # Arena starts at tail+39936 where first cp1251 block is
    arena_start = 39936
    # The first label group is the root's children: абвгдежзийклмнопрстуфхцчшщыэюя
    first_group = []
    i = arena_start
    while i < len(tail) and is_cp1251_byte(tail[i]):
        first_group.append(tail[i])
        i += 1

    lines.append(f"First label group at tail+{arena_start}: {len(first_group)} bytes\n")
    lines.append(f"Characters: {''.join(cp1251_char(b) for b in first_group)}\n")
    lines.append(f"Byte values: {first_group}\n")

    # Create index-to-char mapping from sorted cp1251 alphabet
    # Russian lowercase: а=0xE0, б=0xE1, ..., я=0xFF
    sorted_alphabet = sorted(set(first_group))
    lines.append(f"\nSorted unique chars: {''.join(cp1251_char(b) for b in sorted_alphabet)}\n")
    lines.append(f"Sorted byte values: {sorted_alphabet}\n")
    lines.append(f"Count: {len(sorted_alphabet)}\n")

    # Create mapping: index -> cp1251 byte
    index_to_char = {}
    for idx, byte_val in enumerate(sorted_alphabet):
        index_to_char[idx] = cp1251_char(byte_val)
    lines.append(f"\nIndex-to-char mapping:\n")
    for idx, ch in sorted(index_to_char.items()):
        lines.append(f"  {idx:2d} -> {ch}\n")

    # --- Apply mapping to remaining body data ---
    lines.append(f"\n=== APPLY INDEX MAPPING TO REMAINING BODY ===\n")
    # Skip header (first 24 bytes = 3 u64s)
    node_data = rest[24:]
    lines.append(f"Node data: {len(node_data)} bytes\n")

    # Read first 200 bytes as label indices
    decoded = []
    for b in node_data[:200]:
        if b in index_to_char:
            decoded.append(index_to_char[b])
        elif b < len(sorted_alphabet):
            decoded.append(index_to_char.get(b, f"[{b}]"))
        else:
            decoded.append(f"[{b:02x}]")

    lines.append(f"First 200 bytes decoded: {''.join(decoded)}\n")

    # Also try: mask off flag bits, use remaining as index
    decoded_masked = []
    for b in node_data[:200]:
        idx = b & 0x37  # mask off bits 3,6,7 (flags), keep 0-2,4-5
        ch = index_to_char.get(idx, f"[{idx}]")
        flags = ""
        if b & 0x08: flags += "B"
        if b & 0x40: flags += "S"
        if b & 0x80: flags += "H"
        decoded_masked.append(f"{ch}{'(' + flags + ')' if flags else ''}")

    lines.append(f"\nMasked decode: {' '.join(decoded_masked[:100])}\n")

    # Try: lower 5 bits as index (0-31, enough for Russian alphabet)
    decoded_5bit = []
    for b in node_data[:200]:
        idx = b & 0x1F
        ch = index_to_char.get(idx, f"[{idx}]")
        decoded_5bit.append(ch)

    lines.append(f"\n5-bit index decode: {''.join(decoded_5bit[:200])}\n")

    # --- Check if val3 (50353) is useful ---
    lines.append(f"\n=== VAL3 = {val3} ===\n")
    # Maybe it's the node count for this section
    # Or the number of entries in some table
    lines.append(f"  {val3} * 16 = {val3 * 16}\n")
    lines.append(f"  len(node_data) / {val3} = {len(node_data) / val3:.2f}\n")
    lines.append(f"  len(rest) / {val3} = {len(rest) / val3:.2f}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")


if __name__ == "__main__":
    main()
