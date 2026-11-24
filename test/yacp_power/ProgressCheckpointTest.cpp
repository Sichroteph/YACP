#include <Arduino.h>
#include <HalStorage.h>
#include <cassert>
#include <iostream>
#include "activities/reader/ProgressCheckpoint.h"

int main() {
  ProgressCheckpoint checkpoint;
  uint8_t data[10] = {3, 0, 0, 0, 20, 0, 42, 0, 0, 0};
  testMillis = 100;
  assert(checkpoint.observe("/epub", data, 10, 0));
  assert(testStorageWrites == 1 && testFiles["/epub/progress.bin"].size() == 10);
  for (unsigned i = 1; i <= 9; ++i) {
    data[2] = i; data[6] = 42 + i; testMillis += 1000;
    assert(checkpoint.observe("/epub", data, 10, i));
  }
  assert(testStorageWrites == 1 && testFiles["/epub/progress.bin"][6] == 42);
  data[2] = 10; data[6] = 52;
  assert(checkpoint.observe("/epub", data, 10, 10));
  assert(testStorageWrites == 2 && testFiles["/epub/progress.bin"][6] == 52);
  for (unsigned i = 0; i < 30; ++i) assert(checkpoint.observe("/epub", data, 10, 10));
  assert(testStorageWrites == 2);
  data[2] = 9; data[6] = 51; assert(checkpoint.observe("/epub", data, 10, 9));
  assert(testStorageWrites == 2);
  assert(checkpoint.flush("/epub") && testStorageWrites == 3);
  assert(testFiles["/epub/progress.bin"][6] == 51);
  assert(checkpoint.flush("/epub") && testStorageWrites == 3);

  // Metadata-only repaint preserves the complete new payload until exit.
  data[4] = 30; assert(checkpoint.observe("/epub", data, 10, 9));
  assert(testStorageWrites == 3); assert(checkpoint.flush("/epub"));
  assert(testStorageWrites == 4 && testFiles["/epub/progress.bin"][4] == 30);

  // A changed page after five minutes checkpoints, including millis wrap.
  testMillis += 300001; data[2] = 8;
  assert(checkpoint.observe("/epub", data, 10, 8) && testStorageWrites == 5);
  testMillis = UINT32_MAX - 5000;
  assert(checkpoint.observe("/epub", data, 10, 8, true));
  data[2] = 7; assert(checkpoint.observe("/epub", data, 10, 7, true));
  const unsigned beforeWrap = testStorageWrites;
  testMillis = 294998; data[2] = 6;
  assert(checkpoint.observe("/epub", data, 10, 6) && testStorageWrites == beforeWrap);
  testMillis = 295000; data[2] = 5;
  assert(checkpoint.observe("/epub", data, 10, 5) && testStorageWrites == beforeWrap + 1);

  // Failed writes retain a retryable snapshot and leave the previous canonical data intact.
  const auto previous = testFiles["/epub/progress.bin"];
  data[2] = 4; failStatsWrite = true;
  assert(!checkpoint.observe("/epub", data, 10, 4, true));
  assert(testFiles["/epub/progress.bin"] == previous);
  failStatsWrite = false; assert(checkpoint.flush("/epub"));
  assert(testFiles["/epub/progress.bin"][2] == 4);
  assert(!checkpoint.observe("/epub", data, 11, 0));
  assert(!checkpoint.observe("/epub", nullptr, 10, 0));

  // TXT/XTC keep their four-byte representation.
  ProgressCheckpoint fixed;
  const uint8_t page[4] = {255, 255, 1, 0};
  assert(fixed.observe("/xtc", page, 4, 131071));
  assert(testFiles["/xtc/progress.bin"] == std::vector<uint8_t>(page, page + 4));
  std::cout << "Progress checkpoint: batching, byte preservation, wrap, failure and fixed formats passed\n";
}
