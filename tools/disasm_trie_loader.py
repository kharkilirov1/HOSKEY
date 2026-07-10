#!/usr/bin/env python3
"""Disassemble BinaryDictionary_open and nearby trie-loading functions.
Also disassemble around label_lookup and weight_read which failed earlier."""
from __future__ import annotations
import pathlib
import lief
import capstone


def disasm_range(md, text_data, text_base, addr, size, label):
    """Disassemble a range and return lines."""
    off = addr - text_base
    if off < 0 or off >= len(text_data):
        return [f"\n--- {label} at 0x{addr:08x}: OUT OF RANGE ---\n"]

    lines = [f"\n--- {label} at 0x{addr:08x} ---\n"]
    code = text_data[off:off + size]
    count = 0
    for insn in md.disasm(code, addr):
        lines.append(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
        count += 1
        if count >= 80:
            lines.append("  ... (truncated)\n")
            break
    if count == 0:
        # Try Thumb mode? No, this is AArch64
        lines.append(f"  (no valid instructions, first 8 bytes: {code[:8].hex()})\n")
    return lines


def main():
    so_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\libs\arm64-v8a\libjni_ykeyboard3.so"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\trie_loader_disasm.txt"
    )

    binary = lief.parse(str(so_path))

    text_sec = None
    for sec in binary.sections:
        if sec.name == ".text":
            text_sec = sec
            break

    text_data = bytes(text_sec.content)
    text_base = text_sec.virtual_address

    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True

    lines = []
    lines.append(f".text: base=0x{text_base:x} size={len(text_data)}\n")

    # --- BinaryDictionary functions ---
    jni_funcs = [
        (0x0026bf2c, "getSuggestions", 800),
        (0x0026bbdc, "checkWord", 400),
        # BinaryDictionary_open is likely nearby. Search for it.
    ]

    # Find BinaryDictionary_open
    for sym in binary.exported_functions:
        if "open" in sym.name.lower() and "BinaryDictionary" in sym.name:
            jni_funcs.append((sym.address, "BD_open", 800))
        elif "BinaryDictionary" in sym.name and "Dictionary" in sym.name:
            if sym.address not in [f[0] for f in jni_funcs]:
                jni_funcs.append((sym.address, sym.name.split("_")[-1][:30], 400))

    for addr, label, size in jni_funcs:
        lines.extend(disasm_range(md, text_data, text_base, addr, size, label))

    # --- Functions around the rank_op area (0x660000-0x662000) ---
    # Scan for function prologues (stp x29, x30 pattern)
    lines.append("\n=== FUNCTION PROLOGUES IN 0x660000-0x662000 ===\n")
    scan_start = 0x660000 - text_base
    scan_end = 0x662000 - text_base
    if scan_start >= 0 and scan_end <= len(text_data):
        stp_pattern = bytes([0xfd, 0x7b])  # stp x29, x30 has these bytes
        pos = scan_start
        func_starts = []
        while pos < scan_end:
            # AArch64 stp x29, x30, [sp, #-N]! pattern
            # Look for common prologue patterns
            if pos + 4 <= len(text_data):
                insn_word = int.from_bytes(text_data[pos:pos+4], 'little')
                # stp x29, x30, [sp, #imm]! has encoding: 1x101001 1xxxxxxx xxxxx11111011101
                # Simplified: check for common stp x29,x30 opcodes
                if (insn_word & 0xFFFF0000) == 0xA9800000 or \
                   (insn_word & 0xFFFF0000) == 0xA9BF0000 or \
                   (insn_word & 0x7FC003FF) == 0x290003FD:
                    func_addr = text_base + pos
                    func_starts.append(func_addr)
            pos += 4

        lines.append(f"Found {len(func_starts)} potential function starts\n")
        for fa in func_starts:
            lines.append(f"  0x{fa:08x}\n")

    # --- Disassemble around 0x661462 (label_lookup) more carefully ---
    # Try starting from a few bytes before
    for offset_adj in [-16, -8, -4, 0, 4, 8]:
        addr = 0x661462 + offset_adj
        off = addr - text_base
        if off < 0 or off >= len(text_data):
            continue
        code = text_data[off:off+64]
        lines.append(f"\n--- label_lookup+{offset_adj} at 0x{addr:08x} ---\n")
        for insn in md.disasm(code, addr):
            lines.append(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
            if insn.mnemonic == "ret":
                break

    # --- Disassemble around 0x660dca (weight_read) ---
    for offset_adj in [-16, -8, -4, 0, 4, 8]:
        addr = 0x660dca + offset_adj
        off = addr - text_base
        if off < 0 or off >= len(text_data):
            continue
        code = text_data[off:off+128]
        lines.append(f"\n--- weight_read+{offset_adj} at 0x{addr:08x} ---\n")
        count = 0
        for insn in md.disasm(code, addr):
            lines.append(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
            count += 1
            if insn.mnemonic == "ret" or count >= 30:
                break

    # --- Look for the function that reads "1nc7" magic ---
    lines.append("\n=== SEARCH FOR 1nc7 MAGIC (0x37636E31) IN .rodata ===\n")
    rodata_sec = None
    for sec in binary.sections:
        if sec.name == ".rodata":
            rodata_sec = sec
            break
    if rodata_sec:
        rodata = bytes(rodata_sec.content)
        magic_bytes = b"1nc7"
        magic_le = (0x37636E31).to_bytes(4, 'little')
        for pattern, label in [(magic_bytes, "ASCII"), (magic_le, "LE32")]:
            pos = rodata.find(pattern)
            if pos >= 0:
                abs_addr = rodata_sec.virtual_address + pos
                ctx = rodata[max(0,pos-8):pos+20]
                lines.append(f"  '{label}' found at rodata+{pos} (0x{abs_addr:08x}): {ctx.hex()}\n")

    # --- Search .text for immediate load of 0x37636E31 ---
    lines.append("\n=== SEARCH .text FOR 0x37636E31 IMMEDIATE ===\n")
    magic_bytes_le = (0x37636E31).to_bytes(4, 'little')
    search_pos = 0
    found_in_text = []
    while search_pos < len(text_data) - 4:
        pos = text_data.find(magic_bytes_le, search_pos)
        if pos < 0:
            break
        found_in_text.append(text_base + pos)
        search_pos = pos + 1
        if len(found_in_text) >= 5:
            break

    for addr in found_in_text:
        lines.append(f"  0x{addr:08x}\n")
        # Disassemble around it
        lines.extend(disasm_range(md, text_data, text_base, addr - 32, 80, f"context_of_magic"))

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")


if __name__ == "__main__":
    main()
