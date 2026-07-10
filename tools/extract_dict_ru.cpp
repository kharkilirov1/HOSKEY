// Extract words from dict_ru_full
// Format: container magic 0xFE3AC19B, DFS interleaved trie inside

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <fstream>

static constexpr uint32_t kContainerMagic = 0xFE3AC19B;

// DFS markers
static constexpr uint8_t kNodeMarker = 0x40;  // ')' in CP866 - start node
static constexpr uint8_t kTermMarker = 0x48;  // 'H' - terminal
static constexpr uint8_t kLeafMarker = 0x50;  // 'P' - leaf
static constexpr uint8_t kXtndMarker = 0x58;  // 'X' - extended
static constexpr uint8_t kSpaceMarker = 0x20; // ' ' - space separator

struct DfsParser {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    
    std::string current_path;
    std::vector<std::string> words;
    
    void Parse() {
        // Skip container header, find DFS blob
        // Based on analysis, DFS starts after header at ~0x2720
        pos = 0x2720;
        
        while (pos < size) {
            uint8_t marker = data[pos];
            
            switch (marker) {
                case kNodeMarker:
                    pos++;
                    ParseNode();
                    break;
                case kTermMarker:
                    pos++;
                    // End of current word
                    if (!current_path.empty()) {
                        words.push_back(current_path);
                    }
                    // Pop back to parent
                    PopToParent();
                    break;
                case kLeafMarker:
                    pos++;
                    // Terminal with text
                    ParseText();
                    if (!current_path.empty()) {
                        words.push_back(current_path);
                    }
                    PopToParent();
                    break;
                case kXtndMarker:
                    pos++;
                    // Extended marker - skip or handle
                    break;
                case kSpaceMarker:
                    pos++;
                    // Just a separator
                    break;
                default:
                    // Unexpected byte, try to recover
                    pos++;
                    break;
            }
        }
    }
    
    void ParseNode() {
        // Read UTF-8 text until next marker
        std::string text = ReadText();
        current_path += text;
    }
    
    void ParseText() {
        std::string text = ReadText();
        current_path += text;
    }
    
    std::string ReadText() {
        size_t start = pos;
        while (pos < size) {
            uint8_t b = data[pos];
            if (b == kNodeMarker || b == kTermMarker || 
                b == kLeafMarker || b == kXtndMarker || b == kSpaceMarker) {
                break;
            }
            pos++;
        }
        
        if (pos > start) {
            // Validate UTF-8
            std::string result;
            result.reserve(pos - start);
            for (size_t i = start; i < pos; i++) {
                result.push_back(static_cast<char>(data[i]));
            }
            return result;
        }
        return "";
    }
    
    void PopToParent() {
        // Find last node boundary - simplistic
        // In real DFS, we'd track a stack
        if (!current_path.empty()) {
            // Pop last UTF-8 character
            size_t i = current_path.size();
            while (i > 0 && (current_path[i-1] & 0xC0) == 0x80) {
                i--;
            }
            if (i > 0) {
                current_path.resize(i - 1);
            } else {
                current_path.clear();
            }
        }
    }
};

int main(int argc, char** argv) {
    const char* input_path = R"(C:\Users\Kharki\Desktop\yandex_extracted\dict_ru_full)";
    const char* output_path = R"(C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\words.txt)";
    
    // Read file
    FILE* f = fopen(input_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open input file\n");
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::vector<uint8_t> data(size);
    fread(data.data(), 1, size, f);
    fclose(f);
    
    // Check magic
    uint32_t magic = *reinterpret_cast<uint32_t*>(data.data());
    printf("Magic: 0x%08X (expected 0x%08X)\n", magic, kContainerMagic);
    
    // Parse
    DfsParser parser;
    parser.data = data.data();
    parser.size = data.size();
    parser.Parse();
    
    printf("Found %zu words\n", parser.words.size());
    
    // Deduplicate and filter
    std::set<std::string> unique_words;
    for (const auto& w : parser.words) {
        if (w.size() >= 2 && w.size() <= 32) {
            unique_words.insert(w);
        }
    }
    
    printf("Unique valid words: %zu\n", unique_words.size());
    
    // Write output
    std::ofstream out(output_path);
    for (const auto& w : unique_words) {
        out << w << "\n";
    }
    
    printf("Written to %s\n", output_path);
    return 0;
}
