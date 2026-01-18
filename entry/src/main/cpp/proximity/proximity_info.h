/**
 * Proximity Info for HOSKEY
 * Keyboard layout awareness for intelligent correction
 *
 * Supports:
 * - QWERTY (English)
 * - ЙЦУКЕН (Russian)
 * - AZERTY (French)
 * - QWERTZ (German)
 */

#ifndef HOSKEY_PROXIMITY_INFO_H
#define HOSKEY_PROXIMITY_INFO_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hoskey {

/**
 * Key position on keyboard
 */
struct KeyPosition {
    float x;    // Column position (0-based)
    float y;    // Row position (0-based)

    KeyPosition() : x(0), y(0) {}
    KeyPosition(float _x, float _y) : x(_x), y(_y) {}
};

/**
 * Proximity information for keyboard layout
 */
class ProximityInfo {
public:
    ProximityInfo(const std::string& layout, float keyWidth, float keyHeight);
    ~ProximityInfo();

    // Check if two keys are physically close on keyboard
    bool areProximate(char a, char b) const;

    // Get distance between two keys (normalized 0-1)
    float getDistance(char a, char b) const;

    // Get all keys near a given key
    std::vector<char> getNearbyKeys(char key) const;

    // Get proximity score (1.0 = same key, 0.0 = far apart)
    float getProximityScore(char expected, char actual) const;

    // Check if layout is initialized
    bool isInitialized() const { return initialized_; }

    // Get current layout name
    const std::string& getLayoutName() const { return layoutName_; }

private:
    std::string layoutName_;
    float keyWidth_;
    float keyHeight_;
    float proximityThreshold_;
    bool initialized_;

    // Key positions map
    std::unordered_map<char, KeyPosition> keyPositions_;

    // Pre-computed proximity sets for fast lookup
    std::unordered_map<char, std::unordered_set<char>> proximityCache_;

    // Initialize keyboard layout
    void initializeLayout(const std::string& layout);
    void initializeQWERTY();
    void initializeYCUKEN();  // Russian ЙЦУКЕН
    void initializeAZERTY();
    void initializeQWERTZ();

    // Build proximity cache
    void buildProximityCache();

    // Calculate squared distance between keys
    float calculateSquaredDistance(const KeyPosition& a, const KeyPosition& b) const;
};

} // namespace hoskey

#endif // HOSKEY_PROXIMITY_INFO_H
