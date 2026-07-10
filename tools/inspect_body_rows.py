#!/usr/bin/env python3
"""Inspect body rows byte-by-byte to understand per-row structure.

Focus: first few rows (Level 0 of trie) and known BITVEC/LABEL boundaries.
"""
from __future__ import annotations
import struct
import pathlib
import sys


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def cp1251_char(b: int) -> str:
    if is_cp1251_byte(b):
        return bytes([b]).decode("cp1251", errors="replace")
    return ""


def byte_class(b: int) -> str:
    if b == 0:
        return "."
    if is_cp1251_byte(b):
        return "L"  # Label
    if b < 0x20:
        return "c"  # Control/small
    if 0x20 <= b < 0x80:
        return "a"  # ASCII printable
    return "h"  # High non-cp1251


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\row_inspection.txt"
    )

    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    body = data[24:payload_hint]
    body_rows = len(body) // 64

    lines = []

    # --- Header detail ---
    lines.append("=== HEADER ===\n")
    for i in range(6):
        v = header[i]
        lines.append(f"  dword[{i}]: {v} (0x{v:08X})\n")

    # --- First 20 rows byte-by-byte ---
    lines.append("\n=== FIRST 20 ROWS (byte-by-byte) ===\n")
    for ri in range(min(20, body_rows)):
        row = body[ri * 64 : (ri + 1) * 64]
        abs_off = 24 + ri * 64
        lines.append(f"\nrow[{ri}] abs={abs_off}:\n")
        lines.append(f"  hex: {row.hex()}\n")

        # Byte classification map
        classmap = "".join(byte_class(b) for b in row)
        lines.append(f"  cls: {classmap}\n")

        # CP1251 characters inline
        cp_line = ""
        for b in row:
            c = cp1251_char(b)
            if c:
                cp_line += c
            else:
                cp_line += " "
        lines.append(f"  cp1: {cp_line}\n")

        # As u16 LE pairs
        u16s = [struct.unpack_from("<H", row, i)[0] for i in range(0, 64, 2)]
        lines.append(f"  u16: {[hex(v) for v in u16s]}\n")

        # As u32 LE quads
        u32s = [struct.unpack_from("<I", row, i)[0] for i in range(0, 64, 4)]
        lines.append(f"  u32: {[hex(v) for v in u32s]}\n")

    # --- Rows around major zone transitions ---
    lines.append("\n\n=== ZONE TRANSITION ROWS ===\n")
    transitions = [75, 76, 77, 78,  # first LABEL->non-LABEL transition
                   137, 138,  # first clear BITVEC row
                   5415, 5416, 5417, 5418,  # approaching big LABEL zone
                   6386, 6387, 6388, 6389,  # start of biggest LABEL section
                   7012, 7013, 7014, 7015,  # end of biggest LABEL section
                   10370, 10371,  # start of biggest BITVEC section
                   ]
    for ri in transitions:
        if ri >= body_rows:
            continue
        row = body[ri * 64 : (ri + 1) * 64]
        abs_off = 24 + ri * 64
        cp = sum(1 for b in row if is_cp1251_byte(b))
        pc = sum(bin(b).count('1') for b in row)
        classmap = "".join(byte_class(b) for b in row)

        cp_text = ""
        if cp > 5:
            cp_text = bytes(b for b in row if is_cp1251_byte(b)).decode("cp1251", errors="replace")

        lines.append(f"\nrow[{ri}] abs={abs_off} cp={cp} pop={pc}:\n")
        lines.append(f"  hex: {row.hex()}\n")
        lines.append(f"  cls: {classmap}\n")
        if cp_text:
            lines.append(f"  cp1: '{cp_text}'\n")

    # --- Check if row structure has fixed-position fields ---
    lines.append("\n\n=== FIXED FIELD ANALYSIS (first 100 rows) ===\n")
    # For each byte position 0-63, compute: % cp1251, % zero, mean value
    pos_cp = [0] * 64
    pos_zero = [0] * 64
    pos_sum = [0] * 64
    n = min(100, body_rows)
    for ri in range(n):
        row = body[ri * 64 : (ri + 1) * 64]
        for j in range(64):
            if is_cp1251_byte(row[j]):
                pos_cp[j] += 1
            if row[j] == 0:
                pos_zero[j] += 1
            pos_sum[j] += row[j]

    lines.append("Position analysis (first 100 rows):\n")
    lines.append("pos  cp%   zero%  mean   interpretation\n")
    for j in range(64):
        cp_pct = pos_cp[j] / n * 100
        zero_pct = pos_zero[j] / n * 100
        mean = pos_sum[j] / n
        interp = ""
        if cp_pct > 50:
            interp = "LABEL"
        elif zero_pct > 50:
            interp = "SPARSE/PAD"
        elif mean < 10:
            interp = "small_int"
        elif 10 <= mean <= 128 and cp_pct < 20:
            interp = "packed/bv"
        elif cp_pct > 25:
            interp = "mixed_label"

        lines.append(f" {j:2d}  {cp_pct:5.1f}  {zero_pct:5.1f}  {mean:6.1f}  {interp}\n")

    # --- Same analysis for rows 5000-5100 (middle of body) ---
    lines.append("\n\nPosition analysis (rows 5000-5100):\n")
    pos_cp2 = [0] * 64
    pos_zero2 = [0] * 64
    pos_sum2 = [0] * 64
    start_r = 5000
    n2 = min(100, body_rows - start_r)
    for ri in range(start_r, start_r + n2):
        row = body[ri * 64 : (ri + 1) * 64]
        for j in range(64):
            if is_cp1251_byte(row[j]):
                pos_cp2[j] += 1
            if row[j] == 0:
                pos_zero2[j] += 1
            pos_sum2[j] += row[j]

    lines.append("pos  cp%   zero%  mean   interpretation\n")
    for j in range(64):
        cp_pct = pos_cp2[j] / n2 * 100
        zero_pct = pos_zero2[j] / n2 * 100
        mean = pos_sum2[j] / n2
        interp = ""
        if cp_pct > 50:
            interp = "LABEL"
        elif zero_pct > 50:
            interp = "SPARSE/PAD"
        elif mean < 10:
            interp = "small_int"
        elif 10 <= mean <= 128 and cp_pct < 20:
            interp = "packed/bv"
        elif cp_pct > 25:
            interp = "mixed_label"

        lines.append(f" {j:2d}  {cp_pct:5.1f}  {zero_pct:5.1f}  {mean:6.1f}  {interp}\n")

    # --- Same analysis for rows 10000-10100 (late body) ---
    lines.append("\n\nPosition analysis (rows 10000-10100):\n")
    pos_cp3 = [0] * 64
    pos_zero3 = [0] * 64
    pos_sum3 = [0] * 64
    start_r = 10000
    n3 = min(100, body_rows - start_r)
    for ri in range(start_r, start_r + n3):
        row = body[ri * 64 : (ri + 1) * 64]
        for j in range(64):
            if is_cp1251_byte(row[j]):
                pos_cp3[j] += 1
            if row[j] == 0:
                pos_zero3[j] += 1
            pos_sum3[j] += row[j]

    lines.append("pos  cp%   zero%  mean   interpretation\n")
    for j in range(64):
        cp_pct = pos_cp3[j] / n3 * 100
        zero_pct = pos_zero3[j] / n3 * 100
        mean = pos_sum3[j] / n3
        interp = ""
        if cp_pct > 50:
            interp = "LABEL"
        elif zero_pct > 50:
            interp = "SPARSE/PAD"
        elif mean < 10:
            interp = "small_int"
        elif 10 <= mean <= 128 and cp_pct < 20:
            interp = "packed/bv"
        elif cp_pct > 25:
            interp = "mixed_label"

        lines.append(f" {j:2d}  {cp_pct:5.1f}  {zero_pct:5.1f}  {mean:6.1f}  {interp}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")


if __name__ == "__main__":
    main()
