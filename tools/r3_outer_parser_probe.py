#!/usr/bin/env python3
"""Emulate the post-root part of FUN_0065f2d8 on the extracted trie stream.

This is a targeted offset probe for the fields read immediately after the root
FUN_00660496 call, especially the 4-byte value copied into param_1+0x1e.

Important: this remains an offline structural emulation, not runtime proof.
"""

from __future__ import annotations

import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
ROOT_REMAINDER_OFF = 0x207948  # from r3_emulate_fun_00660496.py level-2 child_start after child_flag=0


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def subblob(off: int, total_len: int, start: int, end: int | None = None) -> tuple[int, int]:
    if end is None:
        end = total_len
    if not (0 <= start <= end <= total_len):
        raise ValueError(f"invalid subblob off=0x{off:X} len={total_len} start=0x{start:X} end=0x{end:X}")
    return off + start, end - start


def parse_intvector(data: bytes, off: int, total_len: int) -> dict[str, int]:
    bitwidth = u64(data, off)
    hdr1 = u64(data, off + 8)
    hdr2 = u64(data, off + 16)
    span = u64(data, off + 24)
    rem_off, rem_len = subblob(off, total_len, span * 8 + 0x20)
    return {
        "off": off,
        "len": total_len,
        "bitwidth": bitwidth,
        "hdr1": hdr1,
        "hdr2": hdr2,
        "span": span,
        "rem_off": rem_off,
        "rem_len": rem_len,
    }


def parse_bitvector(data: bytes, off: int, total_len: int) -> dict[str, int]:
    word_count = u64(data, off)
    rank_size = u64(data, off + 8)
    popcount = u64(data, off + 16)
    ds_off = off + word_count * 4 + (4 if (word_count & 1) == 0 else 0) + 0x1C
    ds_count = u64(data, ds_off + 8)
    rem_off, rem_len = subblob(off, total_len, (ds_off - off) + ds_count * 8 + 0x20)
    return {
        "off": off,
        "len": total_len,
        "word_count": word_count,
        "rank_size": rank_size,
        "popcount": popcount,
        "ds_off": ds_off,
        "ds_count": ds_count,
        "rem_off": rem_off,
        "rem_len": rem_len,
    }


def main() -> None:
    data = TRIE_PATH.read_bytes()
    root_len = len(data) - ROOT_REMAINDER_OFF

    print(f"TRIE_PATH={TRIE_PATH}")
    print(f"SIZE={len(data)}")
    print(f"ROOT_REMAINDER_OFF=0x{ROOT_REMAINDER_OFF:X}")
    print(f"ROOT_REMAINDER_LEN={root_len}")
    print()

    # Equivalent to:
    #   param_1[7] = *local_c8;
    #   puVar17 = local_c8[1];
    #   param_1[8] = local_c8 + 2;
    #   FUN_003a1310(&local_a8, &local_c8, puVar17*8 + 0x10, local_c8_len)
    q0 = u64(data, ROOT_REMAINDER_OFF)
    q1 = u64(data, ROOT_REMAINDER_OFF + 8)
    print(f"A header @0x{ROOT_REMAINDER_OFF:X}: q0={q0} q1={q1} data=0x{ROOT_REMAINDER_OFF + 0x10:X}")

    a_rem_off, a_rem_len = subblob(ROOT_REMAINDER_OFF, root_len, q1 * 8 + 0x10)
    print(f"A remainder: off=0x{a_rem_off:X} len={a_rem_len}")
    print(
        f"B header @0x{a_rem_off:X}: q0={u64(data, a_rem_off)} q1={u64(data, a_rem_off+8)} q2={u64(data, a_rem_off+16)}"
    )
    print()

    b0 = u64(data, a_rem_off)
    if b0 == 0:
        cur_off, cur_len = subblob(a_rem_off, a_rem_len, 0x18)
        print("B q0 == 0 path")
    else:
        print("B q0 != 0 path")
        h_off, h_len = subblob(a_rem_off, a_rem_len, 0x18)
        h0 = u64(data, h_off)
        h1 = u64(data, h_off + 8)
        print(f"H header @0x{h_off:X}: q0={h0} q1={h1} data=0x{h_off + 0x10:X}")
        i_in_off, i_in_len = subblob(h_off, h_len, h1 * 8 + 0x10)
        print(f"I input: off=0x{i_in_off:X} len={i_in_len}")
        i = parse_intvector(data, i_in_off, i_in_len)
        print(
            f"I intvector @0x{i['off']:X}: bw={i['bitwidth']} hdr1={i['hdr1']} hdr2={i['hdr2']} span={i['span']} -> rem=0x{i['rem_off']:X} len={i['rem_len']}"
        )
        bv = parse_bitvector(data, i["rem_off"], i["rem_len"])
        print(
            f"I trailing bitvector @0x{bv['off']:X}: words={bv['word_count']} rank={bv['rank_size']} pop={bv['popcount']} ds_count={bv['ds_count']} -> rem=0x{bv['rem_off']:X} len={bv['rem_len']}"
        )
        cur_off, cur_len = bv["rem_off"], bv["rem_len"]
    print()

    e = parse_intvector(data, cur_off, cur_len)
    print(
        f"E intvector @0x{e['off']:X}: bw={e['bitwidth']} hdr1={e['hdr1']} hdr2={e['hdr2']} span={e['span']} -> rem=0x{e['rem_off']:X} len={e['rem_len']}"
    )
    codepage_candidate = u32(data, e["rem_off"])
    aux_u32 = u32(data, e["rem_off"] + 4)
    raw_u64 = u64(data, e["rem_off"])
    print()
    print(f"param_1+0x1e candidate read @0x{e['rem_off']:X}: u32_0={codepage_candidate} u32_1={aux_u32} raw_u64=0x{raw_u64:X}")
    print()
    print("Interpretation:")
    print("- first u32 is the current offline candidate for the field copied into param_1+0x1e")
    print("- second u32 is the follow-up 4-byte value read into local_a8 in FUN_0065f2d8")
    print("- this is structural evidence only; runtime truth still needs independent confirmation")


if __name__ == "__main__":
    main()
