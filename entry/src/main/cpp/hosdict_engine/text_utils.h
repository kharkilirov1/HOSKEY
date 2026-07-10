#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hoskey::engine {

// Decode UTF-8 string to a sequence of Unicode codepoints.
std::vector<char32_t> DecodeUtf8(std::string_view text);

// Encode a sequence of Unicode codepoints back to UTF-8.
std::string EncodeUtf8(const std::vector<char32_t>& codepoints);

// Standard Levenshtein distance on raw bytes (ASCII-safe only).
size_t LevenshteinDistance(std::string_view a, std::string_view b);

// UTF-8 aware Levenshtein distance operating on Unicode codepoints.
size_t LevenshteinDistanceUtf8(std::string_view a, std::string_view b);

}  // namespace hoskey::engine
