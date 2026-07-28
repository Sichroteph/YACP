#include "OptionSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

OptionSelectionActivity::OptionSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::string activityName, StrId titleId,
                                                 std::vector<std::string> options, uint8_t selectedIndex,
                                                 bool readerMode)
    : Activity(std::move(activityName), renderer, mappedInput),
      titleId_(titleId),
      options_(std::move(options)),
      currentIndex_(selectedIndex),
      selectedIndex_(selectedIndex),
      readerMode_(readerMode) {}

void OptionSelectionActivity::onEnter() {
  Activity::onEnter();

  if (options_.empty()) {
    cancel();
    return;
  }

  if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(options_.size())) {
    currentIndex_ = 0;
  }
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(options_.size())) {
    selectedIndex_ = 0;
  }
  requestUpdate();
}

void OptionSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    mappedInput.suppressNextBackRelease();
    cancel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    select();
    return;
  }

  const int listSize = static_cast<int>(options_.size());
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator_.onNextRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onNextContinuous([this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator_.onPreviousContinuous([this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });
}

void OptionSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void OptionSelectionActivity::select() {
  setResult(OptionSelectionResult{static_cast<uint8_t>(selectedIndex_)});
  finish();
}

void OptionSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = readerMode_ && orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = readerMode_ && orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.buttonHintsHeight : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;

  GUI.drawHeader(renderer, Rect{contentX, metrics.topPadding, contentWidth, metrics.headerHeight}, I18N.get(titleId_),
                 nullptr, readerMode_);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{contentX, contentTop, contentWidth, contentHeight}, static_cast<int>(options_.size()),
      selectedIndex_, [this](int index) { return options_[index]; }, nullptr, nullptr,
      [this](int index) -> std::string { return index == currentIndex_ ? tr(STR_SELECTED) : ""; }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, readerMode_);

  renderer.displayBuffer();
}
