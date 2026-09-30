#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ReadingStatsUtils.h"

struct GlobalReadingStats;

// Rolling per-day reading duration stored separately from global_stats.bin so
// nearby sync and the existing global statistics format remain unchanged.
class DailyReadingHistory {
 public:
  static constexpr uint8_t LEGACY_FILE_VERSION = 1;
  static constexpr uint8_t CURRENT_FILE_VERSION = 2;
  static constexpr size_t FILE_HEADER_SIZE = 9;
  static constexpr size_t LEGACY_FILE_SIZE = FILE_HEADER_SIZE + READING_HISTORY_DAYS;
  static constexpr size_t CURRENT_FILE_SIZE = FILE_HEADER_SIZE + READING_HISTORY_DAYS * sizeof(uint16_t);
  static constexpr uint16_t MAX_DAILY_READING_MINUTES = 24u * 60u;

  // Loads /.crosspoint/daily_reading.bin. If no valid file exists,
  // legacy read/not-read days are preserved as one-minute entries.
  void load(const GlobalReadingStats& legacyStats);

  // Writes only after a change. The update is published atomically.
  bool save();

  // Clears local daily history without changing global_stats.bin.
  static bool reset();

  void recordReadingSpan(const ReadingStatsDateTime& localStart, uint32_t seconds);
  uint16_t minutesOnDay(uint32_t dayIndex) const;
  uint32_t anchorDay() const { return anchorDay_; }

 private:
  uint32_t anchorDay_ = 0;
  // Version 2 deliberately spends 730 extra fixed bytes so long reading days
  // retain exact minutes. This member adds no heap allocation or fragmentation.
  std::array<uint16_t, READING_HISTORY_DAYS> minutes_{};
  bool hasData_ = false;
  bool dirty_ = false;
  bool saveBlocked_ = false;

  void seedFromLegacy(const GlobalReadingStats& legacyStats);
  void advanceAnchor(uint32_t dayIndex);
  void addMinutes(uint32_t dayIndex, uint32_t minutes);
};
