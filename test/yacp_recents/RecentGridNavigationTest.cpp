#include "RecentGridNavigation.h"
#include <cassert>
#include <set>

int main() {
  using namespace recent_grid;
  assert(horizontal(0, 0, true) == 0 && vertical(0, 0, false) == 0);
  for (int count = 1; count <= 18; ++count) {
    std::set<int> reached;
    int selected = 0;
    for (int step = 0; step < count; ++step) {
      reached.insert(selected);
      selected = horizontal(selected, count, true);
    }
    assert(selected == 0 && static_cast<int>(reached.size()) == count);
    for (int i = 0; i < count; ++i) {
      assert(horizontal(horizontal(i, count, true), count, false) == i);
      for (bool down : {false, true}) {
        const auto next = vertical(i, count, down);
        assert(next >= 0 && next < count);
      }
    }
  }
  assert(vertical(8, 18, true) == 11);
  assert(vertical(17, 18, true) == 2);
  assert(vertical(0, 18, false) == 15);
  assert(vertical(2, 17, false) == 14);
  assert(vertical(11, 17, true) == 14);
  assert(vertical(14, 17, true) == 2);
  assert(vertical(1, 2, false) == 1);
}
