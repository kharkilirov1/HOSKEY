// Test UTF-8 to label mapping
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

// CP866 mapping from Ghidra analysis
uint8_t CharToLabel(char32_t ch) {
    if (ch >= 0x0430 && ch <= 0x044F) {
        // а-я lowercase → 0-31
        return static_cast<uint8_t>(ch - 0x0430);
    } else if (ch >= 0x0410 && ch <= 0x042F) {
        // А-Я uppercase → 0-31 (same as lowercase)
        return static_cast<uint8_t>(ch - 0x0410);
    } else if (ch == 0x0451 || ch == 0x0401) {
        // ё/Ё → 33
        return 33;
    } else if (ch == '-') {
        return 32;
    } else if (ch >= 'a' && ch <= 'z') {
        // Latin lowercase → 64-89
        return 64 + (ch - 'a');
    } else if (ch >= 'A' && ch <= 'Z') {
        return 64 + (ch - 'A');
    }
    return 255; // Invalid
}

// Decode UTF-8 to char32_t
char32_t DecodeUTF8(const char* str, size_t& pos) {
    unsigned char c = str[pos];
    if ((c & 0x80) == 0) {
        pos++;
        return c;
    } else if ((c & 0xE0) == 0xC0 && str[pos+1]) {
        char32_t ch = ((c & 0x1F) << 6) | (str[pos+1] & 0x3F);
        pos += 2;
        return ch;
    } else if ((c & 0xF0) == 0xE0 && str[pos+1] && str[pos+2]) {
        char32_t ch = ((c & 0x0F) << 12) | ((str[pos+1] & 0x3F) << 6) | (str[pos+2] & 0x3F);
        pos += 3;
        return ch;
    }
    pos++;
    return 0;
}

int main() {
    // Test words
    const char* words[] = {
        "а",      // label 0
        "б",      // label 1  
        "в",      // label 2
        "привет", // multiple labels
        "ё",      // label 33
        "-",      // label 32
    };
    
    printf("Label mapping test:\n");
    for (const char* word : words) {
        printf("  '%s' → [", word);
        size_t pos = 0;
        while (pos < strlen(word)) {
            char32_t ch = DecodeUTF8(word, pos);
            if (ch) {
                uint8_t label = CharToLabel(ch);
                printf("%d ", label);
            }
        }
        printf("]\n");
    }
    
    return 0;
}
