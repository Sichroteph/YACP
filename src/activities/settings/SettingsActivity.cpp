#include "SettingsActivity.h"

#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AboutActivity.h"
#include "BackupStatsActivity.h"
#include "BleSettingsActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ClockSettingsActivity.h"
#include "CrossPointSettings.h"
#include "FontDownloadActivity.h"
#include "HomeButtonSettingsActivity.h"
#include "KOReaderSettingsActivity.h"
#include "KeyboardLayoutsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "ReaderCommandSelectionActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "SilentRestart.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/power/PowerStatsActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/PluginEvents.h"

namespace fui = freeink::ui;

namespace {
// Shared-list filtering for this board; false drops the row from the device UI
// (it stays in the shared list for the web settings API).
SettingsSection sectionFor(const SettingInfo& setting);

bool showOnDevice(const SettingInfo& setting) {
  if (sectionFor(setting) == SettingsSection::End || home_button::isSetting(setting.valuePtr)) return false;
  // The sunlight fading fix is a grayscale-waveform compensation that does
  // not apply on the X4 Pro / X4 Classic (plain OTP waveform, same panels).
  if (setting.valuePtr == &CrossPointSettings::fadingFix && (BoardConfig::isX4Pro() || BoardConfig::isX4Classic())) {
    return false;
  }
  // Merged into the Text Settings screen.
  if (setting.category == StrId::STR_CAT_READER && setting.inTextSettings) return false;
  if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
      SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
    return false;
  }
  return true;
}

// End = not a row here: other categories (KOReader Sync, Status Bar, ...)
// only group the web UI; their rows live on their own sub-screens.
SettingsSection sectionFor(const SettingInfo& setting) {
  if (setting.valuePtr == &CrossPointSettings::libraryUseMetadata ||
      setting.valuePtr == &CrossPointSettings::removeReadBooksFromRecents ||
      setting.valuePtr == &CrossPointSettings::moveFinishedToReadFolder) {
    return SettingsSection::Library;
  }
  if (setting.valuePtr == &CrossPointSettings::fileBrowserDisplay ||
      setting.valuePtr == &CrossPointSettings::hideFileExtension) {
    return SettingsSection::Library;
  }
  if (setting.valuePtr == &CrossPointSettings::sleepTimeoutMinutes) return SettingsSection::Display;
  if (setting.nameId == StrId::STR_DEVICE_NAME) return SettingsSection::General;
  if (setting.category == StrId::STR_CAT_DISPLAY) return SettingsSection::Display;
  if (setting.category == StrId::STR_CAT_READER) return SettingsSection::Reader;
  if (setting.category == StrId::STR_CAT_CONTROLS) return SettingsSection::Controls;
  if (setting.category == StrId::STR_CAT_SYSTEM) return SettingsSection::General;
  return SettingsSection::End;
}

// Device-only action rows added per section, beyond the shared list.
constexpr size_t EXTRA_ROWS_PER_SECTION = 6;

constexpr StrId GROUP_TITLES[] = {
    StrId::STR_SLEEP_SCREEN,            StrId::STR_SETTINGS_APPEARANCE,    StrId::STR_SETTINGS_SCREEN_REFRESH,
    StrId::STR_SETTINGS_POWER_BUTTONS, StrId::STR_SETTINGS_FRONT_BUTTONS, StrId::STR_SETTINGS_SIDE_BUTTONS,
    StrId::STR_TOUCH_READER_CONTROLS,   StrId::STR_READING_STATS,          StrId::STR_SETTINGS_MAINTENANCE,
    StrId::STR_SETTINGS_DEVICE,        StrId::STR_SETTINGS_READING,
};

StrId groupFor(SettingsSection section, const SettingInfo& setting) {
  switch (section) {
    case SettingsSection::Display:
      switch (setting.nameId) {
        case StrId::STR_SLEEP_SCREEN:
        case StrId::STR_SLEEP_COVER_MODE:
        case StrId::STR_SLEEP_COVER_FILTER:
        case StrId::STR_QUICK_RESUME_TIMEOUT:
        case StrId::STR_TIME_TO_SLEEP:
          return StrId::STR_SLEEP_SCREEN;
        case StrId::STR_REFRESH_FREQ:
        case StrId::STR_DISPLAY_MODE:
        case StrId::STR_SUNLIGHT_FADING_FIX:
        case StrId::STR_TEXT_AA:
          return StrId::STR_SETTINGS_SCREEN_REFRESH;
        default:
          return StrId::STR_SETTINGS_APPEARANCE;
      }
    case SettingsSection::Controls:
      switch (setting.nameId) {
        case StrId::STR_HOME_BUTTON:
        case StrId::STR_SHORT_PWR_BTN:
        case StrId::STR_EPUB_HOLD_POWER:
        case StrId::STR_EPUB_SHORT_POWER:
        case StrId::STR_DBL_CLICK_PWR_LIGHT:
        case StrId::STR_PWR_BTN_FOOTNOTE_BACK:
          return StrId::STR_SETTINGS_POWER_BUTTONS;
        case StrId::STR_SIDE_BTN_LAYOUT:
        case StrId::STR_SIDE_BTN_LONG_PRESS:
        case StrId::STR_READER_SIDE_ORIENTATION:
        case StrId::STR_TILT_PAGE_TURN:
          return StrId::STR_SETTINGS_SIDE_BUTTONS;
        case StrId::STR_TOUCH_READER_CONTROLS:
        case StrId::STR_NEXT_PAGE_GESTURE:
        case StrId::STR_PREV_PAGE_GESTURE:
        case StrId::STR_SHOW_READER_MENU:
          return StrId::STR_TOUCH_READER_CONTROLS;
        default:
          return StrId::STR_SETTINGS_FRONT_BUTTONS;
      }
    case SettingsSection::Reader:
      if (setting.type != SettingType::ACTION && setting.nameId != StrId::STR_DICTIONARY) {
        return StrId::STR_SETTINGS_READING;
      }
      break;
    case SettingsSection::General:
      switch (setting.nameId) {
        case StrId::STR_BACKUP_NOW:
        case StrId::STR_AUTO_BACKUP_STATS:
        case StrId::STR_TRACK_READING_STATS:
        case StrId::STR_IDLE_TIME_THRESHOLD:
          return StrId::STR_READING_STATS;
        case StrId::STR_CHECK_UPDATES:
        case StrId::STR_SD_FIRMWARE_UPDATE:
          return StrId::STR_SETTINGS_MAINTENANCE;
        case StrId::STR_CLOCK:
        case StrId::STR_POWER_STATS_TITLE:
        case StrId::STR_DEVICE_NAME:
        case StrId::STR_ABOUT:
          return StrId::STR_SETTINGS_DEVICE;
        default:
          break;
      }
      break;
    default:
      break;
  }
  return StrId::STR_NONE_OPT;
}

StrId refreshHint(const SettingInfo& setting) {
  if (SETTINGS.isMagicFlash()) {
    if (setting.nameId == StrId::STR_REFRESH_FREQ) return StrId::STR_REFRESH_LOCK_HINT;
    if (setting.nameId == StrId::STR_TEXT_AA) return StrId::STR_AA_MAGIC_HINT;
  }
  return StrId::STR_NONE_OPT;
}
}  // namespace

void SettingsActivity::rebuildSettings() {
  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  auto all = getSettingsList(&sdFontSystem.registry(), &dictionaries);

  settings.clear();
  const auto belongs = [this](const SettingInfo& setting) { return groupFor(section, setting) == group; };
  const auto visible = [this](const SettingInfo& setting) {
    return sectionFor(setting) == section && showOnDevice(setting);
  };
  settings.reserve(std::count_if(all.begin(), all.end(), [&visible, &belongs](const SettingInfo& setting) {
                     return visible(setting) && belongs(setting);
                   }) + EXTRA_ROWS_PER_SECTION);

  bool groupsPresent[std::size(GROUP_TITLES)]{};
  const auto addRow = [this, &belongs, &groupsPresent](SettingInfo&& setting) {
    if (belongs(setting)) {
      settings.push_back(std::move(setting));
    } else if (group == StrId::STR_NONE_OPT) {
      const auto rowGroup = groupFor(section, setting);
      const auto* found = std::find(std::begin(GROUP_TITLES), std::end(GROUP_TITLES), rowGroup);
      if (found != std::end(GROUP_TITLES)) groupsPresent[found - GROUP_TITLES] = true;
    }
  };

  const auto addAction = [this, &addRow](SettingsSection target, StrId name, SettingAction action) {
    if (target == section) addRow(SettingInfo::Action(name, action));
  };

  // Language leads so a user stuck in an unfamiliar language finds it first.
  addAction(SettingsSection::General, StrId::STR_LANGUAGE, SettingAction::Language);
  addAction(SettingsSection::General, StrId::STR_KEYBOARD_LAYOUTS, SettingAction::KeyboardLayouts);
  addAction(SettingsSection::Reader, StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings);
  addAction(SettingsSection::Reader, StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts);
  if (BoardConfig::hasHomeKey()) {
    addAction(SettingsSection::Controls, StrId::STR_HOME_BUTTON, SettingAction::HomeButton);
  }
  if (!BoardConfig::hasTouch()) {
    addAction(SettingsSection::Controls, StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons);
  }

  for (auto& setting : all) {
    if (!visible(setting)) continue;
    addRow(std::move(setting));
  }
  if (section == SettingsSection::Display && SETTINGS.supportsMagicFlash()) {
    addRow(SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing"));
  }

  // Clock configuration only exists where the RTC probe found hardware.
  if (halClock.isAvailable()) {
    addAction(SettingsSection::General, StrId::STR_CLOCK, SettingAction::ClockSettings);
  }
  addAction(SettingsSection::Reader, StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar);
  addAction(SettingsSection::Reader, StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache);
  addAction(SettingsSection::General, StrId::STR_POWER_STATS_TITLE, SettingAction::PowerStats);
  addAction(SettingsSection::General, StrId::STR_BACKUP_NOW, SettingAction::BackupStats);
#if FREEINK_MCU_C3
  addAction(SettingsSection::Controls, StrId::STR_REMAP_FRONT_BUTTONS_READER, SettingAction::RemapReaderButtons);
#endif

  addAction(SettingsSection::Network, StrId::STR_WIFI_NETWORKS, SettingAction::Network);
  if (bleturner::status().compiledIn) {
    addAction(SettingsSection::Network, StrId::STR_BT_PAGE_TURNER, SettingAction::Bluetooth);
  }
  addAction(SettingsSection::Network, StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync);
  addAction(SettingsSection::Network, StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser);
  addAction(SettingsSection::Network, StrId::STR_PLUGINS, SettingAction::Plugins);

  // OTA fetches this board's own release asset (see OtaUpdater); boards whose
  // asset isn't published yet just report no update available. The Picco's
  // stock layout has one app slot (partitions_picco.csv), so it cannot update
  // itself either way.
  if (!BoardConfig::isPicco()) {
    addAction(SettingsSection::General, StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates);
    addAction(SettingsSection::General, StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate);
  }
  addAction(SettingsSection::General, StrId::STR_ABOUT, SettingAction::About);
  for (size_t i = 0; i < std::size(GROUP_TITLES); i++) {
    if (groupsPresent[i]) settings.push_back(SettingInfo::Action(GROUP_TITLES[i], SettingAction::OpenGroup));
  }
  rebuildRowItems();
}

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const SettingsSection section, const StrId group)
    : UiListActivity("Settings", renderer, mappedInput), section(section), group(group) {}

void SettingsActivity::onEnter() {
  UiListActivity::onEnter();

  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettings();
}

// Rebuilds rowValues_/rowItems_ (label + actionValue) for settings.
// Structural — call only when the setting list changes, never from
// buildScreen(), which only refreshes rowValues_ content and rowItems_[].value
// pointers in place.
void SettingsActivity::rebuildRowItems() {
  rowValues_.assign(settings.size(), std::string());
  rowItems_.clear();
  rowItems_.reserve(settings.size());
  for (size_t i = 0; i < settings.size(); i++) {
    fui::ListItem item;
    item.label = I18N.get(settings[i].nameId);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

const char* SettingsActivity::headerTitle() const {
  return I18N.get(group == StrId::STR_NONE_OPT ? SETTINGS_SECTION_TITLES[static_cast<size_t>(section)] : group);
}

void SettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // Most rows repaint a different surface (popup, sub-activity, new value);
  // a lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  nav.selected = index;
  toggleSelectedSetting();
  requestUpdate();
}

void SettingsActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  // Theme changes take effect immediately, on this screen — reload the theme
  // and re-derive the app's tokens so the very next repaint is in the new look.
  if (valuePtr != &CrossPointSettings::uiTheme) {
    return;
  }
  UITheme::getInstance().reload();
  // Re-derive the shared tokens for the new look; the gate stays closed until
  // the repaint that rebuilds the interaction table in the new layout.
  resetUi();
}

bool SettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void SettingsActivity::toggleSelectedSetting() {
  mappedInput.resetHomeButtonInput();
  const int selectedSetting = nav.selected;
  if (selectedSetting < 0 || selectedSetting >= static_cast<int>(settings.size())) {
    return;
  }

  const auto& setting = settings[selectedSetting];
  const auto hint = refreshHint(setting);
  if (hint != StrId::STR_NONE_OPT ||
      (setting.nameId == StrId::STR_DICTIONARY && setting.enumStringValues.size() == 1)) {
    static constexpr StrId button[] = {StrId::STR_BACK};
    optionPopup.show(setting.nameId, button, 1, 0, [](int) {}, nullptr, 0,
                     I18N.get(hint == StrId::STR_NONE_OPT ? StrId::STR_DICT_INSTALL_HINT : hint));
    return;
  }
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.nameId == StrId::STR_IDLE_TIME_THRESHOLD) {
    openReadingIdlePicker();
    return;
  }
  if (setting.nameId == StrId::STR_DEVICE_NAME) {
    openDeviceNameEditor();
    return;
  }

  if (setting.nameId == StrId::STR_EPUB_HOLD_MENU || setting.nameId == StrId::STR_EPUB_HOLD_BACK ||
      setting.nameId == StrId::STR_EPUB_HOLD_POWER || setting.nameId == StrId::STR_EPUB_SHORT_POWER) {
    const auto setter = setting.valueSetter;
    // Screen-lifetime ownership is required by ActivityManager. No allocation
    // is added until this explicitly requested list is opened.
    auto picker = makeUniqueNoThrow<ReaderCommandSelectionActivity>(renderer, mappedInput, setting.nameId,
                                                                    setting.enumLabels(), setting.valueGetter());
    if (!picker) {
      LOG_ERR("SET", "OOM: reader command choices");
      return;
    }
    startActivityForResult(std::move(picker), [this, setter](const ActivityResult& result) {
      if (!result.isCancelled && std::holds_alternative<MenuResult>(result.data)) {
        setter(static_cast<uint8_t>(std::get<MenuResult>(result.data).action));
        SETTINGS.saveToFile();
        rebuildSettings();
      }
      requestUpdate();
    });
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    if (enumLabels.size() > 2) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, enumLabels.data(), static_cast<int>(enumLabels.size()), currentValue,
                       [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
                         SETTINGS.*valuePtr = idx;
                         syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
                         SETTINGS.saveToFile();
                         rebuildSettings();
                         applyUiSettingChange(valuePtr);
                       });
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(enumLabels.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumLabels().size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    const bool displayMode = setting.nameId == StrId::STR_DISPLAY_MODE;
    const bool homeTheme = setting.nameId == StrId::STR_UI_THEME;
    if (totalValues > 2 || displayMode) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged, homeTheme](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildSettings();
        if (homeTheme) applyUiSettingChange(&CrossPointSettings::uiTheme);
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        const auto enumLabels = setting.enumLabels();
        const bool unavailableMagic = displayMode && !SETTINGS.supportsMagicFlash();
        optionPopup.show(setting.nameId, enumLabels.data(), static_cast<int>(enumLabels.size()), cur,
                         std::move(onSelect), nullptr, unavailableMagic ? 1 : 0,
                         unavailableMagic ? tr(STR_MAGIC_FLASH_UNAVAILABLE) : nullptr);
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::OpenGroup: {
        auto activity = makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, section, setting.nameId);
        if (!activity) {
          LOG_ERR("SET", "OOM: settings group");
          return;
        }
        startActivityForResult(std::move(activity), [this](const ActivityResult&) { rebuildSettings(); });
        return;
      }
      case SettingAction::HomeButton: {
        // Activities must outlive this call and are owned by the activity stack.
        auto activity = makeUniqueNoThrow<HomeButtonSettingsActivity>(renderer, mappedInput);
        if (!activity) {
          LOG_ERR("SET", "OOM: Home button settings");
          return;
        }
        startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
        return;
      }
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ClockSettings:
        if (auto activity = makeUniqueNoThrow<ClockSettingsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SETTINGS", "OOM: ClockSettingsActivity");
        }
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network: {
        auto activity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, false);
        if (!activity) {
          LOG_ERR("SETTINGS", "OOM: WifiSelectionActivity");
          return;
        }
        startActivityForResult(std::move(activity), [](const ActivityResult&) {
          SETTINGS.saveToFile();
          // Every other WiFi consumer hands the radio to a session it owns;
          // these rows only save credentials, so nothing here would ever
          // release the driver's heap. The scan alone brings it up, so tear
          // down whether or not the user joined a network.
          if (WiFi.getMode() == WIFI_MODE_NULL) return;
          WiFi.disconnect(false);
          delay(30);
          // Unlike the onExit() teardowns, this runs from the loop task with
          // no lock held; the restart popup paints straight to the panel.
          RenderLock lock;
          silentRestartToSettings();
        });
        break;
      }
      case SettingAction::Bluetooth: {
        auto activity = makeUniqueNoThrow<BleSettingsActivity>(renderer, mappedInput);
        if (!activity) {
          LOG_ERR("SETTINGS", "OOM: BleSettingsActivity");
          return;
        }
        startActivityForResult(std::move(activity), resultHandler);
        break;
      }
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettings();
                               });
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 // TextSettingsActivity saves on each change; no save needed here.
                                 rebuildSettings();
                               });
        break;
      case SettingAction::Language:
        // Row labels are translated once in rebuildRowItems() and don't
        // re-run on Pop (see ActivityManager::loop()), so a language switch
        // needs an explicit rebuild here rather than the generic resultHandler.
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettings();
                               });
        break;
      case SettingAction::Plugins: {
        static constexpr StrId OPTIONS[] = {StrId::STR_ENABLED, StrId::STR_DISABLED, StrId::STR_CANCEL};
        optionPopup.show(StrId::STR_PLUGINS, OPTIONS, 3, SETTINGS.pluginsEnabled ? 0 : 1, [this](const int idx) {
          if (idx == 2) return;
          SETTINGS.pluginsEnabled = idx == 0 ? 1 : 0;
          // Turning plugins on brings back a dismissed Plugin Hub install row.
          if (SETTINGS.pluginsEnabled) SETTINGS.pluginHubPromptHidden = 0;
          SETTINGS.saveToFile();
          // Applies to event delivery now, not after a reboot.
          pluginevents::refreshSubscriptions();
          rebuildSettings();
        });
        requestUpdate();
        return;
      }
      case SettingAction::KeyboardLayouts:
        if (auto activity = makeUniqueNoThrow<KeyboardLayoutsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: KeyboardLayoutsActivity");
        }
        break;
      case SettingAction::PowerStats:
        if (auto activity = makeUniqueNoThrow<PowerStatsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: PowerStatsActivity");
        }
        break;
      case SettingAction::BackupStats:
        if (auto activity = makeUniqueNoThrow<BackupStatsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: BackupStatsActivity");
        }
        break;
      case SettingAction::RemapReaderButtons:
        if (auto activity = makeUniqueNoThrow<ButtonRemapActivity>(renderer, mappedInput, true)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SETTINGS", "OOM: reader button remapping");
        }
        break;
      case SettingAction::About:
        if (auto activity = makeUniqueNoThrow<AboutActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: AboutActivity");
        }
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  const auto valuePtr = setting.valuePtr;  // rebuildSettings() replaces `setting`
  rebuildSettings();
  applyUiSettingChange(valuePtr);
  // Rows can appear/disappear with a change (e.g. the footnote-back toggle).
  nav.selected = std::min(selectedSetting, static_cast<int>(settings.size()) - 1);
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::openDeviceNameEditor() {
  // The existing keyboard owns its buffers only while editing, outside reading.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
      renderer, mappedInput, tr(STR_DEVICE_NAME), SETTINGS.getEffectiveDeviceName(),
      CrossPointSettings::MAX_DEVICE_NAME_LENGTH, InputType::Text, CrossPointSettings::MIN_DEVICE_NAME_LENGTH);
  if (!keyboard) {
    LOG_ERR("SET", "OOM: device-name keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      std::string_view name = std::get<KeyboardResult>(result.data).text;
      const auto first = name.find_first_not_of(' ');
      if (first != std::string_view::npos) {
        name = name.substr(first, name.find_last_not_of(' ') - first + 1);
        if (CrossPointSettings::isValidDeviceName(name)) {
          char previous[sizeof(SETTINGS.deviceName)];
          memcpy(previous, SETTINGS.deviceName, sizeof(previous));
          memcpy(SETTINGS.deviceName, name.data(), name.size());
          SETTINGS.deviceName[name.size()] = '\0';
          if (!SETTINGS.saveToFile()) {
            memcpy(SETTINGS.deviceName, previous, sizeof(previous));
            LOG_ERR("SET", "Failed to save device name");
          }
        }
      }
    }
    requestUpdate();
  });
}

void SettingsActivity::openReadingIdlePicker() {
  auto picker = makeUniqueNoThrow<IntervalSelectionActivity>(
      renderer, mappedInput, "ReadingIdleInterval", StrId::STR_IDLE_TIME_THRESHOLD,
      SETTINGS.getReadingIdleTimeThresholdSeconds(), 30, 600, 10, 60, StrId::STR_SECONDS_VALUE_FORMAT, false);
  if (!picker) {
    LOG_ERR("SET", "OOM: reading idle interval picker");
    return;
  }
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      SETTINGS.readingIdleTimeThresholdUnits = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value / 10);
      SETTINGS.saveToFile();
    }
    requestUpdate();
  });
}

std::string SettingsActivity::settingValueText(const SettingInfo& setting) {
  if (setting.nameId == StrId::STR_REFRESH_FREQ && SETTINGS.isMagicFlash()) return tr(STR_PAGES_1);
  if (setting.nameId == StrId::STR_DEVICE_NAME) return SETTINGS.getEffectiveDeviceName();
  if (setting.nameId == StrId::STR_IDLE_TIME_THRESHOLD) {
    char value[16];
    snprintf(value, sizeof(value), tr(STR_SECONDS_VALUE_FORMAT),
             static_cast<unsigned>(SETTINGS.getReadingIdleTimeThresholdSeconds()));
    return value;
  }
  if (setting.action == SettingAction::HomeButton) return tr(STR_CONFIGURE);
  if (setting.action == SettingAction::Plugins) return SETTINGS.pluginsEnabled ? tr(STR_ENABLED) : tr(STR_DISABLED);
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    // Guard like the valueGetter branch below: a corrupt/migrated settings
    // byte must not index past the enum table.
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    if (value >= enumLabels.size()) return "";
    return I18N.get(enumLabels[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    const auto enumLabels = setting.enumLabels();
    if (value < enumLabels.size()) {
      return I18N.get(enumLabels[value]);
    }
    return "";
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
        return tr(STR_SLEEP_NEVER);
      }
      char valueBuffer[32];
      snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
      return valueBuffer;
    }
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  return "";
}

void SettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_ (label/actionValue) was built by rebuildRowItems(); only the
  // live value text needs refreshing here, by assigning into the existing
  // rowValues_ strings (no vector growth).
  for (size_t i = 0; i < settings.size(); i++) {
    const auto& setting = settings[i];
    const auto labels = setting.enumLabels();
    const bool checkbox = setting.type == SettingType::TOGGLE ||
                          (setting.type == SettingType::ENUM && setting.enumStringValues.empty() &&
                           labels.size() == 2 && labels[0] == StrId::STR_STATE_OFF && labels[1] == StrId::STR_STATE_ON);
    if (checkbox && (setting.valuePtr || setting.valueGetter)) {
      const bool checked = refreshHint(setting) == StrId::STR_NONE_OPT &&
                           (setting.valuePtr ? SETTINGS.*(setting.valuePtr) != 0 : setting.valueGetter() != 0);
      rowValues_[i].clear();
      GUI.setCheckboxRow(rowItems_[i], checked);
    } else {
      rowValues_[i] = settingValueText(setting);
      rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
    }
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Touch uses the standard list sizing, as in Bluetooth; buttons keep compact labels.
  // maxLines also marks smallText as explicit so the list does not replace it with bodyText.
  props.labelText = mappedInput.hasTouch() ? screen.theme().bodyText : screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void SettingsActivity::drawFooter() {
  const int selected = nav.selected;
  const bool picker =
      selected >= 0 && selected < static_cast<int>(settings.size()) &&
      (settings[selected].type == SettingType::ACTION || settings[selected].nameId == StrId::STR_TIME_TO_SLEEP);
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), picker ? tr(STR_SELECT) : tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
