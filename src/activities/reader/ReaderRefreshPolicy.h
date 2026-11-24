#pragma once

#include <atomic>

namespace reader_refresh {

class ReinforcementInterrupt {
  std::atomic<bool> requested{false};

 public:
  void request() { requested.store(true, std::memory_order_release); }
  void clear() { requested.store(false, std::memory_order_release); }
  bool pending() const { return requested.load(std::memory_order_acquire); }
};

// The first pass completes the page even if navigation arrived during drawing.
// Later passes only reinforce it and can be skipped at a safe command boundary.
template <typename Pass>
int reinforceMonochrome(Pass pass, const ReinforcementInterrupt* interrupt) {
  constexpr int passes = 3;
  int completed = 0;
  while (completed < passes) {
    if (completed != 0 && interrupt && interrupt->pending()) break;
    pass();
    ++completed;
  }
  return completed;
}

enum class Action { Fast, Cleanup, Reinforce };

// Zero forces cleanup after arbitrary prior content. One is a periodic deadline.
// Image pages and unsupported controllers always retain the upstream cleanup.
constexpr Action select(int remaining, bool x3, bool grayscale, bool textPage, bool noFlash) {
  if (remaining <= 0) return Action::Cleanup;
  if (remaining > 1) return Action::Fast;
  return x3 && grayscale && textPage && noFlash ? Action::Reinforce : Action::Cleanup;
}

constexpr int advance(int remaining, int frequency) { return remaining <= 1 ? frequency : remaining - 1; }

}  // namespace reader_refresh
