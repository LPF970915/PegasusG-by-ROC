#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

// Offline, first-reading pinyin keys, following retrosync's full/initial search.
namespace game_search {
struct Reading { std::uint32_t code; char text[7]; };
inline constexpr Reading readings[] = {
#include "pinyin_table.inc"
};

inline std::string Normalize(const std::string &text) {
  std::string result;
  for (unsigned char c : text) {
    if (c < 128 && (std::isspace(c) || c == '\'')) continue;
    result += c < 128 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
  }
  return result;
}

inline void Backspace(std::string &text) {
  if (text.empty()) return;
  size_t start = text.size() - 1;
  while (start && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
  text.resize(start);
}

inline std::string Key(const std::string &title) {
  std::string full, initials;
  for (size_t i = 0; i < title.size();) {
    const size_t start = i;
    const auto lead = static_cast<unsigned char>(title[i++]);
    std::uint32_t code = lead;
    int count = 0;
    if ((lead & 0xe0) == 0xc0) { code = lead & 0x1f; count = 1; }
    else if ((lead & 0xf0) == 0xe0) { code = lead & 0x0f; count = 2; }
    else if ((lead & 0xf8) == 0xf0) { code = lead & 0x07; count = 3; }
    while (count-- > 0 && i < title.size() &&
           (static_cast<unsigned char>(title[i]) & 0xc0) == 0x80)
      code = (code << 6) | (static_cast<unsigned char>(title[i++]) & 0x3f);
    const auto *reading = std::lower_bound(std::begin(readings), std::end(readings), code,
        [](const Reading &r, std::uint32_t value) { return r.code < value; });
    if (reading != std::end(readings) && reading->code == code) {
      full += reading->text;
      initials += reading->text[0];
    } else {
      full += title.substr(start, i - start);
      initials += title.substr(start, i - start);
    }
  }
  return Normalize(title) + "\n" + Normalize(full) + "\n" + Normalize(initials);
}
inline bool Matches(const std::string &key, const std::string &query) {
  return key.find(Normalize(query)) != std::string::npos;
}
}  // namespace game_search
