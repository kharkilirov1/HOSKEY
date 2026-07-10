#!/usr/bin/env python3
"""Explore 1nc7 tree structure"""

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
    
    # Parse header
    magic = struct.unpack('<I', data[0:4])[0]
    item_count = struct.unpack('<I', data[16:20])[0]
    payload_hint = struct.unpack('<I', data[8:12])[0]
    
    print(f"Magic: 0x{magic:08X}")
    print(f"Item count: {item_count}")
    print(f"Payload: {payload_hint}")
    
    # Bitvector sections
    BITS_SECTION = 124992
    bv_offsets = [130744, 292744, 448936, 605128]
    
    # Parse LOUDS (BV2)
    bv2_offset = bv_offsets[2]
    bv2_num_words = struct.unpack('<Q', data[bv2_offset:bv2_offset+8])[0]
    bv2_popcount = struct.unpack('<Q', data[bv2_offset+16:bv2_offset+24])[0]
    bv2_bits_offset = bv2_offset + 24 - BITS_SECTION
    
    print(f"\nBV2 (LOUDS):")
    print(f"  Num words: {bv2_num_words}")
    print(f"  Popcount: {bv2_popcount}")
    print(f"  Bits offset: {bv2_bits_offset}")
    
    # Read LOUDS bits
    louds_bits = data[bv2_bits_offset:bv2_bits_offset + bv2_num_words * 8]
    
    # Get first 64 positions
    print(f"\nLOUDS first 64 bits:")
    positions = []
    for pos in range(64):
        bit = get_bit(louds_bits, pos)
        if bit:
            positions.append(pos)
        if pos < 32:
            print(f"  Pos {pos:2d}: {bit}")
    
    print(f"\nNodes (first 20 '1's in LOUDS):")
    for i, pos in enumerate(positions[:20]):
        print(f"  Node {i:2d} at position {pos}")
    
    # Parse labels
    bv3_offset = bv_offsets[3]
    bv3_num_words = struct.unpack('<Q', data[bv3_offset:bv3_offset+8])[0]
    rank_size = ((bv3_num_words + 7) // 8) * 16
    labels_offset = bv3_offset + 24 + rank_size + 40
    
    # Find actual labels start (look for metadata)
    for p in range(bv3_offset + 24 + rank_size, payload_hint, 8):
        m0 = struct.unpack('<Q', data[p:p+8])[0]
        m1 = struct.unpack('<Q', data[p+8:p+16])[0] if p+16 < len(data) else 0
        m2 = struct.unpack('<Q', data[p+16:p+24])[0] if p+24 < len(data) else 0
        if m1 == 8 and m2 == 255 and 400000 < m0 < 410000:
            labels_offset = p + 40
            print(f"\nLabels metadata at {p}, labels start at {labels_offset}")
            break
    
    labels = data[labels_offset:labels_offset + 100]
    print(f"\nFirst 20 labels:")
    for i in range(20):
        print(f"  labels[{i}] = {labels[i]}")
    
    # Try to reconstruct tree
    print(f"\n--- Tree Reconstruction ---")
    print("Assuming LOUDS encodes parent-child relationships...")
    print("Root is node 0 (first '1' in LOUDS)")
    
    # In LOUDS:
    # - Each '1' marks a node start
    # - Children of node i are between Select1(i)+1 and Select1(i+1)-1
    
    node_positions = positions[:20]  # First 20 nodes
    
    print(f"\nNode positions in LOUDS:")
    for i, pos in enumerate(node_positions):
        print(f"  Node {i}: position {pos}")
    
    # Find children of root (node 0)
    root_pos = node_positions[0]  # 1
    next_node_pos = node_positions[1]  # 2
    
    print(f"\nRoot (node 0) at position {root_pos}:")
    print(f"  Next node (node 1) at position {next_node_pos}")
    print(f"  Positions between: {root_pos+1} to {next_node_pos-1}")
    
    # Check if there are '1's between root and node 1
    children_of_root = []
    for pos in range(root_pos + 1, next_node_pos):
        if get_bit(louds_bits, pos):
            children_of_root.append(pos)
    
    print(f"  '1's in this range: {children_of_root}")
    if not children_of_root:
        print(f"  -> Root has only 1 direct child (node 1)")
    
    # Find children of node 1
    node1_pos = node_positions[1]  # 2
    node2_pos = node_positions[2]  # 4
    
    print(f"\nNode 1 at position {node1_pos}:")
    print(f"  Next sibling/parent's next at position {node2_pos}")
    print(f"  Checking positions {node1_pos+1} to {node2_pos-1}:")
    
    children_of_node1 = []
    for pos in range(node1_pos + 1, node2_pos):
        bit = get_bit(louds_bits, pos)
        print(f"    Pos {pos}: {bit}")
        if bit:
            children_of_node1.append(pos)
    
    print(f"  Children of node 1: positions {children_of_node1}")
    
    # Map positions to node IDs
    pos_to_node = {pos: i for i, pos in enumerate(node_positions)}
    
    print(f"\n--- Final Tree Structure ---")
    print(f"Root (node 0, label=?)")
    if 1 in pos_to_node:
        child1 = pos_to_node[1]
        label1 = labels[child1] if child1 < len(labels) else '?'
        print(f"  -> Node {child1} (label={label1})")
        
        if 2 in pos_to_node:
            child2 = pos_to_node[2]
            label2 = labels[child2] if child2 < len(labels) else '?'
            print(f"       -> Node {child2} (label={label2})")

if __name__ == '__main__':
    main()
