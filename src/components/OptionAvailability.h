#pragma once

#include <cstdint>

namespace option_popup {
constexpr bool enabled(uint16_t disabled, int index) {
  return index >= 0 && index < 16 && (disabled & (uint16_t{1} << index)) == 0;
}

constexpr int nextEnabled(uint16_t disabled, int count, int current, int direction) {
  if (count <= 0 || count > 16) return current;
  int next = current;
  for (int i = 0; i < count; ++i) {
    next = (next + direction + count) % count;
    if (enabled(disabled, next)) return next;
  }
  return current;
}
}  // namespace option_popup
