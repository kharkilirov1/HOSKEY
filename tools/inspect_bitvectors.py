#!/usr/bin/env python3
"""Inspect the first/last bytes of each identified bitvector section.
Also try FST-style LOUDS-Sparse interpretation."""
from __future__ import annotations
import struct
import pathlib


def bit_at(data, pos):
    return (data[pos >> 3] >> (pos & 7)) & 1


def read_unary(data, start, one_val=1):
    """Count consecutive bits equal to one_val, return (count, next_pos)."""
    pos = start
    mx = len(data) * 8
    count = 0
    while pos < mx and bit_at(data, pos) == one_val:
        count += 1
        pos += 1
    if pos < mx:
        pos += 1
    return count, pos


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    out = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\bitvector_inspect.txt"
    )

    data = trie_path.read_bytes()
    h = struct.unpack_from("<6I", data, 0)
    body = data[24:h[2]]

    bvs = [
        ("BV1", 5752, 124992, 130744),   # bits, size, rank_off
        ("BV2", 167752, 124992, 292744),
        ("BV3", 323944, 124992, 448936),
        ("BV4", 480136, 124992, 605128),
    ]

    lines = []

    for name, off, sz, rank_off in bvs:
        bv = body[off:off+sz]
        rank = body[rank_off:rank_off+31248]

        pop = sum(bin(b).count('1') for b in bv)
        zeros = sz * 8 - pop

        lines.append(f"\n{'='*60}\n{name} at body+{off} ({sz}B)\n")
        lines.append(f"  popcount={pop} zeros={zeros} density={pop/(sz*8):.4f}\n")
        lines.append(f"  first_16_hex: {bv[:16].hex()}\n")
        lines.append(f"  first_16_bin: {' '.join(f'{b:08b}' for b in bv[:16])}\n")
        lines.append(f"  last_16_hex:  {bv[-16:].hex()}\n")

        # Rank L1 first values
        l1_vals = [struct.unpack_from("<Q", rank, i*16)[0] for i in range(min(10, len(rank)//16))]
        l1_last = [struct.unpack_from("<Q", rank, (len(rank)//16 - 5 + i)*16)[0] for i in range(5)]
        lines.append(f"  rank_L1_first_10: {l1_vals}\n")
        lines.append(f"  rank_L1_last_5: {l1_last}\n")

        # L1 increments (= popcount per 512-bit superblock)
        incs = [l1_vals[i+1] - l1_vals[i] for i in range(len(l1_vals)-1)]
        lines.append(f"  L1_increments_first_9: {incs}\n")

        # Read first 50 unary degrees (1=child, 0=end)
        degs_1, _ = [], 0
        pos = 0
        for _ in range(50):
            d, pos = read_unary(bv, pos, 1)
            degs_1.append(d)
        lines.append(f"  LOUDS_1child_0end_first50: {degs_1}\n")

        # Read first 50 inverted (0=child, 1=end)
        degs_0 = []
        pos = 0
        for _ in range(50):
            d, pos = read_unary(bv, pos, 0)
            degs_0.append(d)
        lines.append(f"  LOUDS_0child_1end_first50: {degs_0}\n")

        # FST S-LOUDS: 1 marks first child of each parent
        # Count runs of (1 followed by 0s) to get degree
        fst_degrees = []
        pos = 0
        for _ in range(50):
            if pos >= sz * 8:
                break
            if bit_at(bv, pos) == 1:
                # Start of a new node's children
                d = 1
                pos += 1
                while pos < sz * 8 and bit_at(bv, pos) == 0:
                    d += 1
                    pos += 1
                fst_degrees.append(d)
            else:
                pos += 1
        lines.append(f"  FST_S_LOUDS_degrees_first50: {fst_degrees}\n")

    # --- Body header (0..5752) ---
    lines.append(f"\n{'='*60}\nBODY HEADER (0..5752)\n")
    hdr = body[:5752]
    lines.append(f"  size={len(hdr)}B\n")
    lines.append(f"  first_32_hex: {hdr[:32].hex()}\n")
    lines.append(f"  first_32_bin: {' '.join(f'{b:08b}' for b in hdr[:32])}\n")

    # Check if header has a section table: [count:u32] [entries...]
    first_u32 = struct.unpack_from("<I", hdr, 0)[0]
    lines.append(f"  first_u32={first_u32} (0x{first_u32:08X})\n")

    # Maybe it's a small structure
    u32s = [struct.unpack_from("<I", hdr, i)[0] for i in range(0, min(64, len(hdr)), 4)]
    lines.append(f"  first_16_u32: {[hex(v) for v in u32s]}\n")

    # Check if any u32 matches known offsets or sizes
    known = {5752: "BV1_off", 130744: "R1_off", 167752: "BV2_off", 292744: "R2_off",
             323944: "BV3_off", 448936: "R3_off", 480136: "BV4_off", 605128: "R4_off",
             124992: "BV_size", 31248: "rank_size", 999936: "total_bits",
             369743: "BV1/2_pop", 801071: "BV3_pop", 402824: "BV4_pop",
             198865: "~nodes", 636376: "data_after_R4"}
    for i, v in enumerate(u32s):
        if v in known:
            lines.append(f"  *** u32[{i}] at hdr+{i*4} = {v} = {known[v]} ***\n")
        # Also check ±1
        for kv, kn in known.items():
            if abs(v - kv) <= 2 and v != kv:
                lines.append(f"  ~ u32[{i}] at hdr+{i*4} = {v} ~ {kv} ({kn})\n")

    # --- Gap between BV1 and RANK1 (body+130744..161992) ---
    # This gap shouldn't exist if BV1 is exactly before RANK1
    # BV1 at body+5752, size 124992 → ends at 5752+124992 = 130744 = exactly RANK1 start!
    lines.append(f"\n{'='*60}\nSECTION LAYOUT VERIFICATION\n")
    lines.append(f"  BV1: [{5752}, {5752+124992}) = [5752, 130744)\n")
    lines.append(f"  R1:  [{130744}, {130744+31248}) = [130744, 161992)\n")
    lines.append(f"  GAP: [{161992}, {167752}) = 5760 bytes\n")
    lines.append(f"  BV2: [{167752}, {167752+124992}) = [167752, 292744)\n")
    lines.append(f"  R2:  [{292744}, {292744+31248}) = [292744, 323992)\n")
    lines.append(f"  GAP: [{323992}, {323944}) = -48 bytes (OVERLAP!)\n")
    lines.append(f"  BV3: [{323944}, {323944+124992}) = [323944, 448936)\n")
    lines.append(f"  R3:  [{448936}, {448936+31248}) = [448936, 480184)\n")
    lines.append(f"  GAP: [{480184}, {480136}) = -48 bytes (OVERLAP!)\n")
    lines.append(f"  BV4: [{480136}, {480136+124992}) = [480136, 605128)\n")
    lines.append(f"  R4:  [{605128}, {605128+31248}) = [605128, 636376)\n")
    lines.append(f"  REST:[{636376}, {len(body)}) = {len(body)-636376} bytes\n")

    # The -48 byte overlaps suggest the BV sizes might be 124944 instead of 124992
    # 124944 = 1952 * 64, while 124992 = 1953 * 64
    # Let me check: rank entries = 1953 but maybe last superblock is partial
    lines.append(f"\n  124992 / 64 = {124992/64} (superblocks)\n")
    lines.append(f"  124944 / 64 = {124944/64} (superblocks)\n")
    lines.append(f"  If BV_size=124944: R1_end + gap = 161992 + 5760 = 167752 OK\n")
    lines.append(f"  If BV_size=124944: BV2_end = 167752+124944 = {167752+124944}\n")
    lines.append(f"  R2 at 292744 - 292696 = 48 bytes gap -> matches!\n")

    # Actually let me check: does 124944 + rank give clean layout?
    bv_size_alt = 124944
    layout = [
        ("HDR", 0, 5752),
        ("BV1", 5752, bv_size_alt),
        ("R1", 5752+bv_size_alt, 31248),
    ]
    pos_check = 5752 + bv_size_alt + 31248
    lines.append(f"\n  With BV=124944: after R1 = {pos_check}\n")
    gap1 = 167752 - pos_check
    lines.append(f"  Gap to BV2: {gap1} bytes\n")

    # Try with 1952 superblocks of 64 bytes = 124928 bytes
    for candidate_sb in [1950, 1951, 1952, 1953]:
        bv_sz = candidate_sb * 64
        rank_sz = candidate_sb * 16
        section = bv_sz + rank_sz
        after_hdr = 5752
        after_s1 = after_hdr + section
        gap = 167752 - after_s1
        lines.append(f"  superblocks={candidate_sb}: bv={bv_sz} rank={rank_sz} "
                     f"section={section} after_S1={after_s1} gap_to_BV2={gap}\n")

    out.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out}")

    # Print key results
    for name, off, sz, rank_off in bvs:
        bv = body[off:off+sz]
        print(f"{name}: first_4_hex={bv[:4].hex()} first_4_bin={''.join(f'{b:08b}' for b in bv[:4])}")


if __name__ == "__main__":
    main()
