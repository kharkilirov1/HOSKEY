/**
 * Yandex LOUDS Trie Reader Implementation
 *
 * Based on reverse-engineering of libjni_ykeyboard3.so.
 * Uses LOUDS (Level-Order Unary Degree Sequence) format.
 */

#include "comptrie_reader.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <hilog/log.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "HOSKEY-LOUDS"

namespace yandex {

//==============================================================================
// Constructor / Destructor
//==============================================================================

CompTrieReader::CompTrieReader() {
    initAlphabet();
}

CompTrieReader::~CompTrieReader() {
    unload();
}

//==============================================================================
// Alphabet Mapping
//==============================================================================

void CompTrieReader::initAlphabet() {
    // Initialize all to empty
    for (int i = 0; i < 64; i++) {
        alphabet_[i] = "";
    }

    // Zone 1: Frequency-sorted characters (indices 0-31)
    // Based on analysis of dictionary structure
    // Known mappings from reverse engineering:
    alphabet_[0x00] = "";      // Reserved/null
    alphabet_[0x01] = " ";     // Space (word separator in n-grams)
    alphabet_[0x02] = "а";     // Most frequent vowel
    alphabet_[0x03] = "о";     // Second most frequent
    alphabet_[0x04] = "е";     // Third
    alphabet_[0x05] = "и";     // Fourth (was 'н' in earlier analysis, corrected)
    alphabet_[0x06] = "н";     // Fifth
    alphabet_[0x07] = "т";     // Sixth
    alphabet_[0x08] = "с";     // Seventh
    alphabet_[0x09] = "р";     // Eighth (was 'е' earlier)
    alphabet_[0x0A] = "в";     // Ninth
    alphabet_[0x0B] = "л";     // Tenth
    alphabet_[0x0C] = "к";
    alphabet_[0x0D] = "м";
    alphabet_[0x0E] = "д";
    alphabet_[0x0F] = "п";
    alphabet_[0x10] = "у";
    alphabet_[0x11] = "я";
    alphabet_[0x12] = "ы";
    alphabet_[0x13] = "ь";
    alphabet_[0x14] = "г";
    alphabet_[0x15] = "з";
    alphabet_[0x16] = "б";
    alphabet_[0x17] = "ч";
    alphabet_[0x18] = "й";
    alphabet_[0x19] = "х";
    alphabet_[0x1A] = "ж";
    alphabet_[0x1B] = "ш";
    alphabet_[0x1C] = "ю";
    alphabet_[0x1D] = "ц";
    alphabet_[0x1E] = "щ";
    alphabet_[0x1F] = "э";

    // Zone 2: Linear alphabet (indices 32-63)
    // Char = 'а' + (index - 32)
    // Russian lowercase: а=0x430, б=0x431, ..., я=0x44F (32 letters)
    const char* russianAlphabet[] = {
        "а", "б", "в", "г", "д", "е", "ж", "з",
        "и", "й", "к", "л", "м", "н", "о", "п",
        "р", "с", "т", "у", "ф", "х", "ц", "ч",
        "ш", "щ", "ъ", "ы", "ь", "э", "ю", "я"
    };

    for (int i = 0; i < 32; i++) {
        alphabet_[32 + i] = russianAlphabet[i];
    }

    OH_LOG_DEBUG(LOG_APP, "LOUDS: Alphabet initialized (64 entries)");
}

std::string CompTrieReader::decodeLabel(uint8_t labelByte) const {
    uint8_t index = labelByte & LABEL_INDEX_MASK;
    if (index < 64) {
        return alphabet_[index];
    }
    return "";
}

int CompTrieReader::encodeChar(const std::string& utf8Char) const {
    for (int i = 0; i < 64; i++) {
        if (alphabet_[i] == utf8Char) {
            return i;
        }
    }
    return -1;
}

//==============================================================================
// Load / Unload
//==============================================================================

bool CompTrieReader::load(const std::string& path) {
    unload();

    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: Failed to open file: %{public}s", path.c_str());
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return false;
    }
    fileSize_ = st.st_size;

    void* mapped = mmap(nullptr, fileSize_, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: mmap failed");
        return false;
    }

    mapHandle_ = mapped;
    mapLength_ = fileSize_;
    data_ = static_cast<const uint8_t*>(mapped);

    if (!parseStructure()) {
        unload();
        return false;
    }

    OH_LOG_INFO(LOG_APP, "LOUDS: Loaded successfully, %{public}zu nodes", nodeCount_);
    return true;
}

bool CompTrieReader::loadFromFd(int fd, size_t offset, size_t length) {
    unload();

    if (fd < 0 || length == 0) {
        return false;
    }

    fileSize_ = length;

    // mmap offset must be page-aligned
    long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pageSize <= 0) pageSize = 4096;

    size_t alignedOffset = offset - (offset % static_cast<size_t>(pageSize));
    size_t delta = offset - alignedOffset;
    size_t mapLength = length + delta;

    void* mapped = mmap(nullptr, mapLength, PROT_READ, MAP_PRIVATE, fd, alignedOffset);
    if (mapped == MAP_FAILED) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: mmap from fd failed");
        return false;
    }

    mapHandle_ = mapped;
    mapLength_ = mapLength;
    data_ = static_cast<const uint8_t*>(mapped) + delta;

    madvise(mapped, mapLength, MADV_SEQUENTIAL);

    if (!parseStructure()) {
        unload();
        return false;
    }

    OH_LOG_INFO(LOG_APP, "LOUDS: Loaded from fd, %{public}zu nodes", nodeCount_);
    return true;
}

void CompTrieReader::unload() {
    if (mapHandle_) {
        munmap(mapHandle_, mapLength_ > 0 ? mapLength_ : fileSize_);
        mapHandle_ = nullptr;
    }
    data_ = nullptr;
    header_ = nullptr;
    louds_ = nullptr;
    labels_ = nullptr;
    fileSize_ = 0;
    mapLength_ = 0;
    loudsSize_ = 0;
    nodeCount_ = 0;
    wordCount_ = 0;
    wordCountCached_ = false;
}

//==============================================================================
// Structure Parsing
//==============================================================================

bool CompTrieReader::parseStructure() {
    if (!data_ || fileSize_ < 64) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: File too small");
        return false;
    }

    // Check global magic
    uint32_t globalMagic = *reinterpret_cast<const uint32_t*>(data_);
    if (globalMagic != YANDEX_MAGIC) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: Invalid global magic: 0x%{public}08X", globalMagic);
        return false;
    }

    // Find JSON config end (starts at offset 32)
    size_t jsonStart = 32;
    int depth = 0;
    size_t jsonEnd = jsonStart;

    for (size_t i = jsonStart; i < fileSize_ && i < jsonStart + 20000; i++) {
        if (data_[i] == '{') depth++;
        else if (data_[i] == '}') {
            depth--;
            if (depth == 0) {
                jsonEnd = i + 1;
                break;
            }
        }
    }

    OH_LOG_DEBUG(LOG_APP, "LOUDS: JSON config ends at offset %{public}zu", jsonEnd);

    // Search for trie magic "1nc7" after JSON
    // The file is a container with multiple trie entries:
    //   - Small trie @ ~384,496 (4.5 MB) - quick suggestions
    //   - Large trie (KEYBOARD-13421_trie) @ ~117,496,192 (11 MB) - main dictionary
    // We want to find the LARGEST trie for best results

    const uint8_t trieMagic[] = {'1', 'n', 'c', '7'};
    size_t trieOffset = 0;
    size_t bestTrieOffset = 0;
    uint64_t bestNodeCount = 0;

    // Search entire file for all '1nc7' occurrences
    size_t searchStart = jsonEnd;

    OH_LOG_DEBUG(LOG_APP, "LOUDS: Searching for '1nc7' magic signatures...");

    for (size_t i = searchStart; i < fileSize_ - 24; i++) {
        if (memcmp(data_ + i, trieMagic, 4) == 0) {
            // Found a trie header, check its size
            const LOUDSHeader* hdr = reinterpret_cast<const LOUDSHeader*>(data_ + i);

            // Validate this looks like a real header
            if (hdr->version <= 10 && hdr->node_count > 0 && hdr->node_count < 100000000) {
                OH_LOG_DEBUG(LOG_APP, "LOUDS: Found '1nc7' @ 0x%{public}zX, nodes=%{public}llu, chunks=%{public}llu",
                             i, (unsigned long long)hdr->node_count, (unsigned long long)hdr->louds_chunks);

                // Track the largest trie
                if (hdr->node_count > bestNodeCount) {
                    bestNodeCount = hdr->node_count;
                    bestTrieOffset = i;
                }

                // Also keep first valid trie as fallback
                if (trieOffset == 0) {
                    trieOffset = i;
                }
            }
        }
    }

    // Use the largest trie found
    if (bestTrieOffset != 0) {
        trieOffset = bestTrieOffset;
        OH_LOG_INFO(LOG_APP, "LOUDS: Selected largest trie @ 0x%{public}zX with %{public}llu nodes",
                    trieOffset, (unsigned long long)bestNodeCount);
    }

    if (trieOffset == 0) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: Trie magic '1nc7' not found");
        return false;
    }

    // Parse LOUDS header
    if (trieOffset + sizeof(LOUDSHeader) > fileSize_) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: Header extends beyond file");
        return false;
    }

    header_ = reinterpret_cast<const LOUDSHeader*>(data_ + trieOffset);

    // Validate header
    if (memcmp(header_->magic, "1nc7", 4) != 0) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: Invalid trie magic in header");
        return false;
    }

    nodeCount_ = header_->node_count;
    size_t loudsChunks = header_->louds_chunks;

    OH_LOG_DEBUG(LOG_APP, "LOUDS: Header - version=%{public}u, nodes=%{public}zu, chunks=%{public}zu",
                 header_->version, nodeCount_, loudsChunks);

    // Validate reasonable values
    if (nodeCount_ == 0 || nodeCount_ > 100000000) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: Invalid node_count: %{public}zu", nodeCount_);
        return false;
    }

    if (loudsChunks == 0 || loudsChunks > 10000000) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: Invalid louds_chunks: %{public}zu", loudsChunks);
        return false;
    }

    // === CRITICAL: Correct memory layout ===
    // Structure inside '1nc7' section:
    //   [Header 24 bytes]
    //   [Labels array] @ offset 24, size = node_count bytes
    //   [LOUDS bitvector] @ offset 24 + node_count
    //
    // We were reading LOUDS first, but Labels come first!

    // 1. Labels start immediately after header (offset 24)
    size_t labelsOffset = trieOffset + sizeof(LOUDSHeader);  // +24

    if (labelsOffset + nodeCount_ > fileSize_) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: Labels array extends beyond file");
        return false;
    }

    labels_ = data_ + labelsOffset;

    OH_LOG_DEBUG(LOG_APP, "LOUDS: Labels @ 0x%{public}zX, size=%{public}zu bytes",
                 labelsOffset, nodeCount_);

    // 2. LOUDS bitvector starts after Labels
    size_t loudsOffset = labelsOffset + nodeCount_;

    // Check alignment for uint64_t access (ARM requires 8-byte alignment)
    if (loudsOffset % 8 != 0) {
        OH_LOG_WARN(LOG_APP, "LOUDS: Bitvector not 8-byte aligned at 0x%{public}zX", loudsOffset);
        // For now continue, but may need memcpy to aligned buffer on some devices
    }

    // louds_chunks from header could be:
    // - Number of 16-byte chunks (128 bits each)
    // - Or size in bytes directly
    // From JSON analysis: louds_chunks appears to be size in bytes or words
    // Let's try interpreting it as size in bytes first
    size_t loudsBytes = loudsChunks;  // Try as direct byte count first

    // Sanity check: if too small, maybe it's chunks
    if (loudsBytes < 1000 && nodeCount_ > 10000) {
        // Probably chunks, not bytes
        loudsBytes = loudsChunks * 16;
        OH_LOG_DEBUG(LOG_APP, "LOUDS: Interpreting louds_chunks as 16-byte chunks: %{public}zu bytes", loudsBytes);
    } else {
        OH_LOG_DEBUG(LOG_APP, "LOUDS: Interpreting louds_chunks as byte count: %{public}zu bytes", loudsBytes);
    }

    if (loudsOffset + loudsBytes > fileSize_) {
        OH_LOG_ERROR(LOG_APP, "LOUDS: LOUDS bitvector extends beyond file");
        return false;
    }

    louds_ = reinterpret_cast<const uint64_t*>(data_ + loudsOffset);
    loudsSize_ = loudsBytes * 8;  // Size in bits

    OH_LOG_DEBUG(LOG_APP, "LOUDS: Structure parsed - Labels @ 0x%{public}zX, LOUDS @ 0x%{public}zX (%{public}zu bits)",
                 labelsOffset, loudsOffset, loudsSize_);

    // Log first few labels for debugging (decode them too)
    OH_LOG_DEBUG(LOG_APP, "LOUDS: First 10 label bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 labels_[0], labels_[1], labels_[2], labels_[3], labels_[4],
                 labels_[5], labels_[6], labels_[7], labels_[8], labels_[9]);

    // Decode first 10 labels to characters
    for (int i = 0; i < 10 && i < (int)nodeCount_; i++) {
        uint8_t labelByte = labels_[i];
        uint8_t index = labelByte & LABEL_INDEX_MASK;
        bool isTerminal = (labelByte & LABEL_TERMINAL) != 0;
        std::string ch = decodeLabel(labelByte);
        OH_LOG_DEBUG(LOG_APP, "LOUDS: Label[%{public}d] = 0x%02X -> index=%{public}d, char='%{public}s', terminal=%{public}s",
                     i, labelByte, index, ch.c_str(), isTerminal ? "yes" : "no");
    }

    // Log first few LOUDS words for debugging
    OH_LOG_DEBUG(LOG_APP, "LOUDS: First 4 LOUDS words: %016llX %016llX %016llX %016llX",
                 (unsigned long long)louds_[0], (unsigned long long)louds_[1],
                 (unsigned long long)louds_[2], (unsigned long long)louds_[3]);

    return true;
}

//==============================================================================
// LOUDS Bit Operations
//==============================================================================

size_t CompTrieReader::rank1(size_t bitIdx) const {
    if (bitIdx == 0) return 0;

    size_t wordIdx = bitIdx / 64;
    size_t bitPos = bitIdx % 64;

    size_t count = 0;

    // Count full words
    for (size_t i = 0; i < wordIdx; i++) {
        count += __builtin_popcountll(louds_[i]);
    }

    // Count partial word
    if (bitPos > 0) {
        uint64_t mask = (1ULL << bitPos) - 1;
        count += __builtin_popcountll(louds_[wordIdx] & mask);
    }

    return count;
}

size_t CompTrieReader::select0(size_t k) const {
    if (k == 0) return 0;

    size_t pos = 0;
    size_t zeros = 0;

    // Search word by word
    size_t maxWords = loudsSize_ / 64;
    for (size_t wordIdx = 0; wordIdx < maxWords; wordIdx++) {
        uint64_t word = louds_[wordIdx];
        size_t ones = __builtin_popcountll(word);
        size_t wordZeros = 64 - ones;

        if (zeros + wordZeros >= k) {
            // k-th zero is in this word
            uint64_t invWord = ~word;
            for (int bit = 0; bit < 64; bit++) {
                if ((invWord >> bit) & 1) {
                    zeros++;
                    if (zeros == k) {
                        return wordIdx * 64 + bit;
                    }
                }
            }
        }

        zeros += wordZeros;
        pos += 64;
    }

    return pos;  // Not found, return end position
}

size_t CompTrieReader::select1(size_t k) const {
    if (k == 0) return 0;

    size_t ones = 0;

    size_t maxWords = loudsSize_ / 64;
    for (size_t wordIdx = 0; wordIdx < maxWords; wordIdx++) {
        uint64_t word = louds_[wordIdx];
        size_t wordOnes = __builtin_popcountll(word);

        if (ones + wordOnes >= k) {
            // k-th one is in this word
            for (int bit = 0; bit < 64; bit++) {
                if ((word >> bit) & 1) {
                    ones++;
                    if (ones == k) {
                        return wordIdx * 64 + bit;
                    }
                }
            }
        }

        ones += wordOnes;
    }

    return loudsSize_;  // Not found
}

//==============================================================================
// Tree Navigation
//==============================================================================

size_t CompTrieReader::firstChild(size_t nodeIdx) const {
    if (nodeIdx == 0) {
        // Root's first child is at position after first 0
        // In LOUDS, root is represented by "10" (one child) or "110" (two children), etc.
        // FirstChild(0) = position 1 if root has children
        if (loudsSize_ > 0 && getBit(0) == 1) {
            return 1;
        }
        return 0;
    }

    // FirstChild(i) = Select0(Rank1(i)) + 1
    size_t r = rank1(nodeIdx);
    if (r == 0) return 0;

    size_t childPos = select0(r) + 1;

    // Check if this position has a 1-bit (meaning it's a valid child)
    if (childPos < loudsSize_ && getBit(childPos) == 1) {
        // Convert bit position to node index
        return rank1(childPos + 1);
    }

    return 0;  // No children
}

size_t CompTrieReader::parent(size_t nodeIdx) const {
    if (nodeIdx <= 1) return 0;  // Root has no parent

    // Parent(i) = Select1(Rank0(i))
    size_t r0 = rank0(nodeIdx);
    if (r0 == 0) return 0;

    size_t parentBit = select1(r0);
    return rank1(parentBit + 1);
}

bool CompTrieReader::hasChildren(size_t nodeIdx) const {
    return firstChild(nodeIdx) != 0;
}

void CompTrieReader::getChildren(size_t nodeIdx, std::vector<std::pair<std::string, size_t>>& children) const {
    children.clear();

    if (nodeIdx >= nodeCount_) return;

    // Find bit position for this node's children
    size_t bitPos;
    if (nodeIdx == 0) {
        bitPos = 0;
    } else {
        size_t r = rank1(nodeIdx);
        bitPos = select0(r) + 1;
    }

    // Read consecutive 1s as children
    size_t childIdx = 1;
    while (bitPos < loudsSize_ && getBit(bitPos) == 1) {
        // This is a child edge
        size_t childNodeIdx = rank1(bitPos + 1);

        if (childNodeIdx > 0 && childNodeIdx <= nodeCount_) {
            std::string label = decodeLabel(labels_[childNodeIdx - 1]);
            if (!label.empty()) {
                children.push_back({label, childNodeIdx});
            }
        }

        bitPos++;
        childIdx++;
    }
}

//==============================================================================
// Tree Search
//==============================================================================

size_t CompTrieReader::findPrefixNode(const std::string& prefix) const {
    if (!louds_ || !labels_ || prefix.empty()) {
        return 0;
    }

    size_t currentNode = 0;  // Start at root

    // Parse UTF-8 prefix character by character
    size_t i = 0;
    while (i < prefix.size()) {
        // Extract one UTF-8 character
        std::string utf8Char;
        uint8_t c = static_cast<uint8_t>(prefix[i]);

        if ((c & 0x80) == 0) {
            // ASCII
            utf8Char = prefix.substr(i, 1);
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8 (Cyrillic)
            if (i + 1 >= prefix.size()) break;
            utf8Char = prefix.substr(i, 2);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8
            if (i + 2 >= prefix.size()) break;
            utf8Char = prefix.substr(i, 3);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8
            if (i + 3 >= prefix.size()) break;
            utf8Char = prefix.substr(i, 4);
            i += 4;
        } else {
            break;  // Invalid UTF-8
        }

        // Find child with matching label
        std::vector<std::pair<std::string, size_t>> children;
        getChildren(currentNode, children);

        bool found = false;
        for (const auto& [label, childIdx] : children) {
            if (label == utf8Char) {
                currentNode = childIdx;
                found = true;
                break;
            }
        }

        if (!found) {
            OH_LOG_DEBUG(LOG_APP, "LOUDS: Character '%{public}s' not found at node %{public}zu",
                         utf8Char.c_str(), currentNode);
            return 0;  // Character not found
        }
    }

    return currentNode;
}

//==============================================================================
// Word Collection
//==============================================================================

void CompTrieReader::collectWords(size_t nodeIdx, const std::string& prefix,
                                   std::vector<CompTrieSuggestion>& results,
                                   int maxResults, int depth) const {
    if (depth > 30 || results.size() >= static_cast<size_t>(maxResults * 10)) {
        return;
    }

    // Check if current node is a terminal (word ends here)
    if (nodeIdx > 0 && isTerminal(nodeIdx)) {
        CompTrieSuggestion s;
        s.word = prefix;
        s.frequency = 100;  // Default frequency
        s.score = 1.0f / (1.0f + depth);  // Prefer shorter words
        results.push_back(s);
    }

    // Get children and recurse
    std::vector<std::pair<std::string, size_t>> children;
    getChildren(nodeIdx, children);

    for (const auto& [label, childIdx] : children) {
        if (label.empty() || label == " " || label == "@") {
            continue;  // Skip separators
        }

        std::string newPrefix = prefix + label;
        collectWords(childIdx, newPrefix, results, maxResults, depth + 1);

        if (results.size() >= static_cast<size_t>(maxResults * 10)) {
            break;
        }
    }
}

//==============================================================================
// Public API
//==============================================================================

bool CompTrieReader::contains(const std::string& word) const {
    if (!isLoaded() || word.empty()) {
        return false;
    }

    size_t node = findPrefixNode(word);
    return node != 0 && isTerminal(node);
}

uint64_t CompTrieReader::getFrequency(const std::string& word) const {
    if (!contains(word)) {
        return 0;
    }
    // TODO: Extract actual frequency from payload section
    return 100;
}

std::vector<CompTrieSuggestion> CompTrieReader::getSuggestions(const std::string& prefix, int maxResults) const {
    std::vector<CompTrieSuggestion> results;

    if (!isLoaded() || prefix.empty()) {
        OH_LOG_DEBUG(LOG_APP, "LOUDS: getSuggestions - not loaded or empty prefix");
        return results;
    }

    OH_LOG_DEBUG(LOG_APP, "LOUDS: getSuggestions('%{public}s'), maxResults=%{public}d",
                 prefix.c_str(), maxResults);

    // Find node for prefix
    size_t prefixNode = findPrefixNode(prefix);

    if (prefixNode == 0) {
        OH_LOG_DEBUG(LOG_APP, "LOUDS: Prefix '%{public}s' not found in trie", prefix.c_str());
        return results;
    }

    OH_LOG_DEBUG(LOG_APP, "LOUDS: Found prefix at node %{public}zu", prefixNode);

    // Collect words from this subtree
    collectWords(prefixNode, prefix, results, maxResults, 0);

    OH_LOG_DEBUG(LOG_APP, "LOUDS: Collected %{public}zu raw results", results.size());

    // Sort by score (higher is better)
    std::sort(results.begin(), results.end(), [](const CompTrieSuggestion& a, const CompTrieSuggestion& b) {
        return a.score > b.score;
    });

    // Deduplicate
    results.erase(std::unique(results.begin(), results.end(),
        [](const CompTrieSuggestion& a, const CompTrieSuggestion& b) {
            return a.word == b.word;
        }), results.end());

    // Trim to maxResults
    if (static_cast<int>(results.size()) > maxResults) {
        results.resize(maxResults);
    }

    // Log results
    for (size_t i = 0; i < std::min(results.size(), static_cast<size_t>(5)); i++) {
        OH_LOG_DEBUG(LOG_APP, "LOUDS: result[%{public}zu] = '%{public}s'",
                     i, results[i].word.c_str());
    }

    return results;
}

void CompTrieReader::iteratePrefix(const std::string& prefix,
                                    std::function<bool(const std::string& word, uint64_t freq)> callback) const {
    if (!isLoaded() || !callback) {
        return;
    }

    size_t prefixNode = findPrefixNode(prefix);
    if (prefixNode == 0 && !prefix.empty()) {
        return;
    }

    // Use collectWords and call callback for each
    std::vector<CompTrieSuggestion> results;
    collectWords(prefixNode, prefix, results, 10000, 0);

    for (const auto& s : results) {
        if (!callback(s.word, s.frequency)) {
            break;
        }
    }
}

size_t CompTrieReader::getMemoryUsage() const {
    return sizeof(*this) + 4096;  // Minimal - only mmap metadata
}

size_t CompTrieReader::getWordCount() const {
    if (wordCountCached_) {
        return wordCount_;
    }

    if (!isLoaded()) {
        return 0;
    }

    // Count terminal nodes
    wordCount_ = 0;
    for (size_t i = 1; i <= nodeCount_; i++) {
        if (isTerminal(i)) {
            wordCount_++;
        }
    }

    wordCountCached_ = true;
    OH_LOG_DEBUG(LOG_APP, "LOUDS: Word count = %{public}zu", wordCount_);

    return wordCount_;
}

} // namespace yandex
