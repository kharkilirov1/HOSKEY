import struct
import sys
import os

def parse_lm():
    trie_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"
    if not os.path.exists(trie_path):
        print("Trie file not found.")
        return
        
    with open(trie_path, 'rb') as f:
        data = f.read()

    tail_start = 999284
    tail_data = data[tail_start:]
    print(f"--- Phase R4: N-gram LM Analysis ---")
    print(f"Tail size: {len(tail_data)} bytes")

    # Read the first 16 32-bit integers
    print("\nFirst 16 uint32:")
    ints = struct.unpack(f"<{16}I", tail_data[:64])
    for i, val in enumerate(ints):
        print(f"  [{i}]: {val} (0x{val:X})")

    # Read the first 8 64-bit integers
    print("\nFirst 8 uint64:")
    longs = struct.unpack(f"<{8}Q", tail_data[:64])
    for i, val in enumerate(longs):
        print(f"  [{i}]: {val} (0x{val:X})")

    # Search for specific arrays/sizes
    # Is there a chunk that looks like float arrays or count arrays?
    
    # Read footer metadata
    print("\nFooter Metadata:")
    p = len(tail_data) - 4
    # scan back to find _LANG_ID
    while p > len(tail_data) - 200:
        if tail_data[p:p+8] == b'_LANG_ID':
            p -= 4
            break
        p -= 1
        
    while p < len(tail_data) - 8:
        try:
            klen = struct.unpack("<I", tail_data[p:p+4])[0]
            key = tail_data[p+4:p+4+klen].decode('ascii')
            val = struct.unpack("<Q", tail_data[p+4+klen:p+4+klen+8])[0]
            print(f"  {key} = {val}")
            p += 4 + klen + 8
        except:
            p += 1

if __name__ == '__main__':
    parse_lm()
