#!/usr/bin/env python3
"""Decode compact trie from body remaining data using the byte encoding
discovered from disassembly of func_0x661528.

Byte encoding (from AArch64 disasm):
  bit 3 (0x08): back-reference - value is previous byte
  bit 7 (0x80): extended decode needed
  bit 6 (0x40): skip table involved
  bits 0-2: count/type when bit 3 set

Also test the 16-byte record structure from func_0x66131c.
"""
from __future__ import annotations
import struct
import pathlib
import collections


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def cp1251_char(b: int) -> str:
    return bytes([b]).decode("cp1251", errors="replace")


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\compact_trie_decode.txt"
    )

    data = trie_path.read_bytes()
    h = struct.unpack_from("<6I", data, 0)
    body = data[24:h[2]]
    tail = data[h[2]:]

    lines = []

    # Remaining body after bitvector+rank sections
    rest_start = 636376
    rest = body[rest_start:]
    lines.append(f"Remaining body: {len(rest)} bytes starting at body+{rest_start}\n")

    # --- Analyze structure of remaining data ---
    lines.append(f"\n=== REMAINING BODY STRUCTURE ===\n")
    lines.append(f"first_64_hex: {rest[:64].hex()}\n")

    # Check as u64 values
    u64s = [struct.unpack_from("<Q", rest, i)[0] for i in range(0, min(128, len(rest)), 8)]
    lines.append(f"first_16_u64: {[hex(v) for v in u64s[:16]]}\n")

    # Check as 16-byte records
    lines.append(f"\n=== AS 16-BYTE RECORDS ===\n")
    rec_count = len(rest) // 16
    lines.append(f"total_records: {rec_count}\n")

    # Show first 20 records
    for i in range(min(20, rec_count)):
        rec = rest[i*16:(i+1)*16]
        q1 = struct.unpack_from("<Q", rec, 0)[0]
        q2 = struct.unpack_from("<Q", rec, 8)[0]
        lines.append(f"  rec[{i}]: q1=0x{q1:016x} q2=0x{q2:016x} "
                     f"bytes={rec.hex()}\n")

    # --- Apply byte decoding from disasm ---
    lines.append(f"\n=== BYTE FLAG ANALYSIS ===\n")
    # Count bytes by flag pattern
    flag_counts = collections.Counter()
    for b in rest[:50000]:
        flags = ""
        if b & 0x80:
            flags += "H"  # bit 7 = extended
        if b & 0x40:
            flags += "S"  # bit 6 = skip
        if b & 0x08:
            flags += "B"  # bit 3 = back-ref
        if not flags:
            flags = "P"  # plain
        flag_counts[flags] += 1

    lines.append(f"Flag distribution (first 50K bytes):\n")
    for flags, count in flag_counts.most_common():
        pct = count / 50000 * 100
        lines.append(f"  {flags:5s}: {count:6d} ({pct:.1f}%)\n")

    # --- Apply to the WHOLE body (not just remaining) ---
    lines.append(f"\n=== BYTE FLAG ANALYSIS ON WHOLE BODY ===\n")
    whole_flags = collections.Counter()
    for b in body[:100000]:
        flags = ""
        if b & 0x80:
            flags += "H"
        if b & 0x40:
            flags += "S"
        if b & 0x08:
            flags += "B"
        if not flags:
            flags = "P"
        whole_flags[flags] += 1

    for flags, count in whole_flags.most_common():
        pct = count / 100000 * 100
        lines.append(f"  {flags:5s}: {count:6d} ({pct:.1f}%)\n")

    # --- Try to decode labels from remaining data ---
    lines.append(f"\n=== LABEL DECODE ATTEMPT ===\n")
    # The label reader: byte at position, if bit3 set → previous byte value
    # If bit7 set → extended decode
    # The byte value (after masking out flags) might be a label index

    # Try: lower 3 bits + bits 4-6 as label index (6 bits = 0-63)
    # Or: the function returns a byte value from a lookup table
    # Let's check what values the function returns after bit manipulation

    # From disasm: when bit3 is set, it returns the byte BEFORE current position
    # When bit3 is not set and bit7 is not set, it returns w19 (which was set to 0)
    # When bit3 is not set and bit7 IS set, it does complex lookup

    # Let's look at bytes with bit3 set and extract the "back-referenced" labels
    lines.append(f"\nBack-reference pairs (bit3 set, value = prev byte):\n")
    backref_vals = collections.Counter()
    for i in range(1, min(50000, len(rest))):
        if rest[i] & 0x08:
            prev = rest[i-1]
            backref_vals[prev] += 1

    for val, count in backref_vals.most_common(20):
        ch = cp1251_char(val) if is_cp1251_byte(val) else f"0x{val:02x}"
        lines.append(f"  prev=0x{val:02x} ({ch}): {count}\n")

    # --- Check if remaining body has recognizable patterns ---
    # Maybe it's the node data where each node is variable-length
    lines.append(f"\n=== BYTE VALUE DISTRIBUTION IN REMAINING ===\n")
    byte_dist = collections.Counter(rest[:100000])
    for val, count in byte_dist.most_common(30):
        ch = cp1251_char(val) if is_cp1251_byte(val) else ""
        flags = []
        if val & 0x80: flags.append("H")
        if val & 0x40: flags.append("S")
        if val & 0x08: flags.append("B")
        lines.append(f"  0x{val:02x} ({ch:2s}) [{'/'.join(flags):5s}]: {count:5d}\n")

    # --- Try another interpretation: the remaining data might have a header ---
    lines.append(f"\n=== REMAINING DATA HEADER ===\n")
    # First u32s
    first_u32s = [struct.unpack_from("<I", rest, i)[0] for i in range(0, 64, 4)]
    lines.append(f"first_16_u32: {[f'0x{v:08x}' for v in first_u32s]}\n")

    # Check for known values
    for i, v in enumerate(first_u32s):
        if v < 1000000 and v > 0:
            lines.append(f"  u32[{i}] = {v}\n")

    # Maybe first few bytes are: [count:u32] [entry_size:u32] [total_size:u32] etc
    lines.append(f"\n  As potential header:\n")
    lines.append(f"    [0] = {first_u32s[0]} (0x{first_u32s[0]:x})\n")
    lines.append(f"    [1] = {first_u32s[1]}\n")
    lines.append(f"    [2] = {first_u32s[2]}\n")
    lines.append(f"    [3] = {first_u32s[3]}\n")

    # The very first byte is 0xFF, then lots of zeros
    # ff 00 00 00 00 00 00 00 40 2c 31 00 00 00 00 00 b1 c4 00 00 ...
    # u64[0] = 0xFF = 255
    # u64[1] = 0x312c40 = 3222592
    # u64[2] = 0xc4b1 = 50353

    lines.append(f"\n  255 could be alphabet size (256-1)\n")
    lines.append(f"  3222592 = 0x312c40 — unknown\n")
    lines.append(f"  50353 = 0xc4b1 — unknown\n")

    # Check if 3222592 relates to label count or node count
    # 999936 bits / 8 = 124992 bytes per BV. 3222592 / 124992 = 25.78... no.
    # But 3222592 bytes would be in the tail region!
    lines.append(f"  3222592 as tail offset: tail[3222592:] would be near end of tail\n")
    lines.append(f"  tail_size = {len(tail)}, 3222592 < {len(tail)}: {3222592 < len(tail)}\n")

    if 3222592 < len(tail):
        sample = tail[3222592:3222592+32]
        lines.append(f"  tail[3222592:+32] hex: {sample.hex()}\n")
        cp_count = sum(1 for b in sample if is_cp1251_byte(b))
        lines.append(f"  cp1251 bytes in sample: {cp_count}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")


if __name__ == "__main__":
    main()
