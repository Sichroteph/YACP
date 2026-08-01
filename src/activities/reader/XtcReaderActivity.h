/**
 * XtcReaderActivity.h
 *
 * XTC ebook reader activity for CrossPoint Reader
 * Displays pre-rendered XTC pages on e-ink display
 */

#pragma once

#include <Xtc.h>

#include <string>
#include <utility>

#include "BookReadingStats.h"
#include "DailyReadingHistory.h"
#include "EndOfBookOptions.h"
#include "GlobalReadingStats.h"
#include "ReaderProgressSaveDebouncer.h"
#include "activities/Activity.h"

class XtcReaderActivity final : public Activity {
  std::shared_ptr<Xtc> xtc;

  uint32_t currentPage = 0;
  int pagesUntilFullRefresh = 0;
  unsigned long pageShownAtMs = 0UL;
  uint32_t sessionReadingSeconds = 0;
  BookReadingStats stats;
  GlobalReadingStats globalStats;
  // Fixed 730-byte buffer is owned by the activity and reused for the session.
  DailyReadingHistory dailyReadingHistory;
  ReadingStatsDateTime sessionStartLocalDateTime;
  bool hasSessionStartLocalDateTime = false;
  bool sessionAdvancedPage = false;
  bool longPowerPageTurnHandled = false;
  bool sideButtonLongPressHandled = false;
  bool frontButtonLongPressHandled = false;
  bool longPressBackHandled = false;
  bool showCompletionAchievementOnExit = false;
  ReaderProgressSaveDebouncer progressSaveDebouncer;
  // Next-book suggestion menu for the End-of-Book screen
  EndOfBookOptions endOfBookOptions;

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  void renderStatusBarOverlay(StatusBarOverlayPosition position) const;
  StatusBarInfo getStatusBarInfo() const;
  bool saveProgress(uint32_t page);
  bool queueProgressSave();
  bool flushQueuedProgress();
  void loadProgress();
  void pauseReadingStatsTimer(const char* source = "unknown");
  void resumeReadingStatsTimer(const char* source = "unknown");
  bool currentPageReadingSecondsForStats(uint32_t& seconds, const char* source) const;
  bool forwardPageReadElapsed(uint32_t& seconds, const char* source) const;
  void recordCurrentPageReadingTime(const char* source = "unknown");
  void recordForwardPageTurn(uint32_t seconds);
  void commitReadingStats();
  void resetCurrentBookStatsAfterDelete();
  void syncFinishedBookIndex();
  void handleBookStatsReturn();
  BookReadingStats achievementStatsPreview(uint32_t* pendingReadingSeconds = nullptr) const;
  void goHomeOrShowCompletionAchievement();
  void setBookCompleted(bool isCompleted);
  float getCurrentBookProgressPercent() const;
  void openChapterSelection();
  void openReadingStats();
  void deleteBookStats();
  void deleteBookCache();
  void onReaderMenuConfirm(int action);
  bool executeLongPressBackAction();

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Xtc> xtc,
                             int initialRefreshCountdown = 0)
      : Activity("XtcReader", renderer, mappedInput),
        xtc(std::move(xtc)),
        pagesUntilFullRefresh(initialRefreshCountdown) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowReaderIdlePowerSaving() const override { return true; }
  bool canSnapshotForSleepOverlay() const override { return true; }
  std::string getCurrentBookPath() const override { return xtc ? xtc->getPath() : std::string{}; }

  // Renders the last saved page to the frame buffer without flushing to display.
  // Used by SleepActivity to prepare the background for the overlay sleep mode.
  // Returns false if the page cannot be loaded (missing cache / file error).
  static bool drawCurrentPageToBuffer(const std::string& filePath, GfxRenderer& renderer);
  ScreenshotInfo getScreenshotInfo() const override;
};
