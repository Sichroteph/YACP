#include "XtcReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kTitleFontId = UI_10_FONT_ID;
constexpr int kTitleMaxLines = 2;
constexpr int kBatteryTextReserveWidth = 90;
constexpr int kTitleLineGap = 1;
}  // namespace

XtcReaderMenuActivity::XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                             const bool hasChapters, const bool isBookCompleted)
    : Activity("XtcReaderMenu", renderer, mappedInput),
      title(std::move(title)),
      items(buildMenuItems(hasChapters, isBookCompleted)) {}

std::vector<XtcReaderMenuActivity::MenuItem> XtcReaderMenuActivity::buildMenuItems(const bool hasChapters,
                                                                                   const bool isBookCompleted) {
  std::vector<MenuItem> menuItems;
  menuItems.reserve(5);
  if (hasChapters) {
    menuItems.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  }
  menuItems.push_back({MenuAction::READING_STATS, StrId::STR_READING_STATS});
  menuItems.push_back(
      {MenuAction::TOGGLE_COMPLETED, isBookCompleted ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
  menuItems.push_back({MenuAction::DELETE_STATS, StrId::STR_DELETE_BOOK_STATS});
  menuItems.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  return menuItems;
}

void XtcReaderMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void XtcReaderMenuActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(MenuResult{static_cast<int>(items[selectedIndex].action)});
    finish();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(items.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(items.size()));
    requestUpdate();
  });
}

void XtcReaderMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int titleX = metrics.contentSidePadding;
  const int titleMaxWidth = std::max(0, pageWidth - titleX - metrics.contentSidePadding - kBatteryTextReserveWidth);
  const auto titleLines =
      renderer.wrappedText(kTitleFontId, title.c_str(), titleMaxWidth, kTitleMaxLines, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int titleBlockHeight = static_cast<int>(titleLines.size()) * titleLineHeight +
                               std::max(0, static_cast<int>(titleLines.size()) - 1) * kTitleLineGap;
  const int headerHeight = std::max(metrics.headerHeight, metrics.batteryBarHeight + titleBlockHeight + 16);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, headerHeight}, "");

  const int titleY = metrics.topPadding + metrics.batteryBarHeight + 3;
  for (int i = 0; i < static_cast<int>(titleLines.size()); ++i) {
    renderer.drawText(kTitleFontId, titleX, titleY + i * (titleLineHeight + kTitleLineGap), titleLines[i].c_str(), true,
                      EpdFontFamily::BOLD);
  }

  const int contentTop = metrics.topPadding + headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(items.size()), selectedIndex,
               [this](int index) { return std::string(I18N.get(items[index].labelId)); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
