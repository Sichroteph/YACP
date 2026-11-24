#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace yacp_release {
struct Version {
  uint32_t parts[3] = {};
  uint32_t build = 0;
  bool development = false;
};

inline bool number(const char*& p, uint32_t& value) {
  if (*p < '0' || *p > '9') return false;
  value = 0;
  do {
    const unsigned digit = *p++ - '0';
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  } while (*p >= '0' && *p <= '9');
  return true;
}

// Public YACP numbering is independent of CrossPoint. Only embedded running
// versions may include a board/variant suffix; release tags must not have one.
inline bool parseVersion(const char* p, Version& result, bool embedded = false) {
  if (!p) return false;
  Version parsed;
  if (*p == 'v') ++p;
  for (unsigned i = 0; i < 3; ++i) {
    if (!number(p, parsed.parts[i])) return false;
    if (i < 2 && *p++ != '.') return false;
  }
  if (std::strncmp(p, "-yacp", 5) != 0) return false;
  p += 5;
  if (*p == '.') {
    ++p;
    parsed.development = true;
    if (!number(p, parsed.build)) return false;
  }
  if (*p && (!embedded || (std::strcmp(p, "-tiny") && std::strcmp(p, "-xlarge") &&
                           std::strcmp(p, "-x4pro-tiny") && std::strcmp(p, "-x4pro-xlarge") &&
                           std::strcmp(p, "-x4c-tiny") && std::strcmp(p, "-x4c-xlarge")))) return false;
  result = parsed;
  return true;
}

inline bool newer(const char* latest, const char* running) {
  Version a, b;
  if (!parseVersion(latest, a) || !parseVersion(running, b, true)) return false;
  for (unsigned i = 0; i < 3; ++i) {
    if (a.parts[i] != b.parts[i]) return a.parts[i] > b.parts[i];
  }
  if (a.development != b.development) return !a.development;
  return a.development && a.build > b.build;
}

inline bool assetName(char* out, size_t capacity, const char* tag, const char* variant) {
  Version parsed;
  if (!parseVersion(tag, parsed)) return false;
  if (*tag == 'v') ++tag;
  const int size = std::snprintf(out, capacity, "YACP-%s-%s.bin", tag, variant);
  return size > 0 && static_cast<size_t>(size) < capacity;
}

inline bool matchesVariant(const char* name, const char* variant) {
  if (std::strncmp(name, "YACP-", 5)) return false;
  const size_t size = std::strlen(name), suffix = std::strlen(variant) + 5;
  if (size <= 5 + suffix || size - suffix - 5 >= 32) return false;
  const char* end = name + size - suffix;
  if (*end != '-' || std::strncmp(end + 1, variant, std::strlen(variant)) ||
      std::strcmp(name + size - 4, ".bin")) return false;
  char version[32];
  std::memcpy(version, name + 5, size - suffix - 5);
  version[size - suffix - 5] = 0;
  Version parsed;
  return parseVersion(version, parsed);
}

inline bool digest(const char* text, uint8_t (&out)[32]) {
  if (!text || std::strlen(text) != 71 || std::strncmp(text, "sha256:", 7)) return false;
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (unsigned i = 0; i < 32; ++i) {
    const int hi = digit(text[7 + i * 2]), lo = digit(text[8 + i * 2]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}
}  // namespace yacp_release
