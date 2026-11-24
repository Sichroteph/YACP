#pragma once
#include <memory>
#include <new>
#include <type_traits>
inline unsigned failAllocations = 0;
inline unsigned allocationAttempts = 0;
template<class T> std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  ++allocationAttempts;
  if (failAllocations) { --failAllocations; return {}; }
  return std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[count]);
}
