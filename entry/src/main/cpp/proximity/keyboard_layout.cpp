/**
 * Keyboard Layout utilities
 * Additional layout-specific functions
 */

#include "proximity_info.h"

namespace hoskey {

/**
 * Get default layout for language code
 */
const char* getDefaultLayoutForLanguage(const char* langCode) {
    if (!langCode) return "qwerty";

    // Russian
    if (langCode[0] == 'r' && langCode[1] == 'u') {
        return "ycuken";
    }

    // French
    if (langCode[0] == 'f' && langCode[1] == 'r') {
        return "azerty";
    }

    // German
    if (langCode[0] == 'd' && langCode[1] == 'e') {
        return "qwertz";
    }

    // Default to QWERTY for English and others
    return "qwerty";
}

/**
 * Check if character is in standard keyboard charset
 */
bool isKeyboardChar(char c, const char* layout) {
    // Lowercase Latin
    if (c >= 'a' && c <= 'z') return true;

    // Uppercase Latin (will be lowercased)
    if (c >= 'A' && c <= 'Z') return true;

    // Numbers
    if (c >= '0' && c <= '9') return true;

    // For Russian layout, check Cyrillic range
    // (simplified - actual check would need UTF-8 handling)

    return false;
}

} // namespace hoskey
