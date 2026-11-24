#include "ReadingSession.h"

#include <algorithm>

namespace {
uint32_t addSaturated(uint32_t a, uint32_t b) { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }
}

void ReadingSession::load(const std::string& cachePath, uint16_t idleSeconds) {
  book = BookReadingStats::load(cachePath);
  global = GlobalReadingStats::load();
  daily.load(global);
  if (book.saveBlocked) { finished_ = true; return; }
  idleSeconds_ = std::clamp<uint16_t>(idleSeconds, 30, 600);
  getCurrentLocalReadingStatsDateTime(startedAt_);
}

void ReadingSession::pageShown(uint32_t now) {
  if (!finished_ && !pageActive_) {
    pageShownAt_ = now;
    pageActive_ = true;
  }
}

uint32_t ReadingSession::endPage(uint32_t now) {
  if (!pageActive_ || finished_) return 0;
  pageActive_ = false;
  // Unsigned subtraction also handles the millisecond clock wrapping.
  const uint32_t elapsed = (now - pageShownAt_) / 1000;
  if (elapsed <= idleSeconds_) seconds_ = addSaturated(seconds_, elapsed);
  return elapsed;
}

void ReadingSession::pause(uint32_t now) {
  endPage(now);
  warmup_ = true;
}

void ReadingSession::turn(uint32_t now, bool forward, bool chapterBoundary) {
  const uint32_t elapsed = endPage(now);
  if (finished_) return;
  if (!forward) {
    warmup_ = true;
    return;
  }
  if (elapsed < 2) return;
  // YACP counts a deliberate forward turn even after a long idle interval,
  // but rejects that interval from active time and pace estimation.
  book.totalPagesTurned = addSaturated(book.totalPagesTurned, 1);
  global.totalPagesTurned = addSaturated(global.totalPagesTurned, 1);
  book.completionPromptDismissedAtHundred = false;
  dirty_ = true;
  if (chapterBoundary) return;
  if (warmup_) {
    warmup_ = false;
    return;
  }
  if (elapsed > idleSeconds_) return;
  if (paceSamples_ < UINT16_MAX && paceSeconds_ <= UINT32_MAX - elapsed) {
    paceSeconds_ += elapsed;
    ++paceSamples_;
  }
  book.recordForwardPageRead(elapsed);
  recoverPace();
}

bool ReadingSession::sessionPace(uint16_t& seconds) const {
  seconds = 0;
  if (paceSamples_ < 10 || paceSeconds_ == 0) return false;
  seconds = static_cast<uint16_t>(std::min<uint32_t>((paceSeconds_ + paceSamples_ / 2) / paceSamples_, UINT16_MAX));
  return seconds != 0;
}

void ReadingSession::recoverPace() {
  uint16_t average = 0;
  if (book.avgSecondsPerForwardPage == 0 || !sessionPace(average)) return;
  const uint32_t slower = (static_cast<uint32_t>(book.avgSecondsPerForwardPage) * 110 + 99) / 100;
  const uint32_t faster = static_cast<uint32_t>(book.avgSecondsPerForwardPage) * 90 / 100;
  if (average >= slower || (paceSamples_ >= 15 && average <= faster)) book.avgSecondsPerForwardPage = average;
}

uint16_t ReadingSession::estimatePace() const {
  uint16_t seconds = 0;
  if (sessionPace(seconds)) return seconds;
  return book.paceSampleCount >= 3 ? book.avgSecondsPerForwardPage : 0;
}

uint32_t ReadingSession::elapsedForEstimate() const {
  return finished_ ? book.totalReadingSeconds : addSaturated(book.totalReadingSeconds, seconds_);
}

void ReadingSession::cacheTimeLeft(const uint32_t seconds) {
  if (book.saveBlocked) return;
  const uint32_t estimate = book.isCompleted ? 0 : seconds;
  if (book.estimatedTimeLeftSeconds != estimate) {
    book.estimatedTimeLeftSeconds = estimate;
    dirty_ = true;
  }
}

void ReadingSession::finish(uint32_t now) {
  if (finished_) return;
  endPage(now);
  finished_ = true;
  if (seconds_ >= 60) {
    if (book.sessionCount < UINT16_MAX) ++book.sessionCount;
    global.totalSessions = addSaturated(global.totalSessions, 1);
  }
  if (seconds_ >= 10) {
    book.totalReadingSeconds = addSaturated(book.totalReadingSeconds, seconds_);
    global.totalReadingSeconds = addSaturated(global.totalReadingSeconds, seconds_);
    if (startedAt_.isValid()) {
      book.recordReadingSpan(startedAt_, seconds_);
      global.recordReadingSpan(startedAt_, seconds_);
      daily.recordReadingSpan(startedAt_, seconds_);
      if (seconds_ >= 120 && (paceSamples_ > 0 || seconds_ >= 300) && !book.startDateManual && !book.startDate.isValid()) {
        book.startDate = startedAt_.date;
      }
    }
    dirty_ = true;
  }
  recoverPace();
}

void ReadingSession::save(const std::string& cachePath) {
  if (!finished_ || !dirty_) return;
  book.save(cachePath);
  global.save();
  daily.save();
}

bool ReadingSession::setCompleted(bool completed, const ReadingStatsDateTime& when) {
  if (book.saveBlocked || book.isCompleted == completed) return false;
  book.isCompleted = completed;
  book.completionAchievementPending = completed;
  book.completionPromptDismissedAtHundred = false;
  if (completed) {
    global.completedBooks = addSaturated(global.completedBooks, 1);
    book.estimatedTimeLeftSeconds = 0;
    if (!book.finishedDateManual && !book.finishedDate.isValid() && when.isValid()) book.finishedDate = when.date;
  } else if (global.completedBooks > 0) {
    --global.completedBooks;
  }
  dirty_ = true;
  return true;
}

void ReadingSession::preview(BookReadingStats& bookOut, GlobalReadingStats& globalOut,
                             DailyReadingHistory& dailyOut) const {
  bookOut = book;
  globalOut = global;
  dailyOut = daily;
  if (finished_) return;
  if (seconds_ >= 60) {
    if (bookOut.sessionCount < UINT16_MAX) ++bookOut.sessionCount;
    globalOut.totalSessions = addSaturated(globalOut.totalSessions, 1);
  }
  if (seconds_ >= 10) {
    bookOut.totalReadingSeconds = addSaturated(bookOut.totalReadingSeconds, seconds_);
    globalOut.totalReadingSeconds = addSaturated(globalOut.totalReadingSeconds, seconds_);
    if (startedAt_.isValid()) {
      bookOut.recordReadingSpan(startedAt_, seconds_);
      globalOut.recordReadingSpan(startedAt_, seconds_);
      dailyOut.recordReadingSpan(startedAt_, seconds_);
    }
  }
}

void ReadingSession::applyEdits(const BookReadingStats& edited) {
  if (book.saveBlocked) return;
  ReadingStatsDateTime now;
  getCurrentLocalReadingStatsDateTime(now);
  setCompleted(edited.isCompleted, now);
  book.startDate = edited.startDate;
  book.finishedDate = edited.finishedDate;
  book.startDateManual = edited.startDateManual;
  book.finishedDateManual = edited.finishedDateManual;
  book.completionAchievementPending = edited.completionAchievementPending;
  book.completionPromptDismissedAtHundred = edited.completionPromptDismissedAtHundred;
  dirty_ = true;
}
