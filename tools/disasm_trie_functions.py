#!/usr/bin/env python3
"""Disassemble key trie functions from libjni_ykeyboard3.so using LIEF + Capstone.

Target functions (from C++ code comments):
  FUN_00661538 — rank operation (L1/L2 structure)
  FUN_00661462 — label lookup (label_high << 8 | label_low)
  FUN_00660dca — weight reading

Also search for NSuccinctTrie related symbols and cross-reference.
"""
from __future__ import annotations
import pathlib
import struct
import lief
import capstone


def main():
    so_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\libs\arm64-v8a\libjni_ykeyboard3.so"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\trie_disasm.txt"
    )

    binary = lief.parse(str(so_path))
    if binary is None:
        print("ERROR: failed to parse binary")
        return

    lines = []
    lines.append(f"Binary: {so_path.name}\n")
    lines.append(f"Type: {binary.header.file_type}\n")
    lines.append(f"Machine: {binary.header.machine_type}\n")
    lines.append(f"Sections: {len(binary.sections)}\n\n")

    # --- Find symbols related to NSuccinctTrie ---
    lines.append("=== NSuccinctTrie SYMBOLS ===\n")
    trie_symbols = []
    for sym in binary.symbols:
        name = sym.name
        if not name:
            continue
        if "SuccinctTrie" in name or "succinct_trie" in name:
            trie_symbols.append(sym)
        elif "TTrie" in name and "Succinct" in name:
            trie_symbols.append(sym)

    lines.append(f"Found {len(trie_symbols)} NSuccinctTrie symbols\n")
    for sym in trie_symbols[:50]:
        lines.append(f"  0x{sym.value:08x} size={sym.size:5d} {sym.name[:120]}\n")

    # --- Find all symbols with size > 0 in the 0x0066xxxx range ---
    lines.append("\n=== FUNCTIONS IN 0x0066xxxx RANGE ===\n")
    target_funcs = []
    for sym in binary.symbols:
        if sym.value >= 0x660000 and sym.value < 0x670000 and sym.size > 0:
            target_funcs.append(sym)

    target_funcs.sort(key=lambda s: s.value)
    lines.append(f"Found {len(target_funcs)} symbols in range\n")
    for sym in target_funcs[:50]:
        # Try to demangle
        name = sym.name
        if len(name) > 100:
            name = name[:100] + "..."
        lines.append(f"  0x{sym.value:08x} size={sym.size:5d} {name}\n")

    # --- Search for specific function addresses ---
    target_addrs = [0x00661538, 0x00661462, 0x00660dca,
                    0x006602fe, 0x00663be3, 0x00663c34, 0x00663e2e,
                    0x0066442f, 0x00664480, 0x0066458a, 0x006645db,
                    0x0066471f, 0x00664770, 0x0066499a, 0x00664bf8,
                    0x006674f4, 0x006675ec, 0x006676e4, 0x006677dd]

    lines.append("\n=== SPECIFIC TARGET FUNCTIONS ===\n")
    for addr in target_addrs:
        found = None
        for sym in binary.symbols:
            if sym.value <= addr < sym.value + max(sym.size, 1):
                found = sym
                break
        if found:
            lines.append(f"  0x{addr:08x} -> {found.name[:120]} "
                        f"(base=0x{found.value:08x} size={found.size})\n")
        else:
            lines.append(f"  0x{addr:08x} -> (no symbol)\n")

    # --- Disassemble key functions ---
    # Setup Capstone for AArch64
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True

    # Get .text section
    text_section = None
    for section in binary.sections:
        if section.name == ".text":
            text_section = section
            break

    if text_section is None:
        lines.append("\nERROR: .text section not found\n")
    else:
        text_data = bytes(text_section.content)
        text_base = text_section.virtual_address
        lines.append(f"\n.text: base=0x{text_base:x} size={len(text_data)}\n")

        # Disassemble functions at target addresses
        funcs_to_disasm = [
            (0x00661538, "rank_operation", 256),
            (0x00661462, "label_lookup", 128),
            (0x00660dca, "weight_reading", 256),
        ]

        for addr, label, max_bytes in funcs_to_disasm:
            offset_in_text = addr - text_base
            if offset_in_text < 0 or offset_in_text >= len(text_data):
                lines.append(f"\n--- {label} at 0x{addr:08x}: OUT OF .text RANGE ---\n")
                continue

            code = text_data[offset_in_text:offset_in_text + max_bytes]
            lines.append(f"\n--- {label} at 0x{addr:08x} (text+0x{offset_in_text:x}) ---\n")

            for insn in md.disasm(code, addr):
                lines.append(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
                # Stop at ret instruction
                if insn.mnemonic == "ret":
                    break

        # Also disassemble any NSuccinctTrie functions found with size > 0
        lines.append("\n=== DISASM OF NSuccinctTrie FUNCTIONS ===\n")
        for sym in trie_symbols:
            if sym.size < 4 or sym.size > 2000:
                continue
            offset_in_text = sym.value - text_base
            if offset_in_text < 0 or offset_in_text >= len(text_data):
                continue

            code = text_data[offset_in_text:offset_in_text + min(sym.size, 512)]
            # Demangle-ish: extract key part
            short_name = sym.name
            if len(short_name) > 80:
                short_name = short_name[:80] + "..."

            lines.append(f"\n--- {short_name} at 0x{sym.value:08x} (size={sym.size}) ---\n")
            count = 0
            for insn in md.disasm(code, sym.value):
                lines.append(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
                count += 1
                if count >= 64 or insn.mnemonic == "ret":
                    if insn.mnemonic != "ret":
                        lines.append(f"  ... (truncated at {count} instructions)\n")
                    break

    # --- Search for string references to trie loading ---
    lines.append("\n=== STRINGS RELATED TO TRIE LOADING ===\n")
    interesting_strings = ["1nc7", "trie", "TTrie", "succinct", "rank", "label",
                          "bitvec", "LOUDS", "child", "terminal", "ngram", "load"]
    for s in interesting_strings:
        # Search in rodata
        for section in binary.sections:
            if section.name in (".rodata", ".data"):
                content = bytes(section.content)
                idx = 0
                finds = []
                while len(finds) < 3:
                    pos = content.find(s.encode(), idx)
                    if pos < 0:
                        break
                    abs_addr = section.virtual_address + pos
                    context = content[max(0, pos-4):pos+len(s)+20]
                    finds.append((abs_addr, context))
                    idx = pos + 1
                if finds:
                    for addr, ctx in finds:
                        lines.append(f"  '{s}' at 0x{addr:08x}: {ctx[:40]}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")
    print(f"Trie symbols: {len(trie_symbols)}")
    print(f"Functions in 0x0066xxxx: {len(target_funcs)}")


if __name__ == "__main__":
    main()
