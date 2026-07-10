// Complete test for SuccinctTrie
// Build: g++ -std=c++17 -I../entry/src/main/cpp -o test_trie_complete test_trie_complete.cpp

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <optional>
#include <limits>

// Minimal implementation for testing
struct RankedBitvector {
    uint64_t num_words = 0, popcount = 0;
    const uint8_t* rank_data = nullptr;
    const uint64_t* bits = nullptr;
    
    uint64_t GetBit(uint64_t pos) const {
        if (!bits || pos >= num_words * 64) return 0;
        return (bits[pos / 64] >> (pos % 64)) & 1ULL;
    }
    
    uint64_t Rank1(uint64_t pos) const {
        if (!bits || !rank_data || pos == 0) return 0;
        if (pos >= num_words * 64) return popcount;
        
        uint64_t word_idx = pos / 64;
        uint64_t sb_idx = word_idx / 8;
        uint64_t word_in_sb = word_idx % 8;
        
        const uint8_t* sb_base = rank_data + sb_idx * 16;
        
        uint64_t l1 = 0;
        std::memcpy(&l1, sb_base, 8);
        
        uint64_t l2_sum = 0;
        if (word_in_sb > 0) {
            uint64_t packed_l2 = 0;
            std::memcpy(&packed_l2, sb_base + 8, 8);
            l2_sum = (packed_l2 >> ((word_in_sb - 1) * 9)) & 0x1FFULL;
        }
        
        uint64_t bit_in_word = pos % 64;
        uint64_t mask = (bit_in_word == 0) ? 0ULL : ((1ULL << bit_in_word) - 1);
        uint64_t partial = __builtin_popcountll(bits[word_idx] & mask);
        
        return l1 + l2_sum + partial;
    }
    
    uint64_t Select1(uint64_t n) const {
        if (!bits || !rank_data || n >= popcount) {
            return std::numeric_limits<uint64_t>::max();
        }
        
        uint64_t num_superblocks = (num_words + 7) / 8;
        uint64_t sb_lo = 0, sb_hi = num_superblocks;
        
        while (sb_lo + 1 < sb_hi) {
            uint64_t mid = sb_lo + (sb_hi - sb_lo) / 2;
            uint64_t l1_mid = 0;
            std::memcpy(&l1_mid, rank_data + mid * 16, 8);
            if (l1_mid <= n) sb_lo = mid;
            else sb_hi = mid;
        }
        
        uint64_t l1 = 0;
        std::memcpy(&l1, rank_data + sb_lo * 16, 8);
        uint64_t remaining = n - l1;
        
        uint64_t base_word = sb_lo * 8;
        uint64_t words_in_sb = std::min<uint64_t>(8, num_words - base_word);
        
        for (uint64_t w = 0; w < words_in_sb; ++w) {
            uint64_t word = bits[base_word + w];
            uint64_t wpc = __builtin_popcountll(word);
            
            if (wpc > remaining) {
                uint64_t r = remaining;
                for (uint64_t bit = 0; bit < 64; ++bit) {
                    if ((word >> bit) & 1ULL) {
                        if (r == 0) return (base_word + w) * 64 + bit;
                        --r;
                    }
                }
            }
            remaining -= wpc;
        }
        return std::numeric_limits<uint64_t>::max();
    }
};

struct TrieLevel {
    RankedBitvector louds;
    const uint8_t* labels = nullptr;
    size_t labels_count = 0;
    
    uint8_t GetLabel(uint64_t node_id) const {
        if (!labels || node_id >= labels_count) return 0;
        return labels[node_id];
    }
    
    std::optional<uint64_t> FirstChild(uint64_t node_id) const {
        uint64_t node_pos = louds.Select1(node_id);
        if (node_pos == std::numeric_limits<uint64_t>::max()) return std::nullopt;
        
        uint64_t child_pos = node_pos + 1;
        if (child_pos >= louds.num_words * 64) return std::nullopt;
        if (!louds.GetBit(child_pos)) return std::nullopt;
        
        uint64_t child_id = louds.Rank1(child_pos);
        if (child_id >= labels_count) return std::nullopt;
        return child_id;
    }
    
    std::optional<uint64_t> NextSibling(uint64_t node_id) const {
        uint64_t node_pos = louds.Select1(node_id);
        if (node_pos == std::numeric_limits<uint64_t>::max()) return std::nullopt;
        
        uint64_t next_pos = node_pos + 1;
        if (next_pos >= louds.num_words * 64) return std::nullopt;
        if (!louds.GetBit(next_pos)) return std::nullopt;
        
        uint64_t sibling_id = louds.Rank1(next_pos);
        if (sibling_id >= labels_count) return std::nullopt;
        return sibling_id;
    }
};

static char32_t DecodeUTF8(const std::string& str, size_t& pos) {
    if (pos >= str.size()) return 0;
    unsigned char c = str[pos];
    if ((c & 0x80) == 0) { pos++; return c; }
    if ((c & 0xE0) == 0xC0 && pos + 1 < str.size()) {
        char32_t ch = ((c & 0x1F) << 6) | (str[pos + 1] & 0x3F);
        pos += 2; return ch;
    }
    if ((c & 0xF0) == 0xE0 && pos + 2 < str.size()) {
        char32_t ch = ((c & 0x0F) << 12) | ((str[pos + 1] & 0x3F) << 6) | (str[pos + 2] & 0x3F);
        pos += 3; return ch;
    }
    pos++; return 0;
}

static uint8_t CharToLabel(char32_t ch) {
    if (ch >= 0x0430 && ch <= 0x044F) return static_cast<uint8_t>(ch - 0x0430);
    if (ch >= 0x0410 && ch <= 0x042F) return static_cast<uint8_t>(ch - 0x0410);
    if (ch == 0x0451 || ch == 0x0401) return 33;
    if (ch == '-') return 32;
    return 255;
}

class SuccinctTrie {
public:
    bool Load(const uint8_t* data, size_t size) {
        const size_t bits_section_size = 124992;
        
        // Parse BV2 (sub louds)
        size_t bv2_offset = 448936;
        sub_.louds.num_words = *(uint64_t*)(data + bv2_offset);
        sub_.louds.popcount = *(uint64_t*)(data + bv2_offset + 16);
        sub_.louds.rank_data = data + bv2_offset + 24;
        sub_.louds.bits = (const uint64_t*)(data + bv2_offset + 24 - bits_section_size);
        
        // Parse labels
        size_t bv3_offset = 605128;
        uint64_t bv3_num_words = *(uint64_t*)(data + bv3_offset);
        size_t rank_size = ((bv3_num_words + 7) / 8) * 16;
        size_t after_bv3 = bv3_offset + 24 + rank_size;
        
        uint32_t payload_hint = *(uint32_t*)(data + 8);
        sub_.labels = data + after_bv3 + 40;
        sub_.labels_count = payload_hint - (after_bv3 + 40);
        
        return true;
    }
    
    std::optional<uint64_t> FindPrefix(const std::string& utf8_str) {
        uint64_t node = 0;
        size_t pos = 0;
        
        while (pos < utf8_str.size()) {
            char32_t ch = DecodeUTF8(utf8_str, pos);
            if (ch == 0) continue;
            
            uint8_t target = CharToLabel(ch);
            if (target == 255) return std::nullopt;
            
            auto child = sub_.FirstChild(node);
            bool found = false;
            
            while (child) {
                if (sub_.GetLabel(*child) == target) {
                    node = *child;
                    found = true;
                    break;
                }
                child = sub_.NextSibling(*child);
            }
            
            if (!found) return std::nullopt;
        }
        return node;
    }
    
    bool IsValidPrefix(const std::string& s) {
        return FindPrefix(s).has_value();
    }
    
private:
    TrieLevel sub_;
};

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections/trie";
    
    FILE* f = fopen(path, "rb");
    if (!f) { printf("Failed to open\n"); return 1; }
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::vector<uint8_t> data(size);
    fread(data.data(), 1, size, f);
    fclose(f);
    
    SuccinctTrie trie;
    if (!trie.Load(data.data(), size)) {
        printf("Failed to load\n");
        return 1;
    }
    
    printf("Testing prefix validation:\n\n");
    
    // Test single letters (labels 0-5 should exist)
    const char* letters[] = {"а", "б", "в", "г", "д", "е", "ё", "ж", "з", "и"};
    int valid_count = 0;
    for (const char* letter : letters) {
        bool valid = trie.IsValidPrefix(letter);
        if (valid) valid_count++;
        printf("  '%s': %s\n", letter, valid ? "VALID" : "INVALID");
    }
    printf("\nValid single letters: %d/%zu\n\n", valid_count, sizeof(letters)/sizeof(letters[0]));
    
    // Test multi-letter prefixes
    const char* words[] = {"при", "прив", "привет", "абв", "xyz", "---"};
    for (const char* word : words) {
        bool valid = trie.IsValidPrefix(word);
        printf("  '%s': %s\n", word, valid ? "VALID" : "INVALID");
    }
    
    return 0;
}
