#include "BookStatsActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "BookStatsView.h"
#include "FinishedBooksIndex.h"
#include "MappedInputManager.h"

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const std::string& bookCachePath, const BookReadingStats& stats,
                                     const float progressPercent, const bool hasEstimatedTimeLeft,
                                     const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                                     const bool returnToHomeOnExit, const InitialPage initialPage)
    : Activity("BookStats", renderer, mappedInput),
      bookTitle(title),
      bookCachePath(bookCachePath),
      stats(stats),
      globalStats(globalStats),
      returnToHomeOnExit(returnToHomeOnExit),
      progressPercent(progressPercent),
      hasEstimatedTimeLeft(hasEstimatedTimeLeft),
      estimatedTimeLeftSeconds(estimatedTimeLeftSeconds),
      page(initialPage == InitialPage::Achievement ? Page::Achievement : Page::Summary) {}

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const std::string& bookCachePath, const BookReadingStats& stats,
                                     const float progressPercent, const bool hasEstimatedTimeLeft,
                                     const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                                     const GlobalReadingStats& allDevicesStats, const bool returnToHomeOnExit,
                                     const InitialPage initialPage)
    : Activity("BookStats", renderer, mappedInput),
      bookTitle(title),
      bookCachePath(bookCachePath),
      stats(stats),
      globalStats(globalStats),
      allDevicesStats(allDevicesStats),
      showAllDevicesStats(true),
      returnToHomeOnExit(returnToHomeOnExit),
      progressPercent(progressPercent),
      hasEstimatedTimeLeft(hasEstimatedTimeLeft),
      estimatedTimeLeftSeconds(estimatedTimeLeftSeconds),
      page(initialPage == InitialPage::Achievement ? Page::Achievement : Page::Summary) {}

void BookStatsActivity::refreshAllDevicesStats() {
  if (showAllDevicesStats) {
    allDevicesStats = GlobalReadingStats::loadAggregated(globalStats);
  }
}

void BookStatsActivity::saveStats() {
  if (!didChangeStats || !hasEditableBook()) {
    return;
  }

  stats.save(bookCachePath);
  globalStats.save();
  if (!FinishedBooksIndex::record(bookCachePath, bookTitle, stats)) {
    LOG_ERR("BSTATS", "Failed to update finished-books index");
  }
  refreshAllDevicesStats();
  didChangeStats = false;
}

void BookStatsActivity::cycleEditField() { selectedEditField = (selectedEditField + 1) % 6; }

ReadingStatsDate BookStatsActivity::defaultDateForField(const bool finishedField) const {
  if (finishedField && stats.finishedDate.isValid()) {
    return stats.finishedDate;
  }
  if (!finishedField && stats.startDate.isValid()) {
    return stats.startDate;
  }
  if (finishedField && stats.startDate.isValid()) {
    return stats.startDate;
  }
  if (!finishedField && stats.finishedDate.isValid()) {
    return stats.finishedDate;
  }

  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now)) {
    return now.date;
  }
  return {2000, 1, 1};
}

void BookStatsActivity::applyCompletedState(const bool completed) {
  if (stats.isCompleted == completed) {
    return;
  }

  stats.isCompleted = completed;
  if (completed) {
    globalStats.completedBooks++;
    if (!stats.finishedDateManual && !stats.finishedDate.isValid()) {
      ReadingStatsDateTime now;
      if (getCurrentLocalReadingStatsDateTime(now)) {
        stats.finishedDate = now.date;
      }
    }
  } else if (globalStats.completedBooks > 0) {
    globalStats.completedBooks--;
  }
}

void BookStatsActivity::normalizeEditedDates(const bool editedFinishedField) {
  if (!stats.startDate.isValid() || !stats.finishedDate.isValid()) {
    return;
  }
  if (compareReadingStatsDate(stats.finishedDate, stats.startDate) >= 0) {
    return;
  }

  if (editedFinishedField) {
    stats.startDate = stats.finishedDate;
  } else {
    stats.finishedDate = stats.startDate;
  }
}

void BookStatsActivity::clearEditedDate(const bool finishedField) {
  ReadingStatsDate& date = finishedField ? stats.finishedDate : stats.startDate;
  date.clear();

  if (finishedField) {
    stats.finishedDateManual = false;
    applyCompletedState(false);
  } else {
    stats.startDateManual = false;
  }

  didChangeStats = true;
  setResult(ReadingStatsResult{true});
  requestUpdate();
}

bool BookStatsActivity::shouldClearDateOnAdjust(const ReadingStatsDate& date, const bool finishedField,
                                                const int fieldIndex, const int delta) const {
  if (!date.isValid()) {
    return false;
  }

  switch (fieldIndex) {
    case 0:
      return (date.month == 1 && delta < 0) || (date.month == 12 && delta > 0);
    case 1: {
      const uint8_t monthDays = daysInMonth(date.year, date.month);
      return (date.day == 1 && delta < 0) || (date.day == monthDays && delta > 0);
    }
    case 2:
      return (date.year == 2000 && delta < 0) || (date.year == 2099 && delta > 0);
    default:
      return false;
  }
}

void BookStatsActivity::adjustSelectedDateField(const int delta) {
  const bool finishedField = selectedEditField >= 3;
  ReadingStatsDate& date = finishedField ? stats.finishedDate : stats.startDate;
  const int fieldIndex = selectedEditField % 3;

  if (shouldClearDateOnAdjust(date, finishedField, fieldIndex, delta)) {
    clearEditedDate(finishedField);
    return;
  }

  if (!date.isValid()) {
    date = defaultDateForField(finishedField);
  }

  switch (fieldIndex) {
    case 0: {
      int month = static_cast<int>(date.month) + delta;
      while (month < 1) {
        month += 12;
      }
      while (month > 12) {
        month -= 12;
      }
      date.month = static_cast<uint8_t>(month);
      break;
    }
    case 1: {
      const int monthDays = daysInMonth(date.year, date.month);
      int day = static_cast<int>(date.day) + delta;
      while (day < 1) {
        day += monthDays;
      }
      while (day > monthDays) {
        day -= monthDays;
      }
      date.day = static_cast<uint8_t>(day);
      break;
    }
    case 2: {
      int year = static_cast<int>(date.year) + delta;
      if (year < 2000) {
        year = 2099;
      } else if (year > 2099) {
        year = 2000;
      }
      date.year = static_cast<uint16_t>(year);
      break;
    }
  }

  const uint8_t monthDays = daysInMonth(date.year, date.month);
  if (date.day > monthDays) {
    date.day = monthDays;
  }

  if (finishedField) {
    stats.finishedDateManual = true;
    applyCompletedState(true);
  } else {
    stats.startDateManual = true;
  }
  normalizeEditedDates(finishedField);

  didChangeStats = true;
  setResult(ReadingStatsResult{true});
  requestUpdate();
}

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  dailyReadingHistory.load(globalStats);
  finishedBooks = FinishedBooksIndex::load();
  requestUpdate();
}

void BookStatsActivity::onExit() {
  saveStats();
  Activity::onExit();
}

void BookStatsActivity::exitStatsActivity(const bool viaBack) {
  if (viaBack) {
    mappedInput.suppressNextBackRelease();
  } else {
    mappedInput.suppressNextConfirmRelease();
  }

  if (viaBack || returnToHomeOnExit) {
    onGoHome();
    return;
  }

  finish();
}

void BookStatsActivity::loop() {
  const bool previousShortcutPressed = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                                       mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool moreShortcutPressed = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                                   mappedInput.wasPressed(MappedInputManager::Button::Right);

  if (usesNoRtcSingleScreenLayout() && page == Page::Summary) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      exitStatsActivity(true);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      exitStatsActivity(false);
      return;
    }
    if (moreShortcutPressed) {
      finishedBooksPage = 0;
      page = Page::FinishedBooks;
      requestUpdate();
    }
    return;
  }

  if (page == Page::EditDates) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      saveStats();
      page = Page::Summary;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      cycleEditField();
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      adjustSelectedDateField(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      adjustSelectedDateField(1);
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    exitStatsActivity(true);
    return;
  }

  if (page == Page::Achievement) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || moreShortcutPressed) {
      exitStatsActivity(false);
    }
    return;
  }

  if (page == Page::Summary) {
    if (hasEditableBook() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      page = Page::EditDates;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      exitStatsActivity(false);
      return;
    }
    if (moreShortcutPressed) {
      page = Page::ReadingRhythm;
      requestUpdate();
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    exitStatsActivity(false);
    return;
  }

  if (page == Page::ReadingRhythm && moreShortcutPressed) {
    finishedBooksPage = 0;
    page = Page::FinishedBooks;
    requestUpdate();
    return;
  }

  if (page == Page::ReadingRhythm && previousShortcutPressed) {
    page = Page::Summary;
    requestUpdate();
    return;
  }

  if (page == Page::FinishedBooks) {
    const size_t pageCount =
        std::max<size_t>(1, (finishedBooks.size() + FINISHED_BOOKS_ENTRIES_PER_PAGE - 1) /
                                FINISHED_BOOKS_ENTRIES_PER_PAGE);
    if (moreShortcutPressed) {
      if (finishedBooksPage + 1 < pageCount) {
        finishedBooksPage++;
        requestUpdate();
      } else if (showAllDevicesStats) {
        page = Page::AllDevices;
        requestUpdate();
      }
      return;
    }
    if (previousShortcutPressed) {
      if (finishedBooksPage > 0) {
        finishedBooksPage--;
      } else {
        page = usesNoRtcSingleScreenLayout() ? Page::Summary : Page::ReadingRhythm;
      }
      requestUpdate();
      return;
    }
  }

  if (page == Page::AllDevices && previousShortcutPressed) {
    page = Page::FinishedBooks;
    requestUpdate();
  }
}

void BookStatsActivity::render(RenderLock&&) {
  if (usesNoRtcSingleScreenLayout() && page == Page::Summary) {
    renderNoRtcCombinedStatsPage(renderer, &mappedInput, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                                 estimatedTimeLeftSeconds, globalStats,
                                 showAllDevicesStats ? &allDevicesStats : nullptr, true);
    renderer.displayBuffer();
    return;
  }

  switch (page) {
    case Page::Summary:
      renderCombinedStatsPage(renderer, &mappedInput, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                              estimatedTimeLeftSeconds, globalStats, true, hasEditableBook(), true);
      break;
    case Page::ReadingRhythm:
      renderReadingRhythmPage(renderer, &mappedInput, dailyReadingHistory, globalStats, true, true);
      break;
    case Page::FinishedBooks:
      {
        const size_t pageCount =
            std::max<size_t>(1, (finishedBooks.size() + FINISHED_BOOKS_ENTRIES_PER_PAGE - 1) /
                                    FINISHED_BOOKS_ENTRIES_PER_PAGE);
        const bool hasPreviousPage = finishedBooksPage > 0;
        const bool hasNextPage = finishedBooksPage + 1 < pageCount;
        renderFinishedBooksPage(renderer, &mappedInput, finishedBooks, globalStats, finishedBooksPage, true,
                                hasPreviousPage, hasNextPage, showAllDevicesStats);
      }
      break;
    case Page::Achievement:
      renderReadingAchievementPage(renderer, &mappedInput, bookTitle, stats, globalStats, true);
      break;
    case Page::AllDevices:
      renderGlobalStatsPage(renderer, &mappedInput, tr(STR_STATS_ALL_DEVICES_SCREEN), allDevicesStats, true, false);
      break;
    case Page::EditDates:
      renderEditBookDatesPage(renderer, &mappedInput, bookTitle, stats, selectedEditField, true);
      break;
  }
  renderer.displayBuffer();
}
