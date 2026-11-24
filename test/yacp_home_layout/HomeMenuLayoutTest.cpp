#include <cassert>
#include <cstdio>

#include "components/HomeMenuLayout.h"

int main() {
  int scenarios = 0;
  // Physical portrait screens and rotated layouts, with optional OPDS/Continue rows.
  for (int screenHeight : {480, 600, 800, 1024}) {
    for (int bottomInset : {0, 20, 40}) {
      for (int rowHeight : {43, 45, 64}) {
        for (int count : {6, 7, 8}) {
          for (int selected = 0; selected < count; ++selected) {
            const auto layout =
                homeMenuLayout(screenHeight, bottomInset, 56, 320, 16, rowHeight, 8, count, selected, 10);
            assert(layout.coverHeight >= 0 && layout.coverHeight <= 320);
            assert(layout.visibleRows > 0);
            assert(selected >= layout.firstRow && selected < layout.firstRow + layout.visibleRows);
            const int bottom = layout.top + layout.leading + layout.visibleRows * (rowHeight + 8);
            assert(bottom <= screenHeight - bottomInset);
            assert(layout.top + layout.height <= screenHeight - bottomInset);
            ++scenarios;
          }
        }
      }
    }
  }
  std::printf("Validated %d bounded Home menu scenarios\n", scenarios);
  const auto empty = homeMenuLayout(800, 40, 56, 260, 16, 64, 8, 0, -1);
  assert(empty.visibleRows == 0 && empty.firstRow == 0);
  static_assert(sizeof(HomeMenuLayout) <= 32);
}
