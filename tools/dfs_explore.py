#!/usr/bin/env python3
import struct

def read_file(path):
    with open(path, 'rb') as f:
        return f.read()

def get_bit(data, pos):
    byte_idx = pos // 8
    bit_idx = pos % 8
    return (data[byte_idx] >> bit_idx) & 1

def main():
    data = read_file(r'C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie')
    
    BITS_SECTION = 124992
    bv2_offset = 448936
    bv3_offset = 605128
    
    # Parse LOUDS
    bv2_num_words = struct.unpack('<Q', data[bv2_offset:bv2_offset+8])[0]
    bv2_bits_offset = bv2_offset + 24 - BITS_SECTION
    louds = data[bv2_bits_offset:bv2_bits_offset + bv2_num_words * 8]
    
    # Parse has_child
    bv3_num_words = struct.unpack('<Q', data[bv3_offset:bv3_offset+8])[0]
    bv3_bits_offset = bv3_offset + 24 - BITS_SECTION
    has_child = data[bv3_bits_offset:bv3_bits_offset + bv3_num_words * 8]
    
    # Parse labels
    rank_size = ((bv3_num_words + 7) // 8) * 16
    labels_offset = bv3_offset + 24 + rank_size + 40
    payload_hint = struct.unpack('<I', data[8:12])[0]
    labels = data[labels_offset:payload_hint]
    
    print(f"Total labels: {len(labels)}")
    
    def label_to_char(label):
        if label < 32:
            return chr(0x0430 + label)
        return f"[{label}]"
    
    # Print nodes with has_child
    print("\n--- Nodes with has_child ---")
    for i in range(20):
        hc = get_bit(has_child, i)
        lbl = labels[i] if i < len(labels) else 0
        print(f"Node {i:2d}: label={lbl:3d} ('{label_to_char(lbl)}'), has_child={hc}")
    
    # Try to find path for 'привет'
    print("\n--- Searching for 'привет' path ---")
    target = [15, 16, 8, 2, 5, 18]  # п,р,и,в,е,т
    
    # Simple approach: check if there's a sequence of nodes with these labels
    # where each next node has increasing ID and has_child=1 (except last)
    
    def find_path(target_labels, start_node=1):
        if not target_labels:
            return []
        
        # Find node with first label
        for i in range(start_node, min(start_node + 1000, len(labels))):
            if labels[i] == target_labels[0]:
                # Found first letter, recurse
                if len(target_labels) == 1:
                    return [i]
                rest = find_path(target_labels[1:], i + 1)
                if rest:
                    return [i] + rest
        return None
    
    path = find_path(target)
    if path:
        print(f"Found path: {path}")
        word = "".join(label_to_char(labels[n]) for n in path)
        print(f"Word: '{word}'")
    else:
        print("Path not found in first 1000 nodes")
        
        # Just find 'п' (label 15)
        print("\n--- Finding all nodes with label 15 ('п') ---")
        count = 0
        for i in range(min(10000, len(labels))):
            if labels[i] == 15:
                print(f"  Node {i}: label=15, has_child={get_bit(has_child, i)}")
                count += 1
                if count > 10:
                    print("  ...")
                    break

if __name__ == '__main__':
    main()
