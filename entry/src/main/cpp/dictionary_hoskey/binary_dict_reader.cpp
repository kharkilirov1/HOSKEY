/**
 * Binary Dictionary Reader for OpenBoard .dict format
 * Format: Patricia Trie v4 (version 54)
 *
 * Ported from OpenBoard (Apache 2.0 License)
 * Based on: patricia_trie_reading_utils.cpp, byte_array_utils.h
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
// Byte reading utilities (big-endian)
// ============================================================================

static inline uint32_t readUint32BE(const uint8_t* buf, int pos) {
    return (static_cast<uint32_t>(buf[pos]) << 24) |
           (static_cast<uint32_t>(buf[pos + 1]) << 16) |
           (static_cast<uint32_t>(buf[pos + 2]) << 8) |
           static_cast<uint32_t>(buf[pos + 3]);
}

static inline uint32_t readUint24BE(const uint8_t* buf, int pos) {
    return (static_cast<uint32_t>(buf[pos]) << 16) |
           (static_cast<uint32_t>(buf[pos + 1]) << 8) |
           static_cast<uint32_t>(buf[pos + 2]);
}

static inline uint16_t readUint16BE(const uint8_t* buf, int pos) {
    return (static_cast<uint16_t>(buf[pos]) << 8) |
           static_cast<uint16_t>(buf[pos + 1]);
}

static inline uint8_t readUint8(const uint8_t* buf, int pos) {
    return buf[pos];
}

// ============================================================================
// Code point reading
// ============================================================================

static int readCodePointAndAdvance(const uint8_t* buffer, int* pos) {
    uint8_t firstByte = buffer[*pos];

    if (firstByte < MINIMUM_ONE_BYTE_CHARACTER_VALUE) {
        if (firstByte == CHARACTER_ARRAY_TERMINATOR) {
            (*pos)++;
            return NOT_A_CODE_POINT;
        } else {
            // 3-byte code point
            int codePoint = readUint24BE(buffer, *pos);
            *pos += 3;
            return codePoint;
        }
    } else {
        (*pos)++;
        return firstByte;
    }
}

// Read string of code points until terminator
static std::u32string readStringAndAdvance(const uint8_t* buffer, int* pos, int maxLength = MAX_WORD_LENGTH) {
    std::u32string result;
    result.reserve(maxLength);

    int codePoint = readCodePointAndAdvance(buffer, pos);
    while (codePoint != NOT_A_CODE_POINT && static_cast<int>(result.length()) < maxLength) {
        result += static_cast<char32_t>(codePoint);
        codePoint = readCodePointAndAdvance(buffer, pos);
    }

    return result;
}

// Convert UTF-32 to UTF-8
static std::string utf32ToUtf8(const std::u32string& utf32) {
    std::string result;
    result.reserve(utf32.length() * 3);

    for (char32_t cp : utf32) {
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

static int readChildrenPosition(const uint8_t* buffer, uint8_t flags, int* pos) {
    int base = *pos;
    int offset = 0;

    switch (flags & MASK_CHILDREN_POSITION_TYPE) {
        case FLAG_CHILDREN_POSITION_TYPE_ONEBYTE:
            offset = buffer[(*pos)++];
            break;
        case FLAG_CHILDREN_POSITION_TYPE_TWOBYTES:
            offset = readUint16BE(buffer, *pos);
            *pos += 2;
            break;
        case FLAG_CHILDREN_POSITION_TYPE_THREEBYTES:
            offset = readUint24BE(buffer, *pos);
            *pos += 3;
            break;
        default:
            return NOT_A_DICT_POS;
    }

    return base + offset;
}

// Skip shortcuts section
static void skipShortcuts(const uint8_t* buffer, int* pos) {
    // Shortcuts format: size (2 bytes) + data
    int shortcutSize = readUint16BE(buffer, *pos);
    *pos += 2 + shortcutSize;
}

// Skip bigrams section
static void skipBigrams(const uint8_t* buffer, int* pos) {
    // Bigrams: sequence of entries until marker
    while (true) {
        uint8_t bigramFlags = buffer[(*pos)++];
        // Skip probability and target
        (*pos)++; // probability

        // Read target position (1-3 bytes based on flags)
        int targetFlags = (bigramFlags >> 4) & 0x03;
        if (targetFlags == 0) {
            (*pos)++;
        } else if (targetFlags == 1) {
            (*pos) += 2;
        } else {
            (*pos) += 3;
        }

        // Check if this is the last bigram
        if ((bigramFlags & 0x80) != 0) {
            break;
        }
    }
}

static PtNodeInfo readPtNode(const uint8_t* buffer, int pos) {
    PtNodeInfo info;
    info.probability = 0;
    info.childrenPos = NOT_A_DICT_POS;
    info.isNotAWord = false;
    info.isPossiblyOffensive = false;

    int readPos = pos;

    // Read flags
    info.flags = buffer[readPos++];
    info.isTerminal = isTerminal(info.flags);
    info.isNotAWord = (info.flags & FLAG_IS_NOT_A_WORD) != 0;
    info.isPossiblyOffensive = (info.flags & FLAG_IS_POSSIBLY_OFFENSIVE) != 0;

    // Read code points
    if (hasMultipleChars(info.flags)) {
        info.codePoints = readStringAndAdvance(buffer, &readPos);
    } else {
        int cp = readCodePointAndAdvance(buffer, &readPos);
        if (cp != NOT_A_CODE_POINT) {
            info.codePoints += static_cast<char32_t>(cp);
        }
    }

    // Read probability if terminal
    if (info.isTerminal) {
        info.probability = buffer[readPos++];
    }

    // Read children position
    if (hasChildrenInFlags(info.flags)) {
        info.childrenPos = readChildrenPosition(buffer, info.flags, &readPos);
    }

    // Skip shortcuts if present
    if (hasShortcutTargets(info.flags)) {
        skipShortcuts(buffer, &readPos);
    }

    // Skip bigrams if present
    if (hasBigrams(info.flags)) {
        skipBigrams(buffer, &readPos);
    }

    info.siblingPos = readPos;
    return info;
}

// Read PtNodeArray size
static int readPtNodeArraySize(const uint8_t* buffer, int* pos) {
    uint8_t firstByte = buffer[(*pos)++];
    if (firstByte < 0x80) {
        return firstByte;
    } else {
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

    header.magic = readUint32BE(data, 0);
    if (header.magic != DICT_MAGIC_NUMBER) {
        return header;
    }

    header.version = readUint16BE(data, 4);
    header.flags = readUint16BE(data, 6);
    header.headerSize = readUint32BE(data, 8);

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
        std::u32string prefix;
        traverseAndLoad(trieStartPos_, prefix, trie);
    }

private:
    const uint8_t* data_;
    size_t size_;
    int trieStartPos_;

    void traverseAndLoad(int nodeArrayPos, std::u32string& prefix, Trie* trie) {
        if (nodeArrayPos < 0 || nodeArrayPos >= static_cast<int>(size_)) {
            return;
        }

        int pos = nodeArrayPos;
        int nodeCount = readPtNodeArraySize(data_, &pos);

        for (int i = 0; i < nodeCount; i++) {
            PtNodeInfo nodeInfo = readPtNode(data_, pos);

            // Build word prefix
            std::u32string wordPrefix = prefix;
            wordPrefix += nodeInfo.codePoints;

            // If terminal and it's a valid word, add to trie
            if (nodeInfo.isTerminal && !nodeInfo.isNotAWord) {
                std::string word = utf32ToUtf8(wordPrefix);
                trie->insert(word, nodeInfo.probability);
            }

            // Recursively process children
            if (nodeInfo.childrenPos != NOT_A_DICT_POS) {
                traverseAndLoad(nodeInfo.childrenPos, wordPrefix, trie);
            }

            // Move to sibling
            pos = nodeInfo.siblingPos;
        }
    }
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

    // printf("[BinaryDict] Loading: magic=0x%08x, version=%d, locale=%s, headerSize=%d\n",
    //        header.magic, header.version, header.locale.c_str(), header.headerSize);

    // Clear existing data
    clear();

    // Load trie data
    BinaryDictLoader loader(data.data(), data.size(), header.headerSize);
    loader.loadIntoTrie(this);

    // printf("[BinaryDict] Loaded %d words\n", getWordCount());

    return wordCount_ > 0;
}

} // namespace hoskey
