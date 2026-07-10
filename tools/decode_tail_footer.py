#!/usr/bin/env python3
"""Decode the footer/metadata at the end of the trie tail."""
from __future__ import annotations
import struct
import pathlib


def main():
    trie_path = pathlib.Path(
        r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\trie"
    )
    data = trie_path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    _, _, payload_hint, _, item_count, _ = header
    tail = data[payload_hint:]

    print(f"tail_size={len(tail)}")

    # --- Scan tail backwards for ASCII strings ---
    print("\n=== TAIL FOOTER (last 512 bytes) ===")
    footer = tail[-512:]
    # Find ASCII strings
    i = 0
    strings_found = []
    while i < len(footer):
        if 0x20 <= footer[i] < 0x7F:
            j = i
            while j < len(footer) and 0x20 <= footer[j] < 0x7F:
                j += 1
            s = footer[i:j].decode("ascii")
            if len(s) >= 3:
                strings_found.append((len(tail) - 512 + i, s))
            i = j
        else:
            i += 1

    for off, s in strings_found:
        print(f"  tail+{off}: '{s}'")

    # --- Decode last 256 bytes as potential metadata entries ---
    print("\n=== LAST 256 BYTES DETAILED ===")
    last256 = tail[-256:]
    print(f"hex: {last256.hex()}")
    # Try to find pattern: [key_len:u32] [key:bytes] [value:varies]
    # or: [key:null-terminated] [value:u32]

    # Scan for _KEY patterns
    pos = 0
    while pos < len(last256):
        if last256[pos] == 0x5F:  # underscore
            # Might be start of a key
            end = pos
            while end < len(last256) and 0x20 <= last256[end] < 0x7F:
                end += 1
            if end - pos >= 3:
                key = last256[pos:end].decode("ascii")
                # Read next few bytes as potential values
                remaining = last256[end:end+16]
                vals = []
                for vi in range(0, min(len(remaining), 16), 4):
                    if vi + 4 <= len(remaining):
                        vals.append(struct.unpack_from("<I", remaining, vi)[0])
                print(f"  key='{key}' at byte {pos}, next_u32s={[hex(v) for v in vals]}")
                pos = end
            else:
                pos += 1
        else:
            pos += 1

    # --- Scan the ENTIRE tail for metadata keys ---
    print("\n=== ALL METADATA KEYS IN TAIL ===")
    known_keys = [b"_MIN_FREQ", b"_TOTAL_WORDS", b"_LANG_ID", b"_VERSION",
                  b"_NUM_NODES", b"_ALPHABET", b"_LEVELS", b"_MAX_DEPTH",
                  b"_ENCODING", b"_FORMAT"]
    for key in known_keys:
        idx = tail.find(key)
        if idx >= 0:
            # Read surrounding context
            ctx_start = max(0, idx - 16)
            ctx_end = min(len(tail), idx + len(key) + 32)
            ctx = tail[ctx_start:ctx_end]
            # Read value after key
            after_key = tail[idx + len(key):idx + len(key) + 16]
            vals = []
            for vi in range(0, min(len(after_key), 16), 4):
                if vi + 4 <= len(after_key):
                    vals.append(struct.unpack_from("<I", after_key, vi)[0])
            print(f"  {key.decode()}: at tail+{idx}, vals={vals} hex_vals={[hex(v) for v in vals]}")
            print(f"    context_hex={ctx.hex()}")

    # --- Try yarchive at the END of the entire file ---
    print("\n=== YARCHIVE AT END OF ENTIRE FILE ===")
    last8 = struct.unpack_from("<Q", data, len(data) - 8)[0]
    print(f"last_u64_of_file={last8} (0x{last8:X})")
    if 0 < last8 < len(data) - 8:
        st = data[last8:]
        sec_count = struct.unpack_from("<I", st, 0)[0]
        print(f"section_count={sec_count}")
        if 0 < sec_count < 200:
            for si in range(min(sec_count, 30)):
                off = 4 + si * 20
                if off + 20 <= len(st):
                    key, sec_off, sec_size = struct.unpack_from("<QQI", st, off)
                    # Try to decode key as ASCII
                    key_bytes = key.to_bytes(8, "little")
                    key_ascii = ""
                    try:
                        key_ascii = key_bytes.rstrip(b"\x00").decode("ascii", errors="replace")
                    except:
                        pass
                    print(f"  section[{si}]: key=0x{key:016X} ('{key_ascii}') offset={sec_off} size={sec_size}")

    # --- Try yarchive within the tail ---
    print("\n=== YARCHIVE WITHIN TAIL ===")
    tail_last8 = struct.unpack_from("<Q", tail, len(tail) - 8)[0]
    print(f"last_u64_of_tail={tail_last8} (0x{tail_last8:X})")

    # The metadata strings suggest a different footer format.
    # Let me scan backwards from the end to find the footer structure.
    print("\n=== REVERSE SCAN FOR FOOTER FORMAT ===")
    # Look for pattern: entries of [key_len:u32][key:ascii][value:u32 or u64]
    # working backwards from end
    pos = len(tail)

    # Try: the footer might use a format where each entry is:
    # [value:u64][key_len:u32][key:ascii]
    # reading backwards
    entries = []
    scan_pos = len(tail)
    for _ in range(20):  # try up to 20 entries
        if scan_pos < 16:
            break
        # Look for ASCII string ending at or near scan_pos
        end = scan_pos
        start = end
        while start > max(0, end - 64) and 0x20 <= tail[start - 1] < 0x7F:
            start -= 1
        if end - start >= 3:
            key = tail[start:end].decode("ascii", errors="replace")
            # Read u32 before the key (key length?)
            if start >= 4:
                pre_u32 = struct.unpack_from("<I", tail, start - 4)[0]
                if pre_u32 == end - start:
                    # key_len matches! Read value before key_len
                    if start >= 12:
                        val_u64 = struct.unpack_from("<Q", tail, start - 12)[0]
                        val_u32 = struct.unpack_from("<I", tail, start - 8)[0]
                        entries.append((key, pre_u32, val_u64, val_u32))
                        scan_pos = start - 12
                        continue
                    elif start >= 8:
                        val_u32 = struct.unpack_from("<I", tail, start - 8)[0]
                        entries.append((key, pre_u32, None, val_u32))
                        scan_pos = start - 8
                        continue
        # If no match, back up one byte
        scan_pos -= 1

    if entries:
        print("Found footer entries (reading backwards):")
        for key, key_len, val_u64, val_u32 in reversed(entries):
            print(f"  key='{key}' key_len={key_len} val_u64={val_u64} val_u32={val_u32}")
    else:
        print("No structured footer entries found with backward scan.")

    # --- Brute force: dump last 128 bytes with annotations ---
    print("\n=== LAST 128 BYTES ANNOTATED ===")
    last128 = tail[-128:]
    pos = 0
    while pos < len(last128):
        b = last128[pos]
        if 0x20 <= b < 0x7F:
            # ASCII run
            end = pos
            while end < len(last128) and 0x20 <= last128[end] < 0x7F:
                end += 1
            s = last128[pos:end].decode("ascii")
            print(f"  [{pos:3d}] ASCII: '{s}'")
            pos = end
        else:
            # Non-ASCII: read as u32
            if pos + 4 <= len(last128):
                v = struct.unpack_from("<I", last128, pos)[0]
                print(f"  [{pos:3d}] u32: {v} (0x{v:08X})  bytes: {last128[pos:pos+4].hex()}")
                pos += 4
            else:
                print(f"  [{pos:3d}] byte: 0x{b:02X}")
                pos += 1


if __name__ == "__main__":
    main()
