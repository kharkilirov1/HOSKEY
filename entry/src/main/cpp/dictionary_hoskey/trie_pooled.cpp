/**
 * Optimized Patricia Trie Implementation with Memory Pool
 *
 * Key optimizations vs original:
 * 1. Pre-allocated node pool (no individual malloc)
 * 2. Inline children storage (no hash map overhead)
 * 3. Conditional debug logging (compile-time switch)
 * 4. Reusable UTF-8 buffer (no string allocations per word)
 */

#include "trie_pooled.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <limits>
#include <hilog/log.h>
#include <sys/mman.h>
#include <unistd.h>

#undef LOG_TAG
#define LOG_TAG "HOSKEY-TRIE-POOLED"

// Compile-time switch for debug logging
// Comment out for release builds to eliminate logging overhead
// #define HOSKEY_TRIE_DEBUG_LOADING

#ifdef HOSKEY_TRIE_DEBUG_LOADING
    #define TRIE_LOG_INFO(...) OH_LOG_INFO(LOG_APP, __VA_ARGS__)
    #define TRIE_LOG_WARN(...) OH_LOG_WARN(LOG_APP, __VA_ARGS__)
    #define TRIE_LOG_ERROR(...) OH_LOG_ERROR(LOG_APP, __VA_ARGS__)
#else
    #define TRIE_LOG_INFO(...) ((void)0)
    #define TRIE_LOG_WARN(...) ((void)0)
    #define TRIE_LOG_ERROR(...) OH_LOG_ERROR(LOG_APP, __VA_ARGS__)  // Keep errors
#endif

namespace hoskey {

// ============================================================================
// UTF-8 Buffer for efficient string handling
// ============================================================================

class Utf8Buffer {
public:
    static constexpr size_t MAX_WORD_LENGTH = 256;

    Utf8Buffer() : length_(0) {
        buffer_[0] = '\0';
    }

    void clear() {
        length_ = 0;
        buffer_[0] = '\0';
    }

    void appendByte(uint8_t byte) {
        if (length_ < MAX_WORD_LENGTH - 1) {
            buffer_[length_++] = static_cast<char>(byte);
            buffer_[length_] = '\0';
        }
    }

    void appendCodePoint(char32_t cp) {
        if (cp < 0x80) {
            appendByte(static_cast<uint8_t>(cp));
        } else if (cp < 0x800) {
            appendByte(0xC0 | ((cp >> 6) & 0x1F));
            appendByte(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            appendByte(0xE0 | ((cp >> 12) & 0x0F));
            appendByte(0x80 | ((cp >> 6) & 0x3F));
            appendByte(0x80 | (cp & 0x3F));
        } else {
            appendByte(0xF0 | ((cp >> 18) & 0x07));
            appendByte(0x80 | ((cp >> 12) & 0x3F));
            appendByte(0x80 | ((cp >> 6) & 0x3F));
            appendByte(0x80 | (cp & 0x3F));
        }
    }

    const char* data() const { return buffer_; }
    size_t size() const { return length_; }
    std::string toString() const { return std::string(buffer_, length_); }

private:
    char buffer_[MAX_WORD_LENGTH];
    size_t length_;
};

// ============================================================================
// TriePooled Implementation
// ============================================================================

TriePooled::TriePooled(size_t expectedWords)
    : pool_(expectedWords * 5)  // ~5 nodes per word average
    , rootIndex_(0)
    , wordCount_(0) {
    // Allocate root node
    rootIndex_ = pool_.allocate();
    OH_LOG_INFO(LOG_APP, "TriePooled: created with pool capacity=%zu, root=%u",
                pool_.capacity(), rootIndex_);
}

TriePooled::~TriePooled() {
    OH_LOG_INFO(LOG_APP, "TriePooled: destroyed, had %d words, %zu nodes",
                wordCount_, pool_.usedCount());
}

bool TriePooled::loadFromFile(const std::string& path) {
    // Determine format by extension
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".dict") {
        return loadFromBinaryFile(path);
    }
    return loadFromTextFile(path);
}

bool TriePooled::loadFromTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        TRIE_LOG_ERROR("loadFromTextFile: failed to open %s", path.c_str());
        return false;
    }

    clear();

    std::string line;
    int lineCount = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::string word;
        int frequency = 100;

        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            word = line.substr(0, tabPos);
            try {
                frequency = std::stoi(line.substr(tabPos + 1));
            } catch (...) {
                frequency = 100;
            }
        } else {
            word = line;
        }

        // Trim whitespace
        while (!word.empty() && (word.back() == ' ' || word.back() == '\r')) {
            word.pop_back();
        }

        if (!word.empty()) {
            insert(word, frequency);
            lineCount++;
        }
    }

    OH_LOG_INFO(LOG_APP, "loadFromTextFile: loaded %d words from %s", lineCount, path.c_str());
    return wordCount_ > 0;
}

// Forward declaration for binary loading
bool loadBinaryDictPooled(const std::string& path, TriePooled& trie);

bool TriePooled::loadFromBinaryFile(const std::string& path) {
    OH_LOG_INFO(LOG_APP, "TriePooled::loadFromBinaryFile: START path=%s", path.c_str());

    // Delegate to specialized loader
    bool success = loadBinaryDictPooled(path, *this);

    if (success) {
        OH_LOG_INFO(LOG_APP, "TriePooled::loadFromBinaryFile: SUCCESS - %d words, %zu nodes, %zu bytes",
                    wordCount_, pool_.usedCount(), getMemoryUsage());
    } else {
        TRIE_LOG_ERROR("TriePooled::loadFromBinaryFile: FAILED for %s", path.c_str());
    }

    return success;
}

void TriePooled::insert(const std::string& word, int frequency) {
    if (word.empty()) return;

    uint32_t current = rootIndex_;

    // Traverse/create path for each byte
    // Use getOrCreateChildSafe() to avoid dangling pointer when pool resizes
    for (size_t i = 0; i < word.size(); i++) {
        uint8_t byte = static_cast<uint8_t>(word[i]);
        current = getOrCreateChildSafe(current, byte, pool_);
        if (current == 0) {
            TRIE_LOG_ERROR("insert: allocation failed at byte %zu", i);
            return;
        }
    }

    // Mark word end - get fresh reference after all allocations are done
    TrieNodePooled& endNode = node(current);
    if (!endNode.isEndOfWord()) {
        wordCount_++;
    }
    endNode.setEndOfWord(true);
    endNode.setFrequency(static_cast<uint16_t>(frequency));
}

bool TriePooled::contains(const std::string& word) const {
    if (word.empty()) return false;

    uint32_t current = rootIndex_;

    for (size_t i = 0; i < word.size(); i++) {
        uint8_t byte = static_cast<uint8_t>(word[i]);
        current = node(current).getChild(byte);
        if (current == 0) return false;
    }

    return node(current).isEndOfWord();
}

int TriePooled::getFrequency(const std::string& word) const {
    if (word.empty()) return 0;

    uint32_t current = rootIndex_;

    for (size_t i = 0; i < word.size(); i++) {
        uint8_t byte = static_cast<uint8_t>(word[i]);
        current = node(current).getChild(byte);
        if (current == 0) return 0;
    }

    const TrieNodePooled& n = node(current);
    return n.isEndOfWord() ? n.getFrequency() : 0;
}

std::vector<WordEntry> TriePooled::findByPrefix(const std::string& prefix, int limit) const {
    std::vector<WordEntry> results;

    TRIE_LOG_INFO("findByPrefix: prefix=\"%s\" limit=%d", prefix.c_str(), limit);

    if (prefix.empty()) {
        collectWords(rootIndex_, "", results, limit);
        return results;
    }

    // Navigate to prefix node
    uint32_t current = rootIndex_;
    for (size_t i = 0; i < prefix.size(); i++) {
        uint8_t byte = static_cast<uint8_t>(prefix[i]);
        current = node(current).getChild(byte);
        if (current == 0) {
            TRIE_LOG_INFO("findByPrefix: no match at byte %zu", i);
            return results;
        }
    }

    // Collect words from this point
    collectWords(current, prefix, results, limit);

    // Sort by frequency (descending)
    std::sort(results.begin(), results.end(),
              [](const WordEntry& a, const WordEntry& b) {
                  return a.frequency > b.frequency;
              });

    // Limit results
    if (static_cast<int>(results.size()) > limit) {
        results.resize(limit);
    }

    TRIE_LOG_INFO("findByPrefix: returning %zu results", results.size());
    return results;
}

void TriePooled::collectWords(uint32_t nodeIndex, const std::string& prefix,
                              std::vector<WordEntry>& results, int limit) const {
    // Early exit at limit*2 (need some buffer for sorting, but not 3x)
    if (nodeIndex == 0 || static_cast<int>(results.size()) >= limit * 2) {
        return;
    }

    const TrieNodePooled& n = node(nodeIndex);

    if (n.isEndOfWord()) {
        results.emplace_back(prefix, n.getFrequency(), n.getProbability());
    }

    // Iterate children
    n.forEachChildIndex([this, &prefix, &results, limit](uint8_t byte, uint32_t childIndex) {
        std::string childPrefix = prefix + static_cast<char>(byte);
        collectWords(childIndex, childPrefix, results, limit);
    });
}

std::vector<std::string> TriePooled::getWordsByFirstLetter(char letter, int limit) const {
    std::vector<std::string> results;

    uint8_t byte = static_cast<uint8_t>(letter);
    uint32_t letterNode = node(rootIndex_).getChild(byte);
    if (letterNode == 0) return results;

    std::vector<WordEntry> entries;
    collectWords(letterNode, std::string(1, letter), entries, limit);

    // Sort by frequency
    std::sort(entries.begin(), entries.end(),
              [](const WordEntry& a, const WordEntry& b) {
                  return a.frequency > b.frequency;
              });

    // Extract words
    for (const auto& entry : entries) {
        if (static_cast<int>(results.size()) >= limit) break;
        results.push_back(entry.word);
    }

    return results;
}

size_t TriePooled::getMemoryUsage() const {
    return pool_.getMemoryUsage();
}

void TriePooled::clear() {
    OH_LOG_INFO(LOG_APP, "TriePooled::clear() called, had %d words", wordCount_);

    pool_.reset();
    rootIndex_ = pool_.allocate();
    wordCount_ = 0;

    OH_LOG_INFO(LOG_APP, "TriePooled::clear() done, new root=%u", rootIndex_);
}

// ============================================================================
// Binary Dictionary Loader (OpenBoard .dict format)
// Same format as binary_dict_reader.cpp but optimized for TriePooled
// ============================================================================

// Constants from OpenBoard format
constexpr uint32_t DICT_MAGIC_NUMBER = 0x9bc13afe;
constexpr int NOT_A_CODE_POINT = -1;
constexpr int NOT_A_DICT_POS = -1;
constexpr int MAX_WORD_LENGTH_POOLED = 48;

// Code point encoding
constexpr uint8_t CHARACTER_ARRAY_TERMINATOR = 0x1F;
constexpr uint8_t MINIMUM_ONE_BYTE_CHARACTER_VALUE = 0x20;

// Node flags
constexpr uint8_t MASK_CHILDREN_POSITION_TYPE = 0xC0;
constexpr uint8_t FLAG_CHILDREN_POSITION_TYPE_NOPOSITION = 0x00;
constexpr uint8_t FLAG_CHILDREN_POSITION_TYPE_ONEBYTE = 0x40;
constexpr uint8_t FLAG_CHILDREN_POSITION_TYPE_TWOBYTES = 0x80;
constexpr uint8_t FLAG_CHILDREN_POSITION_TYPE_THREEBYTES = 0xC0;
constexpr uint8_t FLAG_HAS_MULTIPLE_CHARS = 0x20;
constexpr uint8_t FLAG_IS_TERMINAL = 0x10;
constexpr uint8_t FLAG_HAS_SHORTCUT_TARGETS = 0x08;
constexpr uint8_t FLAG_HAS_BIGRAMS = 0x04;
constexpr uint8_t FLAG_IS_NOT_A_WORD = 0x02;

// Safe byte reading (big-endian)
static inline bool canReadPooled(int pos, int bytesNeeded, size_t size) {
    return pos >= 0 && (pos + bytesNeeded) <= static_cast<int>(size);
}

static inline uint32_t readUint32BEPooled(const uint8_t* buf, int pos) {
    return (static_cast<uint32_t>(buf[pos]) << 24) |
           (static_cast<uint32_t>(buf[pos + 1]) << 16) |
           (static_cast<uint32_t>(buf[pos + 2]) << 8) |
           static_cast<uint32_t>(buf[pos + 3]);
}

static inline uint16_t readUint16BEPooled(const uint8_t* buf, int pos) {
    return (static_cast<uint16_t>(buf[pos]) << 8) | static_cast<uint16_t>(buf[pos + 1]);
}

// Read code point (OpenBoard format)
static int readCodePointPooled(const uint8_t* buffer, int* pos, size_t size, bool* ok) {
    if (!canReadPooled(*pos, 1, size)) {
        *ok = false;
        return NOT_A_CODE_POINT;
    }

    uint8_t firstByte = buffer[*pos];

    if (firstByte < MINIMUM_ONE_BYTE_CHARACTER_VALUE) {
        if (firstByte == CHARACTER_ARRAY_TERMINATOR) {
            (*pos)++;
            *ok = true;
            return NOT_A_CODE_POINT;
        } else {
            // 3-byte code point
            if (!canReadPooled(*pos, 3, size)) {
                *ok = false;
                return NOT_A_CODE_POINT;
            }
            int codePoint = (static_cast<uint32_t>(buffer[*pos]) << 16) |
                           (static_cast<uint32_t>(buffer[*pos + 1]) << 8) |
                           static_cast<uint32_t>(buffer[*pos + 2]);
            *pos += 3;
            *ok = true;
            return codePoint;
        }
    } else {
        (*pos)++;
        *ok = true;
        return firstByte;
    }
}

// Convert UTF-32 code point to lowercase
static char32_t toLowerPooled(char32_t cp) {
    if (cp >= 0x0041 && cp <= 0x005A) return cp + 0x20;  // A-Z
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;  // А-Я
    if (cp == 0x0401) return 0x0451;  // Ё -> ё
    return cp;
}

// Convert code point to UTF-8 and append to string
static void appendCodePointUtf8(std::string& str, char32_t cp, bool toLower) {
    if (toLower) cp = toLowerPooled(cp);

    if (cp < 0x80) {
        str += static_cast<char>(cp);
    } else if (cp < 0x800) {
        str += static_cast<char>(0xC0 | (cp >> 6));
        str += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        str += static_cast<char>(0xE0 | (cp >> 12));
        str += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        str += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        str += static_cast<char>(0xF0 | (cp >> 18));
        str += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        str += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        str += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Read children position
static int readChildrenPosPooled(const uint8_t* buffer, uint8_t flags, int* pos, size_t size, bool* ok) {
    int base = *pos;
    int offset = 0;

    switch (flags & MASK_CHILDREN_POSITION_TYPE) {
        case FLAG_CHILDREN_POSITION_TYPE_ONEBYTE:
            if (!canReadPooled(*pos, 1, size)) { *ok = false; return NOT_A_DICT_POS; }
            offset = buffer[(*pos)++];
            break;
        case FLAG_CHILDREN_POSITION_TYPE_TWOBYTES:
            if (!canReadPooled(*pos, 2, size)) { *ok = false; return NOT_A_DICT_POS; }
            offset = (buffer[*pos] << 8) | buffer[*pos + 1];
            *pos += 2;
            break;
        case FLAG_CHILDREN_POSITION_TYPE_THREEBYTES:
            if (!canReadPooled(*pos, 3, size)) { *ok = false; return NOT_A_DICT_POS; }
            offset = (buffer[*pos] << 16) | (buffer[*pos + 1] << 8) | buffer[*pos + 2];
            *pos += 3;
            break;
        default:
            *ok = true;
            return NOT_A_DICT_POS;
    }

    *ok = true;
    return base + offset;
}

// Skip shortcuts section
static bool skipShortcutsPooled(const uint8_t* buffer, int* pos, size_t size) {
    if (!canReadPooled(*pos, 2, size)) return false;
    int shortcutSize = (buffer[*pos] << 8) | buffer[*pos + 1];
    *pos += 2;
    if (!canReadPooled(*pos, shortcutSize, size)) return false;
    *pos += shortcutSize;
    return true;
}

// Skip bigrams section
static bool skipBigramsPooled(const uint8_t* buffer, int* pos, size_t size) {
    for (int i = 0; i < 10000; i++) {
        if (!canReadPooled(*pos, 1, size)) return false;
        uint8_t bigramFlags = buffer[(*pos)++];
        if (!canReadPooled(*pos, 1, size)) return false;
        (*pos)++;  // Skip probability
        int targetFlags = (bigramFlags >> 4) & 0x03;
        int bytesToSkip = (targetFlags == 0) ? 1 : (targetFlags == 1) ? 2 : 3;
        if (!canReadPooled(*pos, bytesToSkip, size)) return false;
        *pos += bytesToSkip;
        if ((bigramFlags & 0x80) != 0) break;
    }
    return true;
}

// Read PtNodeArray size
static int readNodeArraySizePooled(const uint8_t* buffer, int* pos, size_t size, bool* ok) {
    if (!canReadPooled(*pos, 1, size)) { *ok = false; return 0; }
    uint8_t firstByte = buffer[(*pos)++];
    if (firstByte < 0x80) {
        *ok = true;
        return firstByte;
    } else {
        if (!canReadPooled(*pos, 1, size)) { *ok = false; return 0; }
        *ok = true;
        return ((firstByte & 0x7F) << 8) | buffer[(*pos)++];
    }
}

// Node info structure
struct PtNodeInfoPooled {
    uint8_t flags;
    std::string word;  // UTF-8 encoded
    int probability;
    int childrenPos;
    int siblingPos;
    bool isTerminal;
    bool isNotAWord;
    bool isValid;
};

// Read a single PT node
static PtNodeInfoPooled readPtNodePooled(const uint8_t* buffer, int pos, size_t size) {
    PtNodeInfoPooled info;
    info.probability = 0;
    info.childrenPos = NOT_A_DICT_POS;
    info.isNotAWord = false;
    info.isValid = false;

    int readPos = pos;

    if (!canReadPooled(readPos, 1, size)) return info;
    info.flags = buffer[readPos++];
    info.isTerminal = (info.flags & FLAG_IS_TERMINAL) != 0;
    info.isNotAWord = (info.flags & FLAG_IS_NOT_A_WORD) != 0;

    // Read code points and convert to UTF-8
    bool ok = true;
    if (info.flags & FLAG_HAS_MULTIPLE_CHARS) {
        // Multiple chars
        int cp = readCodePointPooled(buffer, &readPos, size, &ok);
        while (ok && cp != NOT_A_CODE_POINT && info.word.length() < MAX_WORD_LENGTH_POOLED * 4) {
            appendCodePointUtf8(info.word, cp, true);  // toLower
            cp = readCodePointPooled(buffer, &readPos, size, &ok);
        }
    } else {
        // Single char
        int cp = readCodePointPooled(buffer, &readPos, size, &ok);
        if (ok && cp != NOT_A_CODE_POINT) {
            appendCodePointUtf8(info.word, cp, true);
        }
    }
    if (!ok) return info;

    // Read probability
    if (info.isTerminal) {
        if (!canReadPooled(readPos, 1, size)) return info;
        info.probability = buffer[readPos++];
    }

    // Read children position
    if ((info.flags & MASK_CHILDREN_POSITION_TYPE) != FLAG_CHILDREN_POSITION_TYPE_NOPOSITION) {
        info.childrenPos = readChildrenPosPooled(buffer, info.flags, &readPos, size, &ok);
        if (!ok) return info;
    }

    // Skip shortcuts
    if (info.flags & FLAG_HAS_SHORTCUT_TARGETS) {
        if (!skipShortcutsPooled(buffer, &readPos, size)) return info;
    }

    // Skip bigrams
    if (info.flags & FLAG_HAS_BIGRAMS) {
        if (!skipBigramsPooled(buffer, &readPos, size)) return info;
    }

    info.siblingPos = readPos;
    info.isValid = true;
    return info;
}

/**
 * Load OpenBoard binary dictionary into pooled trie
 * Optimized: no debug logging, reuses strings, minimal allocations
 */
bool loadBinaryDictPooled(const std::string& path, TriePooled& trie) {
    auto startTime = std::chrono::steady_clock::now();

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        OH_LOG_ERROR(LOG_APP, "loadBinaryDictPooled: cannot open %s", path.c_str());
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(fileSize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        OH_LOG_ERROR(LOG_APP, "loadBinaryDictPooled: read failed");
        return false;
    }
    file.close();

    auto readTime = std::chrono::steady_clock::now();
    auto readDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(readTime - startTime).count();
    OH_LOG_INFO(LOG_APP, "loadBinaryDictPooled: read %zu bytes in %lld ms", fileSize, readDurationMs);

    // Parse header
    if (fileSize < 12) return false;

    uint32_t magic = readUint32BEPooled(data.data(), 0);
    if (magic != DICT_MAGIC_NUMBER) {
        OH_LOG_ERROR(LOG_APP, "loadBinaryDictPooled: invalid magic 0x%08X", magic);
        return false;
    }

    uint32_t headerSize = readUint32BEPooled(data.data(), 8);
    if (headerSize >= fileSize) return false;

    OH_LOG_INFO(LOG_APP, "loadBinaryDictPooled: headerSize=%u, fileSize=%zu", headerSize, fileSize);

    trie.clear();

    // Stack for iterative traversal
    struct StackFrame {
        int nodeArrayPos;
        std::string prefix;
        int nodeIndex;
        int nodeCount;
        int currentPos;
    };

    std::vector<StackFrame> stack;
    stack.reserve(256);

    int pos = headerSize;
    bool ok = true;
    int nodeCount = readNodeArraySizePooled(data.data(), &pos, fileSize, &ok);

    OH_LOG_INFO(LOG_APP, "loadBinaryDictPooled: initial nodeCount=%d, ok=%d, pos=%d", nodeCount, ok ? 1 : 0, pos);

    if (!ok || nodeCount <= 0) {
        OH_LOG_ERROR(LOG_APP, "loadBinaryDictPooled: FAILED to read initial node count (ok=%d, nodeCount=%d)", ok ? 1 : 0, nodeCount);
        return false;
    }

    stack.push_back({static_cast<int>(headerSize), "", 0, nodeCount, pos});

    constexpr int MAX_WORDS = 2000000;
    constexpr int MAX_ITERATIONS = 5000000;
    int wordsLoaded = 0;
    int iterations = 0;

    while (!stack.empty() && wordsLoaded < MAX_WORDS && iterations < MAX_ITERATIONS) {
        iterations++;

        if (stack.size() > 64) {
            stack.pop_back();
            continue;
        }

        StackFrame& frame = stack.back();

        if (frame.nodeIndex >= frame.nodeCount) {
            stack.pop_back();
            continue;
        }

        if (frame.currentPos < 0 || frame.currentPos >= static_cast<int>(fileSize)) {
            stack.pop_back();
            continue;
        }

        PtNodeInfoPooled nodeInfo = readPtNodePooled(data.data(), frame.currentPos, fileSize);

        if (!nodeInfo.isValid || nodeInfo.word.empty()) {
            frame.nodeIndex++;
            if (nodeInfo.siblingPos > frame.currentPos && nodeInfo.siblingPos < static_cast<int>(fileSize)) {
                frame.currentPos = nodeInfo.siblingPos;
            } else {
                frame.currentPos++;
            }
            continue;
        }

        // Build full word
        std::string fullWord = frame.prefix + nodeInfo.word;

        // Insert if terminal and valid word
        if (nodeInfo.isTerminal && !nodeInfo.isNotAWord && !fullWord.empty()) {
            trie.insert(fullWord, nodeInfo.probability);
            wordsLoaded++;

            // Log first 5 words for debugging
            if (wordsLoaded <= 5) {
                OH_LOG_INFO(LOG_APP, "loadBinaryDictPooled: word[%d]=\"%s\" prob=%d",
                            wordsLoaded, fullWord.c_str(), nodeInfo.probability);
            }

            if (wordsLoaded % 50000 == 0) {
                OH_LOG_INFO(LOG_APP, "loadBinaryDictPooled: %d words, %zu nodes",
                            wordsLoaded, trie.getPool().usedCount());
            }
        }

        // Move to sibling
        frame.nodeIndex++;
        if (nodeInfo.siblingPos > frame.currentPos && nodeInfo.siblingPos < static_cast<int>(fileSize)) {
            frame.currentPos = nodeInfo.siblingPos;
        } else {
            frame.currentPos++;
        }

        // Push children
        if (nodeInfo.childrenPos != NOT_A_DICT_POS &&
            nodeInfo.childrenPos > 0 &&
            nodeInfo.childrenPos < static_cast<int>(fileSize)) {

            int childPos = nodeInfo.childrenPos;
            bool childOk = true;
            int childNodeCount = readNodeArraySizePooled(data.data(), &childPos, fileSize, &childOk);

            if (childOk && childNodeCount > 0 && childNodeCount <= 10000) {
                stack.push_back({nodeInfo.childrenPos, fullWord, 0, childNodeCount, childPos});
            }
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    auto totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    OH_LOG_INFO(LOG_APP, "loadBinaryDictPooled: DONE - %d words, %zu nodes, %zu bytes, %lld ms total",
                wordsLoaded, trie.getPool().usedCount(), trie.getMemoryUsage(), totalDurationMs);

    return wordsLoaded > 0;
}

/**
 * Load dictionary from file descriptor using memory-mapping (mmap)
 * This is INSTANT - no file reading, data accessed on-demand by OS
 * 
 * @param fd File descriptor (from rawfile)
 * @param offset Offset within file where data starts
 * @param length Length of data to map
 * @return true on success
 */
bool TriePooled::loadFromFd(int fd, size_t offset, size_t length) {
    auto startTime = std::chrono::steady_clock::now();

    OH_LOG_INFO(LOG_APP, "loadFromFd: fd=%d, offset=%zu, length=%zu (%.1f MB)",
                fd, offset, length, static_cast<double>(length) / (1024 * 1024));

    if (fd < 0 || length == 0) {
        OH_LOG_ERROR(LOG_APP, "loadFromFd: invalid fd=%d or length=%zu", fd, length);
        return false;
    }

    // Warn if dictionary is very large (>50 MB) - loading will be slow
    if (length > 50 * 1024 * 1024) {
        OH_LOG_WARN(LOG_APP, "loadFromFd: WARNING - large dictionary (%.1f MB), loading may take a while",
                    static_cast<double>(length) / (1024 * 1024));
    }
    
    // mmap offset must be page-aligned.
    long pageSizeLong = sysconf(_SC_PAGE_SIZE);
    size_t pageSize = pageSizeLong > 0 ? static_cast<size_t>(pageSizeLong) : static_cast<size_t>(4096);
    size_t alignedOffset = offset - (offset % pageSize);
    size_t delta = offset - alignedOffset;
    if (length > std::numeric_limits<size_t>::max() - delta) {
        OH_LOG_ERROR(LOG_APP, "loadFromFd: length overflow after alignment");
        return false;
    }
    size_t mapLength = length + delta;

    // Memory-map the file - this is INSTANT, doesn't read the file.
    // MAP_PRIVATE = copy-on-write, safe for read-only access.
    void* mapped = mmap(nullptr, mapLength, PROT_READ, MAP_PRIVATE, fd, alignedOffset);
    if (mapped == MAP_FAILED) {
        OH_LOG_ERROR(LOG_APP, "loadFromFd: mmap failed, errno=%d", errno);
        return false;
    }
    
    auto mmapTime = std::chrono::steady_clock::now();
    auto mmapDurationUs = std::chrono::duration_cast<std::chrono::microseconds>(mmapTime - startTime).count();
    OH_LOG_INFO(LOG_APP, "loadFromFd: mmap completed in %lld us (instant!)", mmapDurationUs);
    
    // Advise kernel we'll read sequentially.
    madvise(mapped, mapLength, MADV_SEQUENTIAL);
    
    const uint8_t* data = static_cast<const uint8_t*>(mapped) + delta;
    size_t fileSize = length;
    
    // Parse header (same as loadBinaryDictPooled)
    if (fileSize < 12) {
        munmap(mapped, mapLength);
        return false;
    }
    
    uint32_t magic = readUint32BEPooled(data, 0);
    if (magic != DICT_MAGIC_NUMBER) {
        OH_LOG_ERROR(LOG_APP, "loadFromFd: invalid magic 0x%08X", magic);
        munmap(mapped, mapLength);
        return false;
    }
    
    uint32_t headerSize = readUint32BEPooled(data, 8);
    if (headerSize >= fileSize) {
        munmap(mapped, mapLength);
        return false;
    }
    
    clear();
    
    // Stack for iterative traversal (same algorithm as loadBinaryDictPooled)
    struct StackFrame {
        int nodeArrayPos;
        std::string prefix;
        int nodeIndex;
        int nodeCount;
        int currentPos;
    };
    
    std::vector<StackFrame> stack;
    stack.reserve(256);
    
    int pos = headerSize;
    bool ok = true;
    int nodeCount = readNodeArraySizePooled(data, &pos, fileSize, &ok);
    
    if (!ok || nodeCount <= 0) {
        OH_LOG_ERROR(LOG_APP, "loadFromFd: failed to read node count");
        munmap(mapped, mapLength);
        return false;
    }
    
    stack.push_back({static_cast<int>(headerSize), "", 0, nodeCount, pos});

    // Limits for mobile keyboard - 200k words is plenty, 10 second timeout
    constexpr int MAX_WORDS = 200000;
    constexpr int MAX_ITERATIONS = 3000000;
    constexpr int64_t MAX_LOAD_TIME_MS = 10000;  // 10 seconds max
    int wordsLoaded = 0;
    int iterations = 0;

    OH_LOG_INFO(LOG_APP, "loadFromFd: starting parse loop, headerSize=%u, nodeCount=%d (max %d words, %d ms timeout)",
                headerSize, nodeCount, MAX_WORDS, static_cast<int>(MAX_LOAD_TIME_MS));

    try {
        auto loopStartTime = std::chrono::steady_clock::now();

        while (!stack.empty() && wordsLoaded < MAX_WORDS && iterations < MAX_ITERATIONS) {
            iterations++;

            // Check time limit every 10000 iterations
            if (iterations % 10000 == 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - loopStartTime).count();
                if (elapsed > MAX_LOAD_TIME_MS) {
                    OH_LOG_WARN(LOG_APP, "loadFromFd: TIME LIMIT reached (%lld ms), stopping at %d words",
                                elapsed, wordsLoaded);
                    break;
                }
            }

            if (stack.size() > 64) {
                stack.pop_back();
                continue;
            }

            StackFrame& frame = stack.back();

            if (frame.nodeIndex >= frame.nodeCount) {
                stack.pop_back();
                continue;
            }

            if (frame.currentPos < 0 || frame.currentPos >= static_cast<int>(fileSize)) {
                stack.pop_back();
                continue;
            }

            PtNodeInfoPooled nodeInfo = readPtNodePooled(data, frame.currentPos, fileSize);

            if (!nodeInfo.isValid || nodeInfo.word.empty()) {
                frame.nodeIndex++;
                if (nodeInfo.siblingPos > frame.currentPos && nodeInfo.siblingPos < static_cast<int>(fileSize)) {
                    frame.currentPos = nodeInfo.siblingPos;
                } else {
                    frame.currentPos++;
                }
                continue;
            }

            std::string fullWord = frame.prefix + nodeInfo.word;

            if (nodeInfo.isTerminal && !nodeInfo.isNotAWord && !fullWord.empty()) {
                insert(fullWord, nodeInfo.probability);
                wordsLoaded++;

                // Progress logging - more frequent at start, then every 50k
                if (wordsLoaded == 1000 || wordsLoaded == 5000 || wordsLoaded == 10000 ||
                    wordsLoaded == 25000 || wordsLoaded % 50000 == 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
                    OH_LOG_INFO(LOG_APP, "loadFromFd: %d words, %zu nodes, %lld ms",
                                wordsLoaded, pool_.usedCount(), elapsed);
                }
            }

            frame.nodeIndex++;
            if (nodeInfo.siblingPos > frame.currentPos && nodeInfo.siblingPos < static_cast<int>(fileSize)) {
                frame.currentPos = nodeInfo.siblingPos;
            } else {
                frame.currentPos++;
            }

            if (nodeInfo.childrenPos != NOT_A_DICT_POS &&
                nodeInfo.childrenPos > 0 &&
                nodeInfo.childrenPos < static_cast<int>(fileSize)) {

                int childPos = nodeInfo.childrenPos;
                bool childOk = true;
                int childNodeCount = readNodeArraySizePooled(data, &childPos, fileSize, &childOk);

                if (childOk && childNodeCount > 0 && childNodeCount <= 10000) {
                    stack.push_back({nodeInfo.childrenPos, fullWord, 0, childNodeCount, childPos});
                }
            }
        }

        // Log why the loop exited
        const char* exitReason = "unknown";
        if (stack.empty()) {
            exitReason = "completed (all nodes parsed)";
        } else if (wordsLoaded >= MAX_WORDS) {
            exitReason = "MAX_WORDS limit reached";
        } else if (iterations >= MAX_ITERATIONS) {
            exitReason = "MAX_ITERATIONS limit reached";
        } else {
            exitReason = "TIME_LIMIT reached";
        }
        OH_LOG_INFO(LOG_APP, "loadFromFd: loop exited - %s, words=%d, iter=%d",
                    exitReason, wordsLoaded, iterations);

    } catch (const std::bad_alloc& e) {
        OH_LOG_ERROR(LOG_APP, "loadFromFd: MEMORY ALLOCATION FAILED at %d words, %zu nodes: %s",
                     wordsLoaded, pool_.usedCount(), e.what());
        munmap(mapped, mapLength);
        return false;
    } catch (const std::exception& e) {
        OH_LOG_ERROR(LOG_APP, "loadFromFd: EXCEPTION at %d words: %s", wordsLoaded, e.what());
        munmap(mapped, mapLength);
        return false;
    } catch (...) {
        OH_LOG_ERROR(LOG_APP, "loadFromFd: UNKNOWN EXCEPTION at %d words", wordsLoaded);
        munmap(mapped, mapLength);
        return false;
    }
    
    // Unmap the file
    munmap(mapped, mapLength);
    
    auto endTime = std::chrono::steady_clock::now();
    auto totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    OH_LOG_INFO(LOG_APP, "loadFromFd: DONE - %d words, %zu nodes, %lld ms (mmap: %lld us)",
                wordsLoaded, pool_.usedCount(), totalDurationMs, mmapDurationUs);
    
    return wordsLoaded > 0;
}

} // namespace hoskey
