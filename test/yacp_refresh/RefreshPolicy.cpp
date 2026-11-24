#include <cassert>
#include <climits>
#include <cstdio>
#include <initializer_list>
#include "activities/reader/ReaderRefreshPolicy.h"

using reader_refresh::Action;
using reader_refresh::advance;
using reader_refresh::select;

int main() {
  // Ten turns, interrupted by an image and an explicit refresh: neither may
  // convert a required cleanup into the configured periodic reinforcement.
  const Action expected[] = {Action::Cleanup, Action::Fast, Action::Fast, Action::Fast, Action::Fast,
                             Action::Reinforce, Action::Cleanup, Action::Fast, Action::Cleanup, Action::Fast};
  int remaining = 0;
  for (int turn = 0; turn < 10; ++turn) {
    if (turn == 6 || turn == 8) remaining = 0;
    assert(select(remaining, true, true, true, true) == expected[turn]);
    remaining = advance(remaining, 5);
  }
  for (int frequency : {1, 5, 10, 15, 30, 60}) {
    int due = 0;
    int count = frequency;
    for (int page = 0; page < 600; ++page) {
      if (select(count, true, true, true, true) == Action::Reinforce) ++due;
      count = advance(count, frequency);
    }
    assert(due == 600 / frequency);
  }
  // Every upstream/default, image, unsupported, and non-X3 combination must
  // preserve the existing cleanup cadence even with the preference retained.
  for (int flags = 0; flags < 16; ++flags) {
    if (flags == 15) continue;
    assert(select(1, flags & 1, flags & 2, flags & 4, flags & 8) == Action::Cleanup);
    assert(select(0, flags & 1, flags & 2, flags & 4, flags & 8) == Action::Cleanup);
    assert(select(2, flags & 1, flags & 2, flags & 4, flags & 8) == Action::Fast);
  }
  remaining = INT_MAX;
  for (int page = 0; page < 100000; ++page) {
    assert(select(remaining, true, true, true, true) == Action::Fast);
    remaining = advance(remaining, INT_MAX);
  }
  assert(advance(0, INT_MAX) == INT_MAX);
  std::puts("Refresh traces passed: cadence, interruptions, controller gates, Never");
}
