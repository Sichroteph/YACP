#include <atomic>
#include <cassert>
#include <thread>

#include "src/activities/reader/ReaderRefreshPolicy.h"

int main() {
  using reader_refresh::reinforceMonochrome;
  reader_refresh::ReinforcementInterrupt interrupt;
  int calls = 0;
  assert(reinforceMonochrome([&] { ++calls; }, &interrupt) == 3);
  assert(calls == 3);

  // An already queued turn still allows the displayed page to complete.
  interrupt.request();
  calls = 0;
  assert(reinforceMonochrome([&] { ++calls; }, &interrupt) == 1);
  assert(calls == 1 && interrupt.pending());
  interrupt.clear();

  for (int stopAfter = 1; stopAfter <= 3; ++stopAfter) {
    calls = 0;
    const int completed = reinforceMonochrome(
        [&] {
          ++calls;
          if (calls == stopAfter) interrupt.request();
        },
        &interrupt);
    assert(completed == stopAfter && calls == stopAfter);
    assert(interrupt.pending());  // Only consuming navigation clears it.
    interrupt.clear();
  }

  // Input from the other core cannot abort a pass halfway through its commands.
  std::atomic<bool> passStarted{false};
  std::atomic<bool> inputLatched{false};
  std::thread input([&] {
    while (!passStarted.load(std::memory_order_acquire)) std::this_thread::yield();
    interrupt.request();
    inputLatched.store(true, std::memory_order_release);
  });
  calls = 0;
  const int completed = reinforceMonochrome(
      [&] {
        passStarted.store(true, std::memory_order_release);
        while (!inputLatched.load(std::memory_order_acquire)) std::this_thread::yield();
        ++calls;
      },
      &interrupt);
  input.join();
  assert(calls == 1 && completed == 1);
  interrupt.clear();
  calls = 0;
  assert(reinforceMonochrome([&] { ++calls; }, &interrupt) == 3 && calls == 3);
  calls = 0;
  assert(reinforceMonochrome([&] { ++calls; }, nullptr) == 3 && calls == 3);
}
