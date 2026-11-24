#include "HeaderDate.h"

#include <HalGPIO.h>
#include <cstdio>

#include "CrossPointSettings.h"
#include "activities/reader/ReadingStatsUtils.h"

bool formatYacpHeaderDate(char* buffer, const size_t length) {
  if (!buffer || length == 0) return false;
  buffer[0] = '\0';
  if (!gpio.deviceIsX3() || !SETTINGS.clockDateHasBeenSynced) return false;
  ReadingStatsDateTime local;
  if (!getCurrentLocalReadingStatsDateTime(local)) return false;
  char month[32];
  formatReadingStatsMonthToken(local.date, month, sizeof(month));
  const int count = snprintf(buffer, length, "%s %u, %u", month, static_cast<unsigned>(local.date.day),
                             static_cast<unsigned>(local.date.year));
  if (count < 0 || static_cast<size_t>(count) >= length) {
    buffer[0] = '\0';
    return false;
  }
  return true;
}
