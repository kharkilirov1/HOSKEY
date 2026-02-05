/**
 * YandexTrie implementation
 * 
 * Extraction strategy:
 * 1. Scan trie region for marker + UTF-8 sequences
 * 2. Build word list with frequencies
 * 3. Create prefix tree for fast lookup
 */

#include "yandex_trie.h"
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <queue>
#include <unistd.h>
#include <vector>

namespace yandex {

//==============================================================================
// UTF-8 helpers
//==============================================================================

bool YandexDict::decodeUtf8Char(const uint8_t* data, size_t pos, size_t maxPos,
                                 char32_t& outChar, size_t& outLen) {
    if (pos >= maxPos) return false;
    
    uint8_t b0 = data[pos];
    
    // ASCII
    if (b0 < 0x80) {
        outChar = b0;
        outLen = 1;
        return true;
    }
    
    // 2-byte UTF-8 (Cyrillic)
    if ((b0 & 0xE0) == 0xC0 && pos + 1 < maxPos) {
        uint8_t b1 = data[pos + 1];
        if ((b1 & 0xC0) == 0x80) {
            outChar = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
            outLen = 2;
            return true;
        }
    }
    
    // 3-byte UTF-8
    if ((b0 & 0xF0) == 0xE0 && pos + 2 < maxPos) {
        uint8_t b1 = data[pos + 1];
        uint8_t b2 = data[pos + 2];
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
            outChar = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            outLen = 3;
            return true;
        }
    }
    
    return false;
}

std::string YandexDict::encodeUtf8(char32_t ch) {
    std::string result;
    
    if (ch < 0x80) {
        result += static_cast<char>(ch);
    } else if (ch < 0x800) {
        result += static_cast<char>(0xC0 | (ch >> 6));
        result += static_cast<char>(0x80 | (ch & 0x3F));
    } else if (ch < 0x10000) {
        result += static_cast<char>(0xE0 | (ch >> 12));
        result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (ch & 0x3F));
    }
    
    return result;
}

//==============================================================================
// YandexDict implementation
//==============================================================================

YandexDict::YandexDict() : root_(std::make_unique<TrieNode>()), compTrie_(std::make_unique<CompTrieReader>()) {
}

YandexDict::~YandexDict() = default;

bool YandexDict::loadFromFd(int fd, size_t offset, size_t length) {
    // Use CompTrieReader for instant mmap-based loading
    if (compTrie_->loadFromFd(fd, offset, length)) {
        useCompTrie_ = true;
        loaded_ = true;
        return true;
    }
    return false;
}

bool YandexDict::load(const std::string& path) {
    // Try CompTrieReader (mmap-based, fast loading)
    if (compTrie_->load(path)) {
        useCompTrie_ = true;
        loaded_ = true;
        return true;
    }
    
    // Fallback to V1 extraction
    useCompTrie_ = false;
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    
    fileSize_ = file.tellg();
    file.seekg(0);
    
    fileData_ = std::make_unique<uint8_t[]>(fileSize_);
    file.read(reinterpret_cast<char*>(fileData_.get()), fileSize_);
    
    if (!file) {
        return false;
    }
    
    const uint8_t* data = fileData_.get();
    
    // Parse header
    if (!parseHeader(data, fileSize_)) {
        return false;
    }
    
    // Parse JSON config
    parseJson(data, fileSize_);
    
    // Find trie end (first TFLite marker or META)
    size_t trieEnd = fileSize_;
    
    // Look for TFL3 signature
    const uint8_t tfl3[] = {'T', 'F', 'L', '3'};
    for (size_t i = TRIE_START; i < fileSize_ - 4; i++) {
        if (memcmp(data + i, tfl3, 4) == 0) {
            trieEnd = i - 4;  // FlatBuffer size is 4 bytes before
            break;
        }
    }
    
    // Extract words from trie region
    if (!extractWords(data, TRIE_START, trieEnd)) {
        return false;
    }
    
    // Build prefix tree
    buildPrefixTree();
    
    loaded_ = true;
    return true;
}

bool YandexDict::loadFromTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    
    words_.clear();
    wordIndex_.clear();
    root_ = std::make_unique<TrieNode>();
    
    std::string line;
    uint32_t wordId = 0;
    
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        std::string word;
        uint8_t freq = 100;
        
        // Parse word=X,f=Y format (OpenBoard style)
        if (line.find("word=") == 0) {
            size_t wordEnd = line.find(',');
            if (wordEnd != std::string::npos) {
                word = line.substr(5, wordEnd - 5);
                
                size_t fPos = line.find("f=");
                if (fPos != std::string::npos) {
                    try {
                        freq = static_cast<uint8_t>(std::stoi(line.substr(fPos + 2)));
                    } catch (...) {
                        freq = 100;
                    }
                }
            }
        }
        // Parse simple word\tfreq format
        else {
            size_t tabPos = line.find('\t');
            if (tabPos != std::string::npos) {
                word = line.substr(0, tabPos);
                try {
                    freq = static_cast<uint8_t>(std::stoi(line.substr(tabPos + 1)));
                } catch (...) {
                    freq = 100;
                }
            } else {
                word = line;
            }
        }
        
        // Trim whitespace
        while (!word.empty() && (word.back() == ' ' || word.back() == '\r' || word.back() == '\n')) {
            word.pop_back();
        }
        
        if (!word.empty()) {
            WordEntry entry;
            entry.word = word;
            entry.frequency = freq;
            entry.wordId = wordId++;
            
            wordIndex_[word] = words_.size();
            words_.push_back(entry);
        }
    }
    
    if (words_.empty()) {
        return false;
    }

    // Build prefix tree
    buildPrefixTree();

    loaded_ = true;
    return true;
}

bool YandexDict::loadFromTextFd(int fd, size_t offset, size_t length) {
    if (fd < 0 || length == 0) {
        return false;
    }

    // Read the entire text content from FD
    std::vector<char> buffer(length + 1);

    // Seek to offset
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return false;
    }

    // Read data
    ssize_t bytesRead = read(fd, buffer.data(), length);
    if (bytesRead <= 0) {
        return false;
    }
    buffer[bytesRead] = '\0';

    // Parse as text
    words_.clear();
    wordIndex_.clear();
    root_ = std::make_unique<TrieNode>();

    uint32_t wordId = 0;
    char* line = strtok(buffer.data(), "\n\r");

    while (line != nullptr) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') {
            line = strtok(nullptr, "\n\r");
            continue;
        }

        std::string word;
        uint8_t freq = 100;

        // Parse word=X,f=Y format
        if (strncmp(line, "word=", 5) == 0) {
            char* comma = strchr(line + 5, ',');
            if (comma != nullptr) {
                word = std::string(line + 5, comma - line - 5);

                char* fPos = strstr(line, "f=");
                if (fPos != nullptr) {
                    freq = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fPos + 2))));
                }
            }
        }
        // Parse simple word\tfreq format
        else {
            char* tab = strchr(line, '\t');
            if (tab != nullptr) {
                word = std::string(line, tab - line);
                freq = static_cast<uint8_t>(std::min(255, std::max(0, atoi(tab + 1))));
            } else {
                word = line;
            }
        }

        // Trim whitespace
        while (!word.empty() && (word.back() == ' ' || word.back() == '\r')) {
            word.pop_back();
        }

        if (!word.empty() && word.length() >= 2) {
            WordEntry entry;
            entry.word = word;
            entry.frequency = freq;
            entry.wordId = wordId++;

            wordIndex_[word] = words_.size();
            words_.push_back(entry);
        }

        line = strtok(nullptr, "\n\r");
    }

    if (words_.empty()) {
        return false;
    }

    // Build prefix tree for fast suggestions
    buildPrefixTree();

    // Disable CompTrie (use in-memory trie)
    useCompTrie_ = false;
    loaded_ = true;

    return true;
}

bool YandexDict::parseHeader(const uint8_t* data, size_t size) {
    if (size < HEADER_SIZE) return false;
    
    uint32_t magic = *reinterpret_cast<const uint32_t*>(data);
    return magic == YANDEX_MAGIC;
}

bool YandexDict::parseJson(const uint8_t* data, size_t size) {
    if (size < JSON_START + 10) return false;
    
    // Find JSON boundaries
    size_t start = JSON_START;
    while (start < size && data[start] != '{') start++;
    
    if (start >= size) return false;
    
    int depth = 0;
    size_t end = start;
    
    while (end < size) {
        if (data[end] == '{') depth++;
        else if (data[end] == '}') {
            depth--;
            if (depth == 0) {
                end++;
                break;
            }
        }
        end++;
    }
    
    jsonConfig_ = std::string(reinterpret_cast<const char*>(data + start), end - start);
    return true;
}

bool YandexDict::extractWords(const uint8_t* data, size_t trieStart, size_t trieEnd) {
    // TFLite vocab format: [word UTF-8][null padding to 4-byte][4-byte length of NEXT word]
    // Vocab is concentrated in 12-14MB range
    
    // CRITICAL: Keep range small to avoid ANR!
    size_t vocabStart = 12500000;  // ~12.5MB
    size_t vocabEnd = std::min(fileSize_, (size_t)14500000);  // ~14.5MB max
    
    const size_t MAX_WORDS = 100000;  // Limit to avoid infinite loop
    
    if (vocabStart >= fileSize_) {
        vocabStart = trieEnd;
        vocabEnd = fileSize_;
    }
    
    std::vector<std::string> extractedWords;
    size_t pos = vocabStart;
    
    // Find first Cyrillic word to sync position
    while (pos < vocabEnd - 10) {
        if (data[pos] == UTF8_CYR_D0 || data[pos] == UTF8_CYR_D1) {
            // Check if this looks like start of a word
            if (pos >= 4) {
                uint32_t prevLen = *reinterpret_cast<const uint32_t*>(data + pos - 4);
                if (prevLen >= 4 && prevLen <= 50) {
                    break;  // Found sync point
                }
            }
        }
        pos++;
    }
    
    // Now extract words: scan for Cyrillic sequences
    while (pos < vocabEnd - 4 && extractedWords.size() < MAX_WORDS) {
        // Check for Cyrillic UTF-8 start
        if (data[pos] != UTF8_CYR_D0 && data[pos] != UTF8_CYR_D1) {
            pos++;
            continue;
        }
        
        // Read word until null or non-Cyrillic
        size_t wordStart = pos;
        size_t cyrCount = 0;
        size_t charCount = 0;
        
        while (pos < vocabEnd - 1) {
            uint8_t b = data[pos];
            
            if (b == UTF8_CYR_D0 && pos + 1 < vocabEnd) {
                uint8_t b1 = data[pos + 1];
                if (b1 >= 0x80 && b1 <= 0xBF) {
                    cyrCount++;
                    charCount++;
                    pos += 2;
                    continue;
                }
            }
            if (b == UTF8_CYR_D1 && pos + 1 < vocabEnd) {
                uint8_t b1 = data[pos + 1];
                if (b1 >= 0x80 && b1 <= 0xBF) {
                    cyrCount++;
                    charCount++;
                    pos += 2;
                    continue;
                }
            }
            // Allow hyphen and apostrophe in words
            if (b == '-' || b == '\'') {
                charCount++;
                pos++;
                continue;
            }
            // Allow Latin letters mixed in
            if (b < 0x80 && isalpha(b)) {
                charCount++;
                pos++;
                continue;
            }
            // End of word
            break;
        }
        
        // Extract word if valid (minimum 3 letters, maximum 25)
        if (cyrCount >= 3 && cyrCount <= 25 && charCount >= 3) {
            size_t wordLen = pos - wordStart;
            std::string word(reinterpret_cast<const char*>(data + wordStart), wordLen);
            
            // Filter out garbage: skip if too many repeated chars or suspicious patterns
            bool isGarbage = false;
            
            // Check for repeated patterns (like "бляблябля")
            if (wordLen >= 6) {
                size_t repeatCount = 0;
                for (size_t i = 2; i < wordLen; i++) {
                    if (word[i] == word[i-2]) repeatCount++;
                }
                if (repeatCount > wordLen / 2) isGarbage = true;
            }
            
            // Basic profanity filter (common Russian swear words)
            static const char* badWords[] = {
                "бля", "хуй", "хуя", "хуе", "пизд", "ебат", "ебан", "ебал", 
                "сука", "сук", "блят", "пидор", "пидар", "залуп", "муда"
            };
            for (const char* bad : badWords) {
                if (word.find(bad) != std::string::npos) {
                    isGarbage = true;
                    break;
                }
            }
            
            if (!isGarbage) {
                extractedWords.push_back(word);
            }
        }
        
        // Skip padding and length field (4-8 bytes typically)
        while (pos < vocabEnd && data[pos] == 0) pos++;
        if (pos < vocabEnd - 4) {
            uint32_t nextLen = *reinterpret_cast<const uint32_t*>(data + pos);
            if (nextLen >= 4 && nextLen <= 50) {
                pos += 4;  // Skip length field
            }
        }
    }
    
    // Deduplicate
    std::sort(extractedWords.begin(), extractedWords.end());
    extractedWords.erase(std::unique(extractedWords.begin(), extractedWords.end()), 
                         extractedWords.end());
    
    // Convert to WordEntry
    words_.reserve(extractedWords.size());
    
    for (size_t i = 0; i < extractedWords.size(); i++) {
        WordEntry entry;
        entry.word = extractedWords[i];
        entry.wordId = static_cast<uint32_t>(i);
        
        // Estimate frequency based on word length
        size_t len = 0;
        for (size_t j = 0; j < entry.word.size(); ) {
            if ((uint8_t)entry.word[j] >= 0x80) j += 2;
            else j++;
            len++;
        }
        
        if (len <= 3) entry.frequency = 255;
        else if (len <= 5) entry.frequency = 200;
        else if (len <= 8) entry.frequency = 150;
        else if (len <= 12) entry.frequency = 100;
        else entry.frequency = 50;
        
        words_.push_back(entry);
        wordIndex_[entry.word] = words_.size() - 1;
    }
    
    return !words_.empty();
}

void YandexDict::buildPrefixTree() {
    root_ = std::make_unique<TrieNode>();
    
    for (size_t idx = 0; idx < words_.size(); idx++) {
        const auto& entry = words_[idx];
        TrieNode* node = root_.get();
        
        // Iterate through UTF-8 codepoints
        size_t pos = 0;
        while (pos < entry.word.size()) {
            char32_t ch;
            size_t len;
            
            if (decodeUtf8Char(reinterpret_cast<const uint8_t*>(entry.word.data()),
                               pos, entry.word.size(), ch, len)) {
                auto it = node->children.find(ch);
                if (it == node->children.end()) {
                    node->children[ch] = std::make_unique<TrieNode>();
                }
                node = node->children[ch].get();
                pos += len;
            } else {
                break;
            }
        }
        
        node->isTerminal = true;
        node->frequency = entry.frequency;
        node->wordId = entry.wordId;
    }
}

bool YandexDict::contains(const std::string& word) const {
    if (useCompTrie_ && compTrie_) {
        return compTrie_->contains(word);
    }
    return wordIndex_.find(word) != wordIndex_.end();
}

uint8_t YandexDict::getFrequency(const std::string& word) const {
    if (useCompTrie_ && compTrie_) {
        uint64_t freq = compTrie_->getFrequency(word);
        // Normalize to 0-255 range
        return static_cast<uint8_t>(std::min(freq, (uint64_t)255));
    }
    auto it = wordIndex_.find(word);
    if (it != wordIndex_.end()) {
        return words_[it->second].frequency;
    }
    return 0;
}

uint32_t YandexDict::getWordId(const std::string& word) const {
    // CompTrieReader doesn't track word IDs, use fallback
    auto it = wordIndex_.find(word);
    if (it != wordIndex_.end()) {
        return words_[it->second].wordId;
    }
    return UINT32_MAX;
}

const TrieNode* YandexDict::findNode(const std::string& prefix) const {
    const TrieNode* node = root_.get();
    
    size_t pos = 0;
    while (pos < prefix.size() && node) {
        char32_t ch;
        size_t len;
        
        if (decodeUtf8Char(reinterpret_cast<const uint8_t*>(prefix.data()),
                           pos, prefix.size(), ch, len)) {
            auto it = node->children.find(ch);
            if (it != node->children.end()) {
                node = it->second.get();
                pos += len;
            } else {
                return nullptr;
            }
        } else {
            break;
        }
    }
    
    return node;
}

void YandexDict::collectWords(const TrieNode* node, const std::string& prefix,
                               std::vector<Suggestion>& results, int maxCount) const {
    if (!node) return;
    
    // DFS to collect all words up to depth limit
    struct StackItem {
        const TrieNode* node;
        std::string word;
        int depth;
    };
    
    std::vector<StackItem> stack;
    stack.push_back({node, prefix, 0});
    
    while (!stack.empty() && (int)results.size() < maxCount) {
        auto item = stack.back();
        stack.pop_back();
        
        if (item.node->isTerminal) {
            Suggestion s;
            s.word = item.word;
            s.score = item.node->frequency / 255.0f;
            results.push_back(s);
        }
        
        // Limit search depth (15 chars after prefix)
        if (item.depth < 15) {
            for (const auto& [ch, child] : item.node->children) {
                stack.push_back({child.get(), item.word + encodeUtf8(ch), item.depth + 1});
            }
        }
    }
}

std::vector<Suggestion> YandexDict::getSuggestions(const std::string& prefix, int maxCount) const {
    std::vector<Suggestion> results;
    
    if (prefix.empty()) return results;
    
    // V2: Use CompTrieReader (fast path)
    if (useCompTrie_ && compTrie_) {
        auto compResults = compTrie_->getSuggestions(prefix, maxCount);
        results.reserve(compResults.size());
        for (const auto& cr : compResults) {
            Suggestion s;
            s.word = cr.word;
            s.score = cr.score;
            results.push_back(s);
        }
        return results;
    }
    
    // V1 Fallback: Use in-memory trie
    const TrieNode* node = findNode(prefix);
    if (!node) return results;
    
    // Collect words starting from this node
    collectWords(node, prefix, results, maxCount * 10);  // Get more candidates
    
    // Sort by score
    std::sort(results.begin(), results.end());
    
    // Trim to maxCount
    if ((int)results.size() > maxCount) {
        results.resize(maxCount);
    }
    
    return results;
}

size_t YandexDict::getWordCount() const {
    if (useCompTrie_ && compTrie_) {
        return compTrie_->getWordCount();
    }
    return words_.size();
}

size_t YandexDict::getMemoryUsage() const {
    // V2: CompTrieReader uses mmap, minimal RAM
    if (useCompTrie_ && compTrie_) {
        return compTrie_->getMemoryUsage();
    }
    
    // V1: Full in-memory trie
    size_t mem = fileSize_;
    mem += words_.capacity() * sizeof(WordEntry);
    for (const auto& w : words_) {
        mem += w.word.capacity();
    }
    // Rough estimate for trie nodes
    mem += words_.size() * sizeof(TrieNode) * 2;
    return mem;
}

} // namespace yandex
