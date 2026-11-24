#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Host-only storage. Failure injection never touches the reader or host files.
inline std::map<std::string, std::vector<uint8_t>> testFiles;
inline unsigned testStorageWrites = 0;
inline bool failStatsClose = false;
inline bool failStatsWrite = false;
inline std::string failStatsRenameTo;

class HalFile {
  std::string path;
  size_t offset = 0;
  bool opened = false;
  bool directory = false;
  std::vector<std::string> children;
  size_t child = 0;

 public:
  void open(const std::string& value, bool asDirectory = false) {
    path = value;
    opened = true;
    directory = asDirectory;
    offset = child = 0;
    if (directory) {
      for (const auto& entry : testFiles) {
        if (entry.first.rfind(path + "/", 0) == 0) children.push_back(entry.first);
      }
    }
  }
  operator bool() const { return opened; }
  bool isDirectory() const { return directory; }
  size_t fileSize() const { return testFiles.at(path).size(); }
  size_t position() const { return offset; }
  int read(void* output, size_t size) {
    const auto& data = testFiles.at(path);
    size = std::min(size, data.size() - offset);
    std::memcpy(output, data.data() + offset, size);
    offset += size;
    return static_cast<int>(size);
  }
  size_t write(const uint8_t* data, size_t size) {
    if (failStatsWrite) return 0;
    auto& bytes = testFiles.at(path);
    bytes.insert(bytes.end(), data, data + size);
    offset += size;
    return size;
  }
  void flush() {}
  bool close() { opened = false; return !failStatsClose; }
  size_t getName(char* output, size_t size) {
    const auto name = path.substr(path.find_last_of('/') + 1);
    std::snprintf(output, size, "%s", name.c_str());
    return name.size();
  }
  HalFile openNextFile() {
    HalFile result;
    if (child < children.size()) result.open(children[child++]);
    return result;
  }
};

struct TestStorage {
  bool ensureDirectoryExists(const char*) { return true; }
  bool exists(const std::string& path) const {
    if (testFiles.count(path)) return true;
    for (const auto& pair : testFiles) if (pair.first.rfind(path + "/", 0) == 0) return true;
    return false;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (!exists(path)) return false;
    file.open(path);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    ++testStorageWrites;
    testFiles[path].clear();
    file.open(path);
    return true;
  }
  bool remove(const std::string& path) { return testFiles.erase(path) != 0; }
  bool rename(const std::string& from, const std::string& to) {
    if (to == failStatsRenameTo) return false;
    if (!exists(from) || exists(to)) return false;
    testFiles[to] = std::move(testFiles[from]);
    testFiles.erase(from);
    return true;
  }
  HalFile open(const std::string& path) {
    HalFile result;
    if (testFiles.count(path)) result.open(path);
    else {
      for (const auto& entry : testFiles) {
        if (entry.first.rfind(path + "/", 0) == 0) { result.open(path, true); break; }
      }
    }
    return result;
  }
};
inline TestStorage Storage;
