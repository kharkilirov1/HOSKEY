#!/usr/bin/env python3
"""Find yarchive section table in the 1nc7 trie file.

The custom footer (128 bytes KV metadata) is at the END of the file.
The yarchive blob might end BEFORE this footer.
Try multiple blob boundaries to find valid section tables.
"""
import struct
import pathlib
import hashlib


def fnv1a_64(data: bytes) -> int:
    """FNV-1a hash used by Yandex yarchive for key hashing."""
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def try_yarchive(blob: bytes, label: str) -> list:
    """Try to parse yarchive section table from a blob."""
    if len(blob) < 16:
        return []

    # Last 8 bytes = section table offset from blob start
    st_off = struct.unpack_from("<Q", blob, len(blob) - 8)[0]

    if st_off == 0 or st_off >= len(blob) - 8:
        return []

    remaining = len(blob) - 8 - st_off
    if remaining < 4:
        return []

    st = blob[st_off:]
    count = struct.unpack_from("<I", st, 0)[0]

    if count == 0 or count > 100:
        return []

    if remaining < 4 + count * 20:
        return []

    sections = []
    pos = 4
    for i in range(count):
        key = struct.unpack_from("<Q", st, pos)[0]
        pos += 8
        offset = struct.unpack_from("<Q", st, pos)[0]
        pos += 8
        size = struct.unpack_from("<I", st, pos)[0]
        pos += 4

        if offset + size > len(blob):
            return []  # invalid section

        sections.append({"key": key, "offset": offset, "size": size})

    return sections


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    data = trie_path.read_bytes()
    h = struct.unpack_from("<6I", data, 0)
    payload_hint = h[2]

    print(f"file_size={len(data)} payload_hint={payload_hint}")

    # Footer is 128 bytes at end
    footer_size = 128
    file_no_footer = len(data) - footer_size

    # Try various blob boundaries
    candidates = [
        ("full_file", data),
        ("after_header", data[24:]),
        ("body_only", data[24:payload_hint]),
        ("file_no_footer", data[:file_no_footer]),
        ("after_header_no_footer", data[24:file_no_footer]),
        ("tail_only", data[payload_hint:]),
        ("tail_no_footer", data[payload_hint:file_no_footer]),
    ]

    for label, blob in candidates:
        if len(blob) < 16:
            continue
        last8 = struct.unpack_from("<Q", blob, len(blob) - 8)[0]
        sections = try_yarchive(blob, label)
        status = f"{len(sections)} sections" if sections else "FAILED"
        print(f"  {label:30s}: blob={len(blob):10d}  last8={last8:#18x}  -> {status}")

        if sections:
            print(f"    FOUND YARCHIVE!")
            # Compute common section name hashes for identification
            known_names = [
                "config", "conf.json", "trie", "labels", "weights",
                "bitvec", "terminal", "has_child", "louds", "rank",
                "alphabet", "frequencies", "ngrams", "data",
                "has_child_bv", "louds_bv", "is_prefix_key",
                "label_data", "label_high", "label_low",
                "has_child_rank", "louds_rank", "terminal_rank",
                "weight_data", "ef_data", "node_count",
                "D-Labels", "D-HasChild", "D-IsPrefixKey",
                "S-Labels", "S-HasChild", "S-IsPrefixKey", "S-LOUDS",
            ]
            name_hashes = {fnv1a_64(n.encode()): n for n in known_names}

            for i, sec in enumerate(sections):
                name = name_hashes.get(sec["key"], f"unknown(0x{sec['key']:016x})")
                print(f"    section[{i}]: key=0x{sec['key']:016x} ({name})"
                      f"  offset={sec['offset']}  size={sec['size']}")

                # Show first 32 bytes of section
                sec_data = blob[sec["offset"]:sec["offset"] + min(32, sec["size"])]
                print(f"      first_32: {sec_data.hex()}")

    # Also try scanning for section tables at various positions
    print(f"\n=== SCAN FOR SECTION TABLES ===")
    for scan_off in range(max(0, len(data) - 2000), len(data) - 24, 4):
        count = struct.unpack_from("<I", data, scan_off)[0]
        if count < 3 or count > 30:
            continue
        if scan_off + 4 + count * 20 > len(data):
            continue

        valid = True
        secs = []
        for i in range(count):
            pos = scan_off + 4 + i * 20
            key = struct.unpack_from("<Q", data, pos)[0]
            offset = struct.unpack_from("<Q", data, pos + 8)[0]
            size = struct.unpack_from("<I", data, pos + 16)[0]
            if offset > len(data) or size > len(data) or offset + size > len(data):
                valid = False
                break
            secs.append({"key": key, "offset": offset, "size": size})

        if valid and len(secs) == count:
            print(f"\n  CANDIDATE at file+{scan_off}: {count} sections")
            for i, sec in enumerate(secs):
                print(f"    [{i}] key=0x{sec['key']:016x} off={sec['offset']} sz={sec['size']}")


if __name__ == "__main__":
    main()
