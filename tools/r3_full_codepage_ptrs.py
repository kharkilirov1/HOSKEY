#!/usr/bin/env python3
"""Dump the full PTR_DAT_00a09840 codepage pointer table from Ghidra memory."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, r"C:\Users\Kharki\AppData\Local\Programs\Python\Python312\Lib\site-packages")

import jpype
import pyghidra


PROJECT_DIR = r"C:\Users\Kharki\ghidra_yandex"
PROJECT_NAME = "YKB"
CODEPAGE_PTRS_ADDR = 0x00A09840
CODEPAGE_PTRS_SIZE = 0x388
BIN_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\native_libs\x86_64\libjni_ykeyboard3.so")
GHIDRA_IMAGE_DELTA = 0x104000


def main() -> None:
    pyghidra.start()
    from pyghidra import open_project
    from ghidra.framework.model import ProjectDataUtils
    from ghidra.util.task import ConsoleTaskMonitor

    mon = ConsoleTaskMonitor()
    consumer = jpype.JClass("java.lang.Object")()
    bin_data = BIN_PATH.read_bytes()

    with open_project(PROJECT_DIR, PROJECT_NAME) as proj:
        root = proj.projectData.getRootFolder()
        domain_file = list(ProjectDataUtils.descendantFiles(root))[0]
        program = domain_file.getDomainObject(consumer, False, False, mon)
        try:
            mem = program.getMemory()
            af = program.getAddressFactory().getDefaultAddressSpace()
            buf = jpype.JArray(jpype.JByte)(CODEPAGE_PTRS_SIZE)
            mem.getBytes(af.getAddress(CODEPAGE_PTRS_ADDR), buf)
            raw = bytes((b + 256) % 256 for b in buf)
        finally:
            program.release(consumer)

    ptrs = [struct.unpack_from("<Q", raw, i)[0] for i in range(0, len(raw), 8)]
    print(f"CODEPAGE_PTRS_ADDR=0x{CODEPAGE_PTRS_ADDR:08X}")
    print(f"entry_count={len(ptrs)}")
    for idx, ptr in enumerate(ptrs):
        if ptr == 0:
            print(f"[{idx:03d}] ptr=0x0000000000000000 entry_id=None")
            continue
        off = ptr - GHIDRA_IMAGE_DELTA
        entry_id = struct.unpack_from("<I", bin_data, off)[0]
        print(f"[{idx:03d}] ptr=0x{ptr:016X} entry_id={entry_id}")


if __name__ == "__main__":
    main()
