#include "RecentBooksActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalMemory.h>
#include <I18n.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "RecentBooksStore.h"
#include "RecentBookMenu.h"
#include "RecentGridNavigation.h"
#include "activities/UiListActivity.h"
#include "components/HomeCoverCache.h"
#include "components/UITheme.h"
#include "components/media/cover-grid.h"
#include "fontIds.h"
#include "util/BookProgress.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;
namespace {
struct RecentModel {
  std::vector<RecentBook> books;
  int selected = 0;

  void load(const std::string& selectedPath) {
    books.clear();
    books.reserve(RecentBooksStore::MAX_RECENT_BOOKS);
    for (const auto& book : RECENT_BOOKS.getBooks()) {
      if (RecentBooksStore::isMissing(book)) continue;
      if (book.path == selectedPath) selected = static_cast<int>(books.size());
      books.push_back(book);
    }
    selected = books.empty() ? 0 : std::clamp(selected, 0, count() - 1);
  }
  int count() const { return static_cast<int>(books.size()); }
};

Rect contentRect(const GfxRenderer& renderer, bool pageCounter) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  safe.x += metrics.contentSidePadding;
  safe.width -= 2 * metrics.contentSidePadding;
  safe.y += top;
  safe.height -= top + metrics.buttonHintsHeight + metrics.verticalSpacing +
                 (pageCounter ? renderer.getLineHeight(UI_10_FONT_ID) : 0);
  return safe;
}

void setBody(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, Rect rect) {
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(rect.y), static_cast<int16_t>(renderer.getScreenWidth() - rect.x - rect.width),
      static_cast<int16_t>(renderer.getScreenHeight() - rect.y - rect.height), static_cast<int16_t>(rect.x)});
}

class RecentList final : public UiListActivity {
  RecentModel model;
  const std::string initialPath;
  std::vector<fui::ListItem> rows;
  fui::ListProps props;
  void rebuildRows() {
    rows.reserve(model.books.size());
    for (const auto& book : model.books) {
      fui::ListItem item;
      item.label = book.title.empty() ? book.path.c_str() : book.title.c_str();
      item.subtitle = book.author.c_str();
      item.actionValue = static_cast<int16_t>(rows.size());
      rows.push_back(item);
    }
  }
  void onRowLongPress(int index) override {
    if (index < 0 || index >= model.count()) return;
    app.clearTapFlash();
    auto menu = createRecentBookMenu(renderer, mappedInput, model.books[index]);
    if (!menu) return;
    model.selected = index;
    startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
      const auto* changed = std::get_if<RecentBooksResult>(&result.data);
      if (!changed || !changed->changed) return;
      RenderLock lock;
      closeRouting();
      const auto path = model.books[model.selected].path;
      rows.clear();
      model.load(path);
      rebuildRows();
      moveSelectionTo(model.selected);
      setResult(RecentBooksResult{true});
    });
  }
  bool handleButtons() override {
    if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 1000)) {
      onRowLongPress(nav.selected);
      return true;
    }
    return UiListActivity::handleButtons();
  }
  int listCount() const override { return model.count(); }
  const char* headerTitle() const override { return tr(STR_MENU_RECENT_BOOKS); }
  void buildScreen(UiScreen& screen) override {
    const auto rect = contentRect(renderer, false);
    setBody(screen, renderer, rect);
    if (rows.empty()) {
      UITheme::drawCenteredWrappedText(renderer, rect, UI_12_FONT_ID, tr(STR_NO_ENTRIES), 2);
      return;
    }
    props.items = rows.data();
    props.count = static_cast<uint16_t>(rows.size());
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch | fui::InputLongPress;
    syncListViewport(screen, props);
    screen.list(props);
  }
  void activateIndex(int index) override {
    if (index < 0 || index >= model.count()) return;
    app.clearTapFlash();
    activityManager.goToReader(model.books[index].path);
  }
 public:
  RecentList(GfxRenderer& renderer, MappedInputManager& input, const std::string& path)
      : UiListActivity("RecentBooksList", renderer, input, true), initialPath(path) {}
  void onEnter() override {
    model.load(initialPath);
    rebuildRows();
    UiListActivity::onEnter();
    moveSelectionTo(model.selected);
  }
};

class RecentGrid final : public Activity, private UiAppHost {
  static constexpr fui::ActionId OPEN_BOOK = 1;
  RecentModel model;
  const std::string initialPath;
  HomeCoverCache coverCache;
  std::array<std::string, recent_grid::PAGE_SIZE> coverPaths;
  fui::CoverGridProps grid;
  uint8_t repeatedNav = 0;
  unsigned long lastNavTime = 0;
  int pageStart = 0;
  int coverHeight = 0;
  int pendingOpen = -1;
  bool pendingMenu = false;
  int progress = -1;

  static void screenFn(UiScreen& screen, void* user) { static_cast<RecentGrid*>(user)->draw(screen); }
  static void onAction(const fui::ActionEvent& event, void* user) {
    auto& self = *static_cast<RecentGrid*>(user);
    self.pendingOpen = event.value;
    self.pendingMenu = event.longPress;
  }
  void configureGrid() {
    const auto rect = contentRect(renderer, true);
    grid.columns = recent_grid::COLUMNS;
    grid.gap = 8;
    grid.rowGap = 8;
    grid.cellInset = fui::Insets{6, 6, 6, 6};
    grid.labelHeight = renderer.getLineHeight(UI_10_FONT_ID) * 2;
    grid.labelGap = 4;
    grid.rowHeight = std::max(1, (rect.height - 2 * grid.rowGap) / 3);
    const int maxWidth = std::max(1, (rect.width - 2 * grid.gap) / 3 - 12);
    coverHeight = std::max(1, std::min<int>(grid.rowHeight - grid.labelHeight - grid.labelGap - 12,
                                         maxWidth * 3 / 2));
    grid.coverSize = fui::Size{static_cast<int16_t>(coverHeight * 2 / 3), static_cast<int16_t>(coverHeight)};
    grid.action = OPEN_BOOK;
    grid.inputMask = fui::InputTouch | fui::InputLongPress;
    grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
    grid.selectedCoverFrameWidth = 3;
    grid.selectedCoverFrameGap = 2;
    grid.scrollIndicator = false;
    grid.itemProviderUserData = this;
    grid.itemProvider = [](uint16_t index, void* context) {
      const auto& self = *static_cast<RecentGrid*>(context);
      const auto& book = self.model.books[index];
      return fui::coverGridItem(book.title.empty() ? book.path.c_str() : book.title.c_str(), index);
    };
    grid.coverPainterUserData = this;
    grid.coverPainter = [](fui::DrawTarget&, fui::Rect rect, const fui::CoverGridItem&, uint16_t index, void* context) {
      auto& self = *static_cast<RecentGrid*>(context);
      const int slot = index - self.pageStart;
      return slot >= 0 && slot < recent_grid::PAGE_SIZE && self.coverCache.paint(rect, slot, self.coverPaths[slot]);
    };
  }
  std::string loadCover(const RecentBook& book, bool& loadingShown) {
    if (!book.coverBmpPath.empty()) {
      auto path = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (Storage.exists(path.c_str())) return path;
    }
    // At most one parser exists, only on explicit grid entry/page changes.
    // Its large state belongs on the heap, outside the render-task stack.
    if (FsHelpers::hasEpubExtension(book.path)) {
      auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
      if (!epub) { LOG_ERR("RECENT", "OOM: cover EPUB"); return {}; }
      auto path = epub->getThumbBmpPath(coverHeight);
      if (Storage.exists(path.c_str())) return path;
      if (!loadingShown) { GUI.drawPopup(renderer, tr(STR_LOADING_POPUP)); loadingShown = true; }
      if (epub->generateThumbBmpFromSource(coverHeight)) return path;
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
      if (!xtc) { LOG_ERR("RECENT", "OOM: cover XTC"); return {}; }
      auto path = xtc->getThumbBmpPath(coverHeight);
      if (Storage.exists(path.c_str())) return path;
      if (!loadingShown) { GUI.drawPopup(renderer, tr(STR_LOADING_POPUP)); loadingShown = true; }
      if (xtc->load() && xtc->generateThumbBmp(coverHeight)) return path;
    }
    return {};
  }
  void loadPageCovers() {
    coverCache.invalidate();
    bool loadingShown = false;
    for (int slot = 0; slot < recent_grid::PAGE_SIZE; ++slot) {
      const int index = pageStart + slot;
      coverPaths[slot] = index < model.count() ? loadCover(model.books[index], loadingShown) : std::string{};
      resetTaskWatchdogIfSubscribed();
    }
    LOG_DBG("RECENT", "Grid page start=%d count=%d height=%d", pageStart, model.count(), coverHeight);
  }
  void select(int index) {
    if (model.count() == 0) return;
    RenderLock lock;
    model.selected = std::clamp(index, 0, model.count() - 1);
    progress = loadBookProgress(model.books[model.selected].path, true);
    const int nextStart = model.selected / recent_grid::PAGE_SIZE * recent_grid::PAGE_SIZE;
    if (nextStart != pageStart) {
      closeRouting();
      pageStart = nextStart;
      loadPageCovers();
    }
    requestUpdate();
  }
  void open(int index) {
    if (index < 0 || index >= model.count()) return;
    app.clearTapFlash();
    activityManager.goToReader(model.books[index].path);
  }
  void showMenu(int index) {
    if (index < 0 || index >= model.count()) return;
    select(index);
    app.clearTapFlash();
    auto menu = createRecentBookMenu(renderer, mappedInput, model.books[index]);
    if (!menu) return;
    startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
      const auto* changed = std::get_if<RecentBooksResult>(&result.data);
      if (!changed || !changed->changed) return;
      RenderLock lock;
      closeRouting();
      const auto path = model.books[model.selected].path;
      model.load(path);
      pageStart = model.selected / recent_grid::PAGE_SIZE * recent_grid::PAGE_SIZE;
      progress = model.count() ? loadBookProgress(model.books[model.selected].path, true) : -1;
      loadPageCovers();
      setResult(RecentBooksResult{true});
    });
  }
  void draw(UiScreen& screen) {
    coverCache.prepare();
    const auto rect = contentRect(renderer, true);
    setBody(screen, renderer, rect);
    if (model.count() == 0) {
      UITheme::drawCenteredWrappedText(renderer, rect, UI_12_FONT_ID, tr(STR_NO_ENTRIES), 2);
      return;
    }
    grid.count = static_cast<uint16_t>(model.count());
    grid.topIndex = static_cast<uint16_t>(pageStart);
    grid.selectedIndex = static_cast<int16_t>(model.selected);
    grid.titleText = screen.theme().smallText;
    grid.titleText.maxLines = 2;
    fui::coverGrid(screen.frame(), screen.body(), grid);
  }
 public:
  RecentGrid(GfxRenderer& renderer, MappedInputManager& input, const std::string& path)
      : Activity("RecentBooksGrid", renderer, input), UiAppHost(renderer), initialPath(path), coverCache(renderer) {}
  void onEnter() override {
    Activity::onEnter();
    model.load(initialPath);
    pageStart = model.selected / recent_grid::PAGE_SIZE * recent_grid::PAGE_SIZE;
    if (!model.books.empty()) progress = loadBookProgress(model.books[model.selected].path, true);
    if (!model.books.empty() && HalMemory::getPsramHeap().totalBytes > 0) coverCache.begin();
    resetUi();
    app.on(OPEN_BOOK, &RecentGrid::onAction, this);
    app.setScreen(&RecentGrid::screenFn, this);
    configureGrid();
    {
      RenderLock lock;
      loadPageCovers();
    }
    requestUpdate();
  }
  void loop() override {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { finish(); return; }
    if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 1000)) { showMenu(model.selected); return; }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) { open(model.selected); return; }
    pendingOpen = -1;
    pendingMenu = false;
    const auto touch = routeTouch(mappedInput, true);
    if (pendingOpen >= 0) {
      if (pendingMenu) showMenu(pendingOpen);
      else open(pendingOpen);
      return;
    }
    if (touch.routed && app.invalidated()) requestUpdate();
    const auto swipe = mappedInput.wasSwipe();
    if (model.count() && swipe != MappedInputManager::SwipeDir::None) {
      const bool forward = swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up;
      const int pages = (model.count() + recent_grid::PAGE_SIZE - 1) / recent_grid::PAGE_SIZE;
      select(((pageStart / recent_grid::PAGE_SIZE + pages + (forward ? 1 : -1)) % pages) * recent_grid::PAGE_SIZE);
      return;
    }
    static constexpr MappedInputManager::Button buttons[] = {MappedInputManager::Button::Right,
        MappedInputManager::Button::Left, MappedInputManager::Button::Down, MappedInputManager::Button::Up};
    for (unsigned i = 0; i < 4; ++i) {
      const uint8_t bit = static_cast<uint8_t>(1u << i);
      bool move = false;
      if (mappedInput.wasReleased(buttons[i])) {
        move = !(repeatedNav & bit);
        repeatedNav &= ~bit;
      } else if (mappedInput.isPressed(buttons[i]) && mappedInput.getHeldTime() >= 500 &&
                 (!(repeatedNav & bit) || millis() - lastNavTime >= 500)) {
        repeatedNav |= bit;
        lastNavTime = millis();
        move = true;
      } else if (!mappedInput.isPressed(buttons[i])) {
        repeatedNav &= ~bit;
      }
      if (move) {
        select(i < 2 ? recent_grid::horizontal(model.selected, model.count(), i == 0)
                     : recent_grid::vertical(model.selected, model.count(), i == 2));
        return;
      }
    }
  }
  void render(RenderLock&&) override {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   tr(STR_MENU_RECENT_BOOKS));
    renderUi();
    if (model.count()) {
      char counter[32];
      if (progress >= 0) {
        snprintf(counter, sizeof(counter), "%d / %d   |   %d%%", pageStart / recent_grid::PAGE_SIZE + 1,
                 (model.count() + recent_grid::PAGE_SIZE - 1) / recent_grid::PAGE_SIZE, progress);
      } else {
        snprintf(counter, sizeof(counter), "%d / %d", pageStart / recent_grid::PAGE_SIZE + 1,
                 (model.count() + recent_grid::PAGE_SIZE - 1) / recent_grid::PAGE_SIZE);
      }
      const auto rect = contentRect(renderer, true);
      UITheme::drawCenteredText(renderer, rect, UI_10_FONT_ID, rect.y + rect.height + 2, counter);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
  }
};
}  // namespace

std::unique_ptr<Activity> createRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                   const std::string& selectedPath, bool grid) {
  std::unique_ptr<Activity> result;
  if (grid) result = makeUniqueNoThrow<RecentGrid>(renderer, input, selectedPath);
  else result = makeUniqueNoThrow<RecentList>(renderer, input, selectedPath);
  if (!result) LOG_ERR("RECENT", "OOM: recent-books activity");
  return result;
}
