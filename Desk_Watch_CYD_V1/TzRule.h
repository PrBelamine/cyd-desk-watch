#pragma once
#include <cctype>
#include <cstddef>
#include <cstring>
// A POSIX TZ rule (e.g. "ICT-7", "CET-1CEST,M3.5.0,M10.5.0/3") is accepted only if it is short
// (below `capacity` bytes) and made of safe characters, so a bad web request cannot corrupt the setting.
inline bool tzRuleValid(const char* rule, size_t capacity = 48) {
  if (!rule) return false;
  const size_t n = std::strlen(rule);
  if (n == 0 || n >= capacity) return false;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(rule[i]);
    if (!(std::isalnum(c) || c == '+' || c == '-' || c == ',' || c == '.' || c == ':' || c == '/' || c == '<' || c == '>')) return false;
  }
  return std::isalpha(static_cast<unsigned char>(rule[0])) || rule[0] == '<';
}
