/**
 * Flat Trie Implementation
 *
 * Optimized for instant loading via memory mapping or bulk read.
 */

#include "flat_trie.h"
#include "trie.h"  // For WordEntry
#include "trie_pooled.h"  // For building from TriePooled

#include <fstream>
#include <algorithm>
#include <cstring>
#include <hilog/log.h>

// Try to use mmap on supported platforms
#ifdef __unix__
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #define HAS_MMAP 1
#else
    #define HAS_MMAP 0
#endif

#undef LOG_TAG
#define LOG_TAG "HOSKEY-FLAT-TRIE"

namespace hoskey {

// ============================================================================
// FlatTrie Implementation
// ============================================================================

FlatTrie::FlatTrie()
    : data_(nullptr)
    , dataSize_(0)
    , isMapped_(false)
    , header_(nullptr)
    , nodes_(nullptr)
    , children_(nullptr) {
}

FlatTrie::~FlatTrie() {
    unload();
}

bool FlatTrie::load(const std::string& path) {
    unload();

    OH_LOG_INFO(LOG_APP, "FlatTrie::load: loading from %{public}s", path.c_str());

#if HAS_MMAP
    // Try memory mapping first (fastest)
    int fd = open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size >= sizeof(FlatTrieHeader)) {
            void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped != MAP_FAILED) {
                data_ = static_cast<uint8_t*>(mapped);
                dataSize_ = st.st_size;
                isMapped_ = true;
                close(fd);

                OH_LOG_INFO(LOG_APP, "FlatTrie::load: mmap successful, %{public}zu bytes", dataSize_);
            } else {
                close(fd);
                fd = -1;
            }
        } else {
            close(fd);
            fd = -1;
        }
    }

    if (!isMapped_)
#endif
    {
        // Fallback to regular file read
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            OH_LOG_ERROR(LOG_APP, "FlatTrie::load: cannot open file");
            return false;
        }

        dataSize_ = file.tellg();
        if (dataSize_ < sizeof(FlatTrieHeader)) {
            OH_LOG_ERROR(LOG_APP, "FlatTrie::load: file too small");
            return false;
        }

        file.seekg(0, std::ios::beg);
        data_ = new uint8_t[dataSize_];
        if (!file.read(reinterpret_cast<char*>(data_), dataSize_)) {
            OH_LOG_ERROR(LOG_APP, "FlatTrie::load: read failed");
            delete[] data_;
            data_ = nullptr;
            return false;
        }

        isMapped_ = false;
        OH_LOG_INFO(LOG_APP, "FlatTrie::load: read successful, %{public}zu bytes", dataSize_);
    }

    // Parse header
    header_ = reinterpret_cast<const FlatTrieHeader*>(data_);

    if (header_->magic != FLAT_TRIE_MAGIC) {
        OH_LOG_ERROR(LOG_APP, "FlatTrie::load: invalid magic 0x%{public}08X", header_->magic);
        unload();
        return false;
    }

    if (header_->version > FLAT_TRIE_VERSION) {
        OH_LOG_ERROR(LOG_APP, "FlatTrie::load: unsupported version %{public}d", header_->version);
        unload();
        return false;
    }

    // Validate offsets
    if (header_->nodesOffset >= dataSize_ ||
        header_->childrenOffset >= dataSize_ ||
        header_->fileSize != dataSize_) {
        OH_LOG_ERROR(LOG_APP, "FlatTrie::load: invalid offsets");
        unload();
        return false;
    }

    // Set up pointers
    nodes_ = reinterpret_cast<const FlatTrieNode*>(data_ + header_->nodesOffset);
    children_ = reinterpret_cast<const FlatChildRef*>(data_ + header_->childrenOffset);

    // Extract locale
    locale_ = std::string(header_->locale, strnlen(header_->locale, sizeof(header_->locale)));

    OH_LOG_INFO(LOG_APP, "FlatTrie::load: SUCCESS - %{public}u nodes, %{public}u words, locale=%{public}s",
                header_->nodeCount, header_->wordCount, locale_.c_str());

    return true;
}

void FlatTrie::unload() {
    if (data_) {
#if HAS_MMAP
        if (isMapped_) {
            munmap(data_, dataSize_);
        } else
#endif
        {
            delete[] data_;
        }
        data_ = nullptr;
    }

    dataSize_ = 0;
    isMapped_ = false;
    header_ = nullptr;
    nodes_ = nullptr;
    children_ = nullptr;
    locale_.clear();
}

const FlatTrieNode* FlatTrie::getNode(uint32_t index) const {
    if (!nodes_ || !header_ || index >= header_->nodeCount) {
        return nullptr;
    }
    return &nodes_[index];
}

const FlatTrieNode* FlatTrie::findChild(const FlatTrieNode* node, uint8_t byte) const {
    if (!node || !children_ || node->childCount == 0) {
        return nullptr;
    }

    // Binary search in children array
    const FlatChildRef* start = &children_[node->childrenStart];
    const FlatChildRef* end = start + node->childCount;

    auto it = std::lower_bound(start, end, byte,
        [](const FlatChildRef& ref, uint8_t b) { return ref.byte < b; });

    if (it != end && it->byte == byte) {
        return getNode(it->nodeIndex);
    }

    return nullptr;
}

bool FlatTrie::contains(const std::string& word) const {
    if (!isLoaded() || word.empty()) {
        return false;
    }

    const FlatTrieNode* current = getNode(0);  // Root
    if (!current) return false;

    for (size_t i = 0; i < word.size(); i++) {
        uint8_t byte = static_cast<uint8_t>(word[i]);
        current = findChild(current, byte);
        if (!current) return false;
    }

    return current->isEndOfWord();
}

int FlatTrie::getFrequency(const std::string& word) const {
    if (!isLoaded() || word.empty()) {
        return 0;
    }

    const FlatTrieNode* current = getNode(0);
    if (!current) return 0;

    for (size_t i = 0; i < word.size(); i++) {
        uint8_t byte = static_cast<uint8_t>(word[i]);
        current = findChild(current, byte);
        if (!current) return 0;
    }

    return current->isEndOfWord() ? current->frequency : 0;
}

std::vector<WordEntry> FlatTrie::findByPrefix(const std::string& prefix, int limit) const {
    std::vector<WordEntry> results;

    if (!isLoaded()) {
        return results;
    }

    // Navigate to prefix node
    const FlatTrieNode* current = getNode(0);
    if (!current) return results;

    for (size_t i = 0; i < prefix.size(); i++) {
        uint8_t byte = static_cast<uint8_t>(prefix[i]);
        current = findChild(current, byte);
        if (!current) return results;  // Prefix not found
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

    return results;
}

void FlatTrie::collectWords(const FlatTrieNode* node, const std::string& prefix,
                           std::vector<WordEntry>& results, int limit) const {
    if (!node || static_cast<int>(results.size()) >= limit * 3) {
        return;
    }

    if (node->isEndOfWord()) {
        results.emplace_back(prefix, node->frequency, 0);
    }

    // Iterate children
    if (node->childCount > 0 && children_) {
        const FlatChildRef* start = &children_[node->childrenStart];
        for (uint16_t i = 0; i < node->childCount; i++) {
            const FlatChildRef& ref = start[i];
            const FlatTrieNode* child = getNode(ref.nodeIndex);
            if (child) {
                std::string childPrefix = prefix + static_cast<char>(ref.byte);
                collectWords(child, childPrefix, results, limit);
            }
        }
    }
}

std::vector<std::string> FlatTrie::getWordsByFirstLetter(char letter, int limit) const {
    std::vector<std::string> results;

    if (!isLoaded()) {
        return results;
    }

    auto entries = findByPrefix(std::string(1, letter), limit);
    for (const auto& entry : entries) {
        results.push_back(entry.word);
    }

    return results;
}

// ============================================================================
// FlatTrieBuilder Implementation
// ============================================================================

// Specialization for TriePooled - must be defined before convert() uses it
template<>
bool FlatTrieBuilder::buildFromTrie(const TriePooled& trie,
                                   const std::string& outputPath,
                                   const std::string& locale) {
    OH_LOG_INFO(LOG_APP, "FlatTrieBuilder::buildFromTrie: building from TriePooled with %{public}d words",
                trie.getWordCount());

    // Prepare output data
    std::vector<FlatTrieNode> nodes;
    std::vector<FlatChildRef> children;

    // Reserve space based on pool size
    nodes.reserve(trie.getPool().usedCount() + 1);
    children.reserve(trie.getPool().usedCount() * 4);  // Estimate

    // BFS traversal to assign indices
    struct QueueEntry {
        uint32_t poolIndex;
        uint32_t flatIndex;
    };

    std::vector<QueueEntry> queue;
    queue.push_back({1, 0});  // Root: pool index 1, flat index 0

    // First pass: count nodes and assign indices
    std::vector<uint32_t> poolToFlat(trie.getPool().usedCount() + 1, UINT32_MAX);
    poolToFlat[1] = 0;  // Root

    uint32_t nextFlatIndex = 1;
    size_t queueHead = 0;

    while (queueHead < queue.size()) {
        QueueEntry entry = queue[queueHead++];
        const TrieNodePooled& poolNode = trie.getPool().node(entry.poolIndex);

        poolNode.forEachChildIndex([&](uint8_t byte, uint32_t childPoolIndex) {
            if (poolToFlat[childPoolIndex] == UINT32_MAX) {
                poolToFlat[childPoolIndex] = nextFlatIndex++;
                queue.push_back({childPoolIndex, poolToFlat[childPoolIndex]});
            }
        });
    }

    OH_LOG_INFO(LOG_APP, "FlatTrieBuilder: assigned %{public}u flat indices", nextFlatIndex);

    // Second pass: build flat nodes and children
    nodes.resize(nextFlatIndex);

    for (const auto& entry : queue) {
        const TrieNodePooled& poolNode = trie.getPool().node(entry.poolIndex);
        FlatTrieNode& flatNode = nodes[entry.flatIndex];

        flatNode.frequency = poolNode.getFrequency();
        flatNode.setEndOfWord(poolNode.isEndOfWord());
        flatNode.childCount = poolNode.getChildCount();

        if (flatNode.childCount > 0) {
            flatNode.childrenStart = static_cast<uint32_t>(children.size());

            // Collect and sort children
            std::vector<FlatChildRef> nodeChildren;
            poolNode.forEachChildIndex([&](uint8_t byte, uint32_t childPoolIndex) {
                FlatChildRef ref;
                ref.byte = byte;
                ref.reserved = 0;
                ref.reserved2 = 0;
                ref.nodeIndex = poolToFlat[childPoolIndex];
                nodeChildren.push_back(ref);
            });

            // Sort by byte for binary search
            std::sort(nodeChildren.begin(), nodeChildren.end(),
                     [](const FlatChildRef& a, const FlatChildRef& b) {
                         return a.byte < b.byte;
                     });

            for (const auto& ref : nodeChildren) {
                children.push_back(ref);
            }
        } else {
            flatNode.childrenStart = 0;
        }

        std::memset(flatNode.reserved, 0, sizeof(flatNode.reserved));
    }

    OH_LOG_INFO(LOG_APP, "FlatTrieBuilder: %{public}zu nodes, %{public}zu children",
                nodes.size(), children.size());

    // Build header
    FlatTrieHeader header;
    std::memset(&header, 0, sizeof(header));

    header.magic = FLAT_TRIE_MAGIC;
    header.version = FLAT_TRIE_VERSION;
    header.flags = 0;
    header.nodeCount = static_cast<uint32_t>(nodes.size());
    header.wordCount = trie.getWordCount();
    header.nodesOffset = sizeof(FlatTrieHeader);
    header.childrenOffset = header.nodesOffset + nodes.size() * sizeof(FlatTrieNode);
    header.stringsOffset = 0;  // Not used
    header.fileSize = header.childrenOffset + children.size() * sizeof(FlatChildRef);

    std::strncpy(header.locale, locale.c_str(), sizeof(header.locale) - 1);

    // Write to file
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        OH_LOG_ERROR(LOG_APP, "FlatTrieBuilder: cannot create output file");
        return false;
    }

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(nodes.data()), nodes.size() * sizeof(FlatTrieNode));
    file.write(reinterpret_cast<const char*>(children.data()), children.size() * sizeof(FlatChildRef));

    file.close();

    OH_LOG_INFO(LOG_APP, "FlatTrieBuilder: SUCCESS - wrote %{public}u bytes to %{public}s",
                header.fileSize, outputPath.c_str());

    return true;
}

bool FlatTrieBuilder::convert(const std::string& inputPath,
                             const std::string& outputPath,
                             const std::string& locale) {
    OH_LOG_INFO(LOG_APP, "FlatTrieBuilder::convert: %{public}s -> %{public}s",
                inputPath.c_str(), outputPath.c_str());

    // Load into TriePooled first (optimized for speed)
    TriePooled trie;
    if (!trie.loadFromFile(inputPath)) {
        OH_LOG_ERROR(LOG_APP, "FlatTrieBuilder::convert: failed to load input");
        return false;
    }

    return buildFromTrie(trie, outputPath, locale);
}

} // namespace hoskey
