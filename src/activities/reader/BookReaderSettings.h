#pragma once

#include <ArduinoJson.h>
#include <cstdint>

class CrossPointSettings;

// Owned by the EPUB activity. Fixed storage, no allocation or work on page turns.
class BookReaderSettings {
 public:
  static constexpr unsigned FIELD_COUNT = 20;
  static constexpr unsigned INTERVAL_INDEX = 17;
  static constexpr unsigned FILE_SIZE = 66;
  struct Profile {
    uint8_t values[FIELD_COUNT]{};
    char family[32]{};
  };

  void begin(CrossPointSettings& settings, const char* cachePath);
  void restore(CrossPointSettings& settings) const;
  void writeGlobalJson(JsonDocument& doc) const;
  bool save(const CrossPointSettings& settings);
  void retainInterval() { forceInterval = !(profileFlags & 2); }
  static bool moveForBookCaches(const char* oldCache, const char* newCache);
  // Commit an inheritance-only record to the alternate slot. No profile file
  // is removed, and future global defaults continue applying after the reset.
  static bool resetForBook(const CrossPointSettings& defaults, const char* cachePath);

 private:
  Profile global;
  Profile persisted;
  char directory[64]{};
  uint32_t generation = 0;
  int8_t activeSlot = -1;
  uint8_t profileFlags = 0;  // bit 0 text override, bit 1 automatic-turn interval.
  bool writable = false;
  bool forceInterval = false;
  static Profile capture(const CrossPointSettings& settings);
  static void apply(CrossPointSettings& settings, const Profile& profile, uint8_t flags = 3);
  // 0 absent, 1 valid, -1 corrupt, -2 unsupported or inaccessible.
  int readSlot(unsigned slot, Profile& profile, uint32_t& sequence, uint8_t& flags) const;
  bool slotPath(unsigned slot, char* path, unsigned capacity) const;
  void load(const CrossPointSettings& settings, const char* cachePath);
  bool write(const Profile& current, uint8_t flags);
};
