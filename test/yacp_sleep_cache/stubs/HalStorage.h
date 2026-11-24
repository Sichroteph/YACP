#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

constexpr int O_RDWR = 2;
inline std::map<std::string, std::vector<uint8_t>> files;
inline int openFiles = 0;
inline int writeBudget = -1;
inline bool failClose = false;
inline bool failRead = false;
inline bool failOpen = false;
inline unsigned writes = 0;
inline unsigned reads = 0;
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)

class HalFile {
  std::string path;
  size_t pos = 0;
  bool opened = false;
 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) : path(std::move(other.path)), pos(other.pos), opened(other.opened) {
    other.opened = false;
  }
  ~HalFile() { assert(!opened); }
  bool open(const std::string& name) {
    assert(!opened && openFiles == 0);
    if (failOpen || !files.count(name)) return false;
    path = name; pos = 0; opened = true; ++openFiles; return true;
  }
  explicit operator bool() const { return opened; }
  size_t fileSize() const { return files.at(path).size(); }
  uint64_t fileSize64() const { return fileSize(); }
  bool seek(size_t offset) { if (offset > fileSize()) return false; pos = offset; return true; }
  int read(void* out, size_t count) {
    ++reads;
    if (failRead) return -1;
    count = std::min(count, fileSize() - pos);
    std::memcpy(out, files.at(path).data() + pos, count); pos += count; return count;
  }
  size_t write(const void* data, size_t count) {
    if (writeBudget >= 0) count = std::min(count, static_cast<size_t>(writeBudget));
    if (writeBudget >= 0) writeBudget -= count;
    auto& bytes = files.at(path);
    bytes.resize(std::max(bytes.size(), pos + count));
    std::memcpy(bytes.data() + pos, data, count); pos += count; ++writes; return count;
  }
  bool close() { assert(opened); opened = false; --openFiles; return !failClose; }
};

struct TestStorage {
  bool ensureDirectoryExists(const char*) { return !failOpen; }
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path); }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    if (failOpen) return false;
    files[path].clear(); return file.open(path);
  }
  HalFile open(const char* path, int) { HalFile file; file.open(path); return file; }
};
inline TestStorage Storage;
