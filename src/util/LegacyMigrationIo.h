#pragma once

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <cstdlib>
#include <cstddef>
#ifdef SIMULATOR
#include <cstdio>
#endif

namespace legacy {
inline constexpr const char* ROOT = "/.yacp-migration-v1";
inline constexpr const char* ORIGINAL = "/.yacp-migration-v1/original";
inline constexpr const char* STAGING = "/.yacp-migration-v1/staging";
inline constexpr size_t JSON_LIMIT = 16384;

inline void checkpoint(const char* phase) {
#ifdef SIMULATOR
  // Native acceptance only: stop at a durable transaction boundary.
  const char* requested = std::getenv("YACP_MIGRATION_STOP_AFTER");
  if (requested && !strcmp(requested, phase)) {
    LOG_INF("MIG", "Simulated interruption after %s", phase);
    std::fflush(nullptr);
    std::_Exit(77);
  }
#else
  (void)phase;
#endif
}

// ArduinoJson owns these blocks through its allocator API and releases them
// with deallocate(). A document cannot exceed 32 KiB, excluding the small aligned allocation headers.
class JsonMemory final : public ArduinoJson::Allocator {
  struct alignas(std::max_align_t) Header { size_t size; };
  size_t used = 0;
 public:
  void* allocate(size_t size) override {
    if (size > 32768 - used || size + sizeof(Header) < size) return nullptr;
    auto* raw = static_cast<Header*>(malloc(sizeof(Header) + size));
    if (!raw) return nullptr;
    raw->size = size; used += size; return raw + 1;
  }
  void deallocate(void* ptr) override {
    if (!ptr) return;
    auto* raw = static_cast<Header*>(ptr) - 1;
    used -= raw->size; free(raw);
  }
  void* reallocate(void* ptr, size_t size) override {
    if (!ptr) return allocate(size);
    const size_t previous = (static_cast<Header*>(ptr) - 1)->size;
    if (size > 32768 - used + previous) return nullptr;
    auto* raw = static_cast<Header*>(realloc(static_cast<Header*>(ptr) - 1, sizeof(Header) + size));
    if (!raw) return nullptr;
    used = used - previous + size; raw->size = size; return raw + 1;
  }
};

inline uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t* p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
inline void put16(uint8_t* p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
inline void put32(uint8_t* p, uint32_t n) { put16(p, n); put16(p + 2, n >> 16); }
inline uint32_t hash32(const uint8_t* p, size_t n, uint32_t h = 2166136261u) {
  while (n--) h = (h ^ *p++) * 16777619u;
  return h;
}
inline uint64_t pathHash(const std::string& path) {
  uint64_t h = 14695981039346656037ull;
  for (unsigned char c : path) h = (h ^ c) * 1099511628211ull;
  return h;
}
inline bool pathSafe(const char* path) {
  if (!path || *path != '/' || strlen(path) > 480 || strstr(path, "//")) return false;
  for (const char* p = path; *p; ++p) {
    if (static_cast<unsigned char>(*p) < 32 || *p == '\\' || *p == ':') return false;
    if (*p == '/' && p[1] == '.' && (p[2] == '/' || p[2] == 0 ||
        (p[2] == '.' && (p[3] == '/' || p[3] == 0)))) return false;
  }
  return true;
}

// One 2 KiB workspace is reused throughout a migration, off the boot stack.
struct Io {
  uint8_t a[1024];
  uint8_t b[1024];
  // Append one deterministic fragment, checking an interrupted run's prefix.
  // This lets bookmark arrays stream without retaining the whole JSON in RAM.
  bool fragment(const std::string& path, size_t offset, const uint8_t* bytes, size_t size) {
    if (!parent(path)) return false;
    size_t present = 0;
    if (Storage.exists(path.c_str())) {
      auto f = Storage.open(path.c_str());
      if (!f || f.isDirectory() || f.size() < offset || !f.seek(offset)) return false;
      present = std::min(size, f.size() - offset);
      for (size_t i = 0; i < present;) {
        const size_t n = std::min(sizeof(a), present - i);
        if (f.read(a, n) != int(n) || memcmp(a, bytes + i, n)) return false;
        i += n;
      }
      if (!f.close()) return false;
    } else if (offset) return false;
    if (present == size) return true;
    auto f = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (!f || f.size() != offset + present || f.write(bytes + present, size - present) != size - present || !f.close()) return false;
    auto check = Storage.open(path.c_str());
    if (!check || check.size() != offset + size || !check.seek(offset)) return false;
    for (size_t i = 0; i < size;) {
      const size_t n = std::min(sizeof(a), size - i);
      if (check.read(a, n) != int(n) || memcmp(a, bytes + i, n)) return false;
      i += n;
    }
    return check.close();
  }
  bool parent(const std::string& path) {
    const auto slash = path.rfind('/');
    // SdFat mkdir fails if the final directory already exists. The HAL
    // helper also verifies that an existing path is a directory.
    if (slash == std::string::npos) return false;
    const auto directory = slash == 0 ? std::string("/") : path.substr(0, slash);
    if (Storage.ensureDirectoryExists(directory.c_str())) return true;
    LOG_ERR("MIG", "Cannot prepare directory: %s", directory.c_str());
    return false;
  }
  bool append(const std::string& path, const uint8_t* bytes, size_t size) {
    if (!parent(path)) return false;
    size_t previous = 0;
    if (Storage.exists(path.c_str())) {
      auto f = Storage.open(path.c_str());
      if (!f || f.isDirectory() || f.size() > size) return false;
      previous = f.size();
      size_t offset = 0;
      while (offset < previous) {
        const auto n = std::min(sizeof(a), previous - offset);
        if (f.read(a, n) != int(n) || memcmp(a, bytes + offset, n)) return false;
        offset += n;
      }
      f.close();
    }
    auto f = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (!f || f.write(bytes + previous, size - previous) != size - previous || !f.close()) return false;
    auto check = Storage.open(path.c_str());
    if (!check || check.size() != size) return false;
    size_t offset = 0;
    while (offset < size) {
      const auto n = std::min(sizeof(a), size - offset);
      if (check.read(a, n) != int(n) || memcmp(a, bytes + offset, n)) return false;
      offset += n;
    }
    return check.close();
  }
  bool copy(const std::string& source, const std::string& target) {
    if (!parent(target)) return false;
    auto src = Storage.open(source.c_str());
    if (!src || src.isDirectory()) return false;
    const size_t total = src.size();
    size_t offset = 0;
    if (Storage.exists(target.c_str())) {
      auto dst = Storage.open(target.c_str());
      if (!dst || dst.isDirectory() || dst.size() > total) return false;
      size_t left = dst.size();
      while (left) {
        const auto n = std::min(left, sizeof(a));
        if (src.read(a, n) != int(n) || dst.read(b, n) != int(n) || memcmp(a, b, n)) return false;
        left -= n; offset += n;
      }
      dst.close();
    }
    auto dst = Storage.open(target.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (!dst) return false;
    while (offset < total) {
      const auto n = std::min(total - offset, sizeof(a));
      if (src.read(a, n) != int(n) || dst.write(a, n) != n) return false;
      offset += n;
      delay(1);
    }
    if (!dst.close()) return false;
    dst = Storage.open(target.c_str());
    if (!dst || dst.size() != total || !src.seek(0)) return false;
    offset = 0;
    while (offset < total) {
      const auto n = std::min(total - offset, sizeof(a));
      if (src.read(a, n) != int(n) || dst.read(b, n) != int(n) || memcmp(a, b, n)) return false;
      offset += n;
    }
    return dst.close() && src.close();
  }
  bool readJson(const std::string& path, JsonDocument& doc) {
    auto f = Storage.open(path.c_str());
    if (!f || f.isDirectory() || f.size() > JSON_LIMIT) return false;
    auto data = makeUniqueNoThrow<char[]>(f.size() + 1);
    if (!data) { LOG_ERR("MIG", "Cannot allocate bounded JSON input"); return false; }
    const auto size = f.size();
    if (f.read(data.get(), size) != int(size) || !f.close()) return false;
    data[size] = 0;
    // Const input owns its strings after this temporary buffer is released.
    return !deserializeJson(doc, static_cast<const char*>(data.get()), size,
                            DeserializationOption::NestingLimit(8)) && !doc.overflowed();
  }
  bool writeJson(const std::string& path, const JsonDocument& doc) {
    if (doc.overflowed()) return false;
    const auto size = measureJson(doc);
    if (size > JSON_LIMIT) return false;
    auto data = makeUniqueNoThrow<uint8_t[]>(size + 1);
    if (!data) { LOG_ERR("MIG", "Cannot allocate bounded JSON output"); return false; }
    return serializeJson(doc, data.get(), size + 1) == size && append(path, data.get(), size);
  }
};
}
