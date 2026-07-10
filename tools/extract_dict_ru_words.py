#!/usr/bin/env python3
"""
Extract words from dict_ru_full (DFS interleaved format).
Format: magic 0xFE3AC19B + container with DFS-encoded trie.
"""

import struct
import sys

def extract_words_from_dict_ru(input_path, output_path):
    with open(input_path, 'rb') as f:
        data = f.read()
    
    # Check magic
    magic = struct.unpack('<I', data[:4])[0]
    if magic != 0xFE3AC19B:
        print(f"Warning: magic mismatch: 0x{magic:08X}")
    
    # Based on analysis, the DFS blob starts around offset 0x2720
    # Markers: 0x40=NODE, 0x48=TERM, 0x50=LEAF, 0x58=XTND, 0x20=SPACE
    # Followed by UTF-8 Cyrillic text
    
    words = []
    current_word = []
    i = 0x2720  # Start of DFS blob
    
    while i < len(data) - 10:
        byte = data[i]
        
        if byte == 0x40:  # NODE - start of new word component
            i += 1
            # Read UTF-8 sequence
            utf8_start = i
            while i < len(data) and data[i] not in (0x40, 0x48, 0x50, 0x58, 0x20):
                i += 1
            if i > utf8_start:
                try:
                    text = data[utf8_start:i].decode('utf-8')
                    current_word.append(text)
                except:
                    pass
                    
        elif byte == 0x48:  # TERM - end of word
            if current_word:
                word = ''.join(current_word)
                if len(word) >= 2 and len(word) <= 32:
                    words.append(word)
                current_word = []
            i += 1
            
        elif byte == 0x50:  # LEAF - terminal node
            i += 1
            # Read text
            utf8_start = i
            while i < len(data) and data[i] not in (0x40, 0x48, 0x50, 0x58, 0x20):
                i += 1
            if i > utf8_start:
                try:
                    text = data[utf8_start:i].decode('utf-8')
                    current_word.append(text)
                    word = ''.join(current_word)
                    if len(word) >= 2 and len(word) <= 32:
                        words.append(word)
                    current_word = []
                except:
                    pass
                    
        elif byte == 0x20:  # SPACE - separator
            i += 1
            
        elif byte == 0x58:  # XTND - extended
            i += 1
            
        else:
            # Skip unknown bytes
            i += 1
    
    # Write output
    with open(output_path, 'w', encoding='utf-8') as f:
        for word in sorted(set(words)):
            f.write(word + '\n')
    
    print(f"Extracted {len(set(words))} unique words to {output_path}")

if __name__ == '__main__':
    input_file = r'C:\Users\Kharki\Desktop\yandex_extracted\dict_ru_full'
    output_file = r'C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\words.txt'
    extract_words_from_dict_ru(input_file, output_file)
