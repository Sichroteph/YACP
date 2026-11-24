#pragma once

#include <algorithm>

// Home uses the same bounded menu geometry for drawing and touch hit testing.
// Shrink the cover first; paginate only when rows alone exceed the usable height.
struct HomeMenuLayout {
  int coverHeight;
  int top;
  int height;
  int leading;
  int firstRow;
  int visibleRows;
};

inline HomeMenuLayout homeMenuLayout(int screenHeight, int bottomInset, int coverTop, int desiredCoverHeight,
                                     int menuGap, int rowHeight, int rowGap, int rowCount, int selectedRow,
                                     int leading = 0) {
  const int bottom = std::max(coverTop, screenHeight - bottomInset);
  const int rowStep = std::max(1, rowHeight + rowGap);
  const int available = std::max(0, bottom - coverTop - menuGap - leading);
  const int coverHeight = std::clamp(available - std::max(0, rowCount) * rowStep, 0, desiredCoverHeight);
  const int top = coverTop + coverHeight + menuGap;
  const int height = std::max(0, bottom - top);
  const int visible = std::min(std::max(0, rowCount), std::max(0, height - leading) / rowStep);
  const int selected = std::clamp(selectedRow, 0, std::max(0, rowCount - 1));
  const int first = visible > 0 ? (selected / visible) * visible : 0;
  return {coverHeight, top, height, leading, first, std::min(visible, std::max(0, rowCount - first))};
}
