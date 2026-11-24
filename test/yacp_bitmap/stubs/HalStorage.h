#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
class HalFile {
  const std::vector<uint8_t>& data;
  size_t offset = 0;
 public:
  explicit HalFile(const std::vector<uint8_t>& d):data(d) {}
  operator bool() const { return true; }
  bool seek(size_t position) { if(position>data.size()) return false; offset=position; return true; }
  bool seekCur(int64_t count) { if(count<0 && uint64_t(-count)>offset) return false; return seek(offset+count); }
  int read(void* out,size_t size) {
    size=std::min(size,data.size()-offset); std::memcpy(out,data.data()+offset,size); offset+=size; return size;
  }
  int read() { return offset<data.size() ? data[offset++] : -1; }
};
