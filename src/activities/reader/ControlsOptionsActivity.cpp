#include "ControlsOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SettingsList.h"
#include "activities/settings/ButtonRemapActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
uint8_t enumDisplayIndexForRawValue(const SettingInfo& setting, uint8_t rawValue) {
  if (setting.enumRawValues.empty()) {
    return rawValue;
  }

  auto it = std::find(setting.enumRawValues.begin(), setting.enumRawValues.end(), rawValue);
  if (it == setting.enumRawValues.end()) {
    return 0;
  }
  return static_cast<uint8_t>(std::distance(setting.enumRawValues.begin(), it));
}

uint8_t enumRawValueForDisplayIndex(const SettingInfo& setting, uint8_t displayIndex) {
  if (setting.enumRawValues.empty()) {
    return displayIndex;
  }
  if (displayIndex >= setting.enumRawValues.size()) {
    return setting.enumRawValues.front();
  }
  return setting.enumRawValues[displayIndex];
}
}  // namespace

void ControlsOptionsActivity::onEnter() {
  Activity::onEnter();

  activeSubmenu = SettingAction::None;
  rebuildSettingsList();
  requestUpdate();
}

void ControlsOptionsActivity::onExit() { Activity::onExit(); }

void ControlsOptionsActivity::rebuildSettingsList() {
  settings.clear();
  powerSettings.clear();
  frontButtonSettings.clear();
  sideButtonSettings.clear();

  const auto allSettings = getSettingsList();
  settings = buildControlsSettingsParentList(allSettings);
  powerSettings = buildControlsPowerSettingsList(allSettings);
  frontButtonSettings = buildControlsFrontButtonSettingsList(allSettings);
  sideButtonSettings = buildControlsSideButtonSettingsList(allSettings);

  setCurrentSettings();
  selectedIndex = 0;
}

void ControlsOptionsActivity::setCurrentSettings() {
  switch (activeSubmenu) {
    case SettingAction::ControlsPowerButton:
      currentSettings = &powerSettings;
      break;
    case SettingAction::ControlsFrontButtons:
      currentSettings = &frontButtonSettings;
      break;
    case SettingAction::ControlsSideButtons:
      currentSettings = &sideButtonSettings;
      break;
    default:
      currentSettings = &settings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
}

StrId ControlsOptionsActivity::activeSubmenuTitleId() const {
  switch (activeSubmenu) {
    case SettingAction::ControlsPowerButton:
      return StrId::STR_POWER_BUTTON;
    case SettingAction::ControlsFrontButtons:
      return StrId::STR_FRONT_BUTTONS;
    case SettingAction::ControlsSideButtons:
      return StrId::STR_SIDE_BUTTONS;
    default:
      return StrId::STR_NONE_OPT;
  }
}

void ControlsOptionsActivity::openSubmenu(SettingAction action) {
  activeSubmenu = action;
  setCurrentSettings();
  selectedIndex = 0;
}

void ControlsOptionsActivity::closeSubmenu() {
  activeSubmenu = SettingAction::None;
  setCurrentSettings();
  selectedIndex = 0;
}

void ControlsOptionsActivity::moveSelection(bool forward) {
  if (settingsCount <= 0) return;

  for (int i = 0; i < settingsCount; i++) {
    selectedIndex = forward ? ButtonNavigator::nextIndex(selectedIndex, settingsCount)
                            : ButtonNavigator::previousIndex(selectedIndex, settingsCount);
    if ((*currentSettings)[selectedIndex].type != SettingType::SECTION_HEADER) {
      break;
    }
  }
}

bool ControlsOptionsActivity::currentSettingUsesOptionMenu(const SettingInfo& setting) const {
  return setting.type == SettingType::ENUM && setting.valuePtr != nullptr && settingEnumOptionCount(setting) > 2;
}

void ControlsOptionsActivity::openEnumOptionPicker(const SettingInfo& setting) {
  const size_t optionCount = settingEnumOptionCount(setting);
  if (optionCount == 0) return;

  std::vector<std::string> options;
  options.reserve(optionCount);
  for (uint8_t i = 0; i < optionCount; i++) {
    options.push_back(settingEnumOptionLabel(setting, i));
  }

  uint8_t currentIndex = 0;
  if (setting.valuePtr != nullptr) {
    currentIndex = enumDisplayIndexForRawValue(setting, SETTINGS.*(setting.valuePtr));
  }
  if (currentIndex >= optionCount) currentIndex = 0;

  const SettingInfo selectedSetting = setting;
  optionPopup.show(setting.nameId, options, currentIndex, [selectedSetting](int selectedIndex) {
    if (selectedSetting.valuePtr != nullptr) {
      SETTINGS.*(selectedSetting.valuePtr) =
          enumRawValueForDisplayIndex(selectedSetting, static_cast<uint8_t>(selectedIndex));
      SETTINGS.saveToFile();
    }
  });
  requestUpdate();
}

void ControlsOptionsActivity::toggleCurrentSetting() {
  if (selectedIndex < 0 || selectedIndex >= settingsCount) return;
  const auto& setting = (*currentSettings)[selectedIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    const bool cur = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !cur;
    SETTINGS.saveToFile();
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    if (currentSettingUsesOptionMenu(setting)) {
      openEnumOptionPicker(setting);
      return;
    }
    const uint8_t cur = SETTINGS.*(setting.valuePtr);
    const uint8_t currentIndex = enumDisplayIndexForRawValue(setting, cur);
    const size_t optionCount = settingEnumOptionCount(setting);
    if (optionCount == 0) return;
    const uint8_t nextIndex = (currentIndex + 1) % static_cast<uint8_t>(optionCount);
    SETTINGS.*(setting.valuePtr) = enumRawValueForDisplayIndex(setting, nextIndex);
    SETTINGS.saveToFile();
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t cur = SETTINGS.*(setting.valuePtr);
    if (cur + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = cur + setting.valueRange.step;
    }
    SETTINGS.saveToFile();
  } else if (setting.type == SettingType::ACTION) {
    if (setting.action == SettingAction::RemapFrontButtons) {
      startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput, false, true),
                             [](const ActivityResult&) { SETTINGS.saveToFile(); });
      return;
    }
    if (setting.action == SettingAction::RemapFrontButtonsReader) {
      startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput, true, true),
                             [](const ActivityResult&) { SETTINGS.saveToFile(); });
      return;
    }
  } else if (setting.type == SettingType::SUBMENU) {
    openSubmenu(setting.action);
    return;
  }
}

void ControlsOptionsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  buttonNavigator.onNextRelease([this] {
    moveSelection(true);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    moveSelection(false);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (activeSubmenu != SettingAction::None) {
      closeSubmenu();
      requestUpdate();
      return;
    }
    SETTINGS.saveToFile();
    finish();
    return;
  }
}

void ControlsOptionsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.buttonHintsHeight : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;

  GUI.drawHeader(renderer, Rect{contentX, metrics.topPadding, contentWidth, metrics.headerHeight}, tr(STR_CAT_CONTROLS),
                 nullptr, true);

  const auto& visibleSettings = *currentSettings;
  Rect listRect{contentX, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, contentWidth,
                pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.buttonHintsHeight +
                              metrics.verticalSpacing * 2)};
  const StrId submenuTitleId = activeSubmenuTitleId();
  if (submenuTitleId != StrId::STR_NONE_OPT) {
    constexpr int submenuHeaderFontId = UI_10_FONT_ID;
    const int headerLineHeight = renderer.getLineHeight(submenuHeaderFontId);
    const int headerOffset = headerLineHeight + metrics.verticalSpacing;
    const int headerMaxWidth = listRect.width - metrics.contentSidePadding * 2;
    const auto headerLabel =
        renderer.truncatedText(submenuHeaderFontId, I18N.get(submenuTitleId), headerMaxWidth, EpdFontFamily::BOLD);
    renderer.drawText(submenuHeaderFontId, listRect.x + metrics.contentSidePadding, listRect.y, headerLabel.c_str(),
                      true, EpdFontFamily::BOLD);
    listRect.y += headerOffset;
    listRect.height = std::max(0, listRect.height - headerOffset);
  }

  GUI.drawList(
      renderer, listRect, settingsCount, selectedIndex,
      [&visibleSettings](int i) { return std::string(I18N.get(visibleSettings[i].nameId)); }, nullptr, nullptr,
      [&visibleSettings](int i) {
        const auto& setting = visibleSettings[i];
        std::string valueText;
        if (setting.type == SettingType::SUBMENU) {
          valueText = ">";
        } else if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          valueText = SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
          const uint8_t value = SETTINGS.*(setting.valuePtr);
          const uint8_t displayValue = enumDisplayIndexForRawValue(setting, value);
          const size_t optionCount = settingEnumOptionCount(setting);
          const uint8_t safeValue = displayValue < optionCount ? displayValue : 0;
          valueText = settingEnumOptionLabel(setting, safeValue);
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          valueText = std::to_string(SETTINGS.*(setting.valuePtr));
        }
        return valueText;
      },
      true, nullptr, [&visibleSettings](int i) { return visibleSettings[i].type == SettingType::SECTION_HEADER; });

  const bool currentIsAction = selectedIndex >= 0 && selectedIndex < settingsCount &&
                               ((*currentSettings)[selectedIndex].type == SettingType::ACTION ||
                                (*currentSettings)[selectedIndex].type == SettingType::SUBMENU ||
                                currentSettingUsesOptionMenu((*currentSettings)[selectedIndex]));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), currentIsAction ? tr(STR_SELECT) : tr(STR_TOGGLE),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}
