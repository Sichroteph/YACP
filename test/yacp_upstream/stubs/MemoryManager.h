#pragma once
#include <cstddef>
namespace freeink {
struct MemoryManager {
  unsigned reclaimCalls = 0;
  size_t lastRequest = 0;
  static MemoryManager& instance() { static MemoryManager manager; return manager; }
  void ensureFree(size_t bytes) { ++reclaimCalls; lastRequest = bytes; }
};
}
