#include "text_utils.h"

#include <algorithm>
#include <cstdint>

namespace hoskey::engine {

std::vector<char32_t> DecodeUtf8(std::string_view text) {
  std::vector<char32_t> result;
  result.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    const auto byte = static_cast<uint8_t>(text[i]);
    char32_t cp = 0;
    size_t len = 0;
    if (byte < 0x80) {
      cp = byte;
      len = 1;
    } else if ((byte & 0xE0) == 0xC0) {
      cp = byte & 0x1F;
      len = 2;
    } else if ((byte & 0xF0) == 0xE0) {
      cp = byte & 0x0F;
      len = 3;
    } else if ((byte & 0xF8) == 0xF0) {
      cp = byte & 0x07;
      len = 4;
    } else {
      // Invalid leading byte — skip.
      ++i;
      continue;
    }
    if (i + len > text.size()) {
      break;
    }
    for (size_t j = 1; j < len; ++j) {
      cp = (cp << 6) | (static_cast<uint8_t>(text[i + j]) & 0x3F);
    }
    result.push_back(cp);
    i += len;
  }
  return result;
}

std::string EncodeUtf8(const std::vector<char32_t>& codepoints) {
  std::string result;
  result.reserve(codepoints.size() * 2);
  for (const char32_t cp : codepoints) {
    if (cp < 0x80) {
      result.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return result;
}

size_t LevenshteinDistance(std::string_view a, std::string_view b) {
  const size_t m = a.size();
  const size_t n = b.size();
  if (m == 0) return n;
  if (n == 0) return m;

  std::vector<size_t> prev(n + 1);
  std::vector<size_t> curr(n + 1);

  for (size_t j = 0; j <= n; ++j) {
    prev[j] = j;
  }
  for (size_t i = 1; i <= m; ++i) {
    curr[0] = i;
    for (size_t j = 1; j <= n; ++j) {
      const size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, curr);
  }
  return prev[n];
}

size_t LevenshteinDistanceUtf8(std::string_view a, std::string_view b) {
  const auto cp_a = DecodeUtf8(a);
  const auto cp_b = DecodeUtf8(b);
  const size_t m = cp_a.size();
  const size_t n = cp_b.size();
  if (m == 0) return n;
  if (n == 0) return m;

  std::vector<size_t> prev(n + 1);
  std::vector<size_t> curr(n + 1);

  for (size_t j = 0; j <= n; ++j) {
    prev[j] = j;
  }
  for (size_t i = 1; i <= m; ++i) {
    curr[0] = i;
    for (size_t j = 1; j <= n; ++j) {
      const size_t cost = (cp_a[i - 1] == cp_b[j - 1]) ? 0 : 1;
      curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, curr);
  }
  return prev[n];
}

}  // namespace hoskey::engine
