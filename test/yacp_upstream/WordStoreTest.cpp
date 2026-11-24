#include <MemoryManager.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "WordStore.h"

int main() {
  auto& memory = freeink::MemoryManager::instance();
  WordStore::StoredWord untouched{42, 3, 7};
  {
    WordStore words;
    failAllocations = 2;
    assert(!words.append("retained", 8, untouched));
    assert(untouched.chunk == 42 && untouched.off == 3 && untouched.len == 7);
    assert(memory.reclaimCalls == 1);
    assert(words.append("retained", 8, untouched));
    assert(words.view(untouched) == "retained");
  }
  {
    WordStore words;
    WordStore::StoredWord first;
    const std::string large(2047, 'a');
    assert(words.append(large.data(), large.size(), first));
    WordStore::StoredWord second;
    failAllocations = 1;
    const auto before = memory.reclaimCalls;
    assert(words.append("next", 4, second));
    assert(memory.reclaimCalls == before + 1 && memory.lastRequest == 2048 + 8192);
    assert(words.view(first) == large && words.view(second) == "next");
    words.release(first);
    assert(words.chunkData(first.chunk) == nullptr && words.view(second) == "next");
    // Suffix storage shares the original allocation and release obligation.
    const auto suffix = WordStore::suffix(second, 1);
    assert(words.view(suffix) == "ext" && strcmp(words.cstr(suffix), "ext") == 0);
    words.release(suffix);
  }
  {
    WordStore words;
    WordStore::StoredWord first, failed{99, 8, 9};
    const std::string large(2047, 'b');
    assert(words.append(large.data(), large.size(), first));
    failAllocations = 2;
    assert(!words.append("next", 4, failed));
    assert(words.view(first) == large && failed.chunk == 99 && failed.off == 8 && failed.len == 9);
    assert(words.append("next", 4, failed) && words.view(failed) == "next");
  }
  std::cout << "CrossPoint WordStore: failed allocation, single reclaim retry, preserved words and suffix lifetime passed\n";
}
