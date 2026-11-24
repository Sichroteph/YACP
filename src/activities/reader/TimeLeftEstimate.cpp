#include "TimeLeftEstimate.h"

#include <algorithm>

namespace {
uint32_t roundedSeconds(const double value) {
  if (value <= 0.0) return 0;
  // Avoid converting the float-rounded value 2^32 into uint32_t.
  if (value >= static_cast<double>(UINT32_MAX) - 0.5) return UINT32_MAX;
  return static_cast<uint32_t>(value + 0.5);
}
}

uint32_t estimateReadingTimeLeft(const TimeLeftInput& input, const bool wholeBook) {
  if (!input.paceSeconds || input.chapterPages <= 0 || input.page < 0 || input.page >= input.chapterPages) return 0;
  if (!wholeBook) {
    return roundedSeconds(static_cast<double>(input.chapterPages - input.page - 1) * input.paceSeconds);
  }
  if (!input.bookBytes || !input.chapterBytes || input.previousChapterBytes >= input.bookBytes ||
      static_cast<uint64_t>(input.previousChapterBytes) + input.chapterBytes > input.bookBytes) return 0;
  const double bytesPerPage = static_cast<double>(input.chapterBytes) / input.chapterPages;
  const double completed = input.previousChapterBytes + input.page * bytesPerPage;
  const double remaining = input.bookBytes - completed;
  uint32_t seconds = roundedSeconds(remaining / bytesPerPage * input.paceSeconds);
  // YACP's safeguard against an implausibly optimistic estimate after a dense
  // chapter: retain at least 90% of the elapsed-time/progress estimate.
  if (completed > 0 && input.elapsedSeconds >= 120) {
    const uint32_t progressEstimate = roundedSeconds(input.elapsedSeconds * remaining / completed);
    const uint32_t floor = static_cast<uint32_t>((static_cast<uint64_t>(progressEstimate) * 90 + 99) / 100);
    seconds = std::max(seconds, floor);
  }
  return seconds;
}
