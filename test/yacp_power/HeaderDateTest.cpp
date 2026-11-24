#include <HalClock.h>
#include <HalGPIO.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include "CrossPointSettings.h"
#include "components/HeaderDate.h"

int main() {
  char date[48] = "old";
  halClock.time = {};
  halClock.time.tm_year = 126;
  halClock.time.tm_mon = 9;
  halClock.time.tm_mday = 1;
  assert(!formatYacpHeaderDate(date, sizeof(date)) && date[0] == '\0');
  SETTINGS.clockDateHasBeenSynced = 1;
  assert(formatYacpHeaderDate(date, sizeof(date)) && strcmp(date, "fixture 1, 2026") == 0);
  // The HAL supplies local time, even across year boundaries. No second offset.
  halClock.time.tm_year = 127;
  halClock.time.tm_mon = 0;
  assert(formatYacpHeaderDate(date, sizeof(date)) && strcmp(date, "fixture 1, 2027") == 0);
  gpio.x3 = false;
  assert(!formatYacpHeaderDate(date, sizeof(date)) && date[0] == '\0');
  gpio.x3 = true; halClock.available = false;
  assert(!formatYacpHeaderDate(date, sizeof(date)));
  halClock.available = true; halClock.time.tm_mday = 32;
  assert(!formatYacpHeaderDate(date, sizeof(date)));
  halClock.time.tm_mday = 1;
  assert(!formatYacpHeaderDate(date, 2) && date[0] == '\0');
  assert(!formatYacpHeaderDate(nullptr, 48));
  assert(!formatYacpHeaderDate(date, 0));
  std::cout << "Header date: trust, local rollover, board, missing RTC and bounded buffer passed\n";
}
