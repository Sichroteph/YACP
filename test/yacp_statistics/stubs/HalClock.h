#pragma once
#include <ctime>
struct TestClock {
  tm time{};
  bool available = true;
  bool localTime(tm& out) const { out = time; return available; }
};
inline TestClock halClock;
