import os

def find_subword_vocab():
    extracted_dir = r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections"
    if not os.path.exists(extracted_dir):
        print("Directory not found")
        return

    for root, dirs, files in os.walk(extracted_dir):
        for f in files:
            path = os.path.join(root, f)
            with open(path, "rb") as file:
                data = file.read()
                # Check if it has 7000 or anything related
                # Just print sizes
                print(f"{f}: {len(data)} bytes")

find_subword_vocab()