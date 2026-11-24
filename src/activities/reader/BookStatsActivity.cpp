#include "BookStatsActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "BookStatsView.h"
#include "ReadingSession.h"
#include "FinishedBooksIndex.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const std::string& bookCachePath, const BookReadingStats& stats,
                                     const float progressPercent, const bool hasEstimatedTimeLeft,
                                     const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                                     const bool returnToHomeOnExit, const InitialPage initialPage)
    : Activity("BookStats", renderer, mappedInput),
      UiAppHost(renderer),
      bookTitle(title),
      bookCachePath(bookCachePath),
      stats(stats),
      globalStats(globalStats),
      returnToHomeOnExit(returnToHomeOnExit),
      progressPercent(progressPercent),
      hasEstimatedTimeLeft(hasEstimatedTimeLeft),
      estimatedTimeLeftSeconds(estimatedTimeLeftSeconds),
      page(initialPage == InitialPage::Achievement   ? Page::Achievement
           : initialPage == InitialPage::FinishedBooks ? Page::FinishedBooks
           : initialPage == InitialPage::ReadingRhythm ? Page::ReadingRhythm
                                                       : Page::Summary) {}

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const std::string& bookCachePath, const BookReadingStats& stats,
                                     const float progressPercent, const bool hasEstimatedTimeLeft,
                                     const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                                     const GlobalReadingStats& allDevicesStats, const bool returnToHomeOnExit,
                                     const InitialPage initialPage)
    : Activity("BookStats", renderer, mappedInput),
      UiAppHost(renderer),
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
      page(initialPage == InitialPage::Achievement   ? Page::Achievement
           : initialPage == InitialPage::FinishedBooks ? Page::FinishedBooks
           : initialPage == InitialPage::ReadingRhythm ? Page::ReadingRhythm
                                                       : Page::Summary) {}

void BookStatsActivity::refreshAllDevicesStats() {
  if (showAllDevicesStats) {
    allDevicesStats = GlobalReadingStats::loadAggregated(globalStats);
  }
}

void BookStatsActivity::saveStats() {
  if (!didChangeStats || !hasEditableBook()) {
    return;
  }

  if (editHandler) {
    editHandler(stats, editContext);
  } else {
    if (!stats.save(bookCachePath)) return;
    globalStats.save();
    if (!bookPath.empty() &&
        !FinishedBooksIndex::recordCanonical(bookPath, bookCachePath, bookTitle, bookAuthor, stats)) {
      LOG_ERR("STAT", "Could not update finished-book history after date editing");
      return;
    }
  }
  finishedBooks = FinishedBooksIndex::load();
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
  stats.completionAchievementPending = completed;
  stats.completionPromptDismissedAtHundred = false;
  if (completed) {
    if (globalStats.completedBooks < UINT32_MAX) ++globalStats.completedBooks;
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

void BookStatsActivity::setSessionPreview(const ReadingSession& session) {
  session.preview(stats, globalStats, dailyReadingHistory);
  hasDailyPreview = true;
}

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(1, &BookStatsActivity::navigationAction, this);
  app.setScreen(&BookStatsActivity::navigationScreen, this);
  showAllDevicesStats = GlobalReadingStats::hasSyncedStats();
  refreshAllDevicesStats();
  if (!hasDailyPreview) dailyReadingHistory.load(globalStats);
  finishedBooks = FinishedBooksIndex::load();
  requestUpdate();
}

void BookStatsActivity::onExit() {
  saveStats();
  Activity::onExit();
}

void BookStatsActivity::exitStatsActivity(const bool) {
  mappedInput.resetHomeButtonInput();
  app.clearTapFlash();
  if (returnToHomeOnExit) onGoHome();
  else finish();
}

void BookStatsActivity::loop() {
  RenderLock lock;
  touchCommand = -1;
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  const bool backReleased = mappedInput.wasReleased(MappedInputManager::Button::Back) || touchCommand == 0;
  const bool previousShortcutPressed = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                                       mappedInput.wasPressed(MappedInputManager::Button::Left) || touchCommand == 2;
  const bool moreShortcutPressed = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                                   mappedInput.wasPressed(MappedInputManager::Button::Right) || touchCommand == 3;
  // wasReleased() consumes one-shot suppression state. Cache it so the Summary
  // page cannot reinterpret the same physical release on a second query.
  const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm) || touchCommand == 1;

  if (usesNoRtcSingleScreenLayout() && page == Page::Summary) {
    if (backReleased) {
      exitStatsActivity(true);
      return;
    }
    if (confirmReleased) {
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
    if (backReleased) {
      saveStats();
      page = Page::Summary;
      requestUpdate();
      return;
    }
    if (confirmReleased) {
      cycleEditField();
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left) || touchCommand == 2) {
      adjustSelectedDateField(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right) || touchCommand == 3) {
      adjustSelectedDateField(1);
      return;
    }
    return;
  }

  if (backReleased) {
    exitStatsActivity(true);
    return;
  }

  if (page == Page::Achievement) {
    if (confirmReleased || moreShortcutPressed) {
      exitStatsActivity(false);
    }
    return;
  }

  if (page == Page::Summary) {
    if (hasEditableBook() && confirmReleased) {
      page = Page::EditDates;
      requestUpdate();
      return;
    }
    if (confirmReleased) {
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

  if (confirmReleased) {
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
  if (bookCachePath.empty() && page == Page::Summary) {
    renderGlobalStatsPage(renderer, nullptr, tr(STR_READING_STATS), globalStats, true, true);
    renderNavigation();
    renderer.displayBuffer();
    return;
  }
  if (usesNoRtcSingleScreenLayout() && page == Page::Summary) {
    renderNoRtcCombinedStatsPage(renderer, nullptr, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                                 estimatedTimeLeftSeconds, globalStats,
                                 showAllDevicesStats ? &allDevicesStats : nullptr, true);
    renderNavigation();
    renderer.displayBuffer();
    return;
  }

  switch (page) {
    case Page::Summary:
      renderCombinedStatsPage(renderer, nullptr, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                              estimatedTimeLeftSeconds, globalStats, true, hasEditableBook(), true);
      break;
    case Page::ReadingRhythm:
      renderReadingRhythmPage(renderer, nullptr, dailyReadingHistory, globalStats, true, true);
      break;
    case Page::FinishedBooks:
      {
        const size_t pageCount =
            std::max<size_t>(1, (finishedBooks.size() + FINISHED_BOOKS_ENTRIES_PER_PAGE - 1) /
                                    FINISHED_BOOKS_ENTRIES_PER_PAGE);
        const bool hasPreviousPage = finishedBooksPage > 0;
        const bool hasNextPage = finishedBooksPage + 1 < pageCount;
        renderFinishedBooksPage(renderer, nullptr, finishedBooks, globalStats, finishedBooksPage, true,
                                hasPreviousPage, hasNextPage, showAllDevicesStats);
      }
      break;
    case Page::Achievement:
      renderReadingAchievementPage(renderer, nullptr, bookTitle, stats, globalStats, true);
      break;
    case Page::AllDevices:
      renderGlobalStatsPage(renderer, nullptr, tr(STR_STATS_ALL_DEVICES_SCREEN), allDevicesStats, true, false);
      break;
    case Page::EditDates:
      renderEditBookDatesPage(renderer, nullptr, bookTitle, stats, selectedEditField, true);
      break;
  }
  renderNavigation();
  renderer.displayBuffer();
}

void BookStatsActivity::navigationAction(const freeink::ui::ActionEvent& event, void* user) {
  static_cast<BookStatsActivity*>(user)->touchCommand = event.value;
}

void BookStatsActivity::navigationScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<BookStatsActivity*>(user);
  const Rect safe = UITheme::getInstance().getScreenSafeArea(self->renderer, false, false);
  const int height = 52;
  const int width = (safe.width - 20) / 4;
  for (int i = 0; i < 4; ++i) {
    freeink::ui::ButtonProps props;
    props.label = self->navigationLabel(i);
    if (!props.label[0]) continue;
    props.action = 1;
    props.value = static_cast<int16_t>(i);
    props.inputMask = freeink::ui::InputTouch;
    screen.button(props, freeink::ui::Rect{static_cast<int16_t>(safe.x + 10 + i * width),
                       static_cast<int16_t>(safe.y + safe.height - height - 6),
                       static_cast<int16_t>(width - 4), static_cast<int16_t>(height)});
  }
}

const char* BookStatsActivity::navigationLabel(const int command) const {
  if (command == 0) return page == Page::Achievement && returnToHomeOnExit ? tr(STR_HOME) : tr(STR_BACK);
  if (command == 1) {
    if (page == Page::EditDates) return tr(STR_NEXT_FIELD);
    if (page == Page::Summary && hasEditableBook()) return tr(STR_EDIT);
    return "";
  }
  if (command == 2) {
    return page == Page::Summary || page == Page::Achievement ? "" : tr(STR_PREVIOUS_SHORT);
  }
  const size_t pages = std::max<size_t>(1, (finishedBooks.size() + FINISHED_BOOKS_ENTRIES_PER_PAGE - 1) /
                                             FINISHED_BOOKS_ENTRIES_PER_PAGE);
  if (page == Page::AllDevices || page == Page::Achievement ||
      (page == Page::FinishedBooks && finishedBooksPage + 1 >= pages && !showAllDevicesStats)) return "";
  return tr(STR_NEXT_SHORT);
}

void BookStatsActivity::renderNavigation() {
  if (mappedInput.hasTouch()) {
    renderUi();
  } else {
    const auto labels = mappedInput.mapLabels(navigationLabel(0), navigationLabel(1),
                                              navigationLabel(2), navigationLabel(3));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}
