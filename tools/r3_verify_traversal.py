import struct
import sys
import os

def popcount64(x):
    x -= (x >> 1) & 0x5555555555555555
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f
    return ((x * 0x0101010101010101) >> 56) & 0xff

def bit_at(bits_data, pos):
    byte_idx = pos >> 3
    if byte_idx >= len(bits_data): return 0
    return (bits_data[byte_idx] >> (7 - (pos & 7))) & 1

def select0(bits_data, n):
    """Find the position of the n-th 0-bit (1-based)."""
    count = 0
    for byte_idx in range(len(bits_data)):
        b = bits_data[byte_idx]
        for bit_idx in range(8):
            if ((b >> (7 - bit_idx)) & 1) == 0:
                count += 1
                if count == n:
                    return byte_idx * 8 + bit_idx
    return -1

def rank1(bits_data, pos):
    pass

def rank1_8(bits_data, pos):
    count = 0
    full_bytes = pos >> 3
    for i in range(full_bytes):
        count += bin(bits_data[i]).count('1')
    rem = pos & 7
    if rem > 0:
        b = bits_data[full_bytes]
        count += bin(b >> (8 - rem)).count('1')
    return count

def build_cp866():
    t = {}
    for i in range(32): t[i] = chr(0x0410 + i)      # А-Я
    for i in range(16): t[32 + i] = chr(0x0430 + i)  # а-п
    for i in range(16): t[96 + i] = chr(0x0440 + i)  # р-я
    t[112] = 'Ё'; t[113] = 'ё'
    t[32] = '-' # wait, 32 is 'а' or '-'? In R1_ANALYSIS it said: '-' -> 32? Let's check decode_1nc7_v2.py
    # decode_1nc7_v2.py says: 32 to 47 is 'а' to 'п'. But R1_ANALYSIS says '-' is 32. We will handle safely.
    return t

CP866 = build_cp866()
CP866[45] = '-' # just in case standard ascii
CP866[32] = '-' # Let's override 32 to be '-' if needed, but in v2 it mapped 32 to 'а'.

def extract():
    trie_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"
    if not os.path.exists(trie_path):
        print("Trie file not found.")
        return
        
    with open(trie_path, 'rb') as f:
        data = f.read()

    print("Loading BV0 (LOUDS) and Labels...")
    # Based on decode_1nc7_v2.py: BV0 bits at 8, BV1 bits at 162008, etc.
    # Wait, BV0 is "pre-data" at offset 8 (size 124944)
    # BV1 bits at 162008 (size 124944)
    # Let's load BV0 and BV1 bits
    N = 15614
    bits_size = N * 8
    
    label_start = 636424
    label_end = 999284
    labels = data[label_start:label_end]
    
    offsets = [8, 162008, 324008, 480200]
    names = ["BV0_8", "BV1_162K", "BV2_324K", "BV3_480K"]
    bvs = [data[off + 24 : off + 24 + bits_size] for off in offsets]

    # Test all pairs (louds, has_child)
    for i in range(4):
        for j in range(4):
            if i == j: continue
            louds_name = names[i]
            hc_name = names[j]
            bv_louds = bvs[i]
            bv_hc = bvs[j]
            
            print(f"\n=== LOUDS: {louds_name}, HAS_CHILD: {hc_name} ===")
            
            def get_children(node_id):
                if bit_at(bv_hc, node_id) == 0:
                    return []
                    
                c_rank = rank1_8(bv_hc, node_id)
                # the number of 1s in has_child up to node_id
                # select0 gives the position of the 0 BEFORE the children
                zero_pos = select0(bv_louds, c_rank)
                if zero_pos == -1: return []
                
                children = []
                pos = zero_pos + 1
                while bit_at(bv_louds, pos) == 1:
                    child_id = rank1_8(bv_louds, pos) - 1
                    children.append(child_id)
                    pos += 1
                return children

            words_found = []
            def dfs(node_id, prefix, depth=0):
                if depth > 15: return
                if len(words_found) >= 10: return
                
                if node_id < len(labels):
                    lbl = labels[node_id]
                    char = CP866.get(lbl, '') if lbl in CP866 else chr(lbl) if lbl < 128 else ''
                else:
                    char = ''
                    
                current = prefix + char
                children = get_children(node_id)
                
                if len(children) == 0 and len(current) > 1:
                    words_found.append(current)
                    
                for c in children:
                    if c > node_id: # avoid loop
                        dfs(c, current, depth + 1)

            try:
                dfs(0, "")
                if words_found:
                    print(f"Found {len(words_found)} words:")
                    for w in words_found:
                        print("  ", w)
                else:
                    print("No valid words found.")
            except Exception as e:
                print(f"Error: {e}")


    
if __name__ == '__main__':
    extract()
