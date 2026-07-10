import sys

def decode_from_utf8_to_cp1251():
    # Since tflite_strings.txt was created by printing strings decoded as utf-8,
    # the bytes that were originally CP1251 were treated as utf-8!
    # Wait, the python script `extract_tflite_strings.py` did:
    # current_str.decode('utf-8')
    # If it was CP1251, `decode('utf-8')` would FAIL (UnicodeDecodeError) for most Russian characters!
    # Unless... The strings in TFLite were ALREADY UTF-8?
    # NO! If they were already UTF-8, then `decode('utf-8')` would succeed and print Russian characters!
    # But it printed ` ∙шўхъ`!
    # Why did UTF-8 decoding print ` ∙шўхъ`?
    # Let's encode ` ∙шўхъ` back to UTF-8 to see the bytes.
    pass

def dump_tflite_cp1251():
    model_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\KEYBOARD-13321_nnlm_model"
    with open(model_path, "rb") as f:
        data = f.read()

    # Search for cp1251 strings
    strings = []
    current_str = bytearray()
    
    for b in data:
        # cp1251 valid range: ascii + 0xC0-0xFF, plus some 0xA8 (Ё), 0xB8 (ё)
        if (32 <= b <= 126) or (0xC0 <= b <= 0xFF) or b in (0xA8, 0xB8):
            current_str.append(b)
        else:
            if len(current_str) >= 4:
                try:
                    s = current_str.decode('cp1251')
                    if any('\u0400' <= c <= '\u04FF' for c in s):
                        strings.append(s)
                except UnicodeDecodeError:
                    pass
            current_str = bytearray()

    # Keep exactly 7000 strings if there's a block!
    with open(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\tflite_cp1251_strings.txt", "w", encoding="utf-8") as out:
        out.write(f"Found {len(strings)} Cyrillic strings\n")
        valid_strings = []
        for s in strings:
            if len(s) < 20:
                valid_strings.append(s)
        
        out.write(f"Valid subwords {len(valid_strings)}\n")
        for s in valid_strings[:5000]:
            out.write(s + "\n")
        
dump_tflite_cp1251()
