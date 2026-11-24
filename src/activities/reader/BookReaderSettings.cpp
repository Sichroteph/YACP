#include "BookReaderSettings.h"

#include <HalStorage.h>
#include <Logging.h>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "util/ParagraphIndentMigration.h"
#include "util/YacpLegacyLineHeight.h"

namespace {
struct Field {
  const char* key;
  uint8_t CrossPointSettings::*member;
  uint8_t min;
  uint8_t max;
};
// Version 5 stores the CrossPoint 2.0 presets and numeric indent widths.
constexpr uint8_t PROFILE_VERSION_NUMERIC_INDENT = 5;
constexpr uint8_t PROFILE_VERSION_FORCED_INDENT = 6;
constexpr unsigned LINE_HEIGHT_INDEX = 2;
constexpr unsigned LINE_HEIGHT_PERCENT_INDEX = 3;
constexpr unsigned IMAGE_RENDERING_INDEX = 11;
constexpr unsigned INDENT_INDEX = 13;
static const Field FIELDS[] = {
    {"fontFamily", &CrossPointSettings::fontFamily, 0, 1},
    {"fontSize", &CrossPointSettings::fontPointSize, 4, 255},
    {"lineHeight", &CrossPointSettings::lineSpacing, 0, 6},
    {"lineHeightPercent", &CrossPointSettings::lineHeightPercent, 0, 200},
    {"orientation", &CrossPointSettings::orientation, 0, 3},
    {"screenMargin", &CrossPointSettings::screenMargin, 5, 40},
    {"paragraphAlignment", &CrossPointSettings::paragraphAlignment, 0, 4},
    {"embeddedStyle", &CrossPointSettings::embeddedStyle, 0, 1},
    {"hyphenationEnabled", &CrossPointSettings::hyphenationEnabled, 0, 1},
    {"textAntiAliasing", &CrossPointSettings::textAntiAliasing, 0, 1},
    {"readerDarkMode", &CrossPointSettings::readerDarkMode, 0, 1},
    {"imageRendering", &CrossPointSettings::imageRendering, 0, 3},
    {"extraParagraphSpacing", &CrossPointSettings::extraParagraphSpacing, 0, 1},
    {"paragraphIndentSpaces", &CrossPointSettings::paragraphIndentSpaces, 0, FORCE_PARAGRAPH_INDENTS},
    {"focusReadingEnabled", &CrossPointSettings::focusReadingEnabled, 0, 1},
    {"wordSpacing", &CrossPointSettings::wordSpacing, 50, 200},
    {"characterSpacing", &CrossPointSettings::characterSpacing, 0, 4},
    {"autoPageTurnSeconds", &CrossPointSettings::autoPageTurnSeconds, 5, 120},
    {"guideReadingEnabled", &CrossPointSettings::guideReadingEnabled, 0, 1},
    {"publisherPageNumbers", &CrossPointSettings::publisherPageNumbers, 0, 1},
};
static_assert(sizeof(FIELDS) / sizeof(FIELDS[0]) == BookReaderSettings::FIELD_COUNT);
static_assert(sizeof(BookReaderSettings) <= 192);

uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void write32(uint8_t* p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (i * 8));
}
uint32_t checksum(const uint8_t* p, unsigned size) {
  uint32_t value = 2166136261u;
  for (unsigned i = 0; i < size; ++i) value = (value ^ p[i]) * 16777619u;
  return value;
}
bool valid(const BookReaderSettings::Profile& profile) {
  for (unsigned i = 0; i < BookReaderSettings::FIELD_COUNT; ++i) {
    if (profile.values[i] < FIELDS[i].min || profile.values[i] > FIELDS[i].max) return false;
  }
  return (profile.values[3] == 0 || profile.values[3] >= 70) &&
         memchr(profile.family, '\0', sizeof(profile.family));
}
bool same(const BookReaderSettings::Profile& a, const BookReaderSettings::Profile& b) {
  return memcmp(a.values, b.values, sizeof(a.values)) == 0 && strcmp(a.family, b.family) == 0;
}
bool sameText(const BookReaderSettings::Profile& a, const BookReaderSettings::Profile& b) {
  for (unsigned i = 0; i < BookReaderSettings::FIELD_COUNT; ++i) {
    if (i != BookReaderSettings::INTERVAL_INDEX && a.values[i] != b.values[i]) return false;
  }
  return strcmp(a.family, b.family) == 0;
}
bool profileDirectory(const char* cache, char* path, unsigned capacity) {
  constexpr char prefix[] = "/.crosspoint/epub_";
  if (strncmp(cache, prefix, sizeof(prefix) - 1)) return false;
  const char* number = cache + sizeof(prefix) - 1;
  if (!*number) return false;
  for (const char* p = number; *p; ++p) if (*p < '0' || *p > '9') return false;
  const int n = snprintf(path, capacity, "/.crosspoint/book_profiles/epub_%s", number);
  return n > 0 && static_cast<unsigned>(n) < capacity;
}
}  // namespace

BookReaderSettings::Profile BookReaderSettings::capture(const CrossPointSettings& settings) {
  Profile profile;
  for (unsigned i = 0; i < FIELD_COUNT; ++i) profile.values[i] = settings.*FIELDS[i].member;
  strncpy(profile.family, settings.sdFontFamilyName, sizeof(profile.family) - 1);
  return profile;
}
void BookReaderSettings::apply(CrossPointSettings& settings, const Profile& profile, const uint8_t flags) {
  if (flags & 1) {
    for (unsigned i = 0; i < FIELD_COUNT; ++i)
      if (i != INTERVAL_INDEX) settings.*FIELDS[i].member = profile.values[i];
    memcpy(settings.sdFontFamilyName, profile.family, sizeof(profile.family));
  }
  if (flags & 2) settings.autoPageTurnSeconds = profile.values[INTERVAL_INDEX];
}
bool BookReaderSettings::slotPath(unsigned slot, char* path, unsigned capacity) const {
  const int n = snprintf(path, capacity, "%s/reader_profile.%u", directory, slot);
  return n > 0 && static_cast<unsigned>(n) < capacity;
}
int BookReaderSettings::readSlot(unsigned slot, Profile& profile, uint32_t& sequence, uint8_t& flags) const {
  char path[88];
  if (!slotPath(slot, path, sizeof(path))) return -2;
  if (!Storage.exists(path)) return 0;
  HalFile file;
  if (!Storage.openFileForRead("BOOKSET", path, file)) return -2;
  uint8_t bytes[FILE_SIZE]{};
  const auto size = file.fileSize();
  const int n = file.read(bytes, sizeof(bytes));
  const bool closed = file.close();
  if (!closed) return -2;
  if (n >= 5 && (memcmp(bytes, "YBRS", 4) || bytes[4] < 1 || bytes[4] > PROFILE_VERSION_FORCED_INDENT)) return -2;
  const bool legacy = bytes[4] == 1;
  const unsigned expectedSize = legacy ? 62 : bytes[4] == 2 ? 64 : bytes[4] == 3 ? 65 : FILE_SIZE;
  if (size != expectedSize || n != static_cast<int>(expectedSize) ||
      read32(bytes + expectedSize - 4) != checksum(bytes, expectedSize - 4)) return -1;
  flags = legacy ? 1 : bytes[9];
  if (flags & ~3u) return -2;
  sequence = read32(bytes + 5);
  profile = global;
  const unsigned fields = legacy ? 17 : bytes[4] == 2 ? 18 : bytes[4] == 3 ? 19 : FIELD_COUNT;
  const unsigned offset = legacy ? 9 : 10;
  memcpy(profile.values, bytes + offset, fields);
  memcpy(profile.family, bytes + offset + fields, sizeof(profile.family));
  auto& indent = profile.values[INDENT_INDEX];
  if (bytes[4] < PROFILE_VERSION_NUMERIC_INDENT) {
    auto& lineHeight = profile.values[LINE_HEIGHT_INDEX];
    if (lineHeight > 3 || indent > 1 || profile.values[IMAGE_RENDERING_INDEX] > 2) return -1;
    auto& percent = profile.values[LINE_HEIGHT_PERCENT_INDEX];
    if (percent == 0) percent = legacyYacpLineHeightPercent(lineHeight, profile.values[0], profile.family[0] != '\0');
    lineHeight += 1;
    indent = indent == 1 ? FORCE_PARAGRAPH_INDENTS : 0;
  } else if (bytes[4] == PROFILE_VERSION_NUMERIC_INDENT && indent > MAX_PARAGRAPH_INDENT_SPACES) {
    return -1;
  }
  return valid(profile) ? 1 : -1;
}
void BookReaderSettings::load(const CrossPointSettings& settings, const char* cachePath) {
  global = persisted = capture(settings);
  generation = 0;
  activeSlot = -1;
  profileFlags = 0;
  forceInterval = false;
  writable = false;
  if (!profileDirectory(cachePath, directory, sizeof(directory))) {
    LOG_ERR("BOOKSET", "Invalid cache identity; book settings disabled");
    return;
  }
  Profile candidate;
  bool damaged = false;
  bool unsupported = false;
  for (unsigned slot = 0; slot < 2; ++slot) {
    uint32_t sequence = 0;
    uint8_t flags = 0;
    const int result = readSlot(slot, candidate, sequence, flags);
    damaged |= result < 0;
    unsupported |= result == -2;
    if (result == 1 && (activeSlot < 0 || (sequence != generation && sequence - generation < 0x80000000u))) {
      persisted = candidate;
      generation = sequence;
      activeSlot = static_cast<int8_t>(slot);
      profileFlags = flags;
    }
  }
  writable = !unsupported && (!damaged || activeSlot >= 0);
  if (damaged) LOG_ERR("BOOKSET", "Damaged or unsupported profile; recovered=%d writable=%d", activeSlot >= 0, writable);
}
void BookReaderSettings::begin(CrossPointSettings& settings, const char* cachePath) {
  load(settings, cachePath);
  apply(settings, persisted, profileFlags);
  persisted = capture(settings);  // Inactive fields continue following current global defaults.
  LOG_INF("BOOKSET", "Opened profile: slot=%d size=%u", activeSlot, settings.fontPointSize);
}
void BookReaderSettings::restore(CrossPointSettings& settings) const { apply(settings, global); }
void BookReaderSettings::writeGlobalJson(JsonDocument& doc) const {
  doc.remove("lineSpacing");
  for (unsigned i = 0; i < FIELD_COUNT; ++i) doc[FIELDS[i].key] = global.values[i];
  if (global.family[0]) doc["sdFontFamilyName"] = global.family;
  else doc.remove("sdFontFamilyName");
}
bool BookReaderSettings::save(const CrossPointSettings& settings) {
  const Profile current = capture(settings);
  if (!forceInterval && same(current, persisted)) return true;
  const uint8_t flags = profileFlags | (sameText(current, persisted) ? 0 : 1) |
                        (!forceInterval && current.values[INTERVAL_INDEX] == persisted.values[INTERVAL_INDEX] ? 0 : 2);
  return write(current, flags);
}
bool BookReaderSettings::write(const Profile& current, const uint8_t flags) {
  if (!writable || !valid(current)) {
    LOG_ERR("BOOKSET", "Cannot save book profile; original files preserved");
    return false;
  }
  const unsigned slot = activeSlot == 0 ? 1 : 0;
  uint8_t bytes[FILE_SIZE]{};
  memcpy(bytes, "YBRS", 4);
  bytes[4] = current.values[INDENT_INDEX] == FORCE_PARAGRAPH_INDENTS ? PROFILE_VERSION_FORCED_INDENT
                                                                    : PROFILE_VERSION_NUMERIC_INDENT;
  write32(bytes + 5, generation + 1);
  bytes[9] = flags;
  memcpy(bytes + 10, current.values, FIELD_COUNT);
  memcpy(bytes + 10 + FIELD_COUNT, current.family, sizeof(current.family));
  write32(bytes + FILE_SIZE - 4, checksum(bytes, FILE_SIZE - 4));
  char path[88];
  if (!slotPath(slot, path, sizeof(path))) return false;
  Storage.mkdir("/.crosspoint");
  Storage.mkdir("/.crosspoint/book_profiles");
  Storage.mkdir(directory);
  HalFile file;
  if (!Storage.openFileForWrite("BOOKSET", path, file)) return false;
  const bool written = file.write(bytes, sizeof(bytes)) == sizeof(bytes);
  const bool closed = file.close();
  Profile verified;
  uint32_t sequence = 0;
  uint8_t verifiedFlags = 0;
  if (!written || !closed || readSlot(slot, verified, sequence, verifiedFlags) != 1 || verifiedFlags != flags ||
      sequence != generation + 1 || !same(current, verified)) {
    LOG_ERR("BOOKSET", "Profile write failed; previous slot retained");
    return false;
  }
  persisted = current;
  generation = sequence;
  activeSlot = static_cast<int8_t>(slot);
  profileFlags = flags;
  forceInterval = false;
  LOG_INF("BOOKSET", "Saved profile generation=%lu", static_cast<unsigned long>(generation));
  return true;
}

bool BookReaderSettings::resetForBook(const CrossPointSettings& defaults, const char* cachePath) {
  BookReaderSettings profile;
  profile.load(defaults, cachePath);
  if (!profile.writable) {
    LOG_ERR("BOOKSET", "Cannot reset unsupported or damaged profile; originals retained");
    return false;
  }
  if (profile.profileFlags == 0) return true;
  return profile.write(profile.global, 0);
}

bool BookReaderSettings::moveForBookCaches(const char* oldCache, const char* newCache) {
  char source[64], target[64];
  if (!profileDirectory(oldCache, source, sizeof(source)) ||
      !profileDirectory(newCache, target, sizeof(target))) return false;
  if (!Storage.exists(source)) return true;
  if (Storage.exists(target) || !Storage.rename(source, target)) {
    LOG_ERR("BOOKSET", "Could not move book profile; original directory retained");
    return false;
  }
  return true;
}
