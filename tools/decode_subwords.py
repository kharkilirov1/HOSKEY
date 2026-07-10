import sys

def decode_from_utf8_to_cp866():
    with open(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\tflite_strings.txt", "r", encoding="utf-8") as f:
        lines = f.readlines()
        
    for line in lines[10:100]:
        line = line.strip()
        # The line was read as UTF-8, but it was originally printed from bytes decoded with errors='replace'.
        # Actually my previous script did: s = current_str.decode('utf-8')
        # Let's write a python script to search the TFLite model specifically for the 7000 strings block!
        pass

def find_subword_array():
    model_path = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\KEYBOARD-13321_nnlm_model"
    with open(model_path, "rb") as f:
        data = f.read()
        
    # Search for "шчек" or something similar in cp866.
    # Let's search for "ских" -> 0xF1 0xEA 0xE8 0xF5
    # Let's search for the pattern of length-prefixed strings.
    
    # Or just use the tflite python library!
    pass

decode_from_utf8_to_cp866()
