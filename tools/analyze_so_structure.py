#!/usr/bin/env python3
"""Analyze libjni_ykeyboard3.so structure: sections, dynamic symbols, and disasm at target addresses."""
from __future__ import annotations
import pathlib
import lief
import capstone


def main():
    so_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\libs\arm64-v8a\libjni_ykeyboard3.so"
    )
    out_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\so_analysis.txt"
    )

    binary = lief.parse(str(so_path))
    lines = []

    lines.append(f"File size: {so_path.stat().st_size}\n")
    lines.append(f"Type: {binary.header.file_type}\n")
    lines.append(f"Machine: {binary.header.machine_type}\n")

    # Sections
    lines.append(f"\n=== SECTIONS ===\n")
    for sec in binary.sections:
        lines.append(f"  {sec.name:20s} addr=0x{sec.virtual_address:08x} "
                     f"size={sec.size:10d} offset=0x{sec.offset:08x}\n")

    # Dynamic symbols
    lines.append(f"\n=== DYNAMIC SYMBOLS (first 50) ===\n")
    dyn_syms = list(binary.dynamic_symbols)
    lines.append(f"Total: {len(dyn_syms)}\n")
    trie_dyn = []
    for sym in dyn_syms:
        if "Trie" in sym.name or "trie" in sym.name or "Succinct" in sym.name:
            trie_dyn.append(sym)
    lines.append(f"Trie-related: {len(trie_dyn)}\n")
    for sym in trie_dyn[:30]:
        lines.append(f"  0x{sym.value:08x} {sym.name[:120]}\n")

    # ALL exported symbols
    lines.append(f"\n=== EXPORTED FUNCTIONS ===\n")
    exports = [s for s in binary.exported_functions]
    lines.append(f"Total exports: {len(exports)}\n")
    for exp in exports[:30]:
        lines.append(f"  0x{exp.address:08x} {exp.name[:100]}\n")

    # Static symbols
    lines.append(f"\n=== STATIC SYMBOLS ===\n")
    static_syms = list(binary.static_symbols) if hasattr(binary, 'static_symbols') else []
    lines.append(f"Total static: {len(static_syms)}\n")
    for sym in static_syms[:20]:
        lines.append(f"  0x{sym.value:08x} size={sym.size} {sym.name[:100]}\n")

    # Check .symtab presence
    has_symtab = any(sec.name == ".symtab" for sec in binary.sections)
    has_dynsym = any(sec.name == ".dynsym" for sec in binary.sections)
    lines.append(f"\n.symtab present: {has_symtab}\n")
    lines.append(f".dynsym present: {has_dynsym}\n")

    # Try to find .text section and disassemble at target addresses
    text_sec = None
    for sec in binary.sections:
        if sec.name == ".text":
            text_sec = sec
            break

    if text_sec:
        text_data = bytes(text_sec.content)
        text_base = text_sec.virtual_address
        lines.append(f"\n.text: base=0x{text_base:x} size={len(text_data)}\n")

        # Check if target addresses are in range
        targets = [0x00661538, 0x00661462, 0x00660dca]
        for addr in targets:
            off = addr - text_base
            if 0 <= off < len(text_data):
                lines.append(f"  0x{addr:08x} is in .text at offset 0x{off:x}\n")
            else:
                lines.append(f"  0x{addr:08x} is OUTSIDE .text (offset={off})\n")

        # Disassemble at target addresses regardless of symbols
        md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
        md.detail = True

        for addr, label in [(0x00661538, "rank_op"), (0x00661462, "label_lookup"),
                            (0x00660dca, "weight_read")]:
            off = addr - text_base
            if off < 0 or off >= len(text_data):
                continue
            code = text_data[off:off + 400]
            lines.append(f"\n--- {label} at 0x{addr:08x} ---\n")
            for insn in md.disasm(code, addr):
                lines.append(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
                if insn.mnemonic == "ret":
                    break

        # Also try to find the trie LOAD function by looking at dynamic symbol callees
        # Search for BinaryDictionary_open or similar
        lines.append(f"\n=== BinaryDictionary SYMBOLS ===\n")
        for sym in dyn_syms:
            if "BinaryDictionary" in sym.name or "DictionaryFacilitator" in sym.name:
                lines.append(f"  0x{sym.value:08x} {sym.name[:120]}\n")

    out_path.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out_path}")


if __name__ == "__main__":
    main()
