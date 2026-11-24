#pragma once

#include "LegacyMigrationIo.h"
#include <cstdlib>

namespace legacy {
inline bool fontSize(JsonDocument& doc, const char* version, int range) {
  if (doc["fontSize"].isNull()) return true;
  const int index = doc["fontSize"] | -1;
  if (index < 0 || index > 15) return false;
  const char* family = doc["sdFontFamilyName"] | "";
  if (*family) {
    if (strlen(family) > 31 || strchr(family, '/') || strchr(family, '\\') ||
        strcmp(family, ".") == 0 || strcmp(family, "..") == 0) return false;
    auto dir = Storage.open((std::string("/.fonts/") + family).c_str());
    if (!dir || !dir.isDirectory()) return false;
    // At most 255 possible point sizes, represented as a 32-byte bitset.
    uint8_t sizes[32]{};
    char name[160];
    for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (!f.isDirectory() && f.getName(name, sizeof(name))) {
        const char* suffix = strrchr(name, '_');
        if (name[0] != '.' && suffix) {
          char* end;
          const long size = strtol(suffix + 1, &end, 10);
          if (size > 0 && size <= 255 && strcmp(end, ".cpfont") == 0) sizes[size / 8] |= 1 << (size & 7);
        }
      }
      f.close();
    }
    dir.close();
    int ordinal = 0, fallback = 0;
    (void)range;
    for (int size = 1; size <= 255; ++size) if (sizes[size / 8] & (1 << (size & 7))) {
      if (ordinal++ == index) { doc["fontSize"] = size; return true; }
      fallback = size;
    }
    if (!fallback) return false;
    doc["fontSize"] = fallback;
    return true;
  }
  const bool large = version && strstr(version, "xlarge");
  const bool standard = version && strstr(version, "tiny");
  if (!large && !standard) {
    LOG_ERR("MIG", "Built-in font source variant is absent; original kept");
    return false;
  }
  if (index >= (large ? 3 : 4)) return false;
  doc["fontSize"] = (large ? 16 : 10) + 2 * index;
  return true;
}

inline bool remap(JsonDocument& doc, const char* key, const int8_t* map, unsigned count) {
  if (doc[key].isNull()) return true;
  const int n = doc[key] | -1;
  if (n < 0 || static_cast<unsigned>(n) >= count || map[n] < 0) return false;
  doc[key] = map[n];
  return true;
}

inline bool convertSettings(JsonDocument& doc, const char* sourceVersion, int sdRange, bool book = false) {
  if (!doc.is<JsonObject>() || !fontSize(doc, sourceVersion, sdRange)) return false;
  static constexpr int8_t sleep[] = {0, 1, 2, 3, 5, 4, -1, -1, -1, 6};
  static constexpr int8_t filter[] = {3, 1, 2, 0};
  static constexpr int8_t refresh[] = {0, 1, 2, 3, 4, 6, 5};
  static constexpr int8_t theme[] = {0, 1, 2, 3, -1, -1, 5};
  if (!remap(doc, "sleepScreen", sleep, sizeof(sleep)) ||
      !remap(doc, "sleepScreenCoverFilter", filter, sizeof(filter)) ||
      !remap(doc, "refreshFrequency", refresh, sizeof(refresh)) ||
      !remap(doc, "uiTheme", theme, sizeof(theme))) return false;
  if (!book && doc["uiTheme"].isNull()) doc["uiTheme"] = 5;
  if (!doc["bionicReadingEnabled"].isNull()) doc["focusReadingEnabled"] = doc["bionicReadingEnabled"];
  if (!doc["displayPowerSaving"].isNull() && doc["fadingFix"].isNull()) doc["fadingFix"] = doc["displayPowerSaving"];
  if (!doc["hideClock"].isNull()) {
    const int n = doc["hideClock"] | -1;
    if (n < 0 || n > 2) return false;
    doc["clockShowHeader"] = n != 2 ? 1 : 0;
    doc["statusBarClock"] = n == 0 ? 1 : 0;
  }
  if (doc["lineHeightPercent"].isNull() && !doc["lineSpacing"].isNull()) {
    const int n = doc["lineSpacing"] | -1;
    if (n < 0 || n > 2) return false;
    static constexpr uint8_t scales[3][3] = {{90, 100, 120}, {95, 110, 130}, {95, 100, 110}};
    const int family = strlen(doc["sdFontFamilyName"] | "") ? 2 : (doc["fontFamily"] | 0);
    if (family < 0 || family > 2) return false;
    doc["lineHeightPercent"] = scales[family][n];
  }
  if (!doc["shortPwrBtn"].isNull()) {
    int n = doc["shortPwrBtn"] | -1;
    if (n < 0 || n > 22) return false;
    if (n == 16) doc["shortPwrBtn"] = 4;
    else if (n > 3) { doc["readerShortPowerAction"] = n; doc["shortPwrBtn"] = 0; }
  }
  if (!doc["tiltPageTurnDirection"].isNull()) {
    int n = doc["tiltPageTurnDirection"] | -1;
    if (n < 0 || n > 3) return false;
    const int schema = doc["tiltPageTurnDirectionSchema"] | 1;
    if (schema != 1 && schema != 2) return false;
    if (schema == 1 && n < 2) n = 1 - n;
    static constexpr uint8_t modes[] = {2, 1, 3, 4};
    const bool enabled = (doc["tiltPageTurn"] | 0) != 0;
    doc["tiltPageTurnLastMode"] = modes[n];
    doc["tiltPageTurn"] = enabled ? modes[n] : 0;
  }
  if (!doc["frontButtonLayout"].isNull() && doc["frontButtonBack"].isNull()) {
    static constexpr uint8_t maps[4][4] = {{0,1,2,3},{2,3,0,1},{1,2,0,3},{0,1,3,2}};
    const int n = doc["frontButtonLayout"] | -1;
    if (n < 0 || n > 3) return false;
    static constexpr const char* keys[] = {"frontButtonBack","frontButtonConfirm","frontButtonLeft","frontButtonRight"};
    for (int i = 0; i < 4; ++i) doc[keys[i]] = maps[n][i];
  }
  if (!doc["clockUtcOffsetQ"].isNull()) { doc["clockTimezone"] = 255; doc["clockDst"] = 2; }
  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    static constexpr uint8_t minutes[] = {1,5,10,15,30};
    const int n = doc["sleepTimeout"] | -1;
    if (n < 0 || n > 4) return false;
    doc["sleepTimeoutMinutes"] = minutes[n];
  }
  return !doc.overflowed();
}

inline bool convertCrossInkSettings(const JsonDocument& source, JsonDocument& target) {
  // CrossInk v1.6.1 stores its settings separately from CrossPoint. Import
  // fields whose persisted values have the same meaning in both firmwares.
  static constexpr const char* common[] = {
      "language", "fontFamily", "sdFontFamilyName", "orientation", "screenMargin",
      "paragraphAlignment", "extraParagraphSpacing", "wordSpacing", "lineHeightPercent", "lineSpacing",
      "textAntiAliasing", "hideBatteryPercentage", "refreshFrequency", "sleepScreenCoverMode",
      "sleepScreenCoverFilter", "statusBarChapterPageCount", "statusBarBookProgressPercentage",
      "statusBarTitle", "statusBarBattery", "statusBarTimeLeft", "statusBarProgressBar",
      "statusBarProgressBarThickness", "xtcStatusBarMode", "frontButtonBack", "frontButtonConfirm",
      "frontButtonLeft", "frontButtonRight", "showHiddenFiles", "trackReadingStats",
  };
  for (const auto* key : common) {
    if (!source[key].isNull()) target[key].set(source[key]);
  }

  const int size = source["fontSize"] | -1;
  if (size >= 0 && size <= 3)
    target["fontSize"] = 10 + 2 * size;
  else if (size >= 8 && size <= 255)
    target["fontSize"] = size;

  // Fallbacks for CrossInk-only cover and dashboard layouts.
  static constexpr uint8_t sleep[] = {0, 1, 2, 3, 5, 4, 3, 5, 5, 6, 5, 5};
  const unsigned sleepMode = source["sleepScreen"] | static_cast<unsigned>(sizeof(sleep));
  if (sleepMode < sizeof(sleep)) target["sleepScreen"] = sleep[sleepMode];
  static constexpr uint8_t themes[] = {0, 1, 2, 3, 5, 5, 5, 4};
  const unsigned theme = source["uiTheme"] | static_cast<unsigned>(sizeof(themes));
  if (theme < sizeof(themes)) target["uiTheme"] = themes[theme];

  static constexpr uint8_t minutes[] = {1, 5, 10, 15, 30};
  const unsigned timeout = source["sleepTimeout"] | static_cast<unsigned>(sizeof(minutes));
  if (timeout < sizeof(minutes)) target["sleepTimeoutMinutes"] = minutes[timeout];
  const int power = source["shortPwrBtn"] | -1;
  if (power >= 0 && power <= 3) target["shortPwrBtn"] = power;
  if (power == 16) target["shortPwrBtn"] = 4;  // Footnotes.

  // The selected font follows the imported profile. Bitter's old slot uses
  // Atkinson in YACP. Keep the unchanged CrossInk file for a downgrade.
  target["yacpReaderFontInitialized"] = true;
  return !target.overflowed();
}
}
