#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

inline std::map<std::string, std::vector<uint8_t>> files;
inline std::set<std::string> directories;
inline std::map<std::string, int> handles;
inline unsigned mutation = 0, failAt = 0;
inline bool tornWrites = false;
inline bool fail() { return ++mutation == failAt; }
struct HalFile {
  std::string path;
  size_t offset = 0;
  bool opened = false, writer = false;
  explicit operator bool() const { return opened; }
  bool isDirectory() const { return directories.count(path); }
  size_t size() const { return files.at(path).size(); }
  size_t read(uint8_t* out, size_t count) {
    auto& data = files.at(path);
    count = std::min(count, data.size() - offset);
    memcpy(out, data.data() + offset, count);
    offset += count;
    return count;
  }
  size_t write(const uint8_t* bytes, size_t count) {
    if (fail()) count = tornWrites ? count / 2 : 0;
    auto& data = files[path];
    data.insert(data.end(), bytes, bytes + count);
    return count;
  }
  bool close() {
    if (opened) { --handles[path]; opened = false; }
    return true;
  }
};
struct FakeStorage {
  bool exists(const char* path) { return files.count(path) || directories.count(path); }
  HalFile open(const char* path) {
    HalFile file;
    if (exists(path)) { file.path = path; file.opened = true; assert(!handles[path]); ++handles[path]; }
    return file;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file = open(path);
    return bool(file) && !file.isDirectory();
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    if (fail()) return false;
    assert(!handles[path]);
    files[path].clear();
    file.path = path; file.opened = true; file.writer = true; ++handles[path];
    return true;
  }
  bool rename(const char* from, const char* to) {
    assert(!handles[from] && !handles[to]);
    if (fail() || !exists(from) || exists(to)) return false;
    const std::string prefix = std::string(from) + '/';
    std::vector<std::string> moving;
    for (const auto& entry : files) if (entry.first == from || entry.first.rfind(prefix, 0) == 0) moving.push_back(entry.first);
    for (const auto& path : moving) {
      files[std::string(to) + path.substr(strlen(from))] = files.at(path);
      files.erase(path);
    }
    if (directories.erase(from)) directories.insert(to);
    return true;
  }
  bool remove(const char* path) {
    assert(!handles[path]);
    if (fail()) return false;
    return files.erase(path) != 0;
  }
};
inline FakeStorage Storage;
