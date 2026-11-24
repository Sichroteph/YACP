#pragma once

#include <algorithm>

namespace recent_grid {
constexpr int COLUMNS = 3;
constexpr int PAGE_SIZE = 9;

constexpr int horizontal(int selected, int count, bool forward) {
  return count > 0 ? (selected + count + (forward ? 1 : -1)) % count : 0;
}

constexpr int vertical(int selected, int count, bool down) {
  if (count <= 0) return 0;
  const int page = selected / PAGE_SIZE;
  const int row = (selected % PAGE_SIZE) / COLUMNS;
  const int column = selected % COLUMNS;
  const int pages = (count + PAGE_SIZE - 1) / PAGE_SIZE;
  if (down) {
    if (row < PAGE_SIZE / COLUMNS - 1 && selected + COLUMNS < count) return selected + COLUMNS;
    const int start = ((page + 1) % pages) * PAGE_SIZE;
    return start + std::min(column, std::min(PAGE_SIZE, count - start) - 1);
  }
  if (row > 0) return selected - COLUMNS;
  const int start = ((page + pages - 1) % pages) * PAGE_SIZE;
  const int end = start + std::min(PAGE_SIZE, count - start);
  int candidate = start + ((end - start - 1) / COLUMNS) * COLUMNS + column;
  while (candidate >= end) candidate -= COLUMNS;
  return std::max(start, candidate);
}
}  // namespace recent_grid
