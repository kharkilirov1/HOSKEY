/**
 * Yandex-style Beam Search for Swipe Recognition
 * 
 * Based on Yandex json_config.json parameters:
 * - BeamWidth: 300
 * - KeySquaredDistanceLimit: 3000
 * - MaxTransitionSquaredDistance: 10000
 * - StartKeyDistanceWeightX: 0.8
 * - StartKeyDistanceWeightY: 1.4
 * - TopK: 24
 */

#ifndef HOSKEY_SWIPE_BEAM_SEARCH_H
#define HOSKEY_SWIPE_BEAM_SEARCH_H

#include <vector>
#include <string>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <functional>

namespace hoskey {

// Yandex Swipe/Rule parameters
struct SwipeParams {
    int beamWidth = 300;
    float keySquaredDistanceLimit = 3000.0f;
    float maxTransitionSquaredDistance = 10000.0f;
    float weightX = 0.8f;
    float weightY = 1.4f;
    int topK = 24;
    float minSamplingDistance = 0.5f;
};

// Key position info
struct KeyInfo {
    std::string key;
    float centerX;
    float centerY;
    float width;
    float height;
};

// Swipe point
struct SwipePoint {
    float x;
    float y;
    int64_t timestamp;
};

// Beam candidate during search
struct BeamCandidate {
    std::string path;           // Accumulated key sequence
    float score;                // Cumulative score
    float lastX, lastY;         // Last point position
    int pathIndex;              // Current position in swipe path
    
    bool operator<(const BeamCandidate& other) const {
        return score < other.score;  // Max heap
    }
};

// Final candidate with word
struct SwipeCandidate {
    std::string word;
    float score;
    float proximityScore;
    float lengthScore;
    int frequency;
};

/**
 * Beam Search Swipe Decoder
 * 
 * Algorithm:
 * 1. For each point in swipe path, expand beam with nearest keys
 * 2. Score candidates based on weighted distance (X*0.8, Y*1.4)
 * 3. Check transition distance constraint
 * 4. Prune to BeamWidth candidates
 * 5. Match final sequences against dictionary
 */
class SwipeBeamSearch {
public:
    SwipeBeamSearch(const SwipeParams& params = SwipeParams())
        : params_(params) {}
    
    /**
     * Set keyboard layout
     */
    void setLayout(const std::vector<KeyInfo>& layout) {
        layout_ = layout;
        buildKeyMap();
    }
    
    /**
     * Main decode function
     * @param path Swipe points
     * @param dictionary Function to check if word exists and get frequency
     * @param getSuggestions Function to get word suggestions for prefix
     * @return Ranked candidates
     */
    std::vector<SwipeCandidate> decode(
        const std::vector<SwipePoint>& path,
        std::function<bool(const std::string&)> contains,
        std::function<int(const std::string&)> getFrequency,
        std::function<std::vector<std::string>(const std::string&, int)> getSuggestions
    ) {
        if (path.size() < 3 || layout_.empty()) {
            return {};
        }
        
        // Step 1: Generate key sequences using beam search
        auto sequences = beamSearchDecode(path);
        
        if (sequences.empty()) {
            return {};
        }
        
        // Step 2: Match sequences to dictionary words
        std::vector<SwipeCandidate> candidates;
        std::unordered_set<std::string> seenWords;
        
        for (const auto& seq : sequences) {
            if (seq.path.empty()) continue;
            
            // Get first letter
            std::string firstLetter = seq.path.substr(0, 1);
            
            // Get suggestions for this prefix
            auto suggestions = getSuggestions(firstLetter, params_.topK * 3);
            
            for (const auto& word : suggestions) {
                if (seenWords.count(word)) continue;
                
                // Length filter
                int lenDiff = std::abs((int)word.length() - (int)seq.path.length());
                if (lenDiff > 3) continue;
                
                // Check if word matches sequence pattern
                float matchScore = calculateMatchScore(word, seq.path);
                if (matchScore < 0.3f) continue;
                
                // Calculate proximity score against path
                float proximityScore = calculateProximityScore(word, path);
                
                // Get frequency
                int freq = getFrequency(word);
                float freqBoost = std::min(0.15f, freq * 0.0001f);
                
                // Length score (prefer matching length)
                float lengthScore = 1.0f - (lenDiff * 0.1f);
                
                // Combined score
                float finalScore = proximityScore * 0.5f + 
                                   matchScore * 0.25f + 
                                   lengthScore * 0.1f +
                                   freqBoost;
                
                candidates.push_back({
                    word,
                    finalScore,
                    proximityScore,
                    lengthScore,
                    freq
                });
                seenWords.insert(word);
                
                if (candidates.size() >= (size_t)params_.topK) break;
            }
            
            if (candidates.size() >= (size_t)params_.topK) break;
        }
        
        // Sort by score
        std::sort(candidates.begin(), candidates.end(),
            [](const SwipeCandidate& a, const SwipeCandidate& b) {
                return a.score > b.score;
            });
        
        // Return top K
        if (candidates.size() > (size_t)params_.topK) {
            candidates.resize(params_.topK);
        }
        
        return candidates;
    }
    
private:
    SwipeParams params_;
    std::vector<KeyInfo> layout_;
    std::unordered_map<std::string, size_t> keyMap_;
    
    void buildKeyMap() {
        keyMap_.clear();
        for (size_t i = 0; i < layout_.size(); ++i) {
            keyMap_[layout_[i].key] = i;
        }
    }
    
    /**
     * Calculate weighted squared distance (Yandex style)
     */
    float weightedDistanceSq(float x1, float y1, float x2, float y2) const {
        float dx = (x1 - x2) * params_.weightX;
        float dy = (y1 - y2) * params_.weightY;
        return dx * dx + dy * dy;
    }
    
    /**
     * Get nearest keys with scores for a point
     */
    std::vector<std::pair<std::string, float>> getNearestKeys(float x, float y, int topN = 3) const {
        std::vector<std::pair<std::string, float>> result;
        
        for (const auto& key : layout_) {
            float distSq = weightedDistanceSq(x, y, key.centerX, key.centerY);
            
            if (distSq <= params_.keySquaredDistanceLimit) {
                float score = 1.0f - (distSq / params_.keySquaredDistanceLimit);
                result.emplace_back(key.key, score);
            }
        }
        
        // Sort by score descending
        std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        if (result.size() > (size_t)topN) {
            result.resize(topN);
        }
        
        return result;
    }
    
    /**
     * Beam search to generate key sequences
     */
    std::vector<BeamCandidate> beamSearchDecode(const std::vector<SwipePoint>& path) {
        // Sample path to reduce computation
        std::vector<SwipePoint> sampledPath = samplePath(path);
        
        if (sampledPath.empty()) return {};
        
        // Initialize beam with first point's keys
        std::priority_queue<BeamCandidate> beam;
        auto firstKeys = getNearestKeys(sampledPath[0].x, sampledPath[0].y, 5);
        
        for (const auto& [key, score] : firstKeys) {
            beam.push({key, score, sampledPath[0].x, sampledPath[0].y, 0});
        }
        
        // Process each subsequent point
        for (size_t i = 1; i < sampledPath.size(); ++i) {
            const auto& point = sampledPath[i];
            auto nearestKeys = getNearestKeys(point.x, point.y, 3);
            
            std::priority_queue<BeamCandidate> newBeam;
            std::unordered_set<std::string> seenPaths;
            
            // Expand each candidate in beam
            int processed = 0;
            while (!beam.empty() && processed < params_.beamWidth) {
                auto cand = beam.top();
                beam.pop();
                processed++;
                
                // Check transition distance constraint
                float transDistSq = weightedDistanceSq(
                    cand.lastX, cand.lastY, point.x, point.y);
                
                if (transDistSq > params_.maxTransitionSquaredDistance) {
                    // Skip if transition too large
                    continue;
                }
                
                // Expand with each possible next key
                for (const auto& [key, keyScore] : nearestKeys) {
                    std::string newPath = cand.path;
                    
                    // Only add if different from last key (avoid repetition)
                    if (newPath.empty() || newPath.back() != key[0]) {
                        newPath += key;
                    }
                    
                    // Skip if we've seen this path
                    if (seenPaths.count(newPath)) continue;
                    seenPaths.insert(newPath);
                    
                    // Calculate new score (geometric mean for stability)
                    float newScore = cand.score * 0.9f + keyScore * 0.1f;
                    
                    newBeam.push({newPath, newScore, point.x, point.y, (int)i});
                }
                
                // Also allow skipping this point (for fast swipes)
                if (cand.pathIndex < (int)i - 1) {
                    newBeam.push({cand.path, cand.score * 0.95f, cand.lastX, cand.lastY, (int)i});
                }
            }
            
            beam = std::move(newBeam);
            
            // Prune to beam width
            std::priority_queue<BeamCandidate> prunedBeam;
            int kept = 0;
            while (!beam.empty() && kept < params_.beamWidth) {
                prunedBeam.push(beam.top());
                beam.pop();
                kept++;
            }
            beam = std::move(prunedBeam);
        }
        
        // Extract top sequences
        std::vector<BeamCandidate> result;
        while (!beam.empty() && result.size() < (size_t)params_.topK) {
            result.push_back(beam.top());
            beam.pop();
        }
        
        return result;
    }
    
    /**
     * Sample path to reduce points while keeping shape
     */
    std::vector<SwipePoint> samplePath(const std::vector<SwipePoint>& path) const {
        if (path.size() <= 10) return path;
        
        std::vector<SwipePoint> sampled;
        sampled.push_back(path[0]);
        
        float accumDist = 0;
        for (size_t i = 1; i < path.size(); ++i) {
            float dx = path[i].x - path[i-1].x;
            float dy = path[i].y - path[i-1].y;
            float dist = std::sqrt(dx*dx + dy*dy);
            accumDist += dist;
            
            if (accumDist >= params_.minSamplingDistance * 10) {  // Sample every ~5 pixels
                sampled.push_back(path[i]);
                accumDist = 0;
            }
        }
        
        // Always include last point
        if (sampled.back().x != path.back().x || sampled.back().y != path.back().y) {
            sampled.push_back(path.back());
        }
        
        return sampled;
    }
    
    /**
     * Calculate how well a word matches a key sequence
     */
    float calculateMatchScore(const std::string& word, const std::string& sequence) const {
        if (word.empty() || sequence.empty()) return 0;
        
        // First and last character match bonus
        float score = 0;
        if (std::tolower(word[0]) == std::tolower(sequence[0])) {
            score += 0.3f;
        }
        if (std::tolower(word.back()) == std::tolower(sequence.back())) {
            score += 0.2f;
        }
        
        // Character overlap
        std::unordered_set<char> wordChars, seqChars;
        for (char c : word) wordChars.insert(std::tolower(c));
        for (char c : sequence) seqChars.insert(std::tolower(c));
        
        int overlap = 0;
        for (char c : wordChars) {
            if (seqChars.count(c)) overlap++;
        }
        
        float overlapRatio = (float)overlap / std::max(wordChars.size(), seqChars.size());
        score += overlapRatio * 0.5f;
        
        return score;
    }
    
    /**
     * Calculate proximity score for word against actual path
     */
    float calculateProximityScore(const std::string& word, const std::vector<SwipePoint>& path) const {
        if (word.empty() || path.empty()) return 0;
        
        // Build ideal path for word
        std::vector<std::pair<float, float>> idealPath;
        for (char c : word) {
            std::string key(1, std::tolower(c));
            auto it = keyMap_.find(key);
            if (it != keyMap_.end()) {
                const auto& keyInfo = layout_[it->second];
                idealPath.emplace_back(keyInfo.centerX, keyInfo.centerY);
            }
        }
        
        if (idealPath.empty()) return 0;
        
        // DTW-like distance calculation
        float totalDist = 0;
        size_t idealIdx = 0;
        
        for (const auto& point : path) {
            if (idealIdx >= idealPath.size()) break;
            
            float distSq = weightedDistanceSq(
                point.x, point.y, 
                idealPath[idealIdx].first, idealPath[idealIdx].second);
            
            // If close enough to current target, move to next
            if (distSq < params_.keySquaredDistanceLimit * 0.5f) {
                idealIdx++;
            }
            
            totalDist += std::sqrt(distSq);
        }
        
        // Normalize and convert to score
        float avgDist = totalDist / path.size();
        float maxDist = std::sqrt(params_.keySquaredDistanceLimit);
        float score = 1.0f - std::min(1.0f, avgDist / maxDist);
        
        // Bonus if we matched all characters
        if (idealIdx >= idealPath.size()) {
            score += 0.1f;
        }
        
        return std::min(1.0f, score);
    }
};

} // namespace hoskey

#endif // HOSKEY_SWIPE_BEAM_SEARCH_H
