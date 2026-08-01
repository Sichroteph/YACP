#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "BookReadingStats.h"

struct FinishedBookEntry {
  uint64_t pathKey = 0;
  std::string title;
  uint32_t totalReadingSeconds = 0;
  ReadingStatsDate startDate;
  ReadingStatsDate finishedDate;
};

class FinishedBooksIndex {
 public:
  static constexpr size_t MAX_ENTRIES = 32;

  static std::vector<FinishedBookEntry> load();
  static bool record(const std::string& path, const std::string& title, const BookReadingStats& stats);
  static bool recordCanonical(const std::string& bookPath, const std::string& legacyCachePath,
                              const std::string& title, const BookReadingStats& stats);
  static bool migratePath(const std::string& oldPath, const std::string& newPath);
};
