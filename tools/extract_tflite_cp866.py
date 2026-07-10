import os
import re

def extract_cp866_strings_from_tflite():
    model_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\KEYBOARD-13321_nnlm_model"
    if not os.path.exists(model_path):
        print("Model not found")
        return
        
    with open(model_path, "rb") as f:
        data = f.read()
        
    strings = []
    current_str = bytearray()
    
    for b in data:
        if (32 <= b <= 126) or (128 <= b <= 255): 
            current_str.append(b)
        else:
            if len(current_str) >= 2:
                try:
                    s = current_str.decode('cp866')
                    if any('\u0400' <= c <= '\u04FF' for c in s):
                        strings.append(s)
                except UnicodeDecodeError:
                    pass
            current_str = bytearray()
            
    with open(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\tflite_cp866_strings.txt", "w", encoding="utf-8") as out:
        out.write(f"Found {len(strings)} Cyrillic strings\n")
        # only keep likely subwords: max length 20, no weird symbols
        valid_strings = []
        for s in strings:
            if len(s) < 20 and re.match(r'^[\u0400-\u04FFa-zA-Z\-_0-9]+$', s):
                valid_strings.append(s)
        
        out.write(f"Valid subwords {len(valid_strings)}\n")
        for s in valid_strings:
            out.write(s + "\n")
        
extract_cp866_strings_from_tflite()
