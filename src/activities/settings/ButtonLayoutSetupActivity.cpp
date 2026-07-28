#include "ButtonLayoutSetupActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <string>

#include "CrossPointSettings.h"
#include "HalDisplay.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ButtonLayoutSetupActivity::onEnter() {
  Activity::onEnter();
  customLayoutAvailable = !isYacpLayout() && !isCrossInkLayout();
  if (customLayoutAvailable) {
    selectedLayout = Layout::Custom;
  }
  requestUpdate();
}

void ButtonLayoutSetupActivity::loop() {
  const uint8_t layoutCount = customLayoutAvailable ? 3 : 2;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    const uint8_t selected = static_cast<uint8_t>(selectedLayout);
    selectedLayout = static_cast<Layout>((selected + layoutCount - 1) % layoutCount);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selectedLayout = static_cast<Layout>((static_cast<uint8_t>(selectedLayout) + 1) % layoutCount);
    requestUpdate();
    return;
  }

  // Outside reader mode, Power is already mirrored as Confirm. This keeps the
  // confirmation control unambiguous while the front-button mapping is unknown.
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
  } else if (selectedLayout == Layout::CrossInk) {
    // CrossInk-compatible physical order: Back, Confirm, Left, Right.
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
  }
  // Layout::Custom deliberately leaves the four existing mapping fields intact.

  SETTINGS.buttonLayoutPromptSeen = 1;

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
  const int helpHeight = renderer.getLineHeight(UI_10_FONT_ID) * 2;
  renderer.drawCenteredText(UI_10_FONT_ID, contentTop, tr(STR_BUTTON_LAYOUT_SETUP_HELP));

  const int listTop = contentTop + helpHeight;
  const int listHeight = pageHeight - listTop - metrics.verticalSpacing;
  const int layoutCount = customLayoutAvailable ? 3 : 2;
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, layoutCount, static_cast<int>(selectedLayout),
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

  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

bool ButtonLayoutSetupActivity::isYacpLayout() {
  return SETTINGS.frontButtonLeft == CrossPointSettings::FRONT_HW_BACK &&
         SETTINGS.frontButtonRight == CrossPointSettings::FRONT_HW_CONFIRM &&
         SETTINGS.frontButtonConfirm == CrossPointSettings::FRONT_HW_LEFT &&
         SETTINGS.frontButtonBack == CrossPointSettings::FRONT_HW_RIGHT;
}

bool ButtonLayoutSetupActivity::isCrossInkLayout() {
  return SETTINGS.frontButtonBack == CrossPointSettings::FRONT_HW_BACK &&
         SETTINGS.frontButtonConfirm == CrossPointSettings::FRONT_HW_CONFIRM &&
         SETTINGS.frontButtonLeft == CrossPointSettings::FRONT_HW_LEFT &&
         SETTINGS.frontButtonRight == CrossPointSettings::FRONT_HW_RIGHT;
}
