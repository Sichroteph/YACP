#pragma once

#include <HalClock.h>

#include <memory>
#include <string>

#include "../Activity.h"
#include "components/UiAppHost.h"
#include "BookReadingStats.h"
#include "DailyReadingHistory.h"
#include "FinishedBooksIndex.h"
#include "GlobalReadingStats.h"

class ReadingSession;

class BookStatsActivity final : public Activity, private UiAppHost {
 public:
  enum class InitialPage : uint8_t { Summary, Achievement, FinishedBooks, ReadingRhythm };

 private:
  bool hasDailyPreview = false;
  // Reader remains alive on the activity stack until this child exits.
  void (*editHandler)(const BookReadingStats&, void*) = nullptr;
  void* editContext = nullptr;
  int touchCommand = -1;
  static void navigationScreen(UiScreen& screen, void* user);
  static void navigationAction(const freeink::ui::ActionEvent& event, void* user);
  void renderNavigation();
  const char* navigationLabel(int command) const;
  enum class Page : uint8_t { Summary, ReadingRhythm, FinishedBooks, Achievement, AllDevices, EditDates };

  std::string bookTitle;
  std::string bookCachePath;
  // Identity is held only by an explicitly opened standalone stats screen.
  // Reader previews persist through their owner instead.
  std::string bookPath;
  std::string bookAuthor;
  BookReadingStats stats;
  GlobalReadingStats globalStats;
  GlobalReadingStats allDevicesStats;
  // Kept in the heap-allocated activity so rendering never allocates or places
  // the 730-day history on the task stack.
  DailyReadingHistory dailyReadingHistory;
  std::vector<FinishedBookEntry> finishedBooks;
  bool showAllDevicesStats = false;
  bool returnToHomeOnExit = false;
  float progressPercent = -1.0f;
  bool hasEstimatedTimeLeft = false;
  uint32_t estimatedTimeLeftSeconds = 0;
  Page page = Page::Summary;
  size_t finishedBooksPage = 0;
  int selectedEditField = 0;
  bool didChangeStats = false;

  bool hasEditableBook() const { return !bookCachePath.empty() && !stats.saveBlocked && halClock.isAvailable(); }
  bool usesNoRtcSingleScreenLayout() const { return !halClock.isAvailable(); }
  void refreshAllDevicesStats();
  void saveStats();
  void cycleEditField();
  void adjustSelectedDateField(int delta);
  void applyCompletedState(bool completed);
  ReadingStatsDate defaultDateForField(bool finishedField) const;
  void clearEditedDate(bool finishedField);
  bool shouldClearDateOnAdjust(const ReadingStatsDate& date, bool finishedField, int fieldIndex, int delta) const;
  void normalizeEditedDates(const bool editedFinishedField);
  void exitStatsActivity(bool viaBack);

 public:
  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                    const std::string& bookCachePath, const BookReadingStats& stats, float progressPercent,
                    bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                    bool returnToHomeOnExit = false, InitialPage initialPage = InitialPage::Summary);
  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                    const std::string& bookCachePath, const BookReadingStats& stats, float progressPercent,
                    bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                    const GlobalReadingStats& allDevicesStats, bool returnToHomeOnExit = false,
                    InitialPage initialPage = InitialPage::Summary);

  void setSessionPreview(const ReadingSession& session);
  void setBookIdentity(const std::string& path, const std::string& author) {
    bookPath = path;
    bookAuthor = author;
  }
  void setEditHandler(void (*handler)(const BookReadingStats&, void*), void* context) {
    editHandler = handler;
    editContext = context;
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
