#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
class Stream;
inline std::vector<uint8_t> stagedImage;
inline bool stageExists = false, failStageRead = false;
inline unsigned openHandles = 0;
class HalFile {
  size_t offset = 0;
  bool opened = false;
 public:
  void open() { opened = true; ++openHandles; }
  void close() { if (opened) --openHandles; opened = false; }
  size_t size() const { return stagedImage.size(); }
  bool seek(size_t n) { if (n > size()) return false; offset = n; return true; }
  int read(void* out, size_t n) {
    if (!opened || failStageRead) return -1;
    n = std::min(n, size() - offset);
    std::memcpy(out, stagedImage.data() + offset, n); offset += n; return n;
  }
};
struct FakeStorage {
  bool ensureDirectoryExists(const char*) { return true; }
  bool openFileForRead(const char*, const char*, HalFile& f) {
    if (!stageExists) return false;
    f.open();
    return true;
  }
};
inline FakeStorage Storage;
