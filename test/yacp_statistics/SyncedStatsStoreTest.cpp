#include "SyncedStatsStore.h"
#include "HalStorage.h"
#include <cassert>
#include <cstdio>

using namespace nearby_stats;
int main() {
  const Mac peer{2,3,4,5,6,7};
  const std::string path = "/.crosspoint/synced_stats/device_020304050607.bin";
  const std::string previous = path + ".previous";
  std::vector<uint8_t> old(159,1), next(159,2); old[0] = next[0] = 3;
  uint8_t data[159]; size_t size = 0;
  assert(readLocalSnapshot(data,size) && size == 159 && data[0] == 3 && data[5] == 0);
  assert(testStorageWrites == 0);
  const std::string local = "/.crosspoint/global_stats.bin";
  testFiles[local] = {3,1}; testFiles[local + ".bak"] = old;
  assert(readLocalSnapshot(data,size) && !memcmp(data,old.data(),159));
  testFiles[local] = {4,1};
  assert(!readLocalSnapshot(data,size));
  testFiles.clear();
  for (size_t budget = 0; budget < 159; ++budget) {
    testFiles.clear(); testFiles[path] = old; statsWriteBudget = budget;
    assert(!savePeerSnapshot(peer,next.data(),next.size()));
    assert(testFiles.at(path) == old);
  }
  statsWriteBudget = SIZE_MAX;
  testFiles.clear(); testFiles[path] = old;
  assert(savePeerSnapshot(peer,next.data(),next.size()));
  assert(testFiles.at(path) == next && testFiles.at(previous) == old);
  const auto writes = testStorageWrites;
  assert(savePeerSnapshot(peer,next.data(),next.size()) && testStorageWrites == writes);
  for (const auto& destination : {path,previous}) {
    testFiles.clear(); testFiles[path] = old; failStatsRenameTo = destination;
    assert(!savePeerSnapshot(peer,next.data(),next.size()));
    assert((testFiles.count(path) && testFiles.at(path) == old) ||
           (testFiles.count(previous) && testFiles.at(previous) == old));
    failStatsRenameTo.clear();
    assert(savePeerSnapshot(peer,next.data(),next.size()));
    assert(testFiles.at(path) == next && testFiles.at(previous) == old);
  }
  testFiles.clear(); testFiles[previous] = old;  // Simulated power cut after the first rename.
  assert(savePeerSnapshot(peer,next.data(),next.size()));
  assert(testFiles.at(path) == next && testFiles.at(previous) == old);
  for (const auto& unknown : {path,previous}) {
    testFiles.clear(); testFiles[path] = old; testFiles[unknown] = {4,1};
    const auto before = testFiles;
    assert(!savePeerSnapshot(peer,next.data(),next.size()));
    assert(testFiles == before);
  }
  testFiles.clear(); testFiles[path] = old; failStatsClose = true;
  assert(!savePeerSnapshot(peer,next.data(),next.size()) && testFiles.at(path) == old);
  std::puts("PASS: 159 partial writes, promotion failures, recovery, no-op duplicate, future preservation");
}
