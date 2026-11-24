#include "YacpHomeActivity.h"

#include <FsHelpers.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <functional>

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"
#include "RecentBooksActivity.h"
#include "activities/UiListActivity.h"
#include "activities/power/PowerStatsActivity.h"
#include "activities/reader/BookStatsActivity.h"
#include "activities/reader/DictionaryHistoryActivity.h"
#include "components/HeaderDate.h"
#include "components/UITheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "fontIds.h"
#include "images/YacpLogo120.generated.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;

namespace {
// Use CrossPoint's list routing, scrolling and touch targets for the secondary
// menu. Its bounded row and string storage is allocated only when requested.
class YacpHomeMenu final : public UiListActivity {
  RecentBook selectedBook;
  std::vector<fui::ListItem> rows;
  bool hasOpds = false;
  std::string selectedBookPath;
  size_t selectedBookOrdinal = 0;
  void reloadSelectedBook() {
    const RecentBook* candidate = nullptr;
    size_t candidateOrdinal = 0;
    size_t ordinal = 0;
    for (const auto& book : RECENT_BOOKS.getBooks()) {
      if (RecentBooksStore::isMissing(book)) continue;
      if (!candidate || ordinal <= selectedBookOrdinal || book.path == selectedBookPath) {
        candidate = &book;
        candidateOrdinal = ordinal;
      }
      if (book.path == selectedBookPath) break;
      ++ordinal;
    }
    selectedBook = candidate ? *candidate : RecentBook{};
    selectedBookPath = selectedBook.path;
    selectedBookOrdinal = candidateOrdinal;
  }
  int listCount() const override { return static_cast<int>(rows.size()); }
  const char* headerTitle() const override { return tr(STR_MENU); }

  void buildScreen(UiScreen& screen) override {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    constexpr int headerListGap = 4;
    screen.setContentMarginFromScreen(
        fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + headerListGap),
                    static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
                    static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height + metrics.buttonHintsHeight),
                    static_cast<int16_t>(safe.x)});
    fui::ListProps props;
    props.items = rows.data();
    props.count = static_cast<uint16_t>(rows.size());
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    syncListViewport(screen, props);
    screen.list(props);
  }

  void activateIndex(int index) override {
    app.clearTapFlash();
    if (index < 0 || index >= listCount()) return;
    if (index == 0) {
      auto recent = createRecentBooksActivity(renderer, mappedInput, selectedBookPath, SETTINGS.recentBooksView == 1);
      if (recent)
        startActivityForResult(std::move(recent), [this](const ActivityResult& result) {
          const auto* changed = std::get_if<RecentBooksResult>(&result.data);
          if (!changed || !changed->changed) return;
          reloadSelectedBook();
          setResult(RecentBooksResult{true});
        });
      return;
    }
    switch (index - 1) {
      case 0:
        activityManager.goToFileBrowser();
        break;
      case 1:
        activityManager.goToLibrary();
        break;
      case 2:
        activityManager.goToFileTransfer();
        break;
      case 3:
        activityManager.goToSettings();
        break;
      case 4: {
        const auto totals = GlobalReadingStats::load();
        const auto* selected = &selectedBook;
        const bool hasBook = !selected->path.empty() && FsHelpers::hasEpubExtension(selected->path);
        const std::string cache =
            hasBook ? "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(selected->path)) : "";
        const auto bookStats = hasBook ? BookReadingStats::load(cache) : BookReadingStats{};
        const uint32_t timeLeft = bookStats.isCompleted ? 0 : bookStats.estimatedTimeLeftSeconds;
        // Statistics and their bounded history allocate only on this explicit
        // action. Ordinary Home painting does not read the global statistics.
        auto stats = makeUniqueNoThrow<BookStatsActivity>(
            renderer, mappedInput, hasBook ? selected->title : std::string{}, cache, bookStats,
            hasBook ? loadBookProgress(selected->path, true) : -1, timeLeft > 0, timeLeft, totals);
        if (!stats) {
          LOG_ERR("HOME", "OOM: statistics activity");
          break;
        }
        if (hasBook) stats->setBookIdentity(selected->path, selected->author);
        startActivityForResult(std::move(stats), [this](const ActivityResult& result) {
          const auto* edited = std::get_if<ReadingStatsResult>(&result.data);
          if (edited && edited->changed) setResult(ReadingStatsResult{true});
        });
        break;
      }
      case 5:
        if (auto activity = makeUniqueNoThrow<PowerStatsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("HOME", "OOM: autonomy activity");
        }
        break;
      case 6:
        if (auto activity = createDictionaryHistoryActivity(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        }
        break;
      case 7:
        if (hasOpds) activityManager.goToBrowser();
        break;
    }
  }

 public:
  YacpHomeMenu(GfxRenderer& renderer, MappedInputManager& input, const std::string& bookPath)
      : UiListActivity("YacpHomeMenu", renderer, input), selectedBookPath(bookPath) {}

  void onEnter() override {
    reloadSelectedBook();
    hasOpds = OPDS_STORE.hasServers();
    rows.reserve(9);
    const StrId actions[] = {StrId::STR_MENU_RECENT_BOOKS, StrId::STR_BROWSE_FILES,   StrId::STR_LIBRARY,
                             StrId::STR_FILE_TRANSFER,     StrId::STR_SETTINGS_TITLE, StrId::STR_READING_STATS,
                             StrId::STR_POWER_STATS_TITLE, StrId::STR_DICT_HISTORY, StrId::STR_OPDS_BROWSER};
    for (size_t i = 0; i < (hasOpds ? 9u : 8u); ++i) {
      fui::ListItem item;
      item.label = I18N.get(actions[i]);
      item.actionValue = static_cast<int16_t>(rows.size());
      rows.push_back(item);
    }
    UiListActivity::onEnter();
  }
};
}  // namespace

void YacpHomeActivity::onEnter() {
  Activity::onEnter();
  books.reserve(RecentBooksStore::MAX_RECENT_BOOKS);
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (!RecentBooksStore::isMissing(book)) books.push_back(book);
  }
  readingInfo = loadReadingInfo(selected);
  requestUpdate();
}

Rect YacpHomeActivity::cardRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect rect = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int sidePadding = 2 * metrics.contentSidePadding;
  rect.x += sidePadding;
  rect.width -= 2 * sidePadding;
  const int bottom = mappedInput.hasTouch() ? statsRect().y : rect.y + rect.height;
  rect.y += metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  rect.height = bottom - metrics.verticalSpacing - rect.y;
  return rect;
}

Rect YacpHomeActivity::statsRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect menu = menuRect();
  const int gap = metrics.verticalSpacing;
  const int width = (menu.width - gap) / 2;
  return Rect{menu.x, menu.y - gap - menu.height, width, menu.height};
}

Rect YacpHomeActivity::autonomyRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect stats = statsRect();
  return Rect{stats.x + stats.width + metrics.verticalSpacing, stats.y,
              menuRect().width - stats.width - metrics.verticalSpacing, stats.height};
}

Rect YacpHomeActivity::menuRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int height = LyraMetrics::values.menuRowHeight;
  return Rect{safe.x + metrics.contentSidePadding, safe.y + safe.height - height,
              safe.width - 2 * metrics.contentSidePadding, height};
}

void YacpHomeActivity::openMenu() {
  auto menu =
      makeUniqueNoThrow<YacpHomeMenu>(renderer, mappedInput, books.empty() ? std::string{} : books[selected].path);
  if (!menu) {
    LOG_ERR("HOME", "OOM: YACP home menu");
    return;
  }
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    const auto* edited = std::get_if<ReadingStatsResult>(&result.data);
    const auto* recent = std::get_if<RecentBooksResult>(&result.data);
    if ((recent && recent->changed) || (edited && edited->changed)) {
      RenderLock lock;
      const auto path = books.empty() ? std::string{} : books[selected].path;
      books.clear();
      for (const auto& book : RECENT_BOOKS.getBooks()) {
        if (RecentBooksStore::isMissing(book)) continue;
        if (book.path == path) selected = books.size();
        books.push_back(book);
      }
      selected = books.empty() ? 0 : std::min(selected, books.size() - 1);
      readingInfo = loadReadingInfo(selected);
    }
  });
}

void YacpHomeActivity::resume() {
  if (books.empty())
    activityManager.goToFileBrowser();
  else
    activityManager.goToReader(books[selected].path);
}

void YacpHomeActivity::openStats() {
  const RecentBook* book = books.empty() ? nullptr : &books[selected];
  const bool hasBook = book && FsHelpers::hasEpubExtension(book->path);
  const std::string cache = hasBook ? "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(book->path)) : "";
  const auto stats = hasBook ? BookReadingStats::load(cache) : BookReadingStats{};
  const uint32_t timeLeft = stats.isCompleted ? 0 : stats.estimatedTimeLeftSeconds;
  auto activity = makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInput, hasBook ? book->title : std::string{},
                                                       cache, stats, hasBook ? loadBookProgress(book->path, true) : -1,
                                                       timeLeft > 0, timeLeft, GlobalReadingStats::load());
  if (!activity) {
    LOG_ERR("HOME", "OOM: statistics activity");
    return;
  }
  if (hasBook) activity->setBookIdentity(book->path, book->author);
  startActivityForResult(std::move(activity), [this](const ActivityResult& result) {
    const auto* edited = std::get_if<ReadingStatsResult>(&result.data);
    if (edited && edited->changed) {
      RenderLock lock(*this);
      readingInfo = loadReadingInfo(selected);
      requestUpdate();
    }
  });
}

void YacpHomeActivity::openAutonomy() {
  auto activity = makeUniqueNoThrow<PowerStatsActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("HOME", "OOM: autonomy activity");
    return;
  }
  startActivityForResult(std::move(activity), nullptr);
}

YacpHomeActivity::ReadingInfo YacpHomeActivity::loadReadingInfo(const size_t index) const {
  ReadingInfo info;
  if (index >= books.size()) return info;
  info.progressPercent = loadBookProgress(books[index].path, true);
  // Read only the selected EPUB cache on entry/selection, never while painting.
  if (FsHelpers::hasEpubExtension(books[index].path)) {
    const auto stats =
        BookReadingStats::load("/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(books[index].path)));
    info.totalReadingSeconds = stats.totalReadingSeconds;
    info.estimatedTimeLeftSeconds = stats.isCompleted ? 0 : stats.estimatedTimeLeftSeconds;
  }
  return info;
}

void YacpHomeActivity::selectBook(const size_t index) {
  const auto info = loadReadingInfo(index);
  {
    RenderLock lock(*this);
    selected = index;
    readingInfo = info;
  }
  requestUpdate();
}

void YacpHomeActivity::loop() {
  const Rect menu = menuRect();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      (mappedInput.hasTouch() && mappedInput.wasTapInRect(menu.x, menu.y, menu.width, menu.height))) {
    openMenu();
    return;
  }
  const Rect stats = statsRect();
  if (mappedInput.hasTouch() && mappedInput.wasTapInRect(stats.x, stats.y, stats.width, stats.height)) {
    openStats();
    return;
  }
  const Rect autonomy = autonomyRect();
  if (mappedInput.hasTouch() && mappedInput.wasTapInRect(autonomy.x, autonomy.y, autonomy.width, autonomy.height)) {
    openAutonomy();
    return;
  }
  const Rect card = cardRect();
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      (mappedInput.hasTouch() && mappedInput.wasTapInRect(card.x, card.y, card.width, card.height))) {
    resume();
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  const bool previous = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                        swipe == MappedInputManager::SwipeDir::Right;
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                    mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                    swipe == MappedInputManager::SwipeDir::Left;
  if (previous && selected > 0) {
    selectBook(selected - 1);
  } else if (next && selected + 1 < books.size()) {
    selectBook(selected + 1);
  }
}

void YacpHomeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  char date[48];
  const char* dateLabel = formatYacpHeaderDate(date, sizeof(date)) ? date : nullptr;
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight}, tr(STR_YACP),
                 dateLabel, false);
  const Rect card = cardRect();
  renderer.fillRoundedRect(card.x, card.y, card.width, card.height, 26, Color::LightGray);
  if (books.empty()) {
    UITheme::drawCenteredWrappedText(renderer, card, UI_12_FONT_ID, tr(STR_NO_OPEN_BOOK), 3);
  } else {
    const auto& book = books[selected];
    constexpr int inset = 20;
    constexpr int gap = 12;
    constexpr int barHeight = 8;
    const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int valueHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int progressHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int panelHeight = 2 * inset + progressHeight + gap + barHeight + gap + labelHeight + 4 + valueHeight;
    // Landscape keeps the identity and statistics beside each other. Touch
    // shortcuts retain their own rows outside this card on capable devices.
    const bool landscape = card.width > card.height && card.height < 420;
    const Rect panel{landscape ? card.x + card.width * 3 / 5 : card.x + inset,
                     landscape ? card.y + (card.height - panelHeight) / 2 : card.y + card.height - inset - panelHeight,
                     landscape ? card.width * 2 / 5 - inset : card.width - 2 * inset, panelHeight};
    const int identityX = card.x + inset;
    const int identityWidth = (landscape ? panel.x - gap : card.x + card.width - inset) - identityX;
    const int pillHeight = valueHeight + 16;
    const auto section =
        renderer.truncatedText(UI_10_FONT_ID, tr(STR_CONTINUE_READING), identityWidth - 24, EpdFontFamily::BOLD);
    const int pillWidth = renderer.getTextWidth(UI_10_FONT_ID, section.c_str(), EpdFontFamily::BOLD) + 24;
    renderer.fillRoundedRect(identityX, card.y + inset, pillWidth, pillHeight, 12, Color::Black);
    renderer.drawText(UI_10_FONT_ID, identityX + 12, card.y + inset + 8, section.c_str(), false, EpdFontFamily::BOLD);
    int identityBottom = card.y + inset + pillHeight + 20;
    const int authorHeight = book.author.empty() ? 0 : renderer.getLineHeight(UI_12_FONT_ID) + gap;
    const int identityLimit = landscape ? card.y + card.height - inset : panel.y - gap;
    const int titleHeight = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
    const int titleLines = std::clamp((identityLimit - identityBottom - authorHeight) / titleHeight, 1, 3);
    // Only a bounded title (at most three lines) and a single author line are
    // laid out here; no cover, EPUB or statistics file is opened by rendering.
    const auto title =
        renderer.wrappedText(NOTOSANS_18_FONT_ID, book.title.empty() ? book.path.c_str() : book.title.c_str(),
                             identityWidth, titleLines, EpdFontFamily::BOLD);
    for (const auto& line : title) {
      renderer.drawText(NOTOSANS_18_FONT_ID, identityX, identityBottom, line.c_str(), true, EpdFontFamily::BOLD);
      identityBottom += titleHeight;
    }
    if (!book.author.empty()) {
      const auto author = renderer.truncatedText(UI_12_FONT_ID, book.author.c_str(), identityWidth);
      renderer.drawText(UI_12_FONT_ID, identityX, identityBottom + gap, author.c_str());
      identityBottom += authorHeight;
    }
    constexpr int logoSize = 120;
    if (!landscape && panel.y - identityBottom >= logoSize + 2 * gap) {
      const int x = card.x + (card.width - logoSize) / 2;
      const int y = identityBottom + (panel.y - identityBottom - logoSize) / 2;
      for (int row = 0; row < logoSize; ++row) {
        for (int col = 0; col < logoSize; ++col) {
          if ((YacpLogo120[row * 15 + col / 8] & (0x80 >> (col & 7))) == 0)
            renderer.drawPixel(x + logoSize - 1 - row, y + col, false);
        }
      }
    }
    renderer.fillRoundedRect(panel.x, panel.y, panel.width, panel.height, 14, Color::White);
    const int contentX = panel.x + inset;
    const int contentWidth = panel.width - 2 * inset;
    char percent[8] = "-";
    const int progress = std::clamp(readingInfo.progressPercent, 0, 100);
    if (readingInfo.progressPercent >= 0) snprintf(percent, sizeof(percent), "%d%%", progress);
    const int percentWidth = renderer.getTextWidth(UI_12_FONT_ID, percent);
    const auto progressLabel = renderer.truncatedText(UI_10_FONT_ID, tr(STR_STATS_PROGRESS_LBL),
                                                      contentWidth - percentWidth - gap, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, contentX, panel.y + inset, progressLabel.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, contentX + contentWidth - percentWidth, panel.y + inset, percent);
    const int barY = panel.y + inset + progressHeight + gap;
    renderer.drawRoundedRect(contentX, barY, contentWidth, barHeight, 1, barHeight / 2, true);
    const int filledWidth = (contentWidth - 4) * progress / 100;
    if (filledWidth > 0) renderer.fillRect(contentX + 2, barY + 2, filledWidth, barHeight - 4, true);
    const int statY = barY + barHeight + gap;
    const int columnWidth = (contentWidth - gap) / 2;
    const auto readingLabel = renderer.truncatedText(SMALL_FONT_ID, tr(STR_STATS_TIME_LBL), columnWidth);
    const auto remainingLabel = renderer.truncatedText(SMALL_FONT_ID, tr(STR_TIME_LEFT), columnWidth);
    renderer.drawText(SMALL_FONT_ID, contentX, statY, readingLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, contentX + contentWidth - columnWidth, statY, remainingLabel.c_str());
    char readingTime[32] = "-";
    char remainingTime[32] = "-";
    if (readingInfo.totalReadingSeconds > 0)
      BookReadingStats::formatDuration(readingInfo.totalReadingSeconds, readingTime, sizeof(readingTime));
    if (readingInfo.estimatedTimeLeftSeconds > 0)
      BookReadingStats::formatDuration(readingInfo.estimatedTimeLeftSeconds, remainingTime, sizeof(remainingTime));
    const auto readingValue = renderer.truncatedText(UI_10_FONT_ID, readingTime, columnWidth, EpdFontFamily::BOLD);
    const auto remainingValue = renderer.truncatedText(UI_10_FONT_ID, remainingTime, columnWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, contentX, statY + labelHeight + 4, readingValue.c_str(), true,
                      EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, contentX + contentWidth - columnWidth, statY + labelHeight + 4,
                      remainingValue.c_str(), true, EpdFontFamily::BOLD);
  }
  if (mappedInput.hasTouch()) {
    const Rect stats = statsRect();
    const Rect autonomy = autonomyRect();
    renderer.drawRoundedRect(stats.x, stats.y, stats.width, stats.height, 1, 12, true);
    renderer.drawRoundedRect(autonomy.x, autonomy.y, autonomy.width, autonomy.height, 1, 12, true);
    UITheme::drawCenteredWrappedText(renderer, stats, UI_12_FONT_ID, tr(STR_READING_STATS), 1);
    UITheme::drawCenteredWrappedText(renderer, autonomy, UI_12_FONT_ID, tr(STR_POWER_STATS_TITLE), 1);
    const Rect menu = menuRect();
    renderer.drawRoundedRect(menu.x, menu.y, menu.width, menu.height, 1, 12, true);
    UITheme::drawCenteredWrappedText(renderer, menu, UI_12_FONT_ID, tr(STR_MENU), 1);
  }
  const auto labels =
      mappedInput.mapLabels(tr(STR_MENU), books.empty() ? tr(STR_BROWSE_FILES) : tr(STR_RESUME),
                            selected > 0 ? tr(STR_DIR_LEFT) : "", selected + 1 < books.size() ? tr(STR_DIR_RIGHT) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(firstRender && cleanInitialRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstRender = false;
}
