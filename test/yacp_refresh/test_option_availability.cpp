#include <cassert>

#include "src/components/OptionAvailability.h"

int main() {
  using option_popup::enabled;
  using option_popup::nextEnabled;
  assert(!enabled(1, 0) && enabled(1, 1));
  assert(nextEnabled(1, 2, 1, 1) == 1);
  assert(nextEnabled(1, 2, 1, -1) == 1);
  assert(nextEnabled(1, 2, 0, 1) == 1);
  assert(nextEnabled(0, 2, 1, 1) == 0);
  assert(nextEnabled(0, 2, 0, -1) == 1);
  assert(nextEnabled(0xFFFF, 16, 4, 1) == 4);
  assert(nextEnabled(0, 0, 0, 1) == 0);
  assert(!enabled(0, -1) && !enabled(0, 16));
  for (int i = 0; i < 16; ++i) {
    const uint16_t disabled = static_cast<uint16_t>(0xFFFF ^ (uint16_t{1} << i));
    assert(nextEnabled(disabled, 16, (i + 7) % 16, 1) == i);
    assert(nextEnabled(disabled, 16, (i + 7) % 16, -1) == i);
  }
}
