#!/usr/bin/env python3
"""Find the section table / section index within the trie file.

Key insight: the tail contains label arrays + metadata footer.
The body must contain bitvectors + some index.
Try to find the section table that maps sections to offsets.
"""
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
    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    magic, version, payload_hint, _, item_count, _ = header
    tail = data[payload_hint:]
    body = data[24:payload_hint]

    print(f"file={len(data)} body={len(body)} tail={len(tail)} payload_hint={payload_hint} item_count={item_count}")

    # --- Check region around payload_hint boundary ---
    print("\n=== BODY/TAIL BOUNDARY ===")
    boundary = data[payload_hint - 64 : payload_hint + 64]
    print(f"body_last_64: {data[payload_hint-64:payload_hint].hex()}")
    print(f"tail_first_64: {data[payload_hint:payload_hint+64].hex()}")

    # Check if there's a section table at the end of the body
    body_last_128 = body[-128:]
    print(f"\nbody_last_128: {body_last_128.hex()}")
    # Look for u32 values that could be section offsets
    last_u32s = struct.unpack_from("<32I", body_last_128, 0)
    print(f"body_last_32_u32s: {[hex(v) for v in last_u32s]}")

    # --- Check yarchive at end of body ---
    body_last_u64 = struct.unpack_from("<Q", body, len(body) - 8)[0]
    print(f"\nbody_last_u64={body_last_u64} (0x{body_last_u64:X})")
    if 0 < body_last_u64 < len(body) - 8:
        print(f"  -> valid offset within body!")
        st = body[body_last_u64:]
        sec_count = struct.unpack_from("<I", st, 0)[0]
        print(f"  section_count={sec_count}")

    # --- Scan body for potential section tables ---
    # A section table would have:
    # - A count (u32 < 100)
    # - Followed by entries with increasing offsets
    print("\n=== SCAN FOR SECTION TABLE PATTERNS IN BODY ===")
    found = []
    for off in range(0, len(body) - 24, 4):
        count = struct.unpack_from("<I", body, off)[0]
        if count < 3 or count > 50:
            continue
        # Check if next 'count' entries look like (key/id, offset, size) or (offset, size)
        valid_8byte = True
        valid_20byte = True
        prev_off_8 = 0
        prev_off_20 = 0

        for i in range(min(count, 5)):
            # 8-byte entries: (offset:u32, size:u32)
            entry_off = off + 4 + i * 8
            if entry_off + 8 > len(body):
                valid_8byte = False
                break
            e_off, e_size = struct.unpack_from("<II", body, entry_off)
            if e_off > len(data) or e_size > len(data):
                valid_8byte = False
            if e_off < prev_off_8:
                valid_8byte = False
            prev_off_8 = e_off

            # 20-byte entries: (key:u64, offset:u64, size:u32)
            entry_off = off + 4 + i * 20
            if entry_off + 20 > len(body):
                valid_20byte = False
                break
            _, e_off_20, e_size_20 = struct.unpack_from("<QQI", body, entry_off)
            if e_off_20 > len(data) or e_size_20 > len(data):
                valid_20byte = False

        if valid_8byte and count >= 5:
            entries = []
            for i in range(min(count, 10)):
                entry_off = off + 4 + i * 8
                e_off, e_size = struct.unpack_from("<II", body, entry_off)
                entries.append((e_off, e_size))
            if all(0 < e[0] < len(data) and 0 < e[1] < len(data) for e in entries[:5]):
                found.append(("8byte", off, count, entries))

        if valid_20byte and count >= 5:
            entries = []
            for i in range(min(count, 10)):
                entry_off = off + 4 + i * 20
                key, e_off, e_size = struct.unpack_from("<QQI", body, entry_off)
                entries.append((key, e_off, e_size))
            if all(0 < e[1] < len(data) and 0 < e[2] < len(data) for e in entries[:5]):
                found.append(("20byte", off, count, entries))

    for fmt, off, count, entries in found[:10]:
        print(f"\n  [{fmt}] body+{off} (abs {24+off}): count={count}")
        for e in entries[:8]:
            if fmt == "8byte":
                print(f"    offset={e[0]} size={e[1]}")
            else:
                print(f"    key=0x{e[0]:016X} offset={e[1]} size={e[2]}")

    # --- Try completely different: the trie might use dword #2 (payload_hint) as ---
    # --- a pointer to a section table, not as a body/tail boundary ---
    print("\n=== WHAT IF PAYLOAD_HINT POINTS TO SECTION TABLE? ===")
    st = data[payload_hint:]
    # First few u32s at payload_hint
    first_u32s = struct.unpack_from("<16I", st, 0)
    print(f"u32s at payload_hint: {[hex(v) for v in first_u32s]}")

    # Check if first u32 could be a section count
    sec_count = first_u32s[0]
    print(f"first_u32={sec_count} (0x{sec_count:X})")

    # --- Try: maybe the body has a simple layout where first N bytes are ---
    # --- one section, next M bytes another, etc. ---
    # --- Use zone transitions to find section boundaries ---
    print("\n=== BODY ZONE ANALYSIS (256-byte granularity) ===")
    BBLOCK = 256
    zones = []
    for bi in range(len(body) // BBLOCK):
        chunk = body[bi * BBLOCK : (bi + 1) * BBLOCK]
        cp_r = sum(1 for b in chunk if is_cp1251_byte(b)) / len(chunk)
        bit_d = sum(bin(b).count('1') for b in chunk) / (len(chunk) * 8)
        zero_r = chunk.count(0) / len(chunk)

        if cp_r > 0.40:
            zone_type = "LABEL"
        elif zero_r > 0.30:
            zone_type = "SPARSE"
        elif bit_d > 0.25 and bit_d < 0.75 and cp_r < 0.15:
            zone_type = "BITVEC"
        else:
            zone_type = "MIXED"
        zones.append((bi, zone_type, cp_r, bit_d, zero_r))

    # Group consecutive same-type blocks into sections
    sections = []
    if zones:
        cur_type = zones[0][1]
        cur_start = 0
        for bi, ztype, cp_r, bit_d, zero_r in zones:
            if ztype != cur_type:
                sections.append((cur_start, bi, cur_type))
                cur_type = ztype
                cur_start = bi
        sections.append((cur_start, len(zones), cur_type))

    print(f"Detected {len(sections)} sections:")
    for start, end, stype in sections[:40]:
        abs_start = 24 + start * BBLOCK
        abs_end = 24 + end * BBLOCK
        size = (end - start) * BBLOCK
        # Get aggregate stats for the section
        sec_data = body[start * BBLOCK : end * BBLOCK]
        cp_r = sum(1 for b in sec_data if is_cp1251_byte(b)) / max(len(sec_data), 1)
        bit_d = sum(bin(b).count('1') for b in sec_data[:4096]) / (min(len(sec_data), 4096) * 8)
        print(f"  [{abs_start:7d}, {abs_end:7d}) {size:7d}B  {stype:7s}  cp={cp_r:.2f} bits={bit_d:.2f}")

    # --- Compute total sizes per zone type ---
    print("\n=== ZONE TYPE TOTALS ===")
    totals = collections.Counter()
    for start, end, stype in sections:
        totals[stype] += (end - start) * BBLOCK
    for stype, size in totals.most_common():
        print(f"  {stype}: {size} bytes ({size/len(body)*100:.1f}%)")

    print("\nDone.")


if __name__ == "__main__":
    main()
