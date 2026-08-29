#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

inline bool equalsIgnoreCase(const auto &a, const auto &b) {
  using V = std::string_view;
  return std::ranges::equal(V{a}, V{b}, [](auto lhs, auto rhs) {
    return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
  });
}

inline void lowerCase(std::string &s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
}

inline void upperCase(std::string &s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
}
