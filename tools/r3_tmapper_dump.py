#!/usr/bin/env python3
"""Dump NDict::NUtil::TMapper langID -> codepage mapping table."""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"C:\Users\Kharki\AppData\Local\Programs\Python\Python312\Lib\site-packages")

import jpype
import pyghidra


PROJECT_DIR = r"C:\Users\Kharki\ghidra_yandex"
PROJECT_NAME = "YKB"
TMAPPER_ADDR = 0x001AB840
TMAPPER_SIZE = 0x308


def main() -> None:
    pyghidra.start()
    from pyghidra import open_project
    from ghidra.framework.model import ProjectDataUtils
    from ghidra.util.task import ConsoleTaskMonitor

    mon = ConsoleTaskMonitor()
    consumer = jpype.JClass("java.lang.Object")()

    with open_project(PROJECT_DIR, PROJECT_NAME) as proj:
        root = proj.projectData.getRootFolder()
        domain_file = list(ProjectDataUtils.descendantFiles(root))[0]
        program = domain_file.getDomainObject(consumer, False, False, mon)
        try:
            mem = program.getMemory()
            af = program.getAddressFactory().getDefaultAddressSpace()
            addr = af.getAddress(TMAPPER_ADDR)
            buf = jpype.JArray(jpype.JByte)(TMAPPER_SIZE)
            mem.getBytes(addr, buf)
            data = bytes((b + 256) % 256 for b in buf)
        finally:
            program.release(consumer)

    pairs: list[tuple[int, int]] = []
    for i in range(0, TMAPPER_SIZE, 8):
        raw_lang_id = struct.unpack_from("<I", data, i)[0]
        mapped_plus_1 = struct.unpack_from("<I", data, i + 4)[0]
        pairs.append((raw_lang_id, mapped_plus_1))

    nonzero_pairs = [(raw_id, mapped, mapped - 1 if mapped else None) for raw_id, mapped in pairs if raw_id or mapped]

    print(f"TMAPPER_ADDR=0x{TMAPPER_ADDR:08X}")
    print(f"entry_count={len(pairs)}")
    print(f"nonzero_entry_count={len(nonzero_pairs)}")
    print()
    print("nonzero_pairs_sorted_by_raw_lang_id:")
    for raw_id, mapped, mapped_id in sorted(nonzero_pairs, key=lambda item: item[0]):
        print(f"  raw_lang_id={raw_id:>3} mapped_plus_1={mapped:>3} mapped_codepage_id={mapped_id}")

    print()
    for interesting in [0, 5, 9, 13, 17, 33, 35]:
        raws = [raw_id for raw_id, mapped, mapped_id in nonzero_pairs if mapped_id == interesting]
        print(f"mapped_codepage_id={interesting}: raw_lang_ids={raws}")


if __name__ == "__main__":
    main()
