#pragma once

#include "BookReadingStats.h"
#include "DailyReadingHistory.h"
#include "GlobalReadingStats.h"

// One instance per open EPUB. The reader owns it on the heap because daily
// history alone is 1460 bytes; there are no allocations or writes per page.
// Call only from the input task. Render completion is delivered separately.
class ReadingSession {
 public:
  BookReadingStats book;
  GlobalReadingStats global;
  DailyReadingHistory daily;

  void load(const std::string& cachePath, uint16_t idleSeconds);
  void pageShown(uint32_t now);
  void pause(uint32_t now);
  void turn(uint32_t now, bool forward, bool chapterBoundary);
  void finish(uint32_t now);
  // An explicit stats-screen snapshot does not end or count a second session.
  void preview(BookReadingStats& bookOut, GlobalReadingStats& globalOut, DailyReadingHistory& dailyOut) const;
  void applyEdits(const BookReadingStats& edited);
  bool setCompleted(bool completed, const ReadingStatsDateTime& when);
  void save(const std::string& cachePath);
  uint32_t activeSeconds() const { return seconds_; }
  uint16_t paceSamples() const { return paceSamples_; }
  bool sessionPace(uint16_t& seconds) const;
  uint16_t estimatePace() const;
  uint32_t elapsedForEstimate() const;
  void cacheTimeLeft(uint32_t seconds);

 private:
  ReadingStatsDateTime startedAt_;
  uint32_t pageShownAt_ = 0;
  uint32_t seconds_ = 0;
  uint32_t paceSeconds_ = 0;
  uint16_t paceSamples_ = 0;
  uint16_t idleSeconds_ = 300;
  bool pageActive_ = false;
  bool warmup_ = true;
  bool finished_ = false;
  bool dirty_ = false;
  uint32_t endPage(uint32_t now);
  void recoverPace();
};
