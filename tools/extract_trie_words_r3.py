import struct
import sys
import os

# Phase R3: Verify trie traversal + word extraction
# This script implements the LOUDS succinct trie traversal logic
# based on the Ghidra reverse engineering.

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

def rank1_incl(bits_data, pos):
    """Count number of 1-bits up to and including pos."""
    count = 0
    full_bytes = pos >> 3
    for i in range(full_bytes):
        count += bin(bits_data[i]).count('1')
    rem = pos & 7
    b = bits_data[full_bytes]
    for j in range(rem + 1):
        if (b >> (7 - j)) & 1:
            count += 1
    return count

def build_cp866():
    t = {}
    for i in range(32): t[i] = chr(0x0410 + i)      # А-Я
    for i in range(16): t[32 + i] = chr(0x0430 + i)  # а-п
    for i in range(16): t[96 + i] = chr(0x0440 + i)  # р-я
    t[112] = 'Ё'; t[113] = 'ё'; t[32] = '-'
    return t

CP866 = build_cp866()

def verify_traversal():
    trie_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"
    if not os.path.exists(trie_path):
        print(f"Error: {trie_path} not found.")
        return

    print("--- Phase R3: Verifying Trie Traversal ---")
    with open(trie_path, 'rb') as f:
        data = f.read()

    N = 15614
    bits_size = N * 8
    
    # Offsets found in R1_ANALYSIS
    bv_hc = data[8 + 24 : 8 + 24 + bits_size]             # BV0 (has_child)
    bv_louds = data[324008 + 24 : 324008 + 24 + bits_size] # BV2 (louds)
    
    label_start = 636424
    labels = data[label_start : 999284]

    def get_children(node_id):
        # 1. Check if node has children
        if bit_at(bv_hc, node_id) == 0:
            return []
        
        # 2. Get rank of node in has_child bitvector
        c_rank = rank1_incl(bv_hc, node_id)
        
        # 3. Find the 0-bit in LOUDS that precedes this node's children
        zero_pos = select0(bv_louds, c_rank)
        if zero_pos == -1: return []
        
        # 4. Iterate over contiguous 1s in LOUDS to get children
        children = []
        pos = zero_pos + 1
        while bit_at(bv_louds, pos) == 1:
            child_id = rank1_incl(bv_louds, pos) - 1
            children.append(child_id)
            pos += 1
        return children

    words_found = []
    
    def dfs(node_id, prefix, depth=0):
        if depth > 15 or len(words_found) >= 5: 
            return
            
        if node_id < len(labels):
            lbl = labels[node_id]
            char = CP866.get(lbl, '?') if lbl in CP866 else chr(lbl) if lbl < 128 else '?'
        else:
            char = ''
            
        current = prefix + char
        children = get_children(node_id)
        
        # If it's a leaf node, we found a word
        if len(children) == 0 and len(current) > 2:
            words_found.append(current)
            
        for c in children:
            if c > node_id: # safeguard against loops
                dfs(c, current, depth + 1)

    print("Running DFS traversal on Succinct Trie...")
    dfs(0, "")
    
    if words_found:
        print("\nSuccess! Extracted initial words:")
        for w in words_found:
            print("  ->", w)
        print("\nTraversal logic validated (R3 completed).")
    else:
        print("\nFailed to extract words.")

if __name__ == '__main__':
    verify_traversal()
