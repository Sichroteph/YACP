#include "TimeLeftEstimate.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include "HalClock.h"
#include "HalStorage.h"
#include "BookReadingStats.h"
#include "GlobalReadingStats.h"
#include "DailyReadingHistory.h"
#include "ReadingSession.h"
#include "FinishedBooksIndex.h"
#include "RecentBooksStore.h"
#include <functional>

void checkFinishedMovePreflight() {
  testFiles.clear();
  BookReadingStats stats;
  stats.isCompleted = true;
  stats.totalReadingSeconds = 987;
  const std::string source = "/source.epub", target = "/read/source.epub";
  RECENT_BOOKS.books = {{source, "Title", "Author"}};
  const auto cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(source));
  assert(stats.save(cache));
  assert(!testFiles.count("/.crosspoint/finished_books.bin"));
  assert(FinishedBooksIndex::canMigratePath(source, target));
  assert(testFiles.count("/.crosspoint/finished_books.bin"));
  assert(FinishedBooksIndex::migratePath(source, target));
  assert(FinishedBooksIndex::migratePath(source, target));
  assert(FinishedBooksIndex::load().size() == 1);
  assert(FinishedBooksIndex::load()[0].totalReadingSeconds == 987);
  const auto before = testFiles.at("/.crosspoint/finished_books.bin");
  assert(!FinishedBooksIndex::canMigratePath("/another.epub", target));
  assert(testFiles.at("/.crosspoint/finished_books.bin") == before);
  assert(FinishedBooksIndex::record(source, "Second", "Author", stats));
  const auto both = testFiles.at("/.crosspoint/finished_books.bin");
  assert(!FinishedBooksIndex::migratePath(source, target));
  assert(testFiles.at("/.crosspoint/finished_books.bin") == both);
  RECENT_BOOKS.books.clear();
}

void checkClockAndMidnight() {
  halClock.time.tm_year = 126;
  halClock.time.tm_mon = 9;
  halClock.time.tm_mday = 1;
  halClock.time.tm_hour = 23;
  halClock.time.tm_min = 59;
  halClock.time.tm_sec = 30;
  ReadingStatsDateTime local;
  assert(getCurrentLocalReadingStatsDateTime(local));
  assert(local.date.year == 2026 && local.date.month == 10 && local.date.day == 1);
  assert(local.hour == 23 && local.minute == 59 && local.second == 30);
  BookReadingStats book;
  book.recordReadingSpan(local, 90);
  assert(book.timeOfDaySeconds[3] == 90);
  assert(book.dayOfWeekSeconds[3] == 30 && book.dayOfWeekSeconds[4] == 60);
  halClock.available = false;
  assert(!getCurrentLocalReadingStatsDateTime(local));
  assert(local.date.year == 0);
  halClock.available = true;
}

void checkFutureBookIsNeverOverwritten() {
  auto& future = testFiles["/book/stats_v6.bin"];
  future.assign(74, 0);
  future[0] = 7;
  const auto original = future;
  auto book = BookReadingStats::load("/book");
  assert(book.saveBlocked);
  book.totalReadingSeconds = 1;
  book.save("/book");
  assert(testFiles.at("/book/stats_v6.bin") == original);
  ReadingSession session;
  session.load("/book", 300);
  session.pageShown(1000);
  session.turn(91000, true, false);
  session.cacheTimeLeft(1000);
  assert(!session.setCompleted(true, ReadingStatsDateTime{{2026,10,1},12,0,0}));
  session.finish(100000);
  session.save("/book");
  assert(session.activeSeconds() == 0 && session.global.totalPagesTurned == 0);
  assert(session.global.completedBooks == 0);
  assert(testFiles.at("/book/stats_v6.bin") == original);
}

void checkLongDaysAndFailedWrite() {
  testFiles.clear();
  GlobalReadingStats global;
  DailyReadingHistory daily;
  daily.load(global);
  ReadingStatsDateTime start{{2026, 10, 1}, 10, 0, 0};
  const uint32_t day = readingStatsDayIndex(start.date);
  daily.recordReadingSpan(start, 300 * 60);
  assert(daily.minutesOnDay(day) == 300);
  assert(daily.save());
  const auto saved = testFiles.at("/.crosspoint/daily_reading.bin");
  daily.recordReadingSpan(start, 10 * 60);
  failStatsWrite = true;
  assert(!daily.save());
  assert(testFiles.at("/.crosspoint/daily_reading.bin") == saved);
  failStatsWrite = false;
  failStatsClose = true;
  assert(!daily.save());
  assert(testFiles.at("/.crosspoint/daily_reading.bin") == saved);
  failStatsClose = false;
  assert(daily.save());
  DailyReadingHistory reloaded;
  reloaded.load(global);
  assert(reloaded.minutesOnDay(day) == 310);
}

void checkSyncedTotalsDoNotCountLocalDeviceTwice() {
  testFiles.clear();
  GlobalReadingStats local;
  local.totalReadingSeconds = 123;
  local.save();
  const auto encoded = testFiles.at("/.crosspoint/global_stats.bin");
  testFiles["/.crosspoint/synced_stats/device_010203040506.bin"] = encoded;
  testFiles["/.crosspoint/synced_stats/device_aabbccddeeff.bin"] = encoded;
  assert(GlobalReadingStats::loadAggregated(local).totalReadingSeconds == 246);
}

void checkBookInterruptedSaveRetainsOriginal() {
  testFiles.clear();
  BookReadingStats original;
  original.totalReadingSeconds = 12345;
  original.save("/book");
  const auto bytes = testFiles.at("/book/stats_v6.bin");
  BookReadingStats changed = original;
  changed.totalReadingSeconds = 23456;
  failStatsWrite = true;
  changed.save("/book");
  assert(testFiles.at("/book/stats_v6.bin") == bytes);
  failStatsWrite = false;
  failStatsClose = true;
  changed.save("/book");
  assert(testFiles.at("/book/stats_v6.bin") == bytes);
  failStatsClose = false;
  changed.save("/book");
  assert(BookReadingStats::load("/book").totalReadingSeconds == 23456);
  assert(testFiles.at("/book/stats_v6.bin.bak") == bytes);
  // Simulate interruption between moving the primary and publishing the new file.
  testFiles.erase("/book/stats_v6.bin");
  assert(BookReadingStats::load("/book").totalReadingSeconds == 12345);
}

void checkSessionThresholdsAndPause() {
  testFiles.clear();
  ReadingSession session;
  session.load("/book", 300);
  session.pageShown(1000);
  session.pageShown(5000);  // A redraw must not restart the reading interval.
  session.turn(61000, true, false);
  assert(session.activeSeconds() == 60 && session.book.totalPagesTurned == 1);
  assert(session.paceSamples() == 0);  // First turn is the pace warmup.
  session.pageShown(62000);
  session.pause(72000);
  session.pause(900000);  // Time in the menu is excluded, however long it lasts.
  session.pageShown(1000000);
  session.turn(1301000, true, false);  // Idle page counts as a turn, not time.
  assert(session.activeSeconds() == 70 && session.book.totalPagesTurned == 2);
  assert(testFiles.empty());  // No filesystem work on the ordinary page path.
  session.finish(1400000);
  session.finish(1500000);  // Repeated exit must not double count.
  assert(session.book.sessionCount == 1 && session.global.totalSessions == 1);
  assert(session.book.totalReadingSeconds == 70 && session.global.totalReadingSeconds == 70);
  session.save("/book");
  assert(BookReadingStats::load("/book").totalReadingSeconds == 70);
}

void checkShortSessionsAndClockWrap() {
  testFiles.clear();
  ReadingSession shortSession;
  shortSession.load("/book", 300);
  shortSession.pageShown(0);
  shortSession.finish(9999);
  shortSession.save("/book");
  assert(testFiles.empty() && shortSession.book.sessionCount == 0);
  ReadingSession wrapped;
  wrapped.load("/book", 300);
  wrapped.pageShown(UINT32_MAX - 4999);
  wrapped.finish(5000);
  assert(wrapped.book.totalReadingSeconds == 10 && wrapped.book.sessionCount == 0);
  ReadingSession failedOpen;
  failedOpen.load("/book", 300);
  failedOpen.finish(900000);
  assert(failedOpen.book.totalReadingSeconds == 0);
}

void checkPaceWarmupAndChapterBoundary() {
  testFiles.clear();
  ReadingSession session;
  session.load("/book", 300);
  uint32_t now = 1000;
  for (int i = 0; i < 11; ++i) {
    session.pageShown(now);
    now += 20000;
    session.turn(now, true, false);
  }
  uint16_t pace = 0;
  assert(session.paceSamples() == 10 && session.sessionPace(pace) && pace == 20);
  session.pageShown(now);
  session.turn(now + 10000, true, true);  // Partial final page must not bias pace.
  assert(session.paceSamples() == 10);
  session.pageShown(now + 10000);
  session.turn(now + 20000, false, false);
  session.pageShown(now + 20000);
  session.turn(now + 30000, true, false);
  assert(session.paceSamples() == 10);  // Backward navigation re-arms warmup.
  session.finish(now + 30000);
  assert(session.book.startDate.isValid());
}

void checkSessionWithoutRtcAndManualDate() {
  testFiles.clear();
  halClock.available = false;
  ReadingSession session;
  session.load("/book", 300);
  session.pageShown(1000);
  session.finish(301000);
  assert(session.book.totalReadingSeconds == 300 && !session.book.startDate.isValid());
  halClock.available = true;
  ReadingSession manual;
  manual.load("/book", 300);
  manual.book.startDateManual = true;
  manual.book.startDate = {2024, 3, 4};
  manual.pageShown(1000);
  manual.finish(301000);
  assert(manual.book.startDate.year == 2024 && manual.book.startDate.month == 3);
}

void checkFinishedIndexIdentityAndFailures() {
  testFiles.clear();
  BookReadingStats book;
  book.isCompleted = true;
  book.totalReadingSeconds = 1200;
  book.startDate = {2026, 9, 1};
  book.finishedDate = {2026, 10, 1};
  assert(FinishedBooksIndex::record("/a.epub", "Title", "Author", book));
  const auto original = testFiles.at("/.crosspoint/finished_books.bin");
  assert(FinishedBooksIndex::record("/a.epub", "Title", "Author", book));
  assert(FinishedBooksIndex::load().size() == 1);
  failStatsWrite = true;
  assert(!FinishedBooksIndex::record("/b.epub", "Second", "Author", book));
  failStatsWrite = false;
  assert(testFiles.at("/.crosspoint/finished_books.bin") == original);
  assert(FinishedBooksIndex::migratePath("/a.epub", "/Read/a.epub"));
  assert(FinishedBooksIndex::record("/Read/a.epub", "Title", "Author", book));
  assert(FinishedBooksIndex::load().size() == 1);
  book.isCompleted = false;
  assert(FinishedBooksIndex::record("/Read/a.epub", "Title", "Author", book));
  assert(FinishedBooksIndex::load().empty());
}

void checkFinishedIndexFutureAndRecovery() {
  testFiles.clear();
  BookReadingStats book;
  book.isCompleted = true;
  const std::string path = "/.crosspoint/finished_books.bin";
  assert(FinishedBooksIndex::record("/a.epub", "Title", "Author", book));
  const auto good = testFiles.at(path);
  testFiles[path][4] = 4;
  const auto future = testFiles.at(path);
  assert(!FinishedBooksIndex::record("/b.epub", "Second", "Author", book));
  assert(testFiles.at(path) == future);
  testFiles[path] = {1, 2, 3};
  assert(!FinishedBooksIndex::record("/b.epub", "Second", "Author", book));
  testFiles[path + ".bak"] = good;
  failStatsRenameTo = path;
  assert(!FinishedBooksIndex::record("/b.epub", "Second", "Author", book));
  assert(testFiles.at(path + ".bak") == good);
  failStatsRenameTo.clear();
  assert(FinishedBooksIndex::load().size() == 1);
}

void checkFinishedIndexLimitAndUtf8() {
  testFiles.clear();
  BookReadingStats book;
  book.isCompleted = true;
  for (int i = 0; i < 34; ++i) {
    assert(FinishedBooksIndex::record("/" + std::to_string(i), std::to_string(i), "Author", book));
  }
  assert(FinishedBooksIndex::load().size() == 32);
  testFiles.clear();
  const std::string title = std::string(159, 'A') + "\xc3\xa9";
  assert(FinishedBooksIndex::record("/a.epub", title, "Author", book));
  assert(FinishedBooksIndex::load()[0].title == std::string(159, 'A'));
}

void checkCompletionIsCountedOnce() {
  testFiles.clear();
  ReadingSession session;
  session.load("/book", 300);
  ReadingStatsDateTime now{{2026, 10, 1}, 12, 0, 0};
  assert(session.setCompleted(true, now));
  assert(!session.setCompleted(true, now));
  assert(session.global.completedBooks == 1 && session.book.finishedDate.day == 1);
  assert(session.book.completionAchievementPending);
  assert(session.setCompleted(false, now));
  assert(session.global.completedBooks == 0);
  session.book.finishedDateManual = true;
  session.book.finishedDate = {2025, 3, 4};
  assert(session.setCompleted(true, now));
  assert(session.book.finishedDate.year == 2025);
  session.finish(1000);
  session.save("/book");
  assert(BookReadingStats::load("/book").isCompleted);
}

void checkPreviewAndEditsDoNotSplitSession() {
  testFiles.clear();
  ReadingSession session;
  session.load("/book", 300);
  session.pageShown(1000);
  session.pause(71000);
  BookReadingStats preview;
  GlobalReadingStats total;
  DailyReadingHistory daily;
  session.preview(preview, total, daily);
  assert(preview.totalReadingSeconds == 70 && total.totalReadingSeconds == 70);
  assert(preview.sessionCount == 1 && total.totalSessions == 1);
  assert(session.book.totalReadingSeconds == 0 && session.book.sessionCount == 0);
  assert(testFiles.empty());
  preview.isCompleted = true;
  preview.finishedDateManual = true;
  preview.finishedDate = {2025, 3, 4};
  session.applyEdits(preview);
  assert(session.book.totalReadingSeconds == 0 && session.global.totalReadingSeconds == 0);
  assert(session.global.completedBooks == 1 && session.book.finishedDate.year == 2025);
  session.applyEdits(preview);
  assert(session.global.completedBooks == 1);
  session.pageShown(100000);
  session.finish(130000);
  session.save("/book");
  assert(session.book.totalReadingSeconds == 100 && session.book.sessionCount == 1);
  assert(session.global.totalSessions == 1 && session.global.completedBooks == 1);
  session.preview(preview, total, daily);
  assert(preview.totalReadingSeconds == 100 && total.totalReadingSeconds == 100);
  assert(BookReadingStats::load("/book").finishedDate.year == 2025);
}

void checkTimeLeftGeometryAndOverflow() {
  TimeLeftInput input{1000, 200, 200, 0, 30, 2, 10};
  assert(estimateReadingTimeLeft(input, false) == 210);
  assert(estimateReadingTimeLeft(input, true) == 1140);
  input.elapsedSeconds = 1200;
  assert(estimateReadingTimeLeft(input, true) == 3420);
  input.page = 9;
  assert(estimateReadingTimeLeft(input, false) == 0);
  input.page = 10;
  assert(estimateReadingTimeLeft(input, true) == 0);
  input = {UINT32_MAX, 0, 1, UINT32_MAX, UINT16_MAX, 0, 1};
  assert(estimateReadingTimeLeft(input, true) == UINT32_MAX);
  input.previousChapterBytes = UINT32_MAX;
  assert(estimateReadingTimeLeft(input, true) == 0);
  input = {1000, 200, 200, 1000, 0, 2, 10};
  assert(estimateReadingTimeLeft(input, true) == 0);
}

void checkTimeLeftPaceAndCachedEstimate() {
  testFiles.clear();
  ReadingSession session;
  session.load("/book", 300);
  session.book.avgSecondsPerForwardPage = 20;
  session.book.paceSampleCount = 2;
  assert(session.estimatePace() == 0);
  session.book.paceSampleCount = 3;
  assert(session.estimatePace() == 20);
  session.cacheTimeLeft(1000);
  session.finish(1000);
  session.save("/book");
  assert(BookReadingStats::load("/book").estimatedTimeLeftSeconds == 1000);
  session.book.isCompleted = true;
  session.cacheTimeLeft(2000);
  assert(session.book.estimatedTimeLeftSeconds == 0);
}

int main(int argc, char** argv) {
  if (argc == 3) {
    std::ifstream file(argv[2], std::ios::binary);
    if (!file) return 2;
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    std::string path;
    const std::string mode = argv[1];
    if (mode == "book") {
      path = "/book/stats_v6.bin";
      testFiles[path] = bytes;
      auto book = BookReadingStats::load("/book");
      if (book.saveBlocked) return 3;
      book.save("/book");
    } else if (mode == "daily") {
      path = "/.crosspoint/daily_reading.bin";
      testFiles[path] = bytes;
      DailyReadingHistory history;
      history.load(GlobalReadingStats{});
      if (!history.save()) return 4;
    } else if (mode == "global") {
      path = "/.crosspoint/global_stats.bin";
      testFiles[path] = bytes;
      GlobalReadingStats::load().save();
    } else return 5;
    for (uint8_t value : testFiles.at(path)) printf("%02x", value);
    printf("\n");
    return 0;
  }
  checkClockAndMidnight();
  checkFutureBookIsNeverOverwritten();
  checkLongDaysAndFailedWrite();
  checkSyncedTotalsDoNotCountLocalDeviceTwice();
  checkBookInterruptedSaveRetainsOriginal();
  checkSessionThresholdsAndPause();
  checkShortSessionsAndClockWrap();
  checkPaceWarmupAndChapterBoundary();
  checkSessionWithoutRtcAndManualDate();
  checkFinishedIndexIdentityAndFailures();
  checkFinishedMovePreflight();
  checkFinishedIndexFutureAndRecovery();
  checkFinishedIndexLimitAndUtf8();
  checkCompletionIsCountedOnce();
  checkPreviewAndEditsDoNotSplitSession();
  checkTimeLeftGeometryAndOverflow();
  checkTimeLeftPaceAndCachedEstimate();
  std::cout << "16 statistics integration scenarios passed\n";
}
