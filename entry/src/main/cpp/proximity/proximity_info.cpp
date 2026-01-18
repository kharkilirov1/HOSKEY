/**
 * Proximity Info implementation
 * Keyboard layouts for QWERTY and ЙЦУКЕН
 */

#include "proximity_info.h"
#include <cmath>
#include <algorithm>

namespace hoskey {

ProximityInfo::ProximityInfo(const std::string& layout, float keyWidth, float keyHeight)
    : layoutName_(layout)
    , keyWidth_(keyWidth > 0 ? keyWidth : 1.0f)
    , keyHeight_(keyHeight > 0 ? keyHeight : 1.0f)
    , proximityThreshold_(1.5f)  // Keys within 1.5 key-widths are "proximate"
    , initialized_(false) {

    initializeLayout(layout);
}

ProximityInfo::~ProximityInfo() = default;

void ProximityInfo::initializeLayout(const std::string& layout) {
    keyPositions_.clear();
    proximityCache_.clear();

    std::string layoutLower = layout;
    std::transform(layoutLower.begin(), layoutLower.end(), layoutLower.begin(), ::tolower);

    if (layoutLower == "qwerty" || layoutLower == "en") {
        initializeQWERTY();
    } else if (layoutLower == "ycuken" || layoutLower == "ru" ||
               layoutLower == "йцукен" || layoutLower == "russian") {
        initializeYCUKEN();
    } else if (layoutLower == "azerty" || layoutLower == "fr") {
        initializeAZERTY();
    } else if (layoutLower == "qwertz" || layoutLower == "de") {
        initializeQWERTZ();
    } else {
        // Default to QWERTY
        initializeQWERTY();
    }

    buildProximityCache();
    initialized_ = true;
}

void ProximityInfo::initializeQWERTY() {
    // Row 0 (top): q w e r t y u i o p
    // Row 1: a s d f g h j k l
    // Row 2 (bottom): z x c v b n m

    // Top row
    keyPositions_['q'] = KeyPosition(0.0f, 0.0f);
    keyPositions_['w'] = KeyPosition(1.0f, 0.0f);
    keyPositions_['e'] = KeyPosition(2.0f, 0.0f);
    keyPositions_['r'] = KeyPosition(3.0f, 0.0f);
    keyPositions_['t'] = KeyPosition(4.0f, 0.0f);
    keyPositions_['y'] = KeyPosition(5.0f, 0.0f);
    keyPositions_['u'] = KeyPosition(6.0f, 0.0f);
    keyPositions_['i'] = KeyPosition(7.0f, 0.0f);
    keyPositions_['o'] = KeyPosition(8.0f, 0.0f);
    keyPositions_['p'] = KeyPosition(9.0f, 0.0f);

    // Middle row (offset by 0.25)
    keyPositions_['a'] = KeyPosition(0.25f, 1.0f);
    keyPositions_['s'] = KeyPosition(1.25f, 1.0f);
    keyPositions_['d'] = KeyPosition(2.25f, 1.0f);
    keyPositions_['f'] = KeyPosition(3.25f, 1.0f);
    keyPositions_['g'] = KeyPosition(4.25f, 1.0f);
    keyPositions_['h'] = KeyPosition(5.25f, 1.0f);
    keyPositions_['j'] = KeyPosition(6.25f, 1.0f);
    keyPositions_['k'] = KeyPosition(7.25f, 1.0f);
    keyPositions_['l'] = KeyPosition(8.25f, 1.0f);

    // Bottom row (offset by 0.75)
    keyPositions_['z'] = KeyPosition(0.75f, 2.0f);
    keyPositions_['x'] = KeyPosition(1.75f, 2.0f);
    keyPositions_['c'] = KeyPosition(2.75f, 2.0f);
    keyPositions_['v'] = KeyPosition(3.75f, 2.0f);
    keyPositions_['b'] = KeyPosition(4.75f, 2.0f);
    keyPositions_['n'] = KeyPosition(5.75f, 2.0f);
    keyPositions_['m'] = KeyPosition(6.75f, 2.0f);
}

void ProximityInfo::initializeYCUKEN() {
    // Russian ЙЦУКЕН layout
    // Row 0: й ц у к е н г ш щ з х ъ
    // Row 1: ф ы в а п р о л д ж э
    // Row 2: я ч с м и т ь б ю

    // Top row (using UTF-8 bytes - first byte of Cyrillic lowercase)
    // Note: For simplicity, we use character codes offset from а (0x430 = 1072)
    // й=1081, ц=1094, у=1091, к=1082, е=1077, н=1085, г=1075, ш=1096, щ=1097, з=1079, х=1093, ъ=1098

    // For UTF-8 chars, we'll use a simple mapping approach
    // Since char is 1 byte, we map Cyrillic to their code points modulo 256 or use special handling

    // Actually, for UTF-8 Russian, each character is 2 bytes
    // We'll store the key positions using the lower byte of the unicode code point

    // Row 0 - Cyrillic top row
    const char* row0 = "йцукенгшщзхъ";
    const char* row1 = "фывапролджэ";
    const char* row2 = "ячсмитьбю";

    // For UTF-8 processing, we need to handle multi-byte characters
    // Simplified: use raw bytes and hope for consistent behavior

    float col = 0.0f;
    for (size_t i = 0; row0[i] != '\0'; ) {
        // Get UTF-8 character (2 bytes for Cyrillic)
        if ((row0[i] & 0x80) != 0) {
            // Multi-byte UTF-8
            char key = row0[i + 1];  // Use second byte as key
            keyPositions_[key] = KeyPosition(col, 0.0f);
            i += 2;
        } else {
            keyPositions_[row0[i]] = KeyPosition(col, 0.0f);
            i++;
        }
        col += 1.0f;
    }

    col = 0.25f;
    for (size_t i = 0; row1[i] != '\0'; ) {
        if ((row1[i] & 0x80) != 0) {
            char key = row1[i + 1];
            keyPositions_[key] = KeyPosition(col, 1.0f);
            i += 2;
        } else {
            keyPositions_[row1[i]] = KeyPosition(col, 1.0f);
            i++;
        }
        col += 1.0f;
    }

    col = 0.75f;
    for (size_t i = 0; row2[i] != '\0'; ) {
        if ((row2[i] & 0x80) != 0) {
            char key = row2[i + 1];
            keyPositions_[key] = KeyPosition(col, 2.0f);
            i += 2;
        } else {
            keyPositions_[row2[i]] = KeyPosition(col, 2.0f);
            i++;
        }
        col += 1.0f;
    }
}

void ProximityInfo::initializeAZERTY() {
    // French AZERTY layout
    // Row 0: a z e r t y u i o p
    // Row 1: q s d f g h j k l m
    // Row 2: w x c v b n

    // Top row
    keyPositions_['a'] = KeyPosition(0.0f, 0.0f);
    keyPositions_['z'] = KeyPosition(1.0f, 0.0f);
    keyPositions_['e'] = KeyPosition(2.0f, 0.0f);
    keyPositions_['r'] = KeyPosition(3.0f, 0.0f);
    keyPositions_['t'] = KeyPosition(4.0f, 0.0f);
    keyPositions_['y'] = KeyPosition(5.0f, 0.0f);
    keyPositions_['u'] = KeyPosition(6.0f, 0.0f);
    keyPositions_['i'] = KeyPosition(7.0f, 0.0f);
    keyPositions_['o'] = KeyPosition(8.0f, 0.0f);
    keyPositions_['p'] = KeyPosition(9.0f, 0.0f);

    // Middle row
    keyPositions_['q'] = KeyPosition(0.25f, 1.0f);
    keyPositions_['s'] = KeyPosition(1.25f, 1.0f);
    keyPositions_['d'] = KeyPosition(2.25f, 1.0f);
    keyPositions_['f'] = KeyPosition(3.25f, 1.0f);
    keyPositions_['g'] = KeyPosition(4.25f, 1.0f);
    keyPositions_['h'] = KeyPosition(5.25f, 1.0f);
    keyPositions_['j'] = KeyPosition(6.25f, 1.0f);
    keyPositions_['k'] = KeyPosition(7.25f, 1.0f);
    keyPositions_['l'] = KeyPosition(8.25f, 1.0f);
    keyPositions_['m'] = KeyPosition(9.25f, 1.0f);

    // Bottom row
    keyPositions_['w'] = KeyPosition(0.75f, 2.0f);
    keyPositions_['x'] = KeyPosition(1.75f, 2.0f);
    keyPositions_['c'] = KeyPosition(2.75f, 2.0f);
    keyPositions_['v'] = KeyPosition(3.75f, 2.0f);
    keyPositions_['b'] = KeyPosition(4.75f, 2.0f);
    keyPositions_['n'] = KeyPosition(5.75f, 2.0f);
}

void ProximityInfo::initializeQWERTZ() {
    // German QWERTZ layout
    // Row 0: q w e r t z u i o p
    // Row 1: a s d f g h j k l
    // Row 2: y x c v b n m

    // Top row
    keyPositions_['q'] = KeyPosition(0.0f, 0.0f);
    keyPositions_['w'] = KeyPosition(1.0f, 0.0f);
    keyPositions_['e'] = KeyPosition(2.0f, 0.0f);
    keyPositions_['r'] = KeyPosition(3.0f, 0.0f);
    keyPositions_['t'] = KeyPosition(4.0f, 0.0f);
    keyPositions_['z'] = KeyPosition(5.0f, 0.0f);  // Z instead of Y
    keyPositions_['u'] = KeyPosition(6.0f, 0.0f);
    keyPositions_['i'] = KeyPosition(7.0f, 0.0f);
    keyPositions_['o'] = KeyPosition(8.0f, 0.0f);
    keyPositions_['p'] = KeyPosition(9.0f, 0.0f);

    // Middle row
    keyPositions_['a'] = KeyPosition(0.25f, 1.0f);
    keyPositions_['s'] = KeyPosition(1.25f, 1.0f);
    keyPositions_['d'] = KeyPosition(2.25f, 1.0f);
    keyPositions_['f'] = KeyPosition(3.25f, 1.0f);
    keyPositions_['g'] = KeyPosition(4.25f, 1.0f);
    keyPositions_['h'] = KeyPosition(5.25f, 1.0f);
    keyPositions_['j'] = KeyPosition(6.25f, 1.0f);
    keyPositions_['k'] = KeyPosition(7.25f, 1.0f);
    keyPositions_['l'] = KeyPosition(8.25f, 1.0f);

    // Bottom row
    keyPositions_['y'] = KeyPosition(0.75f, 2.0f);  // Y instead of Z
    keyPositions_['x'] = KeyPosition(1.75f, 2.0f);
    keyPositions_['c'] = KeyPosition(2.75f, 2.0f);
    keyPositions_['v'] = KeyPosition(3.75f, 2.0f);
    keyPositions_['b'] = KeyPosition(4.75f, 2.0f);
    keyPositions_['n'] = KeyPosition(5.75f, 2.0f);
    keyPositions_['m'] = KeyPosition(6.75f, 2.0f);
}

void ProximityInfo::buildProximityCache() {
    proximityCache_.clear();

    float thresholdSquared = proximityThreshold_ * proximityThreshold_;

    for (const auto& pair1 : keyPositions_) {
        char key1 = pair1.first;
        const KeyPosition& pos1 = pair1.second;

        for (const auto& pair2 : keyPositions_) {
            char key2 = pair2.first;
            if (key1 == key2) continue;

            const KeyPosition& pos2 = pair2.second;
            float distSquared = calculateSquaredDistance(pos1, pos2);

            if (distSquared <= thresholdSquared) {
                proximityCache_[key1].insert(key2);
            }
        }
    }
}

float ProximityInfo::calculateSquaredDistance(const KeyPosition& a, const KeyPosition& b) const {
    float dx = (a.x - b.x) * keyWidth_;
    float dy = (a.y - b.y) * keyHeight_;
    return dx * dx + dy * dy;
}

bool ProximityInfo::areProximate(char a, char b) const {
    if (a == b) return true;

    auto it = proximityCache_.find(a);
    if (it != proximityCache_.end()) {
        return it->second.count(b) > 0;
    }
    return false;
}

float ProximityInfo::getDistance(char a, char b) const {
    if (a == b) return 0.0f;

    auto it1 = keyPositions_.find(a);
    auto it2 = keyPositions_.find(b);

    if (it1 == keyPositions_.end() || it2 == keyPositions_.end()) {
        return 1.0f;  // Max distance if key not found
    }

    float distSquared = calculateSquaredDistance(it1->second, it2->second);
    float maxDist = 10.0f * keyWidth_;  // Approximate max keyboard width

    return std::min(1.0f, std::sqrt(distSquared) / maxDist);
}

std::vector<char> ProximityInfo::getNearbyKeys(char key) const {
    std::vector<char> result;

    auto it = proximityCache_.find(key);
    if (it != proximityCache_.end()) {
        result.reserve(it->second.size());
        for (char c : it->second) {
            result.push_back(c);
        }
    }

    return result;
}

float ProximityInfo::getProximityScore(char expected, char actual) const {
    if (expected == actual) return 1.0f;

    float distance = getDistance(expected, actual);
    return 1.0f - distance;
}

} // namespace hoskey
