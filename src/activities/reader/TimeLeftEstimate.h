#pragma once

#include <cstdint>

struct TimeLeftInput {
  uint32_t bookBytes = 0;
  uint32_t previousChapterBytes = 0;
  uint32_t chapterBytes = 0;
  uint32_t elapsedSeconds = 0;
  uint16_t paceSeconds = 0;
  int page = 0;
  int chapterPages = 0;
};

// Pure arithmetic over CrossPoint's already loaded chapter-size table.
// Zero means there is not enough pace/position information for an estimate.
uint32_t estimateReadingTimeLeft(const TimeLeftInput& input, bool wholeBook);
