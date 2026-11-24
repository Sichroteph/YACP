#pragma once

#include "ProgressComparison.h"

namespace yacpSync {

inline bool hasContentPosition(const CrossPointPosition& position) {
  return position.hasResolvedSpineIndex && position.spineIndex >= 0 &&
         (position.hasVisibleTextOffset || (position.hasMappedPage && position.pageNumber >= 0));
}

// Keep CrossPoint's content-aware ordering. YACP requires a manual choice
// for equality or percentage-only estimates, before any transfer or overwrite.
inline ProgressComparison decision(const CrossPointPosition& local, const float localPercentage,
                                   const CrossPointPosition& remote, const float remotePercentage) {
  if (!hasContentPosition(local) || !hasContentPosition(remote)) return ProgressComparison::Unknown;
  const auto result = compareProgress(local, localPercentage, remote, remotePercentage);
  return result == ProgressComparison::Synchronized ? ProgressComparison::Unknown : result;
}

class ReturnDelay {
  uint32_t started = 0;
  bool pending = false;

 public:
  void start(const uint32_t now) {
    started = now;
    pending = true;
  }
  bool due(const uint32_t now) const { return pending && static_cast<uint32_t>(now - started) >= 1200; }
};

}  // namespace yacpSync
