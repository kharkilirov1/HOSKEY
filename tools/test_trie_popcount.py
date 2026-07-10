import os
import struct

def count_bits(v):
    c = 0
    while v:
        v &= v - 1
        c += 1
    return c

def read_bv_popcount(path, offset):
    with open(path, "rb") as f:
        f.seek(offset)
        # Read header: 16 bytes. Then count words.
        header = f.read(16)
        if len(header) < 16: return -1
        magic, size, ones_count, _ = struct.unpack("<IIII", header)
        if magic != 0x01132002: return -1
        
        words = f.read(size * 8)
        pop = sum(count_bits(w) for w in struct.unpack(f"<{size}Q", words))
        return pop, ones_count

def test_trie(path):
    if not os.path.exists(path):
        return
    with open(path, "rb") as f:
        hdr = f.read(32)
        if len(hdr) < 32: return
        magic, version, unk1, item_count, unk2, padding, payload_hint, count_hint = struct.unpack("<IIIIIIII", hdr)
        
        print(f"File: {os.path.basename(path)}")
        print(f"  magic={magic:08x} items={item_count}")
        
        # Read offsets at 0x120
        f.seek(0x120)
        offsets = struct.unpack("<7Q", f.read(56))
        print(f"  bv offsets: {offsets}")
        
        for i, off in enumerate(offsets[:4]):
            if off > 0:
                res = read_bv_popcount(path, off)
                if res != -1:
                    print(f"  bv[{i}] at {off}: popcount={res[0]} (hdr says {res[1]})")

test_trie(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
test_trie(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\KEYBOARD-13421_trie")

