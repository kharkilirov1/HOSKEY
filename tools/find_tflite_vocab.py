import sys
import struct

def extract_tflite_tensors(model_path):
    with open(model_path, "rb") as f:
        data = f.read()

    # We will look for the typical TFLite string tensor structure.
    # String tensors in TFLite have a list of offsets at the beginning of the buffer.
    # Alternatively, let's just search for the exact subword strings if we know any.
    # BPE subwords often start with ' ' (U+2581) or similar.
    # In Yandex's case, we saw   (NBSP / 0xFF in CP866) and Cyrillic chars!
    
    # Let's search for the pattern: length-prefixed strings or a big block of CP866.
    # Since we found ` ∙шўхъ` (which is CP866 0xFF 0xF9 0xE8 0xE7 0xE5 0xEA), let's look for `\xff\xf9\xe8\xe7\xe5\xea`.
    
    pattern = b'\xff\xf9\xe8\xe7\xe5\xea'
    idx = data.find(pattern)
    if idx != -1:
        print(f"Found pattern at {idx}")
        # It's likely a big buffer containing null-separated or length-prefixed strings.
        # Let's extract the block around it!
        start = max(0, idx - 100)
        end = min(len(data), idx + 1000)
        block = data[start:end]
        print(block)
        
        # Let's try to extract exactly 7000 strings.
        # Usually they are concatenated, and an offset array is stored right before them!
        
        # Let's see if there is an offset array before `start`.
        # TFLite string buffers: [num_strings (int32)] [offset_0 (int32)] ... [offset_N (int32)] [string_0] ...
        
        # We can just extract all strings by splitting on 0x00 if they are null-terminated, or we can look for the offsets.
        pass
    else:
        print("Pattern not found")

extract_tflite_tensors(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\KEYBOARD-13321_nnlm_model")
