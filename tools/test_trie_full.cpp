// Standalone test for full SuccinctTrie implementation
// Build: g++ -std=c++17 -O2 -I../entry/src/main/cpp -o test_trie_full test_trie_full.cpp

#include <cstdio>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <cstring>
#include <limits>
#include <algorithm>

// Minimal Status implementation
struct Status {
    int code;
    const char* msg;
    static Status Ok() { return {0, ""}; }
    static Status ParseError(const char* m) { return {1, m}; }
    bool ok() const { return code == 0; }
};

// Full RankedBitvector implementation
struct RankedBitvector {
    uint64_t num_words = 0;
    uint64_t popcount = 0;
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
        uint64_t sb_lo = 0;
        uint64_t sb_hi = num_superblocks;
        
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
    
    uint64_t Select0(uint64_t n) const {
        uint64_t total_zeros = num_words * 64 - popcount;
        if (!bits || n >= total_zeros) {
            return std::numeric_limits<uint64_t>::max();
        }
        
        uint64_t count = 0;
        for (uint64_t pos = 0; pos < num_words * 64; ++pos) {
            if (!GetBit(pos)) {
                if (count == n) return pos;
                ++count;
            }
        }
        return std::numeric_limits<uint64_t>::max();
    }
};

struct TrieLevel {
    RankedBitvector louds;
    RankedBitvector has_child;
    const uint8_t* labels = nullptr;
    size_t labels_count = 0;
    
    uint8_t GetLabel(uint64_t node_id) const {
        if (!labels || node_id >= labels_count) return 0;
        return labels[node_id];  // labels[0] for node 0
    }
    
    bool HasChildren(uint64_t node_id) const {
        return has_child.GetBit(node_id);
    }
    
    std::optional<uint64_t> FirstChild(uint64_t node_id) const {
        if (!HasChildren(node_id)) return std::nullopt;
        
        // Alternative formula: use Select1 to find node's position, then look for children
        uint64_t node_pos = louds.Select1(node_id);
        if (node_pos == std::numeric_limits<uint64_t>::max()) return std::nullopt;
        
        // Children start after this node's position
        uint64_t child_pos = node_pos + 1;
        if (child_pos >= louds.num_words * 64) return std::nullopt;
        
        // If bit at child_pos is 1, it's a child
        if (!louds.GetBit(child_pos)) return std::nullopt;
        
        uint64_t child_id = louds.Rank1(child_pos);
        if (child_id >= labels_count) return std::nullopt;
        
        return child_id;
    }
};

struct Trie1nc7Header {
    uint32_t magic;
    uint32_t version;
    uint32_t payload_hint;
    uint32_t reserved;
    uint32_t item_count;
};

class SuccinctTrie {
public:
    Status Parse(const uint8_t* data, size_t size) {
        if (size < 24) return Status::ParseError("File too small");
        
        data_ = data;
        data_size_ = size;
        
        std::memcpy(&header_, data, sizeof(header_));
        if (header_.magic != 0x37636E31) {
            return Status::ParseError("Invalid magic");
        }
        
        auto st = ParseBitvectors();
        if (!st.ok()) return st;
        
        st = ParseLabels();
        if (!st.ok()) return st;
        
        loaded_ = true;
        return Status::Ok();
    }
    
    std::optional<uint64_t> FindPrefixNode(const std::string& utf8_str) const {
        if (!loaded_) return std::nullopt;
        
        // Navigate sub trie
        uint64_t node = 0;
        for (char c : utf8_str) {
            uint8_t target_label = static_cast<uint8_t>(c);
            
            printf("  Looking for char '%c' (label=%d) from node %llu\n", c, target_label, node);
            
            auto child_opt = sub_level_.FirstChild(node);
            bool found = false;
            int checked = 0;
            
            while (child_opt && checked < 10) {
                uint64_t child_id = *child_opt;
                uint8_t child_label = sub_level_.GetLabel(child_id);
                
                if (checked < 5) {
                    printf("    Child %llu has label %d\n", child_id, child_label);
                }
                
                if (child_label == target_label) {
                    node = child_id;
                    found = true;
                    printf("    -> Found! New node=%llu\n", node);
                    break;
                }
                
                // Get next sibling via LOUDS
                uint64_t child_pos = sub_level_.louds.Select1(child_id) + 1;
                if (!sub_level_.louds.GetBit(child_pos)) {
                    printf("    -> No more siblings (LOUDS)\n");
                    break;
                }
                child_id = sub_level_.louds.Rank1(child_pos);
                child_opt = child_id;
                checked++;
            }
            
            if (!found) {
                printf("  -> NOT FOUND\n");
                return std::nullopt;
            }
        }
        
        return node;
    }
    
    bool IsValidPrefix(const std::string& utf8_str) const {
        return FindPrefixNode(utf8_str).has_value();
    }
    
    bool loaded() const { return loaded_; }
    size_t token_count() const { return sub_level_.labels_count; }
    
private:
    bool loaded_ = false;
    Trie1nc7Header header_{};
    TrieLevel sub_level_;
    const uint8_t* data_ = nullptr;
    size_t data_size_ = 0;
    
    Status ParseBitvectors() {
        const size_t bits_section_size = 124992;
        
        std::vector<size_t> bv_offsets;
        const uint8_t* scan = data_ + 24;
        const uint8_t* end = data_ + data_size_;
        
        while (scan + 32 <= end && bv_offsets.size() < 8) {
            uint64_t v0 = 0, v1 = 0;
            std::memcpy(&v0, scan, 8);
            std::memcpy(&v1, scan + 8, 8);
            if (v0 == header_.item_count && v1 == 3904) {
                bv_offsets.push_back(scan - data_);
                scan += 32;
            } else {
                scan += 8;
            }
        }
        
        if (bv_offsets.size() < 4) {
            return Status::ParseError("Need 4 bitvectors");
        }
        
        auto parse_bv = [&](size_t offset) -> RankedBitvector {
            RankedBitvector bv;
            bv.num_words = *(uint64_t*)(data_ + offset);
            bv.popcount = *(uint64_t*)(data_ + offset + 16);
            bv.rank_data = data_ + offset + 24;
            
            size_t bits_offset = offset + 24 - bits_section_size;
            if (bits_offset < data_size_) {
                bv.bits = (const uint64_t*)(data_ + bits_offset);
            }
            return bv;
        };
        
        sub_level_.louds = parse_bv(bv_offsets[2]);
        sub_level_.has_child = parse_bv(bv_offsets[3]);
        
        return Status::Ok();
    }
    
    Status ParseLabels() {
        size_t bv3_offset = 605128;
        uint64_t bv3_num_words = *(uint64_t*)(data_ + bv3_offset);
        size_t rank_size = ((bv3_num_words + 7) / 8) * 16;
        size_t after_bv3 = bv3_offset + 24 + rank_size;
        
        sub_level_.labels = data_ + after_bv3 + 40;
        sub_level_.labels_count = header_.payload_hint - (after_bv3 + 40);
        
        // Debug: print first 20 labels
        printf("First 20 labels:\n");
        for (int i = 0; i < 20 && i < sub_level_.labels_count; i++) {
            printf("  [%d]: %d (0x%02X)\n", i, sub_level_.labels[i], sub_level_.labels[i]);
        }
        
        // Debug: print LOUDS bits
        printf("\nLOUDS first 32 bits: ");
        for (int i = 0; i < 32; i++) {
            printf("%d", (int)sub_level_.louds.GetBit(i));
        }
        printf("\n");
        
        // Debug: print has_child first 32 bits
        printf("has_child first 32 bits: ");
        for (int i = 0; i < 32; i++) {
            printf("%d", (int)sub_level_.has_child.GetBit(i));
        }
        printf("\n");
        
        // Debug: check has_child meaning
        printf("\nhas_child analysis:\n");
        for (int i = 0; i < 10; i++) {
            printf("  has_child[%d]=%d -> node %d has children?\n", 
                   i, sub_level_.has_child.GetBit(i), i+1);
        }
        
        // Debug: print root children
        printf("\nRoot (node 0) children:\n");
        auto child = sub_level_.FirstChild(0);
        int count = 0;
        while (child && count < 20) {
            uint64_t cid = *child;
            uint8_t lbl = sub_level_.GetLabel(cid);
            printf("  Child node %llu: label=%d (0x%02X) has_child[%llu]=%d\n", 
                   cid, lbl, lbl, cid, sub_level_.has_child.GetBit(cid));
            
            // If this child has children, print them
            if (sub_level_.has_child.GetBit(cid) && count < 3) {
                printf("    -> Its children:\n");
                auto grandchild = sub_level_.FirstChild(cid);
                int gc_count = 0;
                while (grandchild && gc_count < 5) {
                    uint64_t gcid = *grandchild;
                    printf("      Grandchild %llu: label=%d\n", gcid, sub_level_.GetLabel(gcid));
                    // Next grandchild
                    uint64_t gc_pos = sub_level_.louds.Select1(gcid) + 1;
                    if (!sub_level_.louds.GetBit(gc_pos)) break;
                    gcid = sub_level_.louds.Rank1(gc_pos);
                    grandchild = gcid;
                    gc_count++;
                }
            }
            
            // Get next sibling
            uint64_t child_pos = sub_level_.louds.Select1(cid) + 1;
            if (!sub_level_.louds.GetBit(child_pos)) break;
            cid = sub_level_.louds.Rank1(child_pos);
            child = cid;
            count++;
        }
        
        return Status::Ok();
    }
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
    Status st = trie.Parse(data.data(), size);
    
    printf("Parse: %s\n", st.ok() ? "OK" : st.msg);
    printf("Tokens: %zu\n", trie.token_count());
    
    // Test prefix validation - try single bytes first
    std::vector<std::string> test_prefixes;
    test_prefixes.push_back(std::string(1, (char)0));   // label 0
    test_prefixes.push_back(std::string(1, (char)1));   // label 1
    test_prefixes.push_back(std::string(1, (char)2));   // label 2
    test_prefixes.push_back("a");  // label 97
    test_prefixes.push_back("п");  // UTF-8
    
    printf("\nPrefix validation:\n");
    for (const auto& prefix : test_prefixes) {
        bool valid = trie.IsValidPrefix(prefix);
        printf("  '%s': %s\n", prefix.c_str(), valid ? "VALID" : "INVALID");
    }
    
    return 0;
}
