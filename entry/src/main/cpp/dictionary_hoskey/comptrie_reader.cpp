/**
 * Yandex CompactTrie Reader Implementation
 */

#include "comptrie_reader.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <queue>
#include <hilog/log.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "HOSKEY-TRIE"

namespace yandex {

//==============================================================================
// VarInt Skip Table (from Catboost)
// Maps first byte to total VarInt length
//==============================================================================
static const uint8_t SkipTable[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,6,6,6,6,7,7,8,9
};

//==============================================================================
// Constructor / Destructor
//==============================================================================

CompTrieReader::CompTrieReader() = default;

CompTrieReader::~CompTrieReader() {
    unload();
}

//==============================================================================
// Load / Unload
//==============================================================================

bool CompTrieReader::load(const std::string& path) {
    unload();

    // Open file
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    // Get file size
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return false;
    }
    fileSize_ = st.st_size;

    // Memory map the file
    void* mapped = mmap(nullptr, fileSize_, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED) {
        return false;
    }

    mapHandle_ = mapped;
    mapLength_ = fileSize_;
    data_ = static_cast<const uint8_t*>(mapped);

    // Parse structure to find trie boundaries
    if (!parseStructure()) {
        unload();
        return false;
    }

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

    // Memory map the file region
    void* mapped = mmap(nullptr, mapLength, PROT_READ, MAP_PRIVATE, fd, alignedOffset);
    if (mapped == MAP_FAILED) {
        return false;
    }

    mapHandle_ = mapped;
    mapLength_ = mapLength;  // Store for proper munmap
    data_ = static_cast<const uint8_t*>(mapped) + delta;

    // Advise kernel for sequential access
    madvise(mapped, mapLength, MADV_SEQUENTIAL);

    // Parse structure to find trie boundaries
    if (!parseStructure()) {
        unload();
        return false;
    }

    return true;
}

void CompTrieReader::unload() {
    if (mapHandle_) {
        munmap(mapHandle_, mapLength_ > 0 ? mapLength_ : fileSize_);
        mapHandle_ = nullptr;
    }
    data_ = nullptr;
    fileSize_ = 0;
    mapLength_ = 0;
    trieStart_ = 0;
    trieEnd_ = 0;
    wordCount_ = 0;
    wordCountCached_ = false;
}

bool CompTrieReader::parseStructure() {
    if (!data_ || fileSize_ < 64) {
        return false;
    }

    // Check magic
    uint32_t magic = *reinterpret_cast<const uint32_t*>(data_);
    if (magic != YANDEX_MAGIC) {
        OH_LOG_ERROR(LOG_APP, "HOSKEY-TRIE: Invalid magic: 0x%08X (expected 0x%08X)",
                     magic, YANDEX_MAGIC);
        return false;
    }

    // Find JSON config (starts at offset 32)
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

    // Extract JSON as string for parsing
    std::string jsonConfig(reinterpret_cast<const char*>(data_ + jsonStart), jsonEnd - jsonStart);
    OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: JSON config length: %{public}zu", jsonConfig.size());

    // Parse offsets from JSON
    auto parseJsonInt = [&jsonConfig](const char* key) -> size_t {
        std::string searchKey = std::string("\"") + key + "\":";
        size_t pos = jsonConfig.find(searchKey);
        if (pos == std::string::npos) return 0;
        pos += searchKey.length();
        // Skip whitespace
        while (pos < jsonConfig.size() && (jsonConfig[pos] == ' ' || jsonConfig[pos] == '\t')) pos++;
        // Parse number
        size_t value = 0;
        while (pos < jsonConfig.size() && jsonConfig[pos] >= '0' && jsonConfig[pos] <= '9') {
            value = value * 10 + (jsonConfig[pos] - '0');
            pos++;
        }
        return value;
    };

    // Dictionary structure from Y1 parser:
    //   blacklist: offset=10016, size=374455
    //   trie:      offset=384496, size=4498484
    //
    // DictOffset points to blacklist start, NOT trie!
    // Trie starts AFTER blacklist section.

    size_t dictOffset = parseJsonInt("DictOffset");
    size_t dictSize = parseJsonInt("DictSize");
    size_t blacklistSize = parseJsonInt("BlacklistSize");
    size_t trieOffset = parseJsonInt("TrieOffset");  // May not exist
    size_t trieSize = parseJsonInt("TrieSize");      // May not exist

    OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: JSON DictOffset=%{public}zu, DictSize=%{public}zu, BlacklistSize=%{public}zu",
                 dictOffset, dictSize, blacklistSize);

    // Strategy: find where the actual trie starts (after blacklist)
    if (trieOffset > 0 && trieSize > 0) {
        // Direct trie offset available
        trieStart_ = trieOffset;
        trieEnd_ = trieOffset + trieSize;
        OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: Using TrieOffset: [%{public}zu - %{public}zu]",
                     trieStart_, trieEnd_);
    } else if (dictOffset > 0 && blacklistSize > 0) {
        // Calculate trie start = after blacklist (with 16-byte alignment)
        size_t blacklistEnd = dictOffset + blacklistSize;
        trieStart_ = (blacklistEnd + 15) & ~15;  // Align to 16 bytes
        trieEnd_ = dictOffset + dictSize;
        OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: Calculated after blacklist: [%{public}zu - %{public}zu]",
                     trieStart_, trieEnd_);
    } else if (dictOffset > 0 && dictSize > 0) {
        // Fallback: scan for trie signature after blacklist
        // Blacklist typically ~374KB, trie starts around 384KB
        // Look for first valid trie node marker after ~380KB
        size_t searchStart = dictOffset + 370000;  // Skip most of blacklist
        if (searchStart > fileSize_) searchStart = dictOffset;

        trieStart_ = 0;
        for (size_t i = searchStart; i < dictOffset + dictSize && i < fileSize_ - 10; i++) {
            // Look for valid CompactTrie node pattern
            uint8_t flags = data_[i];
            if ((flags & 0xC0) == 0xC0 || (flags & 0xC0) == 0x80) {
                // Potential trie node - verify next bytes look like UTF-8 Cyrillic
                if (i + 2 < fileSize_ && data_[i+1] == 0xD0 && data_[i+2] >= 0x80) {
                    trieStart_ = i;
                    OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: Found trie signature at %{public}zu", i);
                    break;
                }
            }
        }

        if (trieStart_ == 0) {
            trieStart_ = dictOffset;  // Last resort
        }
        trieEnd_ = dictOffset + dictSize;
        OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: Scanned for trie: [%{public}zu - %{public}zu]",
                     trieStart_, trieEnd_);
    } else {
        // Fallback: skip padding after JSON
        trieStart_ = jsonEnd;
        while (trieStart_ < fileSize_ && (data_[trieStart_] == 0 || data_[trieStart_] == ' ')) {
            trieStart_++;
        }

        // Find first TFLite model (TFL3 signature) as end marker
        const uint8_t tfl3[] = {'T', 'F', 'L', '3'};
        trieEnd_ = fileSize_;

        for (size_t i = trieStart_; i < fileSize_ - 4; i++) {
            if (memcmp(data_ + i, tfl3, 4) == 0) {
                trieEnd_ = i - 4;  // FlatBuffer size is before signature
                break;
            }
        }
        OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: Fallback heuristic: trie [%{public}zu - %{public}zu]",
                     trieStart_, trieEnd_);
    }

    // Find max frequency for normalization (sample first 1000 words)
    maxFrequency_ = 1;
    int sampled = 0;
    iteratePrefix("", [this, &sampled](const std::string&, uint64_t freq) {
        if (freq > maxFrequency_) maxFrequency_ = freq;
        return ++sampled < 1000;
    });

    return trieStart_ < trieEnd_;
}

//==============================================================================
// VarInt helpers
//==============================================================================

size_t CompTrieReader::unpackOffset(const uint8_t* p, size_t len) {
    size_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result = (result << 8) | p[i];
    }
    return result;
}

uint64_t CompTrieReader::unpackVarInt(const uint8_t* p, size_t& bytesRead) {
    uint8_t ch = *p++;
    bytesRead = SkipTable[ch];
    size_t taillen = bytesRead - 1;
    
    uint64_t result = ch & (0x7F >> taillen);
    
    while (taillen--) {
        result = (result << 8) | (*p++ & 0xFF);
    }
    
    return result;
}

size_t CompTrieReader::skipVarInt(const uint8_t* p) {
    return SkipTable[*p];
}

//==============================================================================
// Navigation
//==============================================================================

uint8_t CompTrieReader::leapByte(const uint8_t*& datapos, const uint8_t* dataend, uint8_t label) const {
    while (datapos < dataend) {
        const uint8_t* startpos = datapos;
        uint8_t flags = *datapos++;

        // Check for epsilon link (no MT_FINAL or MT_NEXT)
        if (!(flags & (MT_FINAL | MT_NEXT))) {
            size_t offsetlen = flags & MT_SIZEMASK;
            size_t offset = unpackOffset(datapos, offsetlen);
            if (!offset) break;
            datapos = startpos + offset;
            continue;
        }

        uint8_t ch = *datapos++;
        
        // Left branch
        size_t leftLen = (flags >> MT_LEFTSHIFT) & MT_SIZEMASK;
        if (label < ch) {
            size_t offset = unpackOffset(datapos, leftLen);
            if (!offset) break;
            datapos = startpos + offset;
            continue;
        }
        datapos += leftLen;

        // Right branch
        size_t rightLen = flags & MT_SIZEMASK;
        if (label > ch) {
            size_t offset = unpackOffset(datapos, rightLen);
            if (!offset) break;
            datapos = startpos + offset;
            continue;
        }
        datapos += rightLen;

        // Match found
        return flags;
    }

    datapos = nullptr;
    return 0;
}

bool CompTrieReader::findKey(const uint8_t* key, size_t keylen, const uint8_t** value) const {
    if (!data_ || trieStart_ >= trieEnd_) {
        return false;
    }

    const uint8_t* pos = data_ + trieStart_;
    const uint8_t* end = data_ + trieEnd_;
    
    *value = nullptr;
    uint8_t flags = MT_NEXT;

    for (size_t i = 0; i < keylen; i++) {
        flags = leapByte(pos, end, key[i]);
        if (!pos) {
            return false;
        }

        if (flags & MT_FINAL) {
            // There's a value here
            if (i == keylen - 1) {
                *value = pos;
                return true;
            }
            // Skip value to continue
            pos += skipVarInt(pos);
        }

        if (!(flags & MT_NEXT)) {
            return false;
        }
    }

    return false;
}

//==============================================================================
// Public API
//==============================================================================

bool CompTrieReader::contains(const std::string& word) const {
    if (!data_ || word.empty() || trieStart_ >= trieEnd_) {
        return false;
    }

    // Direct search: scan for the word as UTF-8 byte sequence
    const uint8_t* wordBytes = reinterpret_cast<const uint8_t*>(word.data());
    const size_t wordLen = word.size();
    const uint8_t* start = data_ + trieStart_;
    const uint8_t* end = data_ + trieEnd_ - wordLen;

    // Scan for exact word match followed by word boundary
    for (const uint8_t* pos = start; pos < end; pos++) {
        if (memcmp(pos, wordBytes, wordLen) == 0) {
            // Check word boundary: next byte should not be continuation
            uint8_t nextByte = *(pos + wordLen);
            // Word boundary: null, space, node marker, or non-Cyrillic
            if (nextByte == 0 || nextByte == ' ' || nextByte == '@' ||
                nextByte == NODE_REGULAR || nextByte == NODE_PROPERTY ||
                nextByte == NODE_END || nextByte == NODE_TERMINAL ||
                (nextByte != UTF8_CYR_D0 && nextByte != UTF8_CYR_D1 &&
                 !(nextByte >= 'a' && nextByte <= 'z'))) {
                return true;
            }
        }
    }
    return false;
}

uint64_t CompTrieReader::getFrequency(const std::string& word) const {
    // Since we can't reliably extract frequencies from Yandex format,
    // return a heuristic frequency based on word properties
    if (!contains(word)) {
        return 0;
    }

    // Heuristic frequency: shorter common words get higher frequency
    size_t charCount = 0;
    for (size_t i = 0; i < word.size(); ) {
        uint8_t c = static_cast<uint8_t>(word[i]);
        if ((c & 0x80) == 0) { i++; }
        else if ((c & 0xE0) == 0xC0) { i += 2; }
        else { i += 1; }
        charCount++;
    }

    // Frequency inversely related to length
    if (charCount <= 3) return 255;
    if (charCount <= 5) return 200;
    if (charCount <= 7) return 150;
    if (charCount <= 10) return 100;
    return 50;
}

void CompTrieReader::collectWords(const uint8_t* pos, const uint8_t* end,
                                   const std::string& prefix,
                                   std::vector<CompTrieSuggestion>& results,
                                   int maxResults, int depth) const {
    if (!pos || pos >= end || depth > 30) return;
    if ((int)results.size() >= maxResults * 10) return;

    // Stack-based DFS with explicit state tracking
    struct StackItem {
        const uint8_t* pos;
        std::string word;
        int depth;
    };
    
    std::vector<StackItem> stack;
    stack.reserve(256);
    stack.push_back({pos, prefix, depth});

    const size_t maxWordLen = prefix.size() + 40;  // Max 40 chars after prefix

    while (!stack.empty() && (int)results.size() < maxResults * 10) {
        StackItem item = stack.back();
        stack.pop_back();

        if (!item.pos || item.pos >= end || item.depth > 30) continue;
        if (item.word.size() > maxWordLen) continue;  // Word too long, skip

        const uint8_t* p = item.pos;
        
        // Process one node and its siblings
        while (p && p < end - 1) {
            const uint8_t* startpos = p;
            uint8_t flags = *p++;
            
            // Epsilon link (redirect without symbol)
            if (!(flags & (MT_FINAL | MT_NEXT))) {
                size_t offsetlen = flags & MT_SIZEMASK;
                if (offsetlen == 0 || p + offsetlen > end) break;
                size_t offset = unpackOffset(p, offsetlen);
                if (!offset || startpos + offset >= end) break;
                p = startpos + offset;
                continue;
            }

            // Read label byte
            uint8_t ch = *p++;

            // Sanity check: null byte usually means end of valid data
            if (ch == 0) break;

            // Skip '@' and ' ' branches - these are n-gram separators
            // '@' (0x40) separates current word from next-word prediction
            // ' ' (0x20) separates words in n-gram phrases
            if (ch == '@' || ch == ' ') {
                // Still need to process siblings (left/right branches)
                size_t leftLen = (flags >> MT_LEFTSHIFT) & MT_SIZEMASK;
                size_t rightLen = flags & MT_SIZEMASK;
                if (p + leftLen + rightLen > end) break;

                // Parse and push sibling branches (skip the '@' branch itself)
                size_t leftOffset = 0, rightOffset = 0;
                if (leftLen > 0) leftOffset = unpackOffset(p, leftLen);
                p += leftLen;
                if (rightLen > 0) rightOffset = unpackOffset(p, rightLen);
                p += rightLen;

                if (rightOffset > 0 && startpos + rightOffset < end) {
                    stack.push_back({startpos + rightOffset, item.word, item.depth});
                }
                if (leftOffset > 0 && startpos + leftOffset < end) {
                    stack.push_back({startpos + leftOffset, item.word, item.depth});
                }
                break;  // Don't follow '@' children, move to next stack item
            }
            
            // Read offset lengths
            size_t leftLen = (flags >> MT_LEFTSHIFT) & MT_SIZEMASK;
            size_t rightLen = flags & MT_SIZEMASK;
            
            // Bounds check
            if (p + leftLen + rightLen > end) break;
            
            // Parse branch offsets
            size_t leftOffset = 0, rightOffset = 0;
            if (leftLen > 0) {
                leftOffset = unpackOffset(p, leftLen);
            }
            p += leftLen;
            if (rightLen > 0) {
                rightOffset = unpackOffset(p, rightLen);
            }
            p += rightLen;

            // IMPORTANT: Push RIGHT first, then LEFT
            // Stack is LIFO, so LEFT will be popped first (lower bytes = alphabetical order)
            // This ensures "прив..." is found before "приж..."
            if (rightOffset > 0 && startpos + rightOffset < end) {
                stack.push_back({startpos + rightOffset, item.word, item.depth});
            }
            if (leftOffset > 0 && startpos + leftOffset < end) {
                stack.push_back({startpos + leftOffset, item.word, item.depth});
            }

            // Build current word by appending this byte
            std::string currentWord = item.word;
            currentWord += static_cast<char>(ch);
            
            // Check for word boundary (MT_FINAL)
            if (flags & MT_FINAL) {
                if (p >= end) break;
                
                size_t bytesRead = 0;
                uint64_t freq = unpackVarInt(p, bytesRead);
                
                // Validate VarInt read
                if (bytesRead == 0 || bytesRead > 8 || p + bytesRead > end) break;
                p += bytesRead;
                
                // Validate frequency (reasonable range)
                if (freq > 0 && freq < 0xFFFFFFFF) {
                    // Only add complete UTF-8 words (even number of bytes for Cyrillic)
                    CompTrieSuggestion s;
                    s.word = currentWord;
                    s.frequency = freq;
                    s.score = static_cast<float>(freq) / static_cast<float>(maxFrequency_);
                    results.push_back(s);
                }
            }

            // Queue children (next level, deeper into trie)
            if (flags & MT_NEXT) {
                if (p < end && currentWord.size() <= maxWordLen) {
                    stack.push_back({p, currentWord, item.depth + 1});
                }
            }

            break;  // Processed this node, move to next stack item
        }
    }
}

std::vector<CompTrieSuggestion> CompTrieReader::getSuggestions(const std::string& prefix, int maxResults) const {
    std::vector<CompTrieSuggestion> results;

    if (!data_ || prefix.empty()) {
        return results;
    }

    OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: getSuggestions('%{public}s'): navigating trie [%{public}zu - %{public}zu]",
                 prefix.c_str(), trieStart_, trieEnd_);

    // Navigate to prefix node using proper trie traversal (not byte scanning)
    const uint8_t* pos = data_ + trieStart_;
    const uint8_t* end = data_ + trieEnd_;
    const size_t prefixLen = prefix.size();

    // Navigate to prefix position using leapByte for each byte
    bool prefixFound = true;
    for (size_t i = 0; i < prefix.size(); i++) {
        uint8_t flags = leapByte(pos, end, static_cast<uint8_t>(prefix[i]));
        if (!pos) {
            OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: prefix byte %{public}zu (0x%{public}02X '%{public}c') not found",
                         i, static_cast<uint8_t>(prefix[i]),
                         (prefix[i] >= 32 && prefix[i] < 127) ? prefix[i] : '?');
            prefixFound = false;
            break;
        }

        // If this node has a value, skip it to continue navigation
        if (flags & MT_FINAL) {
            if (pos >= end) {
                prefixFound = false;
                break;
            }
            size_t bytesRead = skipVarInt(pos);
            if (bytesRead == 0 || bytesRead > 8 || pos + bytesRead > end) {
                prefixFound = false;
                break;
            }
            pos += bytesRead;
        }

        // Check if we can continue (except for last byte)
        if (!(flags & MT_NEXT) && i < prefix.size() - 1) {
            OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: no continuation at byte %{public}zu", i);
            prefixFound = false;
            break;
        }
    }

    std::vector<std::string> foundWords;
    foundWords.reserve(maxResults * 10);

    if (!prefixFound) {
        OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: prefix '%{public}s' not found via trie navigation", prefix.c_str());
        // Return empty - no fallback to byte scan
        return results;
    }

    OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: prefix found at offset %{public}zu, collecting words",
                 pos - data_);

    // Collect words from this subtrie using DFS
    struct StackItem {
        const uint8_t* pos;
        std::string word;
        int depth;
    };

    std::vector<StackItem> stack;
    stack.reserve(256);
    stack.push_back({pos, prefix, 0});

    const size_t maxWordLen = prefix.size() + 40;

    while (!stack.empty() && foundWords.size() < static_cast<size_t>(maxResults * 50)) {
        StackItem item = stack.back();
        stack.pop_back();

        if (!item.pos || item.pos >= end || item.depth > 30 || item.word.size() > maxWordLen) {
            continue;
        }

        const uint8_t* p = item.pos;
        while (p && p < end - 1) {
            const uint8_t* startpos = p;
            uint8_t flags = *p++;

            // Epsilon link (redirect without symbol)
            if (!(flags & (MT_FINAL | MT_NEXT))) {
                size_t offsetlen = flags & MT_SIZEMASK;
                if (offsetlen == 0 || p + offsetlen > end) break;
                size_t offset = unpackOffset(p, offsetlen);
                if (!offset || startpos + offset >= end) break;
                p = startpos + offset;
                continue;
            }

            if (p >= end) break;
            uint8_t ch = *p++;
            if (ch == 0) break;

            // Skip '@' and ' ' branches - n-gram separators
            if (ch == '@' || ch == ' ') {
                size_t leftLen = (flags >> MT_LEFTSHIFT) & MT_SIZEMASK;
                size_t rightLen = flags & MT_SIZEMASK;
                if (p + leftLen + rightLen > end) break;

                size_t leftOffset = 0, rightOffset = 0;
                if (leftLen > 0) leftOffset = unpackOffset(p, leftLen);
                p += leftLen;
                if (rightLen > 0) rightOffset = unpackOffset(p, rightLen);
                p += rightLen;

                if (rightOffset > 0 && startpos + rightOffset < end) {
                    stack.push_back({startpos + rightOffset, item.word, item.depth});
                }
                if (leftOffset > 0 && startpos + leftOffset < end) {
                    stack.push_back({startpos + leftOffset, item.word, item.depth});
                }
                break;
            }

            size_t leftLen = (flags >> MT_LEFTSHIFT) & MT_SIZEMASK;
            size_t rightLen = flags & MT_SIZEMASK;
            if (p + leftLen + rightLen > end) break;

            size_t leftOffset = 0, rightOffset = 0;
            if (leftLen > 0) leftOffset = unpackOffset(p, leftLen);
            p += leftLen;
            if (rightLen > 0) rightOffset = unpackOffset(p, rightLen);
            p += rightLen;

            // Push siblings to stack
            if (rightOffset > 0 && startpos + rightOffset < end) {
                stack.push_back({startpos + rightOffset, item.word, item.depth});
            }
            if (leftOffset > 0 && startpos + leftOffset < end) {
                stack.push_back({startpos + leftOffset, item.word, item.depth});
            }

            // Build current word by appending this character byte
            std::string currentWord = item.word;
            currentWord += static_cast<char>(ch);

            // Check for word boundary (MT_FINAL)
            if (flags & MT_FINAL) {
                if (p >= end) break;
                size_t bytesRead = skipVarInt(p);
                if (bytesRead == 0 || bytesRead > 8 || p + bytesRead > end) break;
                p += bytesRead;

                // Save valid word (must be longer than prefix)
                if (currentWord.size() > prefix.size() && currentWord.size() <= 40) {
                    foundWords.push_back(currentWord);
                }
            }

            // Continue to next level if MT_NEXT
            if (flags & MT_NEXT) {
                stack.push_back({p, currentWord, item.depth + 1});
            }

            break;  // Move to next stack item
        }
    }

    OH_LOG_DEBUG(LOG_APP, "HOSKEY-TRIE: getSuggestions('%{public}s'): found %{public}zu raw matches",
                 prefix.c_str(), foundWords.size());

    // Log first few found words
    for (size_t i = 0; i < std::min(foundWords.size(), (size_t)5); i++) {
        OH_LOG_DEBUG(LOG_APP, "  raw[%{public}zu]: '%{public}s'", i, foundWords[i].c_str());
    }

    // Helper: extract clean word from n-gram format
    auto extractWord = [&prefix](const std::string& entry) -> std::string {
        // Find @ delimiter - take part before @ (current word, not prediction)
        size_t atPos = entry.find('@');
        std::string text = (atPos != std::string::npos) ? entry.substr(0, atPos) : entry;

        // If text contains spaces, find word matching prefix
        if (text.find(' ') != std::string::npos) {
            size_t start = 0;
            while (start < text.size()) {
                size_t end = text.find(' ', start);
                if (end == std::string::npos) end = text.size();
                std::string word = text.substr(start, end - start);
                if (!word.empty() && word.size() >= prefix.size() &&
                    word.compare(0, prefix.size(), prefix) == 0) {
                    return word;
                }
                start = end + 1;
            }
            return "";
        }
        return text;
    };
    
    auto isValidUtf8Word = [](const std::string& word) -> bool {
        if (word.empty() || word.size() > 40) return false;

        // Track valid character count
        size_t validChars = 0;

        for (size_t i = 0; i < word.size(); ) {
            uint8_t c = static_cast<uint8_t>(word[i]);

            // Control characters are invalid
            if (c < 0x20) return false;

            // ASCII printable (0x20-0x7E)
            if (c < 0x80) {
                // Only allow: letters, apostrophe, hyphen, space
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    c == '\'' || c == '-' || c == ' ') {
                    i++;
                    validChars++;
                    continue;
                }
                // Reject @, digits, punctuation, etc.
                return false;
            }

            // 2-byte UTF-8 (0xC0-0xDF): includes Cyrillic (0xD0-0xD1)
            if ((c & 0xE0) == 0xC0) {
                if (i + 1 >= word.size()) return false;
                uint8_t c1 = static_cast<uint8_t>(word[i + 1]);
                if ((c1 & 0xC0) != 0x80) return false;

                // Decode codepoint
                uint32_t cp = ((c & 0x1F) << 6) | (c1 & 0x3F);

                // Only allow Cyrillic: U+0400-U+04FF
                if (cp >= 0x0400 && cp <= 0x04FF) {
                    i += 2;
                    validChars++;
                    continue;
                }
                return false;
            }

            // 3-byte UTF-8 (0xE0-0xEF): includes Korean, Chinese - REJECT
            if ((c & 0xF0) == 0xE0) {
                return false;  // No Korean/Chinese in Russian dictionary
            }

            // 4-byte UTF-8 (0xF0-0xF7): emoji etc - REJECT
            if ((c & 0xF8) == 0xF0) {
                return false;
            }

            // Invalid UTF-8 bytes
            return false;
        }

        // Must have at least 1 valid character
        return validChars > 0;
    };
    
    // Common Russian word stems for boosting
    auto isCommonWordPattern = [](const std::string& word) -> bool {
        // Common Russian word beginnings (UTF-8)
        static const char* commonStarts[] = {
            // Very common words
            "привет", "прив", "пока", "спасиб", "здравств", "добр",
            "хорош", "норм", "отлич", "класс", "круто",
            // Common verbs
            "сдела", "буд", "был", "есть", "хоч", "мог", "долж", "нуж",
            "знаю", "знае", "дума", "думаю", "люблю", "любл",
            // Common nouns
            "день", "ночь", "утро", "вечер", "время", "год", "месяц",
            "работ", "дом", "друг", "человек", "жизн",
            // Pronouns/determiners
            "этот", "этого", "тот", "мой", "твой", "наш", "ваш",
            // Adverbs
            "сегодн", "завтра", "вчера", "сейчас", "потом", "всегда", "никогда",
            "очень", "тоже", "также", "только", "уже", "еще",
            // Conjunctions/prepositions
            "потому", "поэтому", "когда", "если", "чтобы", "после", "перед",
            // Common adjectives
            "новый", "новая", "старый", "большой", "маленьк", "красив"
        };

        for (const char* stem : commonStarts) {
            if (word.find(stem) == 0) {
                return true;
            }
        }
        return false;
    };

    // Word quality scoring - prefer common, natural Russian words
    auto getWordQuality = [&isCommonWordPattern](const std::string& word, size_t prefixLen) -> float {
        float quality = 1.0f;

        // Count UTF-8 characters
        size_t charCount = 0;
        for (size_t i = 0; i < word.size(); ) {
            uint8_t c = static_cast<uint8_t>(word[i]);
            if ((c & 0x80) == 0) { i++; charCount++; }
            else if ((c & 0xE0) == 0xC0) { i += 2; charCount++; }
            else if ((c & 0xF0) == 0xE0) { i += 3; charCount++; }
            else { i += 4; charCount++; }
        }

        // Optimal word length: 3-12 characters
        if (charCount >= 3 && charCount <= 12) {
            quality *= 1.5f;
        } else if (charCount < 3 || charCount > 15) {
            quality *= 0.5f;
        }

        // Prefer words where prefix covers more of the word
        float prefixCoverage = static_cast<float>(prefixLen) / static_cast<float>(charCount);
        if (prefixCoverage >= 0.5f) {
            quality *= 1.3f;
        }

        // Penalize very short extensions (might be partial words)
        size_t extensionLen = charCount > prefixLen ? charCount - prefixLen : 0;
        if (extensionLen <= 1 && charCount < 4) {
            quality *= 0.7f;
        }

        // Boost common Russian words significantly
        if (isCommonWordPattern(word)) {
            quality *= 3.0f;
        }

        return quality;
    };
    
    // Count UTF-8 characters (not bytes)
    auto countUtf8Chars = [](const std::string& str) -> size_t {
        size_t count = 0;
        for (size_t i = 0; i < str.size(); ) {
            uint8_t c = static_cast<uint8_t>(str[i]);
            if ((c & 0x80) == 0) { i++; }
            else if ((c & 0xE0) == 0xC0) { i += 2; }
            else if ((c & 0xF0) == 0xE0) { i += 3; }
            else { i += 4; }
            count++;
        }
        return count;
    };

    // Calculate prefix length in characters
    size_t prefixCharLen = countUtf8Chars(prefix);

    // Convert foundWords to CompTrieSuggestion with filtering
    for (const auto& rawWord : foundWords) {
        std::string word = extractWord(rawWord);

        // Skip empty words
        if (word.empty()) continue;

        // Skip words containing spaces (n-gram artifacts)
        if (word.find(' ') != std::string::npos) continue;

        // Verify prefix match
        if (word.size() < prefix.size() || word.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }

        // Validate UTF-8 structure
        if (!isValidUtf8Word(word)) continue;

        // Create suggestion with quality-based score
        CompTrieSuggestion s;
        s.word = word;
        s.frequency = 100;  // Base frequency (we use quality scoring instead)
        s.score = getWordQuality(word, prefixCharLen);
        results.push_back(s);
    }

    OH_LOG_DEBUG(LOG_APP, "getSuggestions('%{public}s'): after filtering: %{public}zu results",
                 prefix.c_str(), results.size());

    // Log first few results
    for (size_t i = 0; i < std::min(results.size(), (size_t)5); i++) {
        OH_LOG_DEBUG(LOG_APP, "  result[%{public}zu]: '%{public}s' score=%.3f",
                     i, results[i].word.c_str(), results[i].score);
    }

    // Deduplicate (same word from different n-grams)
    std::sort(results.begin(), results.end(), [](const CompTrieSuggestion& a, const CompTrieSuggestion& b) {
        if (a.word != b.word) return a.word < b.word;
        return a.score > b.score;  // Keep highest quality version
    });
    results.erase(std::unique(results.begin(), results.end(),
        [](const CompTrieSuggestion& a, const CompTrieSuggestion& b) {
            return a.word == b.word;
        }), results.end());

    // Sort by combined score (frequency * quality) descending
    std::sort(results.begin(), results.end(), [](const CompTrieSuggestion& a, const CompTrieSuggestion& b) {
        return a.score > b.score;
    });

    // Trim to maxResults
    if ((int)results.size() > maxResults) {
        results.resize(maxResults);
    }

    return results;
}

void CompTrieReader::iteratePrefix(const std::string& prefix,
                                    std::function<bool(const std::string& word, uint64_t freq)> callback) const {
    if (!data_ || !callback) {
        return;
    }

    // Navigate to prefix node.
    const uint8_t* pos = data_ + trieStart_;
    const uint8_t* end = data_ + trieEnd_;

    for (size_t i = 0; i < prefix.size(); i++) {
        uint8_t flags = leapByte(pos, end, static_cast<uint8_t>(prefix[i]));
        if (!pos) {
            return;
        }

        if (flags & MT_FINAL) {
            if (pos >= end) return;
            size_t bytesRead = 0;
            uint64_t freq = unpackVarInt(pos, bytesRead);
            if (bytesRead == 0 || bytesRead > 8 || pos + bytesRead > end) return;

            if (i == prefix.size() - 1) {
                if (!callback(prefix, freq)) {
                    return;
                }
            }
            pos += bytesRead;
        }

        if (!(flags & MT_NEXT) && i < prefix.size() - 1) {
            return;
        }
    }

    // DFS traversal without materializing all suggestions in memory.
    struct StackItem {
        const uint8_t* pos;
        std::string word;
        int depth;
    };

    std::vector<StackItem> stack;
    stack.reserve(256);
    stack.push_back({pos, prefix, 0});

    const size_t maxWordLen = prefix.size() + 40;

    while (!stack.empty()) {
        StackItem item = stack.back();
        stack.pop_back();

        if (!item.pos || item.pos >= end || item.depth > 30 || item.word.size() > maxWordLen) {
            continue;
        }

        const uint8_t* p = item.pos;
        while (p && p < end - 1) {
            const uint8_t* startpos = p;
            uint8_t flags = *p++;

            // Epsilon link (redirect without symbol).
            if (!(flags & (MT_FINAL | MT_NEXT))) {
                size_t offsetlen = flags & MT_SIZEMASK;
                if (offsetlen == 0 || p + offsetlen > end) break;
                size_t offset = unpackOffset(p, offsetlen);
                if (!offset || startpos + offset >= end) break;
                p = startpos + offset;
                continue;
            }

            if (p >= end) break;
            uint8_t ch = *p++;
            if (ch == 0) break;

            size_t leftLen = (flags >> MT_LEFTSHIFT) & MT_SIZEMASK;
            size_t rightLen = flags & MT_SIZEMASK;
            if (p + leftLen + rightLen > end) break;

            // Parse branch offsets
            size_t leftOffset = 0, rightOffset = 0;
            if (leftLen > 0) {
                leftOffset = unpackOffset(p, leftLen);
            }
            p += leftLen;
            if (rightLen > 0) {
                rightOffset = unpackOffset(p, rightLen);
            }
            p += rightLen;

            // Push RIGHT first, then LEFT (LIFO order ensures LEFT is processed first)
            if (rightOffset > 0 && startpos + rightOffset < end) {
                stack.push_back({startpos + rightOffset, item.word, item.depth});
            }
            if (leftOffset > 0 && startpos + leftOffset < end) {
                stack.push_back({startpos + leftOffset, item.word, item.depth});
            }

            // Skip '@' and ' ' branches (n-gram separators)
            if (ch == '@' || ch == ' ') {
                break;
            }

            std::string currentWord = item.word;
            currentWord += static_cast<char>(ch);

            if (flags & MT_FINAL) {
                if (p >= end) break;
                size_t bytesRead = 0;
                uint64_t freq = unpackVarInt(p, bytesRead);
                if (bytesRead == 0 || bytesRead > 8 || p + bytesRead > end) break;
                p += bytesRead;
                if (!callback(currentWord, freq)) {
                    return;
                }
            }

            if ((flags & MT_NEXT) && p < end && item.depth < 30 && currentWord.size() <= maxWordLen) {
                stack.push_back({p, currentWord, item.depth + 1});
            }

            break;
        }
    }
}

size_t CompTrieReader::getMemoryUsage() const {
    // Only mmap overhead, actual RAM usage is minimal
    return sizeof(*this) + 4096;  // ~1 page for metadata
}

size_t CompTrieReader::getWordCount() const {
    if (wordCountCached_) {
        return wordCount_;
    }

    wordCount_ = 0;
    iteratePrefix("", [this](const std::string&, uint64_t) {
        wordCount_++;
        return true;
    });
    wordCountCached_ = true;

    return wordCount_;
}

} // namespace yandex
