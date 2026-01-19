/**
 * Native Swipe Engine implementation
 */

#include "swipe_engine.h"
#include "../dictionary/trie.h"
#include "../proximity/proximity_info.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace hoskey {

SwipeEngine::SwipeEngine() : trie_(nullptr), proximityInfo_(nullptr) {}

SwipeEngine::~SwipeEngine() = default;

void SwipeEngine::setDictionary(Trie* trie) {
    trie_ = trie;
}

void SwipeEngine::setProximityInfo(ProximityInfo* proximityInfo) {
    proximityInfo_ = proximityInfo;
}

void SwipeEngine::setKeyboardLayout(const std::vector<KeyBounds>& layout) {
    keyboardLayout_ = layout;
}

SwipeResult SwipeEngine::processSwipe(const std::vector<SwipePoint>& path) {
    SwipeResult result;

    // Validate swipe
    if (!isValidSwipe(path)) {
        return result;
    }

    // Smooth and sample path
    std::vector<SwipePoint> smoothed = smoothPath(path);
    std::vector<SwipePoint> sampled = samplePath(smoothed);

    // Convert to key sequence
    std::string keySequence = pathToKeySequence(sampled);

    if (keySequence.length() < 2) {
        return result;
    }

    result.rawSequence = keySequence;

    // Get candidates from dictionary
    std::vector<Candidate> candidates = getCandidates(keySequence);

    if (candidates.empty()) {
        // Fallback: just collapse the sequence
        result.bestWord = collapseSequence(keySequence);
        result.confidence = 0.3f;
        return result;
    }

    // Rank by path distance
    std::vector<Candidate> ranked = rankCandidates(candidates, sampled);

    if (ranked.empty()) {
        result.bestWord = collapseSequence(keySequence);
        result.confidence = 0.3f;
        return result;
    }

    // Build result
    result.bestWord = ranked[0].word;
    result.confidence = calculateConfidence(ranked[0], ranked);

    for (size_t i = 1; i < ranked.size() && i < 4; i++) {
        result.alternatives.push_back(ranked[i].word);
    }

    return result;
}

bool SwipeEngine::isValidSwipe(const std::vector<SwipePoint>& path) const {
    if (path.size() < MIN_SWIPE_POINTS) {
        return false;
    }

    // Calculate total path distance
    float totalDistance = 0.0f;
    for (size_t i = 1; i < path.size(); i++) {
        totalDistance += distance(path[i-1], path[i]);
    }

    return totalDistance >= MIN_SWIPE_DISTANCE;
}

std::vector<SwipePoint> SwipeEngine::smoothPath(const std::vector<SwipePoint>& path) const {
    if (path.size() < 3) {
        return path;
    }

    std::vector<SwipePoint> smoothed;
    smoothed.reserve(path.size());

    // Add first point unchanged
    smoothed.push_back(path[0]);

    // Simple moving average smoothing (window = 3)
    for (size_t i = 1; i < path.size() - 1; i++) {
        SwipePoint p;
        p.x = (path[i-1].x + path[i].x + path[i+1].x) / 3.0f;
        p.y = (path[i-1].y + path[i].y + path[i+1].y) / 3.0f;
        p.timestamp = path[i].timestamp;
        smoothed.push_back(p);
    }

    // Add last point unchanged
    smoothed.push_back(path[path.size() - 1]);

    return smoothed;
}

std::vector<SwipePoint> SwipeEngine::samplePath(const std::vector<SwipePoint>& path) const {
    if (path.size() <= PATH_SAMPLE_INTERVAL) {
        return path;
    }

    std::vector<SwipePoint> sampled;
    sampled.reserve(path.size() / PATH_SAMPLE_INTERVAL + 2);

    // Always include first point
    sampled.push_back(path[0]);

    // Sample every Nth point
    for (size_t i = PATH_SAMPLE_INTERVAL; i < path.size(); i += PATH_SAMPLE_INTERVAL) {
        sampled.push_back(path[i]);
    }

    // Always include last point
    if (sampled.back().x != path.back().x || sampled.back().y != path.back().y) {
        sampled.push_back(path.back());
    }

    return sampled;
}

std::string SwipeEngine::pathToKeySequence(const std::vector<SwipePoint>& path) const {
    if (keyboardLayout_.empty()) {
        return "";
    }

    std::string sequence;
    sequence.reserve(path.size());

    char lastKey = 0;

    for (const auto& point : path) {
        KeyBounds* nearestKey = const_cast<SwipeEngine*>(this)->findNearestKey(point.x, point.y);

        if (nearestKey && nearestKey->key != lastKey) {
            sequence += nearestKey->key;
            lastKey = nearestKey->key;
        }
    }

    return sequence;
}

KeyBounds* SwipeEngine::findNearestKey(float x, float y) {
    KeyBounds* nearest = nullptr;
    float minDist = std::numeric_limits<float>::max();

    for (auto& key : keyboardLayout_) {
        float dist = pointToRectDistance(x, y, key);

        // Check if inside bounds (distance = 0)
        if (dist == 0.0f) {
            return &key;
        }

        if (dist < minDist) {
            minDist = dist;
            nearest = &key;
        }
    }

    // Only accept keys within reasonable radius
    if (nearest && minDist <= KEY_HIT_RADIUS * std::max(nearest->width, nearest->height)) {
        return nearest;
    }

    return nullptr;
}

float SwipeEngine::pointToRectDistance(float px, float py, const KeyBounds& rect) {
    float left = rect.centerX - rect.width / 2.0f;
    float right = rect.centerX + rect.width / 2.0f;
    float top = rect.centerY - rect.height / 2.0f;
    float bottom = rect.centerY + rect.height / 2.0f;

    // Point inside rectangle
    if (px >= left && px <= right && py >= top && py <= bottom) {
        return 0.0f;
    }

    // Distance to nearest edge
    float dx = std::max(left - px, px - right);
    float dy = std::max(top - py, py - bottom);

    dx = std::max(0.0f, dx);
    dy = std::max(0.0f, dy);

    return std::sqrt(dx * dx + dy * dy);
}

std::string SwipeEngine::collapseSequence(const std::string& sequence) const {
    if (sequence.empty()) {
        return "";
    }

    std::string collapsed;
    collapsed.reserve(sequence.length());

    char last = 0;
    for (char c : sequence) {
        if (c != last) {
            collapsed += c;
            last = c;
        }
    }

    return collapsed;
}

std::vector<SwipeEngine::Candidate> SwipeEngine::getCandidates(const std::string& keySequence) const {
    std::vector<Candidate> candidates;

    if (!trie_ || keySequence.empty()) {
        return candidates;
    }

    // Collapse sequence (remove consecutive duplicates)
    std::string collapsed = collapseSequence(keySequence);

    // Get words starting with first letter
    char firstChar = std::tolower(collapsed[0]);
    auto words = trie_->getWordsByFirstLetter(firstChar, MAX_CANDIDATES * 2);

    // Filter by collapsed sequence matching
    for (const auto& word : words) {
        // Simple substring check
        if (word.length() >= collapsed.length() - 1 &&
            word.length() <= collapsed.length() + 3) {

            int freq = trie_->getFrequency(word);

            Candidate cand;
            cand.word = word;
            cand.distance = 0.0f;
            cand.frequency = freq;
            cand.score = static_cast<float>(freq);

            candidates.push_back(cand);

            if (candidates.size() >= MAX_CANDIDATES) {
                break;
            }
        }
    }

    return candidates;
}

std::vector<SwipeEngine::Candidate> SwipeEngine::rankCandidates(
        const std::vector<Candidate>& candidates,
        const std::vector<SwipePoint>& path) const {

    std::vector<Candidate> ranked = candidates;

    // Calculate path distance for each candidate
    for (auto& cand : ranked) {
        cand.distance = calculatePathDistance(cand.word, path);

        // Combined score: lower distance = higher score, higher frequency = higher score
        // Score = frequency * (1 / (1 + distance))
        cand.score = cand.frequency / (1.0f + cand.distance * 0.1f);
    }

    // Sort by score (descending)
    std::sort(ranked.begin(), ranked.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.score > b.score;
              });

    return ranked;
}

float SwipeEngine::calculatePathDistance(const std::string& word,
                                        const std::vector<SwipePoint>& path) const {
    if (keyboardLayout_.empty() || word.empty()) {
        return 1000.0f;
    }

    // Find key positions for word
    std::vector<KeyBounds*> wordKeys;
    wordKeys.reserve(word.length());

    for (char c : word) {
        for (auto& key : const_cast<std::vector<KeyBounds>&>(keyboardLayout_)) {
            if (std::tolower(key.key) == std::tolower(c)) {
                wordKeys.push_back(&key);
                break;
            }
        }
    }

    if (wordKeys.empty()) {
        return 1000.0f;
    }

    // Calculate average distance from path to word keys
    float totalDist = 0.0f;
    int count = 0;

    for (const auto& point : path) {
        float minDist = std::numeric_limits<float>::max();

        for (const auto* key : wordKeys) {
            float dx = point.x - key->centerX;
            float dy = point.y - key->centerY;
            float dist = std::sqrt(dx * dx + dy * dy);

            minDist = std::min(minDist, dist);
        }

        totalDist += minDist;
        count++;
    }

    return count > 0 ? totalDist / count : 1000.0f;
}

float SwipeEngine::calculateConfidence(const Candidate& best,
                                      const std::vector<Candidate>& candidates) const {
    if (candidates.size() < 2) {
        return 0.9f;
    }

    float bestScore = best.score;
    float secondScore = candidates[1].score;

    if (secondScore == 0) {
        return 0.9f;
    }

    // Confidence = ratio of best to second-best
    float ratio = bestScore / secondScore;

    // Clamp to [0.3, 0.95]
    float confidence = 0.5f + (ratio - 1.0f) * 0.2f;
    confidence = std::max(0.3f, std::min(0.95f, confidence));

    return confidence;
}

float SwipeEngine::distance(const SwipePoint& a, const SwipePoint& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace hoskey
