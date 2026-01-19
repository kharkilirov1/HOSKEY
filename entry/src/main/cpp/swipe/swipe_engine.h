/**
 * Native Swipe Engine for HOSKEY
 * High-performance swipe-to-text recognition
 */

#ifndef HOSKEY_SWIPE_ENGINE_H
#define HOSKEY_SWIPE_ENGINE_H

#include <vector>
#include <string>
#include <memory>

namespace hoskey {

// Forward declarations
class Trie;
class ProximityInfo;

/**
 * 2D point with timestamp
 */
struct SwipePoint {
    float x;
    float y;
    int64_t timestamp;

    SwipePoint() : x(0), y(0), timestamp(0) {}
    SwipePoint(float x_, float y_, int64_t ts) : x(x_), y(y_), timestamp(ts) {}
};

/**
 * Keyboard key bounds
 */
struct KeyBounds {
    char key;
    float centerX;
    float centerY;
    float width;
    float height;

    KeyBounds() : key(0), centerX(0), centerY(0), width(0), height(0) {}
    KeyBounds(char k, float cx, float cy, float w, float h)
        : key(k), centerX(cx), centerY(cy), width(w), height(h) {}
};

/**
 * Swipe recognition result
 */
struct SwipeResult {
    std::string bestWord;
    std::vector<std::string> alternatives;
    float confidence;
    std::string rawSequence;

    SwipeResult() : confidence(0.0f) {}
};

/**
 * Swipe Engine
 * Converts touch path to text using dictionary-based ranking
 */
class SwipeEngine {
public:
    SwipeEngine();
    ~SwipeEngine();

    // Configuration
    void setDictionary(Trie* trie);
    void setProximityInfo(ProximityInfo* proximityInfo);
    void setKeyboardLayout(const std::vector<KeyBounds>& layout);

    // Main processing
    SwipeResult processSwipe(const std::vector<SwipePoint>& path);

    // Parameters
    static constexpr int MIN_SWIPE_POINTS = 5;
    static constexpr float MIN_SWIPE_DISTANCE = 50.0f;
    static constexpr float KEY_HIT_RADIUS = 1.5f;
    static constexpr int PATH_SAMPLE_INTERVAL = 10;  // Sample every N points
    static constexpr int MAX_CANDIDATES = 50;

private:
    Trie* trie_;
    ProximityInfo* proximityInfo_;
    std::vector<KeyBounds> keyboardLayout_;

    // Internal methods
    bool isValidSwipe(const std::vector<SwipePoint>& path) const;
    std::vector<SwipePoint> smoothPath(const std::vector<SwipePoint>& path) const;
    std::vector<SwipePoint> samplePath(const std::vector<SwipePoint>& path) const;
    std::string pathToKeySequence(const std::vector<SwipePoint>& path) const;
    std::string collapseSequence(const std::string& sequence) const;

    struct Candidate {
        std::string word;
        float distance;
        int frequency;
        float score;

        Candidate() : distance(0), frequency(0), score(0) {}
        Candidate(const std::string& w, float d, int f, float s)
            : word(w), distance(d), frequency(f), score(s) {}
    };

    std::vector<Candidate> getCandidates(const std::string& keySequence) const;
    std::vector<Candidate> rankCandidates(const std::vector<Candidate>& candidates,
                                         const std::vector<SwipePoint>& path) const;
    float calculatePathDistance(const std::string& word,
                              const std::vector<SwipePoint>& path) const;
    float calculateConfidence(const Candidate& best,
                            const std::vector<Candidate>& candidates) const;

    // Utility
    static float distance(const SwipePoint& a, const SwipePoint& b);
    static float pointToRectDistance(float px, float py, const KeyBounds& rect);
    KeyBounds* findNearestKey(float x, float y);
};

} // namespace hoskey

#endif // HOSKEY_SWIPE_ENGINE_H
