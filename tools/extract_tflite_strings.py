import os
import struct

def extract_strings_from_tflite():
    model_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\KEYBOARD-13321_nnlm_model"
    if not os.path.exists(model_path):
        print("Model not found")
        return
        
    with open(model_path, "rb") as f:
        data = f.read()
        
    # Search for printable string sequences > 3 chars
    strings = []
    current_str = bytearray()
    
    for b in data:
        if 32 <= b <= 126 or b > 127: # ASCII printable or UTF-8
            current_str.append(b)
        else:
            if len(current_str) > 3:
                try:
                    s = current_str.decode('utf-8')
                    if any(c.isalpha() for c in s):
                        strings.append(s)
                except UnicodeDecodeError:
                    pass
            current_str = bytearray()
            
    print(f"Found {len(strings)} strings")
    for s in strings[:100]:
        print(s)
        
extract_strings_from_tflite()
