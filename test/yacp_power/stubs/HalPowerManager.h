#pragma once
#include <cstdint>
struct TestPower {
  bool available = true;
  bool cached = true;
  uint16_t percent = 90;
  unsigned reads = 0;
  bool getCachedBatteryPercentage(uint16_t& out) { out = percent; return cached && available; }
  uint16_t getBatteryPercentage() { ++reads; cached = available; return percent; }
};
inline TestPower powerManager;
