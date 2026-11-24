#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

inline std::map<std::string, std::vector<uint8_t>> cacheFiles;
inline std::set<std::string> cacheDirectories;
inline std::map<std::string, unsigned> cacheOpenFiles;
inline std::vector<std::string> cacheRemovals;
inline size_t cacheFailRemoval = SIZE_MAX;
inline std::string cacheFailOpen;

class HalFile {
  std::string path;
  bool opened = false;
  bool directory = false;
  std::vector<std::string> children;
  size_t next = 0;
 public:
  explicit operator bool() const { return opened; }
  void open(const std::string& value) {
    path = value;
    directory = cacheDirectories.count(path);
    opened = true;
    ++cacheOpenFiles[path];
    if (!directory) return;
    const auto prefix = path + '/';
    auto append = [&](const std::string& name) {
      if (name.rfind(prefix, 0) == 0 && name.find('/', prefix.size()) == std::string::npos) children.push_back(name);
    };
    for (const auto& item : cacheFiles) append(item.first);
    for (const auto& item : cacheDirectories) append(item);
  }
  bool isDirectory() const { return directory; }
  bool close() { if (opened) --cacheOpenFiles[path]; opened = false; return true; }
  size_t getName(char* out, size_t size) {
    const auto name = path.substr(path.rfind('/') + 1);
    snprintf(out, size, "%s", name.c_str());
    return name.size();
  }
  HalFile openNextFile() {
    HalFile file;
    if (next < children.size()) file.open(children[next++]);
    return file;
  }
};

struct CacheStorage {
  bool exists(const char* path) { return cacheFiles.count(path) || cacheDirectories.count(path); }
  HalFile open(const char* path) {
    HalFile file;
    if (path != cacheFailOpen && exists(path)) file.open(path);
    return file;
  }
  bool remove(const char* path) {
    assert(cacheOpenFiles[path] == 0);
    cacheRemovals.emplace_back(path);
    if (cacheRemovals.size() == cacheFailRemoval) return false;
    return cacheFiles.erase(path) != 0;
  }
};
inline CacheStorage Storage;
