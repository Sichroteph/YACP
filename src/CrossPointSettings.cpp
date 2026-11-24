#include "CrossPointSettings.h"

#include <BlePageTurnerJson.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

#include "I18nKeys.h"
#include "ReaderFontSizes.h"
#include "ReaderLineSpacing.h"
#include "SettingsList.h"
#include "YacpFonts.h"
#include "activities/reader/BookReaderSettings.h"
#include "fontIds.h"
#include "util/ParagraphIndentMigration.h"
#include "util/YacpLegacyLineHeight.h"

namespace {

// Stack buffer for "<key>_obf" key construction — avoids a std::string
// allocation per obfuscated setting on every save and load.
constexpr size_t OBF_KEY_BUF = 64;

// Null-terminated copy into a fixed-size settings field.
void copyToField(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

bool CrossPointSettings::isValidDeviceName(const std::string_view name) {
  if (name.empty()) return true;
  if (name.size() < MIN_DEVICE_NAME_LENGTH || name.size() > MAX_DEVICE_NAME_LENGTH) return false;
  for (const unsigned char byte : name) {
    if (byte < 32 || byte == 127) return false;
  }
  return true;
}

const char* CrossPointSettings::getDefaultDeviceName() {
  if (BoardConfig::isX4Pro()) return "YACP X4 Pro";
  if (BoardConfig::isX4Classic()) return "YACP X4 Classic";
  return gpio.deviceIsX3() ? "YACP X3" : "YACP X4";
}

const char* CrossPointSettings::getEffectiveDeviceName() const {
  return deviceName[0] && isValidDeviceName(deviceName) ? deviceName : getDefaultDeviceName();
}

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
  }
}

bool CrossPointSettings::saveToFile() const {
  std::lock_guard<std::mutex> lock(storeMutex);
  const bool bookSaved = !bookProfile || bookProfile->save(*this);
  JsonDocument doc;
  toJson(doc);
  return writeDocToFile(getFilePath(), doc) && bookSaved;
}

void CrossPointSettings::beginBookProfile(BookReaderSettings& profile, const char* cachePath) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (bookProfile) {
    LOG_ERR("CPS", "Book profile already active");
    return;
  }
  profile.begin(*this, cachePath);
  bookProfile = &profile;
}

void CrossPointSettings::endBookProfile(BookReaderSettings& profile) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (bookProfile != &profile) return;
  if (!profile.save(*this)) LOG_ERR("CPS", "Book profile not saved on exit");
  profile.restore(*this);
  bookProfile = nullptr;
}

void CrossPointSettings::setAutoPageTurnSeconds(const uint8_t seconds) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (seconds < 5 || seconds > 120) { LOG_ERR("CPS", "Invalid automatic-turn interval"); return; }
  autoPageTurnSeconds = seconds;
  if (bookProfile) bookProfile->retainInterval();
}

void CrossPointSettings::toJson(JsonDocument& doc) const {
  const CrossPointSettings& s = *this;

  auto visit = [&](const SettingInfo& info) {
    if (!info.key) return;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) return;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        doc[obfKey] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  };
  forEachBaseSetting(visit);

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = frontButtonBack;
  doc["frontButtonConfirm"] = frontButtonConfirm;
  doc["frontButtonLeft"] = frontButtonLeft;
  doc["frontButtonRight"] = frontButtonRight;
  doc["readerFrontButtonsEnabled"] = readerFrontButtonsEnabled;
  doc["sideButtonLongPress"] = sideButtonLongPress;
  doc["longPressBackAction"] = longPressBackAction;
  doc["longPressMenuAction"] = longPressMenuAction;
  doc["longPwrBtn"] = longPwrBtn;
  doc["readerShortPowerAction"] = readerShortPowerAction;
  doc["frontButtonOrientationAware"] = frontButtonOrientationAware;
  doc["sideButtonOrientationAware"] = sideButtonOrientationAware;
  doc["tiltPageTurn"] = tiltPageTurn;
  // The Pro UI hides this C3 preference; retain it across device migrations.
  doc["fadingFix"] = fadingFix;
  doc["tiltPageTurnLastMode"] = tiltPageTurn != TILT_OFF ? tiltPageTurn : tiltPageTurnLastMode;
  doc["readerFrontButtonBack"] = readerFrontButtonBack;
  doc["readerFrontButtonConfirm"] = readerFrontButtonConfirm;
  doc["readerFrontButtonLeft"] = readerFrontButtonLeft;
  doc["readerFrontButtonRight"] = readerFrontButtonRight;
  // Font family and size — both use dynamic getter/setters in SettingsList (the
  // option lists depend on the SD font registry), so the generic loop skips them.
  doc["fontFamily"] = fontFamily;
  doc["yacpReaderFontInitialized"] = true;
  doc["fontSize"] = fontPointSize;
  doc["lineHeightPercent"] = lineHeightPercent;
  doc["autoPageTurnSeconds"] = autoPageTurnSeconds;
  doc["uiTheme"] = uiTheme;
  doc["clockDateHasBeenSynced"] = clockDateHasBeenSynced;
  // SD card font family name — not in SettingsList, save manually
  if (sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = sdFontFamilyName;
  }
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, save manually
  if (dictionaryName[0] != '\0') {
    doc["dictionaryName"] = dictionaryName;
  }
  // The displayed option list varies by board, so this setting uses a dynamic
  // display-index mapping and is skipped by the generic persistence loop.
  doc["longPressMenuFunction"] = longPressMenuFunction;
  doc["shortPwrBtn"] = shortPwrBtn;

  // Language -- managed by LanguageSelectActivity, not in SettingsList.
  // Stored as ISO code string ("EN", "DE", ...) for stability across enum reorders.
  doc["language"] = (language < getLanguageCount()) ? LANGUAGE_CODES[language] : "EN";

  // A uint16_t mask, so it does not fit the uint8_t generic loop. Omitted while
  // unconfigured, so the default keeps following the UI language.
  if (keyboardLayouts != 0) {
    doc["keyboardLayouts"] = keyboardLayouts;
  }
  if (bookProfile) bookProfile->writeGlobalJson(doc);

  bleturner::writeJson(ble, doc);
}

bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  CrossPointSettings& s = *this;
  bool needsResave = false;
  // A missing mode defaults to Magic Flash only on X3. Retain an explicitly
  // saved Classic choice, and recognize both historical reinforcement IDs.
  refreshAction = gpio.deviceIsX3() ? REFRESH_BW_REINFORCEMENT_2X : REFRESH_FULL;
  const int storedDateSynced = doc["clockDateHasBeenSynced"] | 0;
  clockDateHasBeenSynced = storedDateSynced == 1 ? 1 : 0;
  const int storedLineHeight = doc["lineHeightPercent"] | 0;
  lineHeightPercent = storedLineHeight >= LINE_HEIGHT_MIN && storedLineHeight <= LINE_HEIGHT_MAX
                          ? static_cast<uint8_t>(storedLineHeight)
                          : 0;
  if (storedLineHeight != lineHeightPercent) needsResave = true;
  const auto interval = doc["autoPageTurnSeconds"];
  autoPageTurnSeconds = interval.is<int>() && interval.as<int>() >= 5 && interval.as<int>() <= 120
                            ? static_cast<uint8_t>(interval.as<int>()) : 30;
  if (!interval.isNull() && (!interval.is<int>() || interval.as<int>() != autoPageTurnSeconds)) needsResave = true;
  const uint8_t storedTheme = doc["uiTheme"] | static_cast<uint8_t>(YACP);
  uiTheme = storedTheme <= YACP ? storedTheme : static_cast<uint8_t>(YACP);

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  auto visit = [&](const SettingInfo& info) {
    if (!info.key) return;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) return;

    if (info.stringOffset) {
      // destPtr starts out holding the struct-initializer default; it stays that
      // way unless the document actually carries a value for this key.
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        needsResave = true;
        return;
      }

      bool loaded = false;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        bool ok = false;
        bool tooLong = false;
        const std::string decoded =
            obfuscation::deobfuscateFromBase64(doc[obfKey] | "", info.stringMaxLen - 1, &ok, &tooLong);
        if (tooLong) {
          LOG_ERR("CPS", "Oversized obfuscated value for key '%s'", info.key);
          needsResave = true;
        }
        if (ok && !decoded.empty()) {
          copyToField(destPtr, decoded.c_str(), info.stringMaxLen);
          loaded = true;
        }
      }
      if (!loaded) {
        // Read as const char*, never `| std::string(...)`: ArduinoJson's
        // std::string converter drags a per-TU copy of the serializer into
        // flash. See the note in PersistableStore.h.
        const char* raw = doc[info.key].is<const char*>() ? doc[info.key].as<const char*>() : nullptr;
        if (raw) {
          // Obfuscated field recovered from a legacy plaintext value -> resave.
          if (info.obfuscated && strcmp(raw, destPtr) != 0) needsResave = true;
          const JsonString stored = doc[info.key].as<JsonString>();
          if (strcmp(info.key, "deviceName") == 0 &&
              !isValidDeviceName(std::string_view(stored.c_str(), stored.size()))) {
            LOG_ERR("CPS", "Invalid device name; keeping previous value");
            needsResave = true;
          } else {
            copyToField(destPtr, raw, info.stringMaxLen);
          }
        }
      }
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        v = clamp(v, (uint8_t)info.enumLabels().size(), fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  };
  forEachBaseSetting(visit);

  const auto legacyBrowserLayout = doc["fileBrowserPreview"];
  if (doc["fileBrowserDisplay"].isNull() && legacyBrowserLayout.is<uint8_t>() &&
      legacyBrowserLayout.as<uint8_t>() <= 1) {
    fileBrowserDisplay = legacyBrowserLayout.as<uint8_t>();
    needsResave = true;
  }

  const auto indentSpaces = doc["paragraphIndentSpaces"];
  const bool hasSavedWidth = indentSpaces.is<int>();
  const int savedWidth = hasSavedWidth ? indentSpaces.as<int>() : 0;
  paragraphIndentSpaces = migrateParagraphIndentSpaces(hasSavedWidth, savedWidth, extraParagraphSpacing != 0);
  const auto legacyForcedIndent = doc["forceParagraphIndents"];
  if (!hasSavedWidth && (legacyForcedIndent.is<int>() || legacyForcedIndent.is<bool>())) {
    const bool enabled = legacyForcedIndent.is<bool>() ? legacyForcedIndent.as<bool>() : legacyForcedIndent.as<int>() == 1;
    paragraphIndentSpaces = enabled ? FORCE_PARAGRAPH_INDENTS : 0;
  }
  if (!hasSavedWidth || savedWidth < 0 || savedWidth > FORCE_PARAGRAPH_INDENTS) needsResave = true;

  // Older files stored one combined touch mode under "touchReaderControls":
  // 0=off, 1=tap, 2=swipe, 3=inverted tap. Split it into the master toggle
  // plus the per-direction gesture pair (the generic loop above already folded
  // out-of-range toggle values back to the On default).
  if (doc["pageTurnGesture"].isNull() && doc["previousPageGesture"].isNull() &&
      doc["touchReaderControls"].is<uint8_t>()) {
    const uint8_t mode = doc["touchReaderControls"].as<uint8_t>();
    if (mode >= 1 && mode <= 3) {
      touchReaderControls = TOUCH_READER_ON;
      pageTurnGesture = mode == 1 ? TAP_ONLY : mode == 2 ? SWIPE_ONLY : INVERTED_TAP;
      previousPageGesture = pageTurnGesture;
      needsResave = true;
    }
  }

  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    const uint8_t legacyValue =
        clamp(doc["sleepTimeout"] | (uint8_t)SLEEP_10_MIN, SLEEP_TIMEOUT_COUNT, (uint8_t)SLEEP_10_MIN);
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacyValue);
    needsResave = true;
  }
  // "lineSpacing" saves predate the 1.0 option: Tight/Normal/Wide/Extra Wide (0..3) become 1.1..1.45.
  if (doc["lineHeight"].isNull() && doc["lineSpacing"].is<uint8_t>()) {
    const int legacyValue = doc["lineSpacing"].as<uint8_t>();
    lineSpacing = legacyValue + 1 < LINE_COMPRESSION_COUNT ? static_cast<uint8_t>(legacyValue + 1) : LINE_SPACING_120;
    needsResave = true;
  }
  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  validateFrontButtonMapping(s);

  readerFrontButtonsEnabled = (doc["readerFrontButtonsEnabled"] | 0) == 1 ? 1 : 0;
  const auto sideLongPress = doc["sideButtonLongPress"];
  sideButtonLongPress =
      sideLongPress.is<int>() && sideLongPress.as<int>() >= 0 &&
              sideLongPress.as<int>() <= SIDE_LONG_INHERIT
          ? static_cast<uint8_t>(sideLongPress.as<int>())
          : SIDE_LONG_INHERIT;
  if (!sideLongPress.isNull() && (!sideLongPress.is<int>() || sideLongPress.as<int>() != sideButtonLongPress))
    needsResave = true;
  const auto readOrientationProfile = [&](const char* key, const uint8_t fallback, const uint8_t maximum) -> uint8_t {
    const auto value = doc[key];
    if (value.is<int>() && value.as<int>() >= 0 && value.as<int>() <= maximum)
      return static_cast<uint8_t>(value.as<int>());
    if (!value.isNull()) needsResave = true;
    return fallback;
  };
  longPressBackAction = readOrientationProfile("longPressBackAction", 22, 22);
  longPressMenuAction = readOrientationProfile("longPressMenuAction", 22, 23);
  longPwrBtn = readOrientationProfile("longPwrBtn", 23, 23);
  readerShortPowerAction = readOrientationProfile("readerShortPowerAction", 23, 23);
  frontButtonOrientationAware = readOrientationProfile("frontButtonOrientationAware", 3, 3);
  sideButtonOrientationAware = readOrientationProfile("sideButtonOrientationAware", 2, 2);
  const auto fading = doc["fadingFix"];
  fadingFix = fading.is<bool>() ? static_cast<uint8_t>(fading.as<bool>())
                              : fading.is<int>() && fading.as<int>() == 1 ? 1 : 0;
  if (!fading.isNull() && !fading.is<bool>() &&
      (!fading.is<int>() || fading.as<int>() < 0 || fading.as<int>() > 1)) needsResave = true;
  const auto tiltMode = doc["tiltPageTurn"];
  tiltPageTurn = tiltMode.is<int>() && tiltMode.as<int>() >= 0 && tiltMode.as<int>() < TILT_PAGE_TURN_COUNT
                     ? static_cast<uint8_t>(tiltMode.as<int>()) : TILT_OFF;
  if (!tiltMode.isNull() && (!tiltMode.is<int>() || tiltMode.as<int>() != tiltPageTurn)) needsResave = true;
  const auto tiltLastMode = doc["tiltPageTurnLastMode"];
  tiltPageTurnLastMode = tiltLastMode.is<int>() && tiltLastMode.as<int>() > 0 &&
                                tiltLastMode.as<int>() < TILT_PAGE_TURN_COUNT
                            ? static_cast<uint8_t>(tiltLastMode.as<int>()) : TILT_NORMAL;
  if (!tiltLastMode.isNull() && (!tiltLastMode.is<int>() || tiltLastMode.as<int>() != tiltPageTurnLastMode))
    needsResave = true;
  if (tiltPageTurn != TILT_OFF) tiltPageTurnLastMode = tiltPageTurn;
  const char* const readerKeys[] = {"readerFrontButtonBack", "readerFrontButtonConfirm",
                                    "readerFrontButtonLeft", "readerFrontButtonRight"};
  uint8_t readerMap[4] = {0, 1, 2, 3};
  uint8_t seen = 0;
  bool validReaderMap = true;
  for (uint8_t i = 0; i < 4; ++i) {
    const auto value = doc[readerKeys[i]];
    const int button = value.is<int>() ? value.as<int>() : -1;
    if (button < 0 || button >= 4 || (seen & (1u << button))) {
      validReaderMap = false;
      break;
    }
    readerMap[i] = static_cast<uint8_t>(button);
    seen |= 1u << button;
  }
  if (!validReaderMap) {
    readerFrontButtonsEnabled = 0;
    for (uint8_t i = 0; i < 4; ++i) readerMap[i] = i;
  }
  readerFrontButtonBack = readerMap[0];
  readerFrontButtonConfirm = readerMap[1];
  readerFrontButtonLeft = readerMap[2];
  readerFrontButtonRight = readerMap[3];

  // Reader font size — an actual point size since 1.5. Files written by 1.4 and
  // earlier hold the old SMALL/MEDIUM/LARGE/EXTRA_LARGE slot in 0..3; no font is
  // renderable at those sizes, so the range is unambiguous and folds to the
  // point sizes those slots used to mean. Drop this once 1.4 upgrades are done.
  uint8_t storedFontSize = doc["fontSize"] | DEFAULT_FONT_POINT_SIZE;
  if (storedFontSize <= LEGACY_FONT_SIZE_MAX) {
    storedFontSize = 12 + storedFontSize * 2;  // 0,1,2,3 -> 12,14,16,18
    needsResave = true;
  }
  fontPointSize = storedFontSize;

  const bool existingYacp = (doc["yacpReaderFontInitialized"] | false) ||
                            !doc["clockDateHasBeenSynced"].isNull() || Storage.exists("/.yacp-migration-v1/complete");
  if (!existingYacp && BoardConfig::hasHomeKey()) {
    // CrossPoint 2.0 used value 11 for clipping. In YACP it means Sleep.
    constexpr uint8_t CROSSPOINT_HOME_CLIPPING = 11;
    for (const auto field : home_button::FIELDS) {
      if (s.*field == CROSSPOINT_HOME_CLIPPING) {
        s.*field = static_cast<uint8_t>(HomeButtonAction::CreateClipping);
        needsResave = true;
      }
    }
  }

  // Font family is a dynamic setting and is persisted separately.
  const uint8_t storedFontFamily = doc["fontFamily"] | DEFAULT_FONT_FAMILY;
  fontFamily = clamp(storedFontFamily, BUILTIN_FONT_COUNT, DEFAULT_FONT_FAMILY);
  // The UI maps these commands to different option indices on each model.
  longPressMenuFunction = clamp(doc["longPressMenuFunction"] | static_cast<uint8_t>(LP_MENU_DISABLED),
                                LONG_PRESS_MENU_FUNCTION_COUNT, LP_MENU_DISABLED);
  shortPwrBtn = clamp(doc["shortPwrBtn"] | static_cast<uint8_t>(IGNORE), SHORT_PWRBTN_COUNT, IGNORE);
  if (BoardConfig::hasHomeKey() && doc["homeButtonLongPressAction"].isNull() &&
      !doc["longPressMenuFunction"].isNull()) {
    static constexpr HomeButtonAction LEGACY[] = {HomeButtonAction::Sync,       HomeButtonAction::Ignore,
                                                  HomeButtonAction::Bookmark,   HomeButtonAction::Dictionary,
                                                  HomeButtonAction::ReaderMenu};
    if (s.longPressMenuFunction == LP_MENU_CREATE_CLIPPING) {
      s.homeButtonLongPressAction = static_cast<uint8_t>(HomeButtonAction::CreateClipping);
      needsResave = true;
    } else if (s.longPressMenuFunction < std::size(LEGACY)) {
      s.homeButtonLongPressAction = static_cast<uint8_t>(LEGACY[s.longPressMenuFunction]);
      needsResave = true;
    }
  }

  // SD card font family name — not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  if (storedFontFamily == LEGACY_OPENDYSLEXIC && sdFontFamilyName[0] == '\0') {
    fontFamily = LEXENDDECA;
    strncpy(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName) - 1);
    sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
    needsResave = true;
  } else if (storedFontFamily >= BUILTIN_FONT_COUNT) {
    needsResave = true;
  }
  if (!(doc["yacpReaderFontInitialized"] | false)) {
    if (!existingYacp) {
      fontFamily = DEFAULT_FONT_FAMILY;
      sdFontFamilyName[0] = '\0';
      LOG_INF("CPS", "Selected Atkinson for the first CrossPoint import");
    }
    needsResave = true;
  }
  if (existingYacp && lineHeightPercent == 0 && doc["lineHeight"].isNull() &&
      doc["lineSpacing"].is<uint8_t>()) {
    lineHeightPercent = legacyYacpLineHeightPercent(doc["lineSpacing"].as<uint8_t>(), fontFamily,
                                                  sdFontFamilyName[0] != '\0');
  }
  // Dictionary folder name uses a dynamic setting and is loaded by name.
  copyToField(dictionaryName, doc["dictionaryName"] | "", sizeof(dictionaryName));
  // Language -- stored as code string for stability across enum reorders.
  if (doc["language"].is<const char*>()) {
    language = static_cast<uint8_t>(I18n::languageFromCode(doc["language"].as<const char*>()));
  }

  // Absent means unconfigured, which is the default.
  if (doc["keyboardLayouts"].is<uint16_t>()) {
    keyboardLayouts = doc["keyboardLayouts"].as<uint16_t>();
  }

  if (!bleturner::readJson(ble, doc)) {
    needsResave = true;
  }

  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec() const {
  StatusBarSpec spec;
  spec.showChapterPageCount = statusBarChapterPageCount != 0;
  spec.showBookProgressPercent = statusBarBookProgressPercentage != 0;
  spec.titleMode = statusBarTitle;
  spec.showBattery = statusBarBattery != 0;
  spec.showBatteryCurrent = statusBarBatteryCurrent != 0 && gpio.deviceIsX3();
  spec.showBatteryPercent = hideBatteryPercentage == HIDE_NEVER;
  spec.clockMode = statusBarClock;
  spec.clock12h = clockFormat == 1;
  spec.timeLeftMode = statusBarTimeLeft;
  spec.progressBarMode = statusBarProgressBar;
  spec.progressBarAtTop = statusBarProgressBarPosition == PROGRESS_BAR_TOP;
  spec.progressBarHeightPx =
      statusBarProgressBar != HIDE_PROGRESS ? static_cast<uint8_t>((statusBarProgressBarThickness + 1) * 2) : 0;
  spec.xtcMode = xtcStatusBarMode;
  return spec;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.characterSpacing = getCharacterSpacing();
  spec.wordSpacingPercent = wordSpacing;
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.paragraphIndentSpaces = paragraphIndentWidth(paragraphIndentSpaces, spec.extraParagraphSpacing);
  spec.forceParagraphIndents = paragraphIndentSpaces == FORCE_PARAGRAPH_INDENTS;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  // Same layout as Display, so switching between them keeps the section cache.
  spec.imageRendering = imageRendering == IMAGES_DISPLAY_BW ? static_cast<uint8_t>(IMAGES_DISPLAY) : imageRendering;
  spec.focusReadingEnabled = focusReadingEnabled != 0;
  spec.guideReadingEnabled = guideReadingEnabled != 0;
  spec.publisherPageNumbers = publisherPageNumbers != 0;
  return spec;
}

float CrossPointSettings::getReaderLineCompression() const {
  if (lineHeightPercent != 0) {
    return std::clamp<int>(lineHeightPercent, LINE_HEIGHT_MIN, LINE_HEIGHT_MAX) / 100.0f;
  }
  return LINE_SPACING_MULTIPLIERS[lineSpacing < LINE_COMPRESSION_COUNT ? lineSpacing : LINE_SPACING_120];
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  if (sleepTimeoutMinutes >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (getRefreshFrequencySetting()) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
    case REFRESH_60:
      return 60;
    case REFRESH_NEVER:
      // Effectively disables the periodic full refresh; the page counter
      // counts down from here and never reaches the threshold in practice.
      return std::numeric_limits<int>::max();
  }
}

bool CrossPointSettings::supportsUc8279MagicFlash() const {
  return (BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4 || BoardConfig::isX4Pro()) &&
         BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 &&
         BoardConfig::ACTIVE.displayControllerVariant == 0x68;
}

bool CrossPointSettings::supportsMagicFlash() const { return gpio.deviceIsX3() || supportsUc8279MagicFlash(); }

bool CrossPointSettings::isMagicFlash() const {
  return supportsMagicFlash() &&
         (refreshAction == REFRESH_BW_REINFORCEMENT || refreshAction == REFRESH_BW_REINFORCEMENT_2X);
}

bool CrossPointSettings::isTextAntiAliasingEnabled() const { return !isMagicFlash() && textAntiAliasing; }

uint8_t CrossPointSettings::getRefreshFrequencySetting() const { return isMagicFlash() ? REFRESH_1 : refreshFrequency; }

void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = '\0';
  fontPointSize =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  saveToFile();
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontPointSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  // A built-in family only exists at BUILTIN_READER_POINT_SIZES, so a size
  // carried over from an SD family may not be one of them. ensureLoaded()
  // normally persists the snap; snap again here (without allocating — this runs
  // in the page render loop) so rendering is correct even before it has run.
  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  return yacpReaderFontId(fontFamily, pt);
}
