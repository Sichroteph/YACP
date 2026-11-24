#include "BookReaderSettings.h"
#include <CrossPointSettings.h>
#include <HalStorage.h>
#include <cassert>
#include <cstdio>

int main() {
  CrossPointSettings settings;
  BookReaderSettings profile;
  profile.begin(settings, "/.crosspoint/epub_1");
  assert(profile.save(settings) && testStorageWrites == 0);
  settings.fontPointSize = 16;
  settings.lineHeightPercent = 90;
  strcpy(settings.sdFontFamilyName, "AtkinsonHyperlegible");
  assert(profile.save(settings) && testStorageWrites == 1);
  const auto first = testFiles.at("/.crosspoint/book_profiles/epub_1/reader_profile.0");
  JsonDocument doc;
  doc["fontSize"] = 16;
  doc["sdFontFamilyName"] = "AtkinsonHyperlegible";
  profile.writeGlobalJson(doc);
  assert(doc["fontSize"].as<int>() == 12 && doc["sdFontFamilyName"].isNull());
  assert(doc["lineHeight"].as<int>() == 1 && doc["lineSpacing"].isNull());
  profile.restore(settings);
  assert(settings.fontPointSize == 12 && settings.lineHeightPercent == 0 && !settings.sdFontFamilyName[0]);
  profile.begin(settings, "/.crosspoint/epub_2");
  assert(settings.fontPointSize == 12 && profile.save(settings) && testStorageWrites == 1);
  profile.restore(settings);
  profile.begin(settings, "/.crosspoint/epub_1");
  assert(settings.fontPointSize == 16 && settings.lineHeightPercent == 90);
  settings.fontPointSize = 18;
  assert(profile.save(settings));
  const auto second = testFiles.at("/.crosspoint/book_profiles/epub_1/reader_profile.1");
  profile.restore(settings);
  profile.begin(settings, "/.crosspoint/epub_1");
  assert(settings.fontPointSize == 18);
  const unsigned writes = testStorageWrites;
  for (unsigned i = 0; i < 1000; ++i) assert(profile.save(settings));
  assert(testStorageWrites == writes);
  profile.restore(settings);

  // Every truncated write retains the preceding complete slot after reboot.
  for (unsigned cut = 0; cut < second.size(); ++cut) {
    testFiles["/.crosspoint/book_profiles/epub_1/reader_profile.1"] = std::vector<uint8_t>(second.begin(), second.begin() + cut);
    profile.begin(settings, "/.crosspoint/epub_1");
    assert(settings.fontPointSize == 16);
    profile.restore(settings);
  }
  // Every single-byte corruption is rejected, including headers and checksum.
  for (unsigned i = 0; i < second.size(); ++i) {
    auto corrupt = second;
    corrupt[i] ^= 1;
    testFiles["/.crosspoint/book_profiles/epub_1/reader_profile.1"] = corrupt;
    profile.begin(settings, "/.crosspoint/epub_1");
    assert(settings.fontPointSize == 16);
    profile.restore(settings);
  }
  auto future = second;
  future[4] = 7;
  testFiles["/.crosspoint/book_profiles/epub_1/reader_profile.1"] = future;
  profile.begin(settings, "/.crosspoint/epub_1");
  settings.fontPointSize = 20;
  assert(!profile.save(settings));
  assert(testFiles.at("/.crosspoint/book_profiles/epub_1/reader_profile.1") == future);
  assert(testFiles.at("/.crosspoint/book_profiles/epub_1/reader_profile.0") == first);
  profile.restore(settings);

  testFiles["/.crosspoint/book_profiles/epub_1/reader_profile.1"] = second;
  profile.begin(settings, "/.crosspoint/epub_1");
  settings.fontPointSize = 20;
  failStatsWrite = true;
  assert(!profile.save(settings));
  failStatsWrite = false;
  profile.restore(settings);
  profile.begin(settings, "/.crosspoint/epub_1");
  assert(settings.fontPointSize == 18);
  settings.fontPointSize = 20;
  failStatsClose = true;
  assert(!profile.save(settings));
  failStatsClose = false;
  assert(profile.save(settings));
  profile.restore(settings);
  profile.begin(settings, "/.crosspoint/epub_1");
  assert(settings.fontPointSize == 20);
  profile.restore(settings);

  // An interval-only profile must keep following future global text choices.
  profile.begin(settings, "/.crosspoint/epub_3");
  settings.autoPageTurnSeconds = 10;
  assert(profile.save(settings));
  assert(testFiles.at("/.crosspoint/book_profiles/epub_3/reader_profile.0")[9] == 2);
  profile.restore(settings);
  assert(settings.autoPageTurnSeconds == 30);
  settings.fontPointSize = 20;
  profile.begin(settings, "/.crosspoint/epub_3");
  assert(settings.fontPointSize == 20 && settings.autoPageTurnSeconds == 10);
  const unsigned unchangedWrites = testStorageWrites;
  assert(profile.save(settings) && testStorageWrites == unchangedWrites);
  settings.fontPointSize = 18;
  assert(profile.save(settings));
  assert(testFiles.at("/.crosspoint/book_profiles/epub_3/reader_profile.1")[9] == 3);
  profile.restore(settings);
  settings.fontPointSize = 16;
  profile.begin(settings, "/.crosspoint/epub_3");
  assert(settings.fontPointSize == 18 && settings.autoPageTurnSeconds == 10);
  profile.restore(settings);

  // The earlier text-only v1 format remains readable and upgrades on a change.
  std::vector<uint8_t> legacy(62);
  memcpy(legacy.data(), first.data(), 9);
  legacy[4] = 1;
  memcpy(legacy.data() + 9, first.data() + 10, 17);
  memcpy(legacy.data() + 26, first.data() + 30, 32);
  uint32_t sum = 2166136261u;
  for (unsigned i = 0; i < 58; ++i) sum = (sum ^ legacy[i]) * 16777619u;
  for (unsigned i = 0; i < 4; ++i) legacy[58 + i] = static_cast<uint8_t>(sum >> (i * 8));
  testFiles["/.crosspoint/book_profiles/epub_4/reader_profile.0"] = legacy;
  settings.autoPageTurnSeconds = 50;
  profile.begin(settings, "/.crosspoint/epub_4");
  assert(settings.fontPointSize == 16 && settings.autoPageTurnSeconds == 50);
  settings.autoPageTurnSeconds = 55;
  assert(profile.save(settings));
  assert(testFiles.at("/.crosspoint/book_profiles/epub_4/reader_profile.0") == legacy);
  assert(testFiles.at("/.crosspoint/book_profiles/epub_4/reader_profile.1").size() == BookReaderSettings::FILE_SIZE);
  profile.restore(settings);
  assert(settings.autoPageTurnSeconds == 50);
  profile.begin(settings, "/.crosspoint/epub_5");
  profile.retainInterval();
  assert(profile.save(settings));
  assert(testFiles.at("/.crosspoint/book_profiles/epub_5/reader_profile.0")[9] == 2);
  profile.restore(settings);
  settings.autoPageTurnSeconds = 60;
  profile.begin(settings, "/.crosspoint/epub_5");
  assert(settings.autoPageTurnSeconds == 50);
  profile.restore(settings);
  // Version 2 has an interval but no guide preference. It inherits the current
  // global guide choice until a book-specific change is saved as version 3.
  std::vector<uint8_t> v2(64);
  memcpy(v2.data(), first.data(), 28);
  v2[4] = 2;
  memcpy(v2.data() + 28, first.data() + 30, 32);
  sum = 2166136261u;
  for (unsigned i = 0; i < 60; ++i) sum = (sum ^ v2[i]) * 16777619u;
  for (unsigned i = 0; i < 4; ++i) v2[60 + i] = static_cast<uint8_t>(sum >> (i * 8));
  testFiles["/.crosspoint/book_profiles/epub_6/reader_profile.0"] = v2;
  settings.guideReadingEnabled = 1;
  profile.begin(settings, "/.crosspoint/epub_6");
  assert(settings.guideReadingEnabled == 1);
  settings.guideReadingEnabled = 0;
  assert(profile.save(settings));
  profile.restore(settings);
  assert(settings.guideReadingEnabled == 1);
  profile.begin(settings, "/.crosspoint/epub_6");
  assert(settings.guideReadingEnabled == 0);
  assert(testFiles.at("/.crosspoint/book_profiles/epub_6/reader_profile.0") == v2);
  profile.restore(settings);
  // Version 3 has no publisher override. Inherit it, then persist a book-only change.
  std::vector<uint8_t> v3(65);
  memcpy(v3.data(), first.data(), 29);
  v3[4] = 3;
  memcpy(v3.data() + 29, first.data() + 30, 32);
  sum = 2166136261u;
  for (unsigned i = 0; i < 61; ++i) sum = (sum ^ v3[i]) * 16777619u;
  for (unsigned i = 0; i < 4; ++i) v3[61 + i] = static_cast<uint8_t>(sum >> (i * 8));
  testFiles["/.crosspoint/book_profiles/epub_7/reader_profile.0"] = v3;
  settings.publisherPageNumbers = 1;
  profile.begin(settings, "/.crosspoint/epub_7");
  assert(settings.publisherPageNumbers == 1);
  settings.publisherPageNumbers = 0;
  assert(profile.save(settings));
  profile.restore(settings);
  assert(settings.publisherPageNumbers == 1);
  profile.begin(settings, "/.crosspoint/epub_7");
  assert(settings.publisherPageNumbers == 0);
  assert(testFiles.at("/.crosspoint/book_profiles/epub_7/reader_profile.0") == v3);
  profile.restore(settings);
  // A v4 book retains its old preset and boolean indent while new values
  // use the wider v5 ranges and overwrite only the inactive slot.
  auto v4 = first;
  v4[4] = 4;
  v4[12] = 3;  // Old extra-wide preset with an SD font.
  v4[13] = 0;  // No explicit percentage on the old profile.
  v4[23] = 1;  // Previously a force-indent toggle.
  sum = 2166136261u;
  for (unsigned i = 0; i < v4.size() - 4; ++i) sum = (sum ^ v4[i]) * 16777619u;
  for (unsigned i = 0; i < 4; ++i) v4[v4.size() - 4 + i] = static_cast<uint8_t>(sum >> (i * 8));
  testFiles["/.crosspoint/book_profiles/epub_8/reader_profile.0"] = v4;
  profile.begin(settings, "/.crosspoint/epub_8");
  assert(settings.lineSpacing == 4 && settings.lineHeightPercent == 160 && settings.paragraphIndentSpaces == 6);
  settings.lineSpacing = 6;
  settings.lineHeightPercent = 0;
  settings.paragraphIndentSpaces = 5;
  settings.imageRendering = 3;
  assert(profile.save(settings));
  assert(testFiles.at("/.crosspoint/book_profiles/epub_8/reader_profile.0") == v4);
  assert(testFiles.at("/.crosspoint/book_profiles/epub_8/reader_profile.1")[4] == 5);
  profile.restore(settings);
  profile.begin(settings, "/.crosspoint/epub_8");
  assert(settings.lineSpacing == 6 && settings.lineHeightPercent == 0 && settings.paragraphIndentSpaces == 5);
  assert(settings.imageRendering == 3);
  settings.paragraphIndentSpaces = 6;
  assert(profile.save(settings));
  assert(testFiles.at("/.crosspoint/book_profiles/epub_8/reader_profile.0")[4] == 6);
  profile.restore(settings);
  profile.begin(settings, "/.crosspoint/epub_8");
  assert(settings.paragraphIndentSpaces == 6);
  profile.restore(settings);

  // Reset has the same interruption protection as an ordinary profile update.
  // The whole storage map is in memory; no source card or host file is removed.
  const auto resetSeed = testFiles;
  const auto defaults = settings;
  for (unsigned cut = 0; cut < BookReaderSettings::FILE_SIZE; ++cut) {
    testFiles = resetSeed;
    statsWriteBudget = cut;
    assert(!BookReaderSettings::resetForBook(settings, "/.crosspoint/epub_6"));
    statsWriteBudget = SIZE_MAX;
    profile.begin(settings, "/.crosspoint/epub_6");
    assert(settings.guideReadingEnabled == 0);
    profile.restore(settings);
    assert(settings.guideReadingEnabled == defaults.guideReadingEnabled);
  }
  testFiles = resetSeed;
  assert(BookReaderSettings::resetForBook(settings, "/.crosspoint/epub_6"));
  assert(settings.fontPointSize == defaults.fontPointSize && settings.guideReadingEnabled == 1);
  const unsigned afterReset = testStorageWrites;
  assert(BookReaderSettings::resetForBook(settings, "/.crosspoint/epub_6"));
  assert(testStorageWrites == afterReset);
  settings.fontPointSize = 14;
  settings.autoPageTurnSeconds = 75;
  profile.begin(settings, "/.crosspoint/epub_6");
  assert(settings.guideReadingEnabled == 1 && settings.fontPointSize == 14 && settings.autoPageTurnSeconds == 75);
  assert(profile.save(settings) && testStorageWrites == afterReset);
  profile.restore(settings);
  testFiles = resetSeed;
  testFiles["/.crosspoint/book_profiles/epub_6/reader_profile.0"] = future;
  const auto futureSeed = testFiles;
  assert(!BookReaderSettings::resetForBook(settings, "/.crosspoint/epub_6"));
  assert(testFiles == futureSeed);
  assert(!BookReaderSettings::resetForBook(settings, "/.crosspoint/epub_../"));
  assert(testFiles == futureSeed);
  puts("PASS: isolation, inheritance, guide scope, legacy upgrade, truncations, corruptions, future guard, faults, interrupted/idempotent reset");
}
