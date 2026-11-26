#pragma once

#include <cstdint>

#include "activities/reader/ReaderRefreshPolicy.h"

namespace x3_wake {

struct Snapshot {
  reader_refresh::WakeTextPage page{};
  uint32_t frameCrc = 0;
  uint32_t readerData = 0;
  bool clickToWake = false;
  bool fadingFix = false;
  bool previewOnly = false;
};

// The frame itself already has its own sleep cache. This record is published
// only after the sleep screen and the reader's progress have been committed.
bool save(const Snapshot& snapshot, const uint8_t* moonBackground);
Snapshot consume(bool allowPreview, uint8_t* moonBackground);
bool matchesReaderData(const Snapshot& snapshot);

}  // namespace x3_wake
