/**
 * Binary Dictionary Reader for OpenBoard .dict format
 * Format: Patricia Trie v4 (version 54)
 *
 * Ported from OpenBoard (Apache 2.0 License)
 * Based on: patricia_trie_reading_utils.cpp, byte_array_utils.h
 *
 * SAFETY: All read operations include bounds checking to prevent crashes
 */

#include "trie.h"
#include "trie_node.h"
#include <fstream>
#include <cstring>
#include <vector>
#include <string>

namespace hoskey {

// ============================================================================
// Constants from OpenBoard
// ============================================================================

constexpr uint32_t DICT_MAGIC_NUMBER = 0x9bc13afe;
constexpr int NOT_A_CODE_POINT = -1;
constexpr int NOT_A_DICT_POS = -1;
constexpr int MAX_WORD_LENGTH = 48;
constexpr int MAX_RECURSION_DEPTH = 64;  // Prevent stack overflow

// Code point encoding
constexpr uint8_t CHARACTER_ARRAY_TERMINATOR = 0x1F;
constexpr uint8_t MINIMUM_ONE_BYTE_CHARACTER_VALUE = 0x20;
constexpr uint8_t MAXIMUM_ONE_BYTE_CHARACTER_VALUE = 0xFF;

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
constexpr uint8_t FLAG_IS_POSSIBLY_OFFENSIVE = 0x01;

// ============================================================================
// Safe byte reading utilities (big-endian) with bounds checking
// ============================================================================

static inline bool canRead(int pos, int bytesNeeded, size_t size) {
    return pos >= 0 && (pos + bytesNeeded) <= static_cast<int>(size);
}

static inline uint32_t readUint32BE(const uint8_t* buf, int pos, size_t size, bool* ok) {
    if (!canRead(pos, 4, size)) {
        *ok = false;
        return 0;
    }
    *ok = true;
    return (static_cast<uint32_t>(buf[pos]) << 24) |
           (static_cast<uint32_t>(buf[pos + 1]) << 16) |
           (static_cast<uint32_t>(buf[pos + 2]) << 8) |
           static_cast<uint32_t>(buf[pos + 3]);
}

static inline uint32_t readUint24BE(const uint8_t* buf, int pos, size_t size, bool* ok) {
    if (!canRead(pos, 3, size)) {
        *ok = false;
        return 0;
    }
    *ok = true;
    return (static_cast<uint32_t>(buf[pos]) << 16) |
           (static_cast<uint32_t>(buf[pos + 1]) << 8) |
           static_cast<uint32_t>(buf[pos + 2]);
}

static inline uint16_t readUint16BE(const uint8_t* buf, int pos, size_t size, bool* ok) {
    if (!canRead(pos, 2, size)) {
        *ok = false;
        return 0;
    }
    *ok = true;
    return (static_cast<uint16_t>(buf[pos]) << 8) |
           static_cast<uint16_t>(buf[pos + 1]);
}

static inline uint8_t readUint8Safe(const uint8_t* buf, int pos, size_t size, bool* ok) {
    if (!canRead(pos, 1, size)) {
        *ok = false;
        return 0;
    }
    *ok = true;
    return buf[pos];
}

// ============================================================================
// Code point reading with bounds checking
// ============================================================================

static int readCodePointAndAdvance(const uint8_t* buffer, int* pos, size_t size, bool* ok) {
    if (!canRead(*pos, 1, size)) {
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
            if (!canRead(*pos, 3, size)) {
                *ok = false;
                return NOT_A_CODE_POINT;
            }
            int codePoint = (static_cast<uint32_t>(buffer[*pos]) << 16) |
                           (static_cast<uint32_t>(buffer[*pos + 1]) << 8) |
                           static_cast<uint32_t>(buffer[*pos + 2]);
            *pos += 3;

            // Validate code point is in valid Unicode range
            if (codePoint < 0 || codePoint > 0x10FFFF ||
                (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
                *ok = false;
                return NOT_A_CODE_POINT;
            }

            *ok = true;
            return codePoint;
        }
    } else {
        (*pos)++;
        *ok = true;
        return firstByte;
    }
}

// Read string of code points until terminator
static std::u32string readStringAndAdvance(const uint8_t* buffer, int* pos, size_t size, bool* ok, int maxLength = MAX_WORD_LENGTH) {
    std::u32string result;
    result.reserve(maxLength);

    bool readOk = true;
    int codePoint = readCodePointAndAdvance(buffer, pos, size, &readOk);
    if (!readOk) {
        *ok = false;
        return result;
    }

    while (codePoint != NOT_A_CODE_POINT && static_cast<int>(result.length()) < maxLength) {
        result += static_cast<char32_t>(codePoint);
        codePoint = readCodePointAndAdvance(buffer, pos, size, &readOk);
        if (!readOk) {
            *ok = false;
            return result;
        }
    }

    *ok = true;
    return result;
}

// Check if code point is valid Unicode
static inline bool isValidCodePoint(char32_t cp) {
    // Valid Unicode range: 0x0000-0x10FFFF, excluding surrogates 0xD800-0xDFFF
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;  // Surrogates
    return true;
}

// Convert UTF-32 to UTF-8 with validation
static std::string utf32ToUtf8(const std::u32string& utf32) {
    std::string result;
    result.reserve(utf32.length() * 3);

    for (char32_t cp : utf32) {
        // Skip invalid code points
        if (!isValidCodePoint(cp)) {
            continue;
        }

        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    return result;
}

// ============================================================================
// Patricia Trie Node reading
// ============================================================================

struct PtNodeInfo {
    uint8_t flags;
    std::u32string codePoints;
    int probability;
    int childrenPos;
    int siblingPos;
    bool isTerminal;
    bool isNotAWord;
    bool isPossiblyOffensive;
    bool isValid;  // Indicates if parsing succeeded
};

static inline bool hasMultipleChars(uint8_t flags) {
    return (flags & FLAG_HAS_MULTIPLE_CHARS) != 0;
}

static inline bool isTerminal(uint8_t flags) {
    return (flags & FLAG_IS_TERMINAL) != 0;
}

static inline bool hasChildrenInFlags(uint8_t flags) {
    return (flags & MASK_CHILDREN_POSITION_TYPE) != FLAG_CHILDREN_POSITION_TYPE_NOPOSITION;
}

static inline bool hasShortcutTargets(uint8_t flags) {
    return (flags & FLAG_HAS_SHORTCUT_TARGETS) != 0;
}

static inline bool hasBigrams(uint8_t flags) {
    return (flags & FLAG_HAS_BIGRAMS) != 0;
}

static int readChildrenPosition(const uint8_t* buffer, uint8_t flags, int* pos, size_t size, bool* ok) {
    int base = *pos;
    int offset = 0;

    switch (flags & MASK_CHILDREN_POSITION_TYPE) {
        case FLAG_CHILDREN_POSITION_TYPE_ONEBYTE:
            if (!canRead(*pos, 1, size)) {
                *ok = false;
                return NOT_A_DICT_POS;
            }
            offset = buffer[(*pos)++];
            break;
        case FLAG_CHILDREN_POSITION_TYPE_TWOBYTES:
            if (!canRead(*pos, 2, size)) {
                *ok = false;
                return NOT_A_DICT_POS;
            }
            offset = (static_cast<uint16_t>(buffer[*pos]) << 8) | buffer[*pos + 1];
            *pos += 2;
            break;
        case FLAG_CHILDREN_POSITION_TYPE_THREEBYTES:
            if (!canRead(*pos, 3, size)) {
                *ok = false;
                return NOT_A_DICT_POS;
            }
            offset = (static_cast<uint32_t>(buffer[*pos]) << 16) |
                    (static_cast<uint32_t>(buffer[*pos + 1]) << 8) |
                    buffer[*pos + 2];
            *pos += 3;
            break;
        default:
            *ok = true;
            return NOT_A_DICT_POS;
    }

    *ok = true;
    return base + offset;
}

// Skip shortcuts section with bounds checking
static bool skipShortcuts(const uint8_t* buffer, int* pos, size_t size) {
    if (!canRead(*pos, 2, size)) {
        return false;
    }
    int shortcutSize = (static_cast<uint16_t>(buffer[*pos]) << 8) | buffer[*pos + 1];
    *pos += 2;

    if (!canRead(*pos, shortcutSize, size)) {
        return false;
    }
    *pos += shortcutSize;
    return true;
}

// Skip bigrams section with bounds checking
static bool skipBigrams(const uint8_t* buffer, int* pos, size_t size) {
    int maxIterations = 10000;  // Safety limit
    int iterations = 0;

    while (iterations++ < maxIterations) {
        if (!canRead(*pos, 1, size)) {
            return false;
        }
        uint8_t bigramFlags = buffer[(*pos)++];

        // Skip probability
        if (!canRead(*pos, 1, size)) {
            return false;
        }
        (*pos)++;

        // Read target position (1-3 bytes based on flags)
        int targetFlags = (bigramFlags >> 4) & 0x03;
        int bytesToSkip = (targetFlags == 0) ? 1 : (targetFlags == 1) ? 2 : 3;

        if (!canRead(*pos, bytesToSkip, size)) {
            return false;
        }
        *pos += bytesToSkip;

        // Check if this is the last bigram
        if ((bigramFlags & 0x80) != 0) {
            break;
        }
    }
    return iterations < maxIterations;
}

static PtNodeInfo readPtNode(const uint8_t* buffer, int pos, size_t size) {
    PtNodeInfo info;
    info.probability = 0;
    info.childrenPos = NOT_A_DICT_POS;
    info.isNotAWord = false;
    info.isPossiblyOffensive = false;
    info.isValid = false;

    int readPos = pos;

    // Read flags with bounds check
    if (!canRead(readPos, 1, size)) {
        return info;
    }
    info.flags = buffer[readPos++];
    info.isTerminal = isTerminal(info.flags);
    info.isNotAWord = (info.flags & FLAG_IS_NOT_A_WORD) != 0;
    info.isPossiblyOffensive = (info.flags & FLAG_IS_POSSIBLY_OFFENSIVE) != 0;

    // Read code points
    bool ok = true;
    if (hasMultipleChars(info.flags)) {
        info.codePoints = readStringAndAdvance(buffer, &readPos, size, &ok);
        if (!ok) return info;
    } else {
        int cp = readCodePointAndAdvance(buffer, &readPos, size, &ok);
        if (!ok) return info;
        if (cp != NOT_A_CODE_POINT) {
            info.codePoints += static_cast<char32_t>(cp);
        }
    }

    // Read probability if terminal
    if (info.isTerminal) {
        if (!canRead(readPos, 1, size)) {
            return info;
        }
        info.probability = buffer[readPos++];
    }

    // Read children position
    if (hasChildrenInFlags(info.flags)) {
        info.childrenPos = readChildrenPosition(buffer, info.flags, &readPos, size, &ok);
        if (!ok) return info;
    }

    // Skip shortcuts if present
    if (hasShortcutTargets(info.flags)) {
        if (!skipShortcuts(buffer, &readPos, size)) {
            return info;
        }
    }

    // Skip bigrams if present
    if (hasBigrams(info.flags)) {
        if (!skipBigrams(buffer, &readPos, size)) {
            return info;
        }
    }

    info.siblingPos = readPos;
    info.isValid = true;
    return info;
}

// Read PtNodeArray size with bounds checking
static int readPtNodeArraySize(const uint8_t* buffer, int* pos, size_t size, bool* ok) {
    if (!canRead(*pos, 1, size)) {
        *ok = false;
        return 0;
    }

    uint8_t firstByte = buffer[(*pos)++];
    if (firstByte < 0x80) {
        *ok = true;
        return firstByte;
    } else {
        if (!canRead(*pos, 1, size)) {
            *ok = false;
            return 0;
        }
        *ok = true;
        return ((firstByte & 0x7F) << 8) | buffer[(*pos)++];
    }
}

// ============================================================================
// Dictionary header parsing
// ============================================================================

struct DictHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t headerSize;
    std::string locale;
    bool isValid;

    DictHeader() : magic(0), version(0), flags(0), headerSize(0), isValid(false) {}
};

static DictHeader parseHeader(const uint8_t* data, size_t size) {
    DictHeader header;

    if (size < 12) {
        return header;
    }

    bool ok = true;
    header.magic = readUint32BE(data, 0, size, &ok);
    if (!ok || header.magic != DICT_MAGIC_NUMBER) {
        return header;
    }

    header.version = readUint16BE(data, 4, size, &ok);
    if (!ok) return header;

    header.flags = readUint16BE(data, 6, size, &ok);
    if (!ok) return header;

    header.headerSize = readUint32BE(data, 8, size, &ok);
    if (!ok) return header;

    // Validate header size
    if (header.headerSize > size) {
        return header;
    }

    // Parse attributes
    int pos = 12;
    while (pos < static_cast<int>(header.headerSize) && pos < static_cast<int>(size)) {
        std::string key, value;

        while (pos < static_cast<int>(size) && data[pos] != 0x1f && data[pos] != 0) {
            key += static_cast<char>(data[pos++]);
        }
        if (pos < static_cast<int>(size)) pos++;

        while (pos < static_cast<int>(size) && data[pos] != 0x1f && data[pos] != 0) {
            value += static_cast<char>(data[pos++]);
        }
        if (pos < static_cast<int>(size)) pos++;

        if (key == "locale") {
            header.locale = value;
        }
    }

    header.isValid = true;
    return header;
}

// ============================================================================
// Main dictionary loading
// ============================================================================

class BinaryDictLoader {
public:
    BinaryDictLoader(const uint8_t* data, size_t size, int trieStartPos)
        : data_(data), size_(size), trieStartPos_(trieStartPos) {}

    void loadIntoTrie(Trie* trie) {
        // Use iterative approach with explicit stack to avoid stack overflow
        // Stack frame: (nodeArrayPos, prefix, nodeIndex, nodeCount, currentPos)
        struct StackFrame {
            int nodeArrayPos;
            std::u32string prefix;
            int nodeIndex;
            int nodeCount;
            int currentPos;
        };

        std::vector<StackFrame> stack;
        stack.reserve(256);  // Pre-allocate to reduce reallocations

        // Initial frame
        if (trieStartPos_ < 0 || trieStartPos_ >= static_cast<int>(size_)) {
            return;
        }

        int pos = trieStartPos_;
        bool ok = true;
        int nodeCount = readPtNodeArraySize(data_, &pos, size_, &ok);
        if (!ok || nodeCount <= 0 || nodeCount > 10000) {
            return;
        }

        stack.push_back({trieStartPos_, std::u32string(), 0, nodeCount, pos});

        constexpr int MAX_STACK_DEPTH = 64;    // Limit to prevent memory issues
        constexpr int MAX_WORDS = 250000;     // Increased for larger dictionaries (ru, en have 150k-200k words)
        constexpr int MAX_ITERATIONS = 1000000; // Safety limit for loop iterations
        int wordsLoaded = 0;
        int iterations = 0;

        while (!stack.empty() && wordsLoaded < MAX_WORDS && iterations < MAX_ITERATIONS) {
            iterations++;
            // Limit stack depth to prevent memory exhaustion
            if (static_cast<int>(stack.size()) > MAX_STACK_DEPTH) {
                stack.pop_back();
                continue;
            }

            StackFrame& frame = stack.back();

            // Process next node in current array
            if (frame.nodeIndex >= frame.nodeCount) {
                stack.pop_back();
                continue;
            }

            // Validate position before reading node
            if (frame.currentPos < 0 || frame.currentPos >= static_cast<int>(size_)) {
                stack.pop_back();
                continue;
            }

            PtNodeInfo nodeInfo = readPtNode(data_, frame.currentPos, size_);

            // Check if node parsing succeeded
            if (!nodeInfo.isValid) {
                stack.pop_back();
                continue;
            }

            // Validate code points before building word prefix
            bool validCodePoints = true;
            for (char32_t cp : nodeInfo.codePoints) {
                if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                    validCodePoints = false;
                    break;
                }
            }

            if (!validCodePoints || nodeInfo.codePoints.empty()) {
                // Skip this node but continue with siblings
                frame.nodeIndex++;
                int nextSiblingPos = nodeInfo.siblingPos;
                if (nextSiblingPos <= frame.currentPos || nextSiblingPos >= static_cast<int>(size_)) {
                    frame.nodeIndex = frame.nodeCount;
                } else {
                    frame.currentPos = nextSiblingPos;
                }
                continue;
            }

            // Build word prefix with length limit
            std::u32string wordPrefix = frame.prefix;
            if (wordPrefix.length() + nodeInfo.codePoints.length() > MAX_WORD_LENGTH) {
                // Prefix too long, skip this branch
                frame.nodeIndex++;
                int nextSiblingPos = nodeInfo.siblingPos;
                if (nextSiblingPos <= frame.currentPos || nextSiblingPos >= static_cast<int>(size_)) {
                    frame.nodeIndex = frame.nodeCount;
                } else {
                    frame.currentPos = nextSiblingPos;
                }
                continue;
            }
            wordPrefix += nodeInfo.codePoints;

            // If terminal and it's a valid word, add to trie
            if (nodeInfo.isTerminal && !nodeInfo.isNotAWord) {
                std::string word = utf32ToUtf8(wordPrefix);
                if (!word.empty() && word.length() <= MAX_WORD_LENGTH * 4) {  // UTF-8 can be up to 4 bytes per char
                    trie->insert(word, nodeInfo.probability);
                    wordsLoaded++;
                }
            }

            // Update current frame for next iteration (move to sibling)
            frame.nodeIndex++;
            int nextSiblingPos = nodeInfo.siblingPos;

            // Validate sibling position
            if (nextSiblingPos <= frame.currentPos || nextSiblingPos >= static_cast<int>(size_)) {
                // Invalid sibling, skip remaining nodes in this array
                frame.nodeIndex = frame.nodeCount;
            } else {
                frame.currentPos = nextSiblingPos;
            }

            // Push children to stack if present (process after siblings via stack)
            if (nodeInfo.childrenPos != NOT_A_DICT_POS &&
                nodeInfo.childrenPos > 0 &&
                nodeInfo.childrenPos < static_cast<int>(size_)) {

                int childPos = nodeInfo.childrenPos;
                bool childOk = true;
                int childNodeCount = readPtNodeArraySize(data_, &childPos, size_, &childOk);

                if (childOk && childNodeCount > 0 && childNodeCount <= 10000) {
                    stack.push_back({nodeInfo.childrenPos, wordPrefix, 0, childNodeCount, childPos});
                }
            }
        }
    }

private:
    const uint8_t* data_;
    size_t size_;
    int trieStartPos_;
};

// ============================================================================
// Public API
// ============================================================================

bool Trie::loadFromBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < 100) {
        return false;
    }

    // Read entire file
    std::vector<uint8_t> data(fileSize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        return false;
    }

    // Parse header
    DictHeader header = parseHeader(data.data(), data.size());
    if (!header.isValid) {
        return false;
    }

    // Validate header size is within file bounds
    if (header.headerSize >= data.size()) {
        return false;
    }

    // Clear existing data
    clear();

    // Load trie data
    BinaryDictLoader loader(data.data(), data.size(), header.headerSize);
    loader.loadIntoTrie(this);

    return wordCount_ > 0;
}

} // namespace hoskey
