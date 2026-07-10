#!/usr/bin/env python3
"""Disassemble the 4 core trie functions found at 0x660xxx and find 1nc7 xrefs."""
from __future__ import annotations
import pathlib
import lief
import capstone
import struct


def main():
    so_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\libs\arm64-v8a\libjni_ykeyboard3.so"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\core_trie_disasm.txt"
    )

    binary = lief.parse(str(so_path))
    text_sec = [s for s in binary.sections if s.name == ".text"][0]
    text_data = bytes(text_sec.content)
    text_base = text_sec.virtual_address

    rodata_sec = [s for s in binary.sections if s.name == ".rodata"][0]
    rodata_data = bytes(rodata_sec.content)
    rodata_base = rodata_sec.virtual_address

    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True

    lines = []

    # --- Disassemble 4 core functions + known rank_op ---
    funcs = [
        (0x660af8, "func_0x660af8 (near weight_read)", 600),
        (0x66131c, "func_0x66131c (near label_lookup)", 600),
        (0x661538, "rank_op_0x661538", 256),
        (0x661870, "func_0x661870", 600),
        (0x661c6c, "func_0x661c6c", 600),
    ]

    for addr, label, max_bytes in funcs:
        off = addr - text_base
        if off < 0 or off >= len(text_data):
            lines.append(f"\n--- {label}: OUT OF RANGE ---\n")
            continue

        code = text_data[off:off + max_bytes]
        lines.append(f"\n{'='*70}\n--- {label} at 0x{addr:08x} ---\n")

        ret_count = 0
        for insn in md.disasm(code, addr):
            lines.append(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
            if insn.mnemonic == "ret":
                ret_count += 1
                if ret_count >= 2:  # Stop after 2nd ret (in case of multi-path)
                    break

    # --- Find xrefs to "1nc7" string at 0x40b86 ---
    lines.append(f"\n{'='*70}\n=== XREF TO '1nc7' STRING (rodata 0x40b86) ===\n")

    # In AArch64, string references typically use ADRP + ADD pattern
    # ADRP loads a 4KB-aligned page address, ADD adds the page offset
    target_addr = 0x40b86
    target_page = target_addr & ~0xFFF  # 0x40000
    target_offset = target_addr & 0xFFF  # 0xb86

    # Search for ADRP instructions that reference the target page
    # ADRP encoding: [1] [immlo:2] [10000] [immhi:19] [Rd:5]
    # The immediate is (immhi:immlo) << 12, added to PC aligned to 4KB
    xref_candidates = []

    for i in range(0, len(text_data) - 8, 4):
        insn_word = struct.unpack_from("<I", text_data, i)[0]
        # Check if it's ADRP (bit 31=1, bits 28-24=10000)
        if (insn_word & 0x9F000000) == 0x90000000:
            rd = insn_word & 0x1F
            immlo = (insn_word >> 29) & 0x3
            immhi = (insn_word >> 5) & 0x7FFFF
            imm = ((immhi << 2) | immlo) << 12
            # Sign extend
            if imm & (1 << 32):
                imm -= (1 << 33)
            pc_page = (text_base + i) & ~0xFFF
            result_page = (pc_page + imm) & 0xFFFFFFFF

            if result_page == target_page:
                # Check next instruction for ADD with target_offset
                if i + 4 < len(text_data):
                    next_word = struct.unpack_from("<I", text_data, i + 4)[0]
                    # ADD Xd, Xn, #imm12 encoding: 1001000100 [imm12:12] [Rn:5] [Rd:5]
                    if (next_word & 0xFFC00000) == 0x91000000:
                        add_imm = (next_word >> 10) & 0xFFF
                        add_rn = (next_word >> 5) & 0x1F
                        if add_imm == target_offset and add_rn == rd:
                            xref_addr = text_base + i
                            xref_candidates.append(xref_addr)

    lines.append(f"Found {len(xref_candidates)} ADRP+ADD xrefs to 0x{target_addr:x}\n")
    for xref in xref_candidates[:10]:
        lines.append(f"\n  XREF at 0x{xref:08x}:\n")
        # Disassemble context (32 bytes before, 64 bytes after)
        ctx_start = xref - 32
        off = ctx_start - text_base
        if off >= 0:
            code = text_data[off:off + 128]
            for insn in md.disasm(code, ctx_start):
                marker = " >>>" if insn.address == xref else "    "
                lines.append(f"{marker} 0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
                if insn.address > xref + 48:
                    break

    # --- Also search for the magic value 0x37636E31 as immediate ---
    lines.append(f"\n{'='*70}\n=== SEARCH FOR 0x37636E31 AS MOVZ/MOVK IMMEDIATE ===\n")
    # Check for MOVZ/MOVK with partial values
    # 0x6E31 (lower 16 bits) in MOVZ: 110100101 [hw:2] [imm16:16] [Rd:5]
    target_low = 0x6E31
    target_hi = 0x3763

    movz_hits = []
    for i in range(0, len(text_data) - 4, 4):
        insn_word = struct.unpack_from("<I", text_data, i)[0]
        # MOVZ W: 01010010100 [imm16] [Rd]
        # MOVZ X: 11010010100 [imm16] [Rd]
        if (insn_word & 0x7F800000) == 0x52800000:  # MOVZ
            imm16 = (insn_word >> 5) & 0xFFFF
            if imm16 == target_low or imm16 == target_hi:
                movz_hits.append((text_base + i, imm16))

    lines.append(f"Found {len(movz_hits)} MOVZ with partial magic value\n")
    for addr, imm in movz_hits[:10]:
        lines.append(f"  0x{addr:08x}: MOVZ with 0x{imm:04x}\n")
        # Check nearby for the complementary MOVK
        off = addr - text_base
        ctx = text_data[off:off+16]
        for insn in md.disasm(ctx, addr):
            lines.append(f"    0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")
    print(f"XREF candidates: {len(xref_candidates)}")
    if xref_candidates:
        print(f"First xref: 0x{xref_candidates[0]:08x}")


if __name__ == "__main__":
    main()
