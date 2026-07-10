
# Phase R4: Decoded Helper Functions in Python

def decode_leb128(data, offset):
    """Equivalent to FUN_00609346. Reads LEB128 unsigned int."""
    result = 0
    shift = 0
    while True:
        b = data[offset]
        offset += 1
        result |= (b & 0x7f) << shift
        shift += 7
        if (b & 0x80) == 0:
            break
    return result, offset

def zigzag_decode(val):
    """Decodes ZigZag encoded unsigned integer to signed integer.
    Equivalent to -(uVar3 & 1) ^ (uVar3 >> 1)
    """
    return -(val & 1) ^ (val >> 1)

def skip_record_a(data, offset):
    """Equivalent to FUN_006092f6 (used in TrieNGramFreqs)."""
    b = data[offset]
    if b > 127: # -1 < (char)bVar1 is false
        return None
    offset += 1
    if (b & 0x40) != 0:
        offset += data[offset] + 1
    if (b & 8) == 0:
        return offset
    
    uVar2 = 0
    if (b & 4) != 0:
        uVar2 = 2 << (b & 3)
        
    return offset + uVar2 + ((data[offset + 1] + 1) << (b & 3)) + 2

def skip_record_b(data, offset):
    """Equivalent to FUN_00609a96 (used in WTrieNGramFreqs)."""
    b = data[offset]
    if b > 127:
        return None
    offset += 1
    if (b & 0x40) != 0:
        offset += data[offset] * 2 + 1
    if (b & 8) == 0:
        return offset
        
    uVar2 = 0
    if (b & 4) != 0:
        uVar2 = 2 << (b & 3)
        
    return offset + uVar2 + ((data[offset + 2] + 1) << (b & 3)) + 3

def decode_prefix_varint(data, offset):
    """Equivalent to FUN_0060c9f3. Prefix varint encoding."""
    b = data[offset]
    if b < 128:
        return b, offset + 1
        
    if (b & 0x40) == 0:
        return ((b & 0x3f) << 8) + data[offset + 1] + 0x80, offset + 2
        
    if (b & 0x20) == 0:
        return ((b & 0x1f) << 16) + (data[offset + 1] << 8) + data[offset + 2] + 0x4080, offset + 3
        
    if (b & 0x10) == 0:
        return ((b & 0xf) << 24) + (data[offset + 1] << 16) + (data[offset + 2] << 8) + data[offset + 3] + 0x204080, offset + 4
        
    if (b & 8) == 0:
        val = ((b & 7) << 32) | (data[offset + 1] << 24) | (data[offset + 2] << 16) | (data[offset + 3] << 8) | data[offset + 4]
        return val + 0x10204080, offset + 5
        
    if (b & 4) == 0:
        val = ((b & 3) << 40) | (data[offset + 1] << 32) | (data[offset + 2] << 24) | (data[offset + 3] << 16) | (data[offset + 4] << 8) | data[offset + 5]
        return val + 0x810204080, offset + 6
        
    if (b & 2) != 0:
        if (b & 1) == 0:
            val = (data[offset + 1] << 48) | (data[offset + 2] << 40) | (data[offset + 3] << 32) | (data[offset + 4] << 24) | (data[offset + 5] << 16) | (data[offset + 6] << 8) | data[offset + 7]
            return val + 0x2040810204080, offset + 8
        else:
            val = (data[offset + 1] << 56) | (data[offset + 2] << 48) | (data[offset + 3] << 40) | (data[offset + 4] << 32) | (data[offset + 5] << 24) | (data[offset + 6] << 16) | (data[offset + 7] << 8) | data[offset + 8]
            return val + 0x102040810204080, offset + 9
            
    val = ((b & 1) << 48) | (data[offset + 1] << 40) | (data[offset + 2] << 32) | (data[offset + 3] << 24) | (data[offset + 4] << 16) | (data[offset + 5] << 8) | data[offset + 6]
    return val + 0x40810204080, offset + 7

def test_decoders():
    with open(r"C:UsersKharkiDesktopyandex_extractedextracted_sections	rie", "rb") as f:
        data = f.read()
    
    tail_start = 999284
    
    # Try reading the first few values at the tail start assuming it's an array of records.
    # FUN_007d6f64 uses param_3 + 0x30 to get a type code (2, 3, 4, 5).
    # We don't know the exact array structure yet, but we can scan the first bytes and try to interpret them as records.
    
    print("Testing Skipper A on first bytes of tail...")
    offset = tail_start
    for i in range(10):
        if offset >= len(data) - 100: break
        
        # print the raw bytes of the record header
        b = data[offset]
        print(f"\nRecord {i} at offset {offset - tail_start}: header byte 0x{b:02x}")
        
        val_offset = skip_record_a(data, offset)
        if val_offset is None:
            print("  Skipper returned None (not a valid record start)")
            offset += 1
            continue
            
        print(f"  Value offset: {val_offset - tail_start} (skipped {val_offset - offset} bytes)")
        
        # Try decoding as Case 2 (LEB128 + ZigZag)
        val_leb, next_off = decode_leb128(data, val_offset)
        val_zz = zigzag_decode(val_leb)
        print(f"  Case 2 (LEB128+ZigZag): raw={val_leb}, decoded={val_zz}")
        
        # Try decoding as Case 4 (PrefixVarint + ZigZag)
        val_pv, next_off2 = decode_prefix_varint(data, val_offset)
        val_pv_zz = zigzag_decode(val_pv)
        print(f"  Case 4 (PrefixVar+ZigZag): raw={val_pv}, decoded={val_pv_zz}")
        
        # Assume records are sequential? If the next record starts immediately after the value:
        # We don't know the exact length of the value.
        offset = next_off

test_decoders()
