#include "ButtonLayoutSetupActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "AppVersion.h"
#include "CrossPointSettings.h"
#include "HalDisplay.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ButtonLayoutSetupActivity::onEnter() {
  Activity::onEnter();
  // Every upgrade starts from the same safe choice regardless of whether the
  // previous firmware used YACP, CrossInk, or a custom mix of settings.
  selectedLayout = SETTINGS.buttonLayoutPromptHadPriorChoice ? Layout::KeepCurrent : Layout::Yacp;
  requestUpdate();
}

void ButtonLayoutSetupActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    const uint8_t selected = static_cast<uint8_t>(selectedLayout);
    selectedLayout = static_cast<Layout>((selected + LAYOUT_COUNT - 1) % LAYOUT_COUNT);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selectedLayout = static_cast<Layout>((static_cast<uint8_t>(selectedLayout) + 1) % LAYOUT_COUNT);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    if (mappedInput.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration()) {
      applySelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    applySelection();
  }
}

void ButtonLayoutSetupActivity::applySelection() {
  if (selectedLayout == Layout::Yacp) {
    // Physical order: Left, Right, Confirm, Back.
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_RIGHT;
    SETTINGS.shortPwrBtn = CrossPointSettings::SLEEP;
    SETTINGS.longPwrBtn = CrossPointSettings::FORCE_REFRESH;
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
    SETTINGS.refreshFrequency = CrossPointSettings::REFRESH_1;
    SETTINGS.refreshAction = CrossPointSettings::REFRESH_ACTION_BW_REINFORCEMENT;
    SETTINGS.textAntiAliasing = 0;
    SETTINGS.sleepTimeoutMinutes = 5;
  } else if (selectedLayout == Layout::CrossInk) {
    // CrossInk-compatible physical order: Back, Confirm, Left, Right.
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    SETTINGS.shortPwrBtn = CrossPointSettings::IGNORE;
    SETTINGS.longPwrBtn = CrossPointSettings::SLEEP;
  }
  // Layout::KeepCurrent deliberately leaves every existing setting intact.

  static_assert(sizeof(CROSSINK_VERSION) <= CrossPointSettings::BUTTON_LAYOUT_PROMPT_VERSION_CAPACITY,
                "CROSSINK_VERSION does not fit in buttonLayoutPromptVersion");
  memcpy(SETTINGS.buttonLayoutPromptVersion, CROSSINK_VERSION, sizeof(CROSSINK_VERSION));

  if (!SETTINGS.saveToFile()) {
    LOG_ERR("BLS", "Failed to persist initial button layout");
  }
  activityManager.goHome();
}

void ButtonLayoutSetupActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_BUTTON_LAYOUT_SETUP_TITLE));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int helpLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int helpWidth = std::max(1, pageWidth - metrics.contentSidePadding * 2);
  const auto setupHelp = renderer.wrappedText(UI_10_FONT_ID, tr(STR_BUTTON_LAYOUT_SETUP_HELP), helpWidth, 2);
  int helpY = contentTop;
  for (const auto& line : setupHelp) {
    renderer.drawCenteredText(UI_10_FONT_ID, helpY, line.c_str());
    helpY += helpLineHeight;
  }

  const int listTop = helpY + metrics.verticalSpacing;
  const int detailMinHeight = helpLineHeight * 3 + metrics.verticalSpacing * 2;
  const int maxListHeight =
      std::max(metrics.listWithSubtitleRowHeight,
               pageHeight - listTop - metrics.buttonHintsHeight - detailMinHeight - metrics.verticalSpacing * 2);
  const int listHeight = std::min(static_cast<int>(LAYOUT_COUNT) * metrics.listWithSubtitleRowHeight, maxListHeight);
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, LAYOUT_COUNT, static_cast<int>(selectedLayout),
      [](int index) -> std::string {
        if (index == 0) return tr(STR_BUTTON_LAYOUT_YACP);
        if (index == 1) return tr(STR_BUTTON_LAYOUT_CROSSINK);
        return tr(STR_BUTTON_LAYOUT_CUSTOM);
      },
      [](int index) -> std::string {
        if (index == 0) return tr(STR_RECOMMENDED);
        if (index == 2) return tr(STR_CURRENT_LAYOUT);
        return "";
      });

  std::vector<std::string> detailRows;
  detailRows.reserve(3);
  if (selectedLayout == Layout::Yacp) {
    detailRows.emplace_back(tr(STR_BUTTON_LAYOUT_YACP_POWER_HELP));
    detailRows.emplace_back(std::string(tr(STR_SHORT_PWR_BTN)) + ": " + tr(STR_SLEEP) + " · " +
                            tr(STR_LONG_PRESS_ACTION) + ": " + tr(STR_FORCE_REFRESH));
    detailRows.emplace_back(std::string(tr(STR_TEXT_AA)) + ": " + tr(STR_NO));
  } else if (selectedLayout == Layout::CrossInk) {
    detailRows.emplace_back(std::string(tr(STR_SHORT_PWR_BTN)) + ": " + tr(STR_IGNORE) + " · " +
                            tr(STR_LONG_PRESS_ACTION) + ": " + tr(STR_SLEEP));
    detailRows.emplace_back(std::string(tr(STR_QUICK_RESUME_TIMEOUT)) + " · " + tr(STR_REFRESH_ACTION) + ": " +
                            tr(STR_CURRENT_LAYOUT));
    detailRows.emplace_back(std::string(tr(STR_TEXT_AA)) + " · " + tr(STR_TIME_TO_SLEEP) + ": " +
                            tr(STR_CURRENT_LAYOUT));
  } else {
    detailRows.emplace_back(tr(STR_BUTTON_LAYOUT_CUSTOM));
  }

  const int detailX = metrics.contentSidePadding;
  const int detailY = listTop + listHeight + metrics.verticalSpacing;
  const int detailBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int detailWidth = std::max(1, pageWidth - detailX * 2);
  const int detailHeight = std::max(1, detailBottom - detailY);
  renderer.drawRoundedRect(detailX, detailY, detailWidth, detailHeight, 1, 8, true);

  std::vector<std::string> wrappedDetails;
  wrappedDetails.reserve(6);
  const int detailTextWidth = std::max(1, detailWidth - metrics.contentSidePadding * 2);
  const int maxDetailLines = std::max(1, (detailHeight - metrics.verticalSpacing * 2) / helpLineHeight);
  for (const auto& detail : detailRows) {
    if (static_cast<int>(wrappedDetails.size()) >= maxDetailLines) break;
    const int remainingLines = maxDetailLines - static_cast<int>(wrappedDetails.size());
    auto lines = renderer.wrappedText(UI_10_FONT_ID, detail.c_str(), detailTextWidth, std::min(2, remainingLines));
    wrappedDetails.insert(wrappedDetails.end(), lines.begin(), lines.end());
  }
  int detailTextY = detailY + std::max(
                                      metrics.verticalSpacing,
                                      (detailHeight - static_cast<int>(wrappedDetails.size()) * helpLineHeight) / 2);
  for (const auto& line : wrappedDetails) {
    renderer.drawCenteredText(UI_10_FONT_ID, detailTextY, line.c_str());
    detailTextY += helpLineHeight;
  }

  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
