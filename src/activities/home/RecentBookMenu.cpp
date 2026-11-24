#include "RecentBookMenu.h"

#include <FsHelpers.h>
#include <I18n.h>
#include <Memory.h>
#include <array>
#include <functional>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/UiListActivity.h"
#include "activities/reader/BookReaderSettings.h"
#include "activities/reader/BookStatsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "util/BookProgress.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;
namespace {
class RecentBookMenu final : public UiListActivity {
  enum Action : uint8_t { Open, Statistics, ResetProfile, ClearCache, RemoveRecent };
  RecentBook book;
  std::string cache;
  std::array<fui::ListItem, 5> rows{};
  std::array<Action, 5> actions{};
  fui::ListProps props;
  int count = 0;
  bool failed = false;

  void append(Action action, StrId label) {
    actions[count] = action;
    rows[count].label = I18N.get(label);
    rows[count].actionValue = count;
    ++count;
  }
  int listCount() const override { return count; }
  const char* headerTitle() const override { return failed ? tr(STR_ERROR_GENERAL_FAILURE) : book.title.c_str(); }
  void buildScreen(UiScreen& screen) override {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    screen.setContentMarginFromScreen(fui::Insets{
        static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
        static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
        static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height + metrics.buttonHintsHeight),
        static_cast<int16_t>(safe.x)});
    props.items = rows.data();
    props.count = count;
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    syncListViewport(screen, props);
    screen.list(props);
  }
  void reportFailure() {
    { RenderLock lock; failed = true; }
    requestUpdate();
  }
  void activateIndex(int index) override {
    if (index < 0 || index >= count) return;
    app.clearTapFlash();
    const auto action = actions[index];
    if (action == Open) {
      activityManager.goToReader(book.path);
      return;
    }
    if (action == Statistics) {
      const auto stats = BookReadingStats::load(cache);
      const auto totals = GlobalReadingStats::load();
      const uint32_t left = stats.isCompleted ? 0 : stats.estimatedTimeLeftSeconds;
      auto activity = makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInput, book.title, cache, stats,
                            loadBookProgress(book.path, true), left > 0, left, totals);
      if (!activity) { LOG_ERR("RECENT", "OOM: book statistics"); reportFailure(); return; }
      activity->setBookIdentity(book.path, book.author);
      startActivityForResult(std::move(activity), [this](const ActivityResult& result) {
        const auto* edited = std::get_if<ReadingStatsResult>(&result.data);
        if (edited && edited->changed) setResult(RecentBooksResult{true});
      });
      return;
    }
    auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput,
        std::string(tr(STR_CONFIRM)) + ": " + rows[index].label,
        book.title.empty() ? book.path : book.title);
    if (!confirmation) { LOG_ERR("RECENT", "OOM: book action confirmation"); reportFailure(); return; }
    startActivityForResult(std::move(confirmation), [this, action](const ActivityResult& result) {
      if (result.isCancelled) return;
      const bool success = action == ResetProfile
          ? BookReaderSettings::resetForBook(SETTINGS, cache.c_str()) :
          action == ClearCache ? clearBookCachePreservingUserState(book.path) : RECENT_BOOKS.removeByPath(book.path);
      if (!success) { reportFailure(); return; }
      setResult(RecentBooksResult{true});
      finish();
    });
  }
 public:
  RecentBookMenu(GfxRenderer& renderer, MappedInputManager& input, const RecentBook& selected)
      : UiListActivity("RecentBookMenu", renderer, input), book(selected) {}
  void onEnter() override {
    append(Open, StrId::STR_OPEN);
    const bool epub = FsHelpers::hasEpubExtension(book.path);
    const bool xtc = FsHelpers::hasXtcExtension(book.path);
    if (epub || xtc) {
      cache = std::string("/.crosspoint/") + (epub ? "epub_" : "xtc_") +
              std::to_string(std::hash<std::string>{}(book.path));
      append(Statistics, StrId::STR_READING_STATS);
    }
    if (epub) append(ResetProfile, StrId::STR_RESET_BOOK_READER_SETTINGS);
    if (epub || xtc) append(ClearCache, StrId::STR_DELETE_CACHE);
    append(RemoveRecent, StrId::STR_REMOVE_FROM_RECENTS);
    UiListActivity::onEnter();
  }
};
}  // namespace

std::unique_ptr<Activity> createRecentBookMenu(GfxRenderer& renderer, MappedInputManager& input,
                                               const RecentBook& book) {
  // Five fixed rows belong to the requested activity, not to either reader's
  // task stack. No metadata parser or whole-library snapshot is allocated.
  auto result = makeUniqueNoThrow<RecentBookMenu>(renderer, input, book);
  if (!result) LOG_ERR("RECENT", "OOM: book menu");
  return result;
}
