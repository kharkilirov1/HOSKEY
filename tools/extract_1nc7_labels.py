import struct
import sys
import os

def read_u64(f):
    return struct.unpack("<Q", f.read(8))[0]

def trace_1nc7(filepath):
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found.")
        sys.exit(1)

    print(f"Tracing file: {filepath}")
    with open(filepath, 'rb') as f:
        # Global header: 24 bytes (magic, version, 4 more u32)
        header = f.read(24)
        print(f"Header: {header[:4]} Version: {struct.unpack('<I', header[4:8])[0]}")

        # According to R1_ANALYSIS.md, the stream starts at offset 24
        # But wait, looking at the Python scripts, there's BV0 (bits only?)
        # Let's trace it carefully, or just extract the DataSection known to be at offset ~636424
        # Wait! In decode_1nc7_v2.py, the script already finds it at offset 636424!
        # "Label data: after BV4 metadata, starting around file+636424"
        
        # Let's read the labels from offset 636424 as identified in R1 Analysis and decode_1nc7_v2.py.
        f.seek(636424)
        print(f"\n--- Extracting Labels (Offset 636424) ---")
        # Let's read the metadata and then the data.
        # But wait, R1_ANALYSIS says it's a DataSection.
        # DataSection format: f0: u64, count: u64, f2: u64, data...
        # But wait, in decode_1nc7_v2.py it says "metadata: [8, 255, 3222592, 50353] + labels + hash"
        # Let's read and see.
        f.seek(636392)
        meta = struct.unpack('<4Q', f.read(32))
        print(f"Metadata at 636392: {meta}")
        
        # If it's a raw byte array starting at 636424:
        label_start = 636424
        label_end = 999284
        
        f.seek(label_start)
        label_data = f.read(label_end - label_start)
        
        print(f"Read {len(label_data)} bytes of label data.")
        
        # Let's test CP866 decoding for the first 100 bytes
        print("\n--- First 100 labels (Hex -> Char) ---")
        
        LABEL_MAP = {}
        for i in range(32): LABEL_MAP[i] = chr(0x0410 + i)      # А-Я
        for i in range(16): LABEL_MAP[32 + i] = chr(0x0430 + i)  # а-п
        for i in range(16): LABEL_MAP[96 + i] = chr(0x0440 + i)  # р-я
        LABEL_MAP[112] = 'Ё'; LABEL_MAP[113] = 'ё'
        
        decoded = []
        for i, b in enumerate(label_data[:100]):
            c = LABEL_MAP.get(b, f"\\x{b:02X}")
            decoded.append(f"Node {i:3d}: {b:3d} -> {c}")
            
        for line in decoded[:20]:
            print(line)
        print("...")
        
        out_file = "tools/extracted_labels_cp866.txt"
        with open(out_file, "w", encoding="utf-8") as out:
            for i, b in enumerate(label_data):
                out.write(f"{i}\t{b}\n")
                
        print(f"\nSaved raw labels to {out_file}")

if __name__ == '__main__':
    trie_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"
    trace_1nc7(trie_path)
