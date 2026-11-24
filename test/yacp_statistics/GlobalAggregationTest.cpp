#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "GlobalReadingStats.h"
#include "HalStorage.h"

namespace {
constexpr const char* ROOT = "/.crosspoint/synced_stats/";

std::vector<uint8_t> payload(uint32_t seconds) {
  std::vector<uint8_t> data(159, 0);
  data[0] = 3;
  for (unsigned i = 0; i < 4; ++i) data[5 + i] = static_cast<uint8_t>(seconds >> (8 * i));
  return data;
}

void expect(uint32_t seconds) {
  const auto before = testFiles;
  const auto writes = testStorageWrites;
  GlobalReadingStats local;
  local.totalReadingSeconds = 100;
  assert(GlobalReadingStats::loadAggregated(local).totalReadingSeconds == seconds);
  assert(testFiles == before && testStorageWrites == writes);
}
}  // namespace

int main() {
  testFiles[std::string(ROOT) + "device_010203040506.bin"] = payload(100);  // This device.
  testFiles[std::string(ROOT) + "device_102030405060.bin"] = payload(200);
  expect(300);
  // Repeating synchronization replaces the peer snapshot, never adds a second copy.
  testFiles[std::string(ROOT) + "device_102030405060.bin"] = payload(220);
  expect(320);
  const std::string canonical = std::string(ROOT) + "device_102030405060.bin";
  testFiles[canonical + ".previous"] = payload(190);
  expect(320);
  testFiles.erase(canonical);
  expect(290);
  testFiles[canonical] = {3,1};
  expect(290);
  testFiles[canonical] = payload(220);
  // Recoverable writes and manual copies must not become additional readers.
  for (const char* name : {"device_102030405060.bin.part", "device_102030405060.bin.bak",
                           "device_102030405060.bin.tmp", "copy.bin", "device_10203040506.bin",
                           "device_1020304050600.bin", "device_10203040506g.bin",
                           "device_ABCDEFABCDEF.bin"}) {
    testFiles[std::string(ROOT) + name] = payload(999);
  }
  expect(320);
  auto future = payload(999);
  future[0] = 4;
  testFiles[std::string(ROOT) + "device_203040506070.bin"] = future;
  testFiles[std::string(ROOT) + "device_304050607080.bin"] = {3, 1, 2};
  expect(320);
  testFiles[std::string(ROOT) + "device_405060708090.bin"] = payload(UINT32_MAX);
  expect(UINT32_MAX);
  std::puts("PASS");
}
