#!/usr/bin/env python3
"""Disassemble resolved vtable methods for the 1nc7 handler."""
import pathlib, lief, capstone, struct

def main():
    so = pathlib.Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\libs\arm64-v8a\libjni_ykeyboard3.so")
    out = pathlib.Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\vtable_methods.txt")

    binary = lief.parse(str(so))
    text_sec = [s for s in binary.sections if s.name == ".text"][0]
    text_data = bytes(text_sec.content)
    text_base = text_sec.virtual_address

    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)

    # Resolved vtable methods from relocations
    methods = [
        (0x22d9ec, "vtable_method_0 (0x81cea0)"),
        (0x22da94, "vtable_method_1 (0x81cea8)"),
        (0x22db34, "vtable_method_2 (0x81ceb0)"),
        (0x2d7134, "vtable_method_3_DIFFERENT_REGION (0x81ceb8)"),
        (0x22dd50, "vtable_method_4 (0x81cec0)"),
        (0x22de3c, "vtable_method_5 (0x81cec8)"),
        (0x22d908, "vtable_method_last (0x81cef8)"),
        (0x7310a, "sub_vtable_entry (0x81ced8)"),
    ]

    lines = []
    for addr, label in methods:
        off = addr - text_base
        if off < 0 or off >= len(text_data):
            lines.append(f"\n--- {label} at 0x{addr:08x}: OUTSIDE .text ---\n")
            continue

        code = text_data[off:off + 400]
        lines.append(f"\n{'='*60}\n--- {label} at 0x{addr:08x} ---\n")
        count = 0
        for insn in md.disasm(code, addr):
            lines.append(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}\n")
            count += 1
            if insn.mnemonic == "ret" or count >= 60:
                break

    # Also check rodata strings near the vtable data entries
    lines.append(f"\n{'='*60}\n--- RODATA NEAR VTABLE DATA ENTRIES ---\n")
    rodata = [s for s in binary.sections if s.name == ".rodata"][0]
    rodata_data = bytes(rodata.content)
    rodata_base = rodata.virtual_address

    data_entries = [0x1ee324, 0x1ee328, 0x1ee32c, 0x1ee364, 0x1ee430,
                    0x1ee4fc, 0x1ee50c, 0x1ee518, 0x1ee524, 0x1ee5e8,
                    0x1ee618, 0x1ee620, 0x1ee628, 0x1ee6b8, 0x1ee7c4]

    for addr in data_entries:
        off = addr - rodata_base
        if 0 <= off < len(rodata_data) - 32:
            sample = rodata_data[off:off+32]
            # Try to read as string
            try:
                s = sample.split(b'\x00')[0].decode('ascii', errors='replace')
            except:
                s = ""
            lines.append(f"  0x{addr:08x}: hex={sample[:16].hex()} str='{s}'\n")

    out.write_text("".join(lines), encoding="utf-8")
    print(f"Written to {out}")

if __name__ == "__main__":
    main()
