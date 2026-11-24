#include <cassert>
#include <cstdio>
#include <limits>

#include "YacpSmartSync.h"

int main() {
  CrossPointPosition local{};
  local.spineIndex = 3;
  local.pageNumber = 7;
  local.hasResolvedSpineIndex = true;
  local.hasMappedPage = true;
  local.hasVisibleTextOffset = true;
  local.visibleTextOffset = 100;
  auto remote = local;
  assert(yacpSync::decision(local, 0.5f, remote, 0.9f) == ProgressComparison::Unknown);
  remote.pageNumber = 8;
  remote.visibleTextOffset = 200;
  assert(yacpSync::decision(local, 0.9f, remote, 0.1f) == ProgressComparison::RemoteAhead);
  assert(yacpSync::decision(remote, 0.1f, local, 0.9f) == ProgressComparison::LocalAhead);
  remote.hasMappedPage = false;
  assert(yacpSync::decision(local, 0.9f, remote, 0.1f) == ProgressComparison::RemoteAhead);
  remote.hasVisibleTextOffset = false;
  assert(yacpSync::decision(local, 0.1f, remote, 0.9f) == ProgressComparison::Unknown);
  remote.hasMappedPage = true;
  remote.hasResolvedSpineIndex = false;
  assert(yacpSync::decision(local, 0.1f, remote, 0.9f) == ProgressComparison::Unknown);
  assert(yacpSync::decision(local, 0.1f, remote, std::numeric_limits<float>::quiet_NaN()) ==
         ProgressComparison::Unknown);
  yacpSync::ReturnDelay timer;
  assert(!timer.due(2000));
  timer.start(0);
  assert(!timer.due(1199) && timer.due(1200));
  timer.start(UINT32_MAX - 500);
  assert(!timer.due(698) && timer.due(699));
  puts("Smart Sync: content ordering, manual fallbacks and return timer passed");
}
