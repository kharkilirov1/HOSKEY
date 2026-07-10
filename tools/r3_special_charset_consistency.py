#!/usr/bin/env python3
"""Validate special charset slot semantics against trie charset usage.

Focus:
- prove whether codepage ids 0/1 exist in the full registry
- inspect the special pre-seeded slots used by FUN_00607704
- confirm that trie iterator FUN_00661dc0 passes param_4+0x1e into FUN_006622a7
- list xrefs that connect the special slots and charset lookup path
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, r"C:\Users\Kharki\AppData\Local\Programs\Python\Python312\Lib\site-packages")

import jpype
import pyghidra


BIN_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\native_libs\x86_64\libjni_ykeyboard3.so")
PROJECT_DIR = r"C:\Users\Kharki\ghidra_yandex"
PROJECT_NAME = "YKB"
GHIDRA_IMAGE_DELTA = 0x104000
CODEPAGE_PTRS_ADDR = 0x00A09840
CODEPAGE_PTRS_SIZE = 0x388

SPECIAL_SLOTS = {
    0: 0x00A09BC8,
    1: 0x00A0A0C8,
}


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def inspect_registry(bin_data: bytes, ptrs: list[int]) -> tuple[dict[int, int], list[int]]:
    by_id: dict[int, int] = {}
    ids: list[int] = []
    for ptr in ptrs:
        off = ptr - GHIDRA_IMAGE_DELTA
        entry_id = u32(bin_data, off)
        by_id[entry_id] = ptr
        ids.append(entry_id)
    return by_id, ids


def dump_special_slots(bin_data: bytes) -> list[str]:
    lines: list[str] = []
    for slot_id, addr in SPECIAL_SLOTS.items():
        off = addr - GHIDRA_IMAGE_DELTA
        words = [u32(bin_data, off + i) for i in range(0, 0x20, 4)]
        lines.append(
            f"slot {slot_id}: addr=0x{addr:08X} file_off=0x{off:X} "
            f"head={[f'0x{w:08X}' for w in words]}"
        )
    return lines


def run_ghidra_checks() -> tuple[list[int], list[str]]:
    pyghidra.start()
    from pyghidra import open_project
    from ghidra.framework.model import ProjectDataUtils
    from ghidra.util.task import ConsoleTaskMonitor
    from ghidra.app.decompiler import DecompInterface

    mon = ConsoleTaskMonitor()
    consumer = jpype.JClass("java.lang.Object")()
    lines: list[str] = []
    ptrs: list[int] = []

    with open_project(PROJECT_DIR, PROJECT_NAME) as proj:
        root = proj.projectData.getRootFolder()
        domain_file = list(ProjectDataUtils.descendantFiles(root))[0]
        program = domain_file.getDomainObject(consumer, False, False, mon)
        try:
            af = program.getAddressFactory().getDefaultAddressSpace()
            fm = program.getFunctionManager()
            rm = program.getReferenceManager()
            mem = program.getMemory()

            buf = jpype.JArray(jpype.JByte)(CODEPAGE_PTRS_SIZE)
            mem.getBytes(af.getAddress(CODEPAGE_PTRS_ADDR), buf)
            raw = bytes((b + 256) % 256 for b in buf)
            ptrs = [struct.unpack_from("<Q", raw, i)[0] for i in range(0, len(raw), 8)]
            lines.append(f"full_codepage_ptrs_entries={len(ptrs)}")

            for data_addr in [0x00A09BC8, 0x00A0A0C8]:
                addr = af.getAddress(data_addr)
                refs = list(rm.getReferencesTo(addr))
                lines.append(f"xrefs_to_0x{data_addr:08X} count={len(refs)}")
                for ref in refs:
                    func = fm.getFunctionContaining(ref.getFromAddress())
                    func_name = func.getName() if func else "None"
                    func_ep = str(func.getEntryPoint()) if func else "None"
                    lines.append(
                        f"  from={ref.getFromAddress()} type={ref.getReferenceType()} "
                        f"func={func_name}@{func_ep}"
                    )

            target = af.getAddress(0x006622A7)
            refs = list(rm.getReferencesTo(target))
            lines.append(f"xrefs_to_FUN_006622a7 count={len(refs)}")
            for ref in refs:
                func = fm.getFunctionContaining(ref.getFromAddress())
                func_name = func.getName() if func else "None"
                func_ep = str(func.getEntryPoint()) if func else "None"
                lines.append(
                    f"  from={ref.getFromAddress()} type={ref.getReferenceType()} "
                    f"func={func_name}@{func_ep}"
                )

            decomp = DecompInterface()
            decomp.openProgram(program)
            try:
                for addr_val in [0x00607704, 0x00661DC0, 0x006622A7]:
                    addr = af.getAddress(addr_val)
                    func = fm.getFunctionAt(addr) or fm.getFunctionContaining(addr)
                    lines.append(f"decompile_target=0x{addr_val:08X} func={func.getName()}@{func.getEntryPoint()}")
                    res = decomp.decompileFunction(func, 180, mon)
                    text = res.getDecompiledFunction().getC() if res.decompileCompleted() else ""
                    for needle in [
                        "_DAT_00a5bbf0 = &DAT_00a09bc8;",
                        "_DAT_00a5bbf8 = &DAT_00a0a0c8;",
                        "FUN_006622a7((undefined1)local_92,*(undefined4 *)(param_4 + 0x1e))",
                        "lVar1 = FUN_0060d1d4(lVar1,param_2);",
                    ]:
                        if needle in text:
                            lines.append(f"  found: {needle}")
            finally:
                decomp.dispose()
        finally:
            program.release(consumer)

    return ptrs, lines


def main() -> None:
    bin_data = BIN_PATH.read_bytes()
    ptrs, ghidra_lines = run_ghidra_checks()
    by_id, ids = inspect_registry(bin_data, ptrs)

    print(f"BIN_PATH={BIN_PATH}")
    print(f"pointer_count={len(ptrs)}")
    print(f"registry_ids_sorted={sorted(ids)}")
    print(f"contains_id_0={0 in by_id}")
    print(f"contains_id_1={1 in by_id}")
    print()
    print("special_slot_headers:")
    for line in dump_special_slots(bin_data):
        print(" ", line)
    print()
    print("selected_registry_candidates:")
    for entry_id in [2, 35, 36, 38]:
        ptr = by_id.get(entry_id)
        if ptr is None:
            print(f"  id={entry_id}: missing")
        else:
            print(f"  id={entry_id}: ptr=0x{ptr:08X}")
    print()
    print("ghidra_checks:")
    for line in ghidra_lines:
        print(line)


if __name__ == "__main__":
    main()
