#!/usr/bin/env python3
"""Find the actual 1nc7 parser by reading the vtable at 0x81ce00 and format name at 0x943f60."""
import pathlib
import struct
import lief
import capstone


def main():
    so_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\libs\arm64-v8a\libjni_ykeyboard3.so"
    )

    binary = lief.parse(str(so_path))

    # Read format name string at 0x943f60
    for sec in binary.sections:
        if sec.name in (".data.rel.ro", ".data", ".rodata"):
            content = bytes(sec.content)
            base = sec.virtual_address

            # Read string at 0x943f60
            off_name = 0x943f60 - base
            if 0 <= off_name < len(content) - 20:
                name_bytes = content[off_name:off_name + 64]
                name_str = name_bytes.split(b'\x00')[0].decode('ascii', errors='replace')
                print(f"Format name at 0x943f60: '{name_str}'")

            # Read vtable at 0x81ce00
            off_vt = 0x81ce00 - base
            if 0 <= off_vt < len(content) - 64:
                print(f"\nVtable at 0x81ce00 (section {sec.name}):")
                for i in range(12):
                    if off_vt + i * 8 + 8 <= len(content):
                        ptr = struct.unpack_from("<Q", content, off_vt + i * 8)[0]
                        print(f"  vtable[{i}] = 0x{ptr:016x}")

    # Also check what function at 0x22cda4 does (format registration)
    text_sec = [s for s in binary.sections if s.name == ".text"][0]
    text_data = bytes(text_sec.content)
    text_base = text_sec.virtual_address

    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)

    # Disasm registration function at 0x22cda4
    for addr, label, size in [
        (0x22cda4, "format_register", 128),
        (0x22cdf8, "handler_register_related", 128),
    ]:
        off = addr - text_base
        if off < 0 or off >= len(text_data):
            continue
        code = text_data[off:off + size]
        print(f"\n--- {label} at 0x{addr:08x} ---")
        for insn in md.disasm(code, addr):
            print(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}")
            if insn.mnemonic == "ret":
                break

    # Read the function pointer at 0x22cdf8 (from ADRP+ADD in registration)
    addr_func = 0x22cdf8
    off = addr_func - text_base
    if 0 <= off < len(text_data):
        print(f"\nFunction at 0x{addr_func:08x} (potential parser entry):")
        code = text_data[off:off + 256]
        count = 0
        for insn in md.disasm(code, addr_func):
            print(f"  0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}")
            count += 1
            if insn.mnemonic == "ret" or count >= 40:
                break

    # The vtable methods - disassemble each
    for sec in binary.sections:
        if sec.name == ".data.rel.ro":
            content = bytes(sec.content)
            base = sec.virtual_address
            off_vt = 0x81ce00 - base
            if off_vt < 0 or off_vt >= len(content):
                continue

            print(f"\n=== VTABLE METHOD DISASSEMBLY ===")
            for i in range(8):
                if off_vt + i * 8 + 8 > len(content):
                    break
                ptr = struct.unpack_from("<Q", content, off_vt + i * 8)[0]
                if ptr == 0:
                    continue
                func_off = ptr - text_base
                if func_off < 0 or func_off >= len(text_data):
                    print(f"\n  vtable[{i}] = 0x{ptr:016x} (outside .text)")
                    continue

                code = text_data[func_off:func_off + 200]
                print(f"\n  --- vtable[{i}] = 0x{ptr:08x} ---")
                count = 0
                for insn in md.disasm(code, ptr):
                    print(f"    0x{insn.address:08x}: {insn.mnemonic:8s} {insn.op_str}")
                    count += 1
                    if insn.mnemonic == "ret" or count >= 30:
                        break


if __name__ == "__main__":
    main()
