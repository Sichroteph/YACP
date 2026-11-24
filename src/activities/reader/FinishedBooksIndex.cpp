#include "FinishedBooksIndex.h"

#include <HalStorage.h>
#include <Logging.h>
#include "CheckedStatsIO.h"
#include <FsHelpers.h>

#include <algorithm>
#include <cstring>

#include "RecentBooksStore.h"

namespace {
constexpr char INDEX_PATH[] = "/.crosspoint/finished_books.bin";
constexpr char INDEX_TMP_PATH[] = "/.crosspoint/finished_books.bin.tmp";
constexpr char INDEX_BACKUP_PATH[] = "/.crosspoint/finished_books.bin.bak";
constexpr uint8_t INDEX_VERSION = 3;
constexpr uint8_t START_DATE_INDEX_VERSION = 2;
constexpr uint8_t LEGACY_INDEX_VERSION = 1;
constexpr uint16_t MAX_TITLE_BYTES = 160;
constexpr uint16_t MAX_AUTHOR_BYTES = 120;
constexpr char MAGIC[] = {'C', 'P', 'F', 'B'};
bool blockSave = false;
bool recoveredBackup = false;
bool futureFormat = false;
bool loadPath(const char* path, std::vector<FinishedBookEntry>& entries);

size_t boundedUtf8Size(const std::string& text, size_t limit) {
  if (text.size() <= limit) return text.size();
  while (limit > 0 && (static_cast<uint8_t>(text[limit]) & 0xc0) == 0x80) --limit;
  return limit;
}

uint64_t pathKey(const std::string& path) {
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : path) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

bool isNewer(const FinishedBookEntry& a, const FinishedBookEntry& b) {
  if (a.finishedDate.isValid() != b.finishedDate.isValid()) {
    return a.finishedDate.isValid();
  }
  if (a.finishedDate.isValid()) {
    const int dateOrder = compareReadingStatsDate(a.finishedDate, b.finishedDate);
    if (dateOrder != 0) {
      return dateOrder > 0;
    }
  }
  return a.title < b.title;
}

bool writeIndex(const std::vector<FinishedBookEntry>& entries) {
  if (blockSave) {
    LOG_ERR("FBI", "Refusing to overwrite unreadable finished-books data");
    return false;
  }
  if (Storage.exists(INDEX_TMP_PATH) && !Storage.remove(INDEX_TMP_PATH)) {
    LOG_ERR("FBI", "Could not remove stale finished-books temp file");
    return false;
  }

  HalFile file;
  if (!Storage.openFileForWrite("FBI", INDEX_TMP_PATH, file)) {
    LOG_ERR("FBI", "Could not open finished-books temp file");
    return false;
  }

  const uint8_t count = static_cast<uint8_t>(std::min(entries.size(), FinishedBooksIndex::MAX_ENTRIES));
  bool ok = file.write(reinterpret_cast<const uint8_t*>(MAGIC), sizeof(MAGIC)) == sizeof(MAGIC) &&
            statsio::writePod(file, INDEX_VERSION) && statsio::writePod(file, count);
  const uint16_t reserved = 0;
  ok = ok && statsio::writePod(file, reserved);

  for (size_t i = 0; ok && i < count; ++i) {
    const auto& entry = entries[i];
    const uint16_t titleLength =
        static_cast<uint16_t>(boundedUtf8Size(entry.title, MAX_TITLE_BYTES));
    const uint16_t authorLength =
        static_cast<uint16_t>(boundedUtf8Size(entry.author, MAX_AUTHOR_BYTES));
    ok = statsio::writePod(file, entry.pathKey) &&
         statsio::writePod(file, entry.totalReadingSeconds) &&
         statsio::writePod(file, entry.startDate.year) &&
         statsio::writePod(file, entry.startDate.month) &&
         statsio::writePod(file, entry.startDate.day) &&
         statsio::writePod(file, entry.finishedDate.year) &&
         statsio::writePod(file, entry.finishedDate.month) &&
         statsio::writePod(file, entry.finishedDate.day) &&
         statsio::writePod(file, titleLength) &&
         file.write(reinterpret_cast<const uint8_t*>(entry.title.data()), titleLength) == titleLength &&
         statsio::writePod(file, authorLength) &&
         file.write(reinterpret_cast<const uint8_t*>(entry.author.data()), authorLength) == authorLength;
  }

  file.flush();
  const bool closed = file.close();
  ok = ok && closed;
  if (!ok) {
    LOG_ERR("FBI", "Could not finish finished-books temp file");
    Storage.remove(INDEX_TMP_PATH);
    return false;
  }

  // Verification is outside the reading loop. At most 32 bounded entries are
  // allocated while validating a completion update, never per page.
  std::vector<FinishedBookEntry> check;
  if (!loadPath(INDEX_TMP_PATH, check) || check.size() != count) {
    LOG_ERR("FBI", "Finished-books temporary file failed verification");
    return false;
  }

  if (!recoveredBackup && Storage.exists(INDEX_BACKUP_PATH) && !Storage.remove(INDEX_BACKUP_PATH)) {
    LOG_ERR("FBI", "Could not replace finished-books backup");
    Storage.remove(INDEX_TMP_PATH);
    return false;
  }
  if (Storage.exists(INDEX_PATH) &&
      !(recoveredBackup ? Storage.remove(INDEX_PATH) : Storage.rename(INDEX_PATH, INDEX_BACKUP_PATH))) {
    LOG_ERR("FBI", "Could not rotate finished-books index");
    Storage.remove(INDEX_TMP_PATH);
    return false;
  }
  if (!Storage.rename(INDEX_TMP_PATH, INDEX_PATH)) {
    LOG_ERR("FBI", "Could not install finished-books index");
    if (Storage.exists(INDEX_BACKUP_PATH) && !Storage.exists(INDEX_PATH)) {
      Storage.rename(INDEX_BACKUP_PATH, INDEX_PATH);
    }
    Storage.remove(INDEX_TMP_PATH);
    return false;
  }
  recoveredBackup = false;
  return true;
}

bool loadPath(const char* path, std::vector<FinishedBookEntry>& entries) {
  HalFile file;
  if (!Storage.openFileForRead("FBI", path, file)) {
    return false;
  }

  char magic[sizeof(MAGIC)] = {};
  uint8_t version = 0;
  uint8_t count = 0;
  uint16_t reserved = 0;
  bool ok = file.read(reinterpret_cast<uint8_t*>(magic), sizeof(magic)) == sizeof(magic) &&
            statsio::readPod(file, version) && statsio::readPod(file, count) &&
            statsio::readPod(file, reserved) && std::memcmp(magic, MAGIC, sizeof(MAGIC)) == 0 &&
            (version == INDEX_VERSION || version == START_DATE_INDEX_VERSION || version == LEGACY_INDEX_VERSION) &&
            count <= FinishedBooksIndex::MAX_ENTRIES;
  if (std::memcmp(magic, MAGIC, sizeof(MAGIC)) == 0 && version > INDEX_VERSION) futureFormat = true;

  if (ok) {
    // Bounded activity-lifetime allocation: at most 32 entries, loaded only on
    // the statistics screen or during one completion update.
    entries.reserve(count);
  }
  for (uint8_t i = 0; ok && i < count; ++i) {
    FinishedBookEntry entry;
    uint16_t titleLength = 0;
    ok = statsio::readPod(file, entry.pathKey) && statsio::readPod(file, entry.totalReadingSeconds);
    if (ok && version >= START_DATE_INDEX_VERSION) {
      ok = statsio::readPod(file, entry.startDate.year) &&
           statsio::readPod(file, entry.startDate.month) &&
           statsio::readPod(file, entry.startDate.day);
    }
    ok = ok && statsio::readPod(file, entry.finishedDate.year) &&
         statsio::readPod(file, entry.finishedDate.month) &&
         statsio::readPod(file, entry.finishedDate.day) && statsio::readPod(file, titleLength) &&
         titleLength <= MAX_TITLE_BYTES;
    if (!ok) {
      break;
    }
    entry.title.resize(titleLength);
    ok = titleLength == 0 || file.read(&entry.title[0], titleLength) == titleLength;
    if (ok && version >= INDEX_VERSION) {
      uint16_t authorLength = 0;
      ok = statsio::readPod(file, authorLength) && authorLength <= MAX_AUTHOR_BYTES;
      if (ok) {
        entry.author.resize(authorLength);
        ok = authorLength == 0 || file.read(&entry.author[0], authorLength) == authorLength;
      }
    }
    if (ok) {
      entries.push_back(std::move(entry));
    }
  }
  ok = ok && file.position() == file.fileSize();
  file.close();
  if (!ok) {
    entries.clear();
  }
  return ok;
}
}  // namespace

std::vector<FinishedBookEntry> FinishedBooksIndex::load() {
  blockSave = false;
  recoveredBackup = false;
  futureFormat = false;
  std::vector<FinishedBookEntry> entries;
  const bool primaryLoaded = loadPath(INDEX_PATH, entries);
  if (futureFormat) {
    blockSave = true;
    LOG_ERR("FBI", "Newer finished-books index found; preserving it");
    return entries;
  }
  bool loaded = primaryLoaded;
  if (!primaryLoaded && Storage.exists(INDEX_BACKUP_PATH)) {
    LOG_ERR("FBI", "Primary finished-books index invalid, trying backup");
    loaded = loadPath(INDEX_BACKUP_PATH, entries);
    recoveredBackup = loaded;
  }
  if (futureFormat || (!loaded && (Storage.exists(INDEX_PATH) || Storage.exists(INDEX_BACKUP_PATH)))) {
    blockSave = true;
    LOG_ERR("FBI", "Finished-books index cannot be safely replaced");
  }
  if (!loaded && !Storage.exists(INDEX_PATH) && !Storage.exists(INDEX_BACKUP_PATH)) {
    const auto& recentBooks = RECENT_BOOKS.getBooks();
    entries.reserve(std::min(recentBooks.size(), MAX_ENTRIES));
    for (const auto& book : recentBooks) {
      std::string cachePath;
      if (FsHelpers::hasEpubExtension(book.path)) {
        cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(book.path));
      } else if (FsHelpers::hasXtcExtension(book.path)) {
        cachePath = "/.crosspoint/xtc_" + std::to_string(std::hash<std::string>{}(book.path));
      } else {
        continue;
      }
      const BookReadingStats stats = BookReadingStats::load(cachePath);
      if (stats.isCompleted) {
        entries.push_back(
            {pathKey(book.path), book.title, book.author, stats.totalReadingSeconds, stats.startDate,
             stats.finishedDate});
      }
      if (entries.size() >= MAX_ENTRIES) {
        break;
      }
    }
    std::sort(entries.begin(), entries.end(), isNewer);
    // Merely opening the history does not write to storage. The next explicit
    // completion update persists these recovered entries together with its edit.
  }
  std::sort(entries.begin(), entries.end(), isNewer);
  return entries;
}

bool FinishedBooksIndex::record(const std::string& path, const std::string& title, const std::string& author,
                                const BookReadingStats& stats) {
  return recordCanonical(path, std::string{}, title, author, stats);
}

bool FinishedBooksIndex::recordCanonical(const std::string& bookPath, const std::string& legacyCachePath,
                                         const std::string& title, const std::string& author,
                                         const BookReadingStats& stats) {
  const bool needsInitialWrite = !Storage.exists(INDEX_PATH) && !Storage.exists(INDEX_BACKUP_PATH);
  auto entries = load();
  if (blockSave) return false;
  entries.reserve(MAX_ENTRIES + 1);
  const uint64_t key = pathKey(bookPath);
  bool changed = false;

  if (!legacyCachePath.empty() && legacyCachePath != bookPath) {
    const uint64_t legacyKey = pathKey(legacyCachePath);
    const size_t previousSize = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [legacyKey](const FinishedBookEntry& entry) {
                                   return entry.pathKey == legacyKey;
                                 }),
                  entries.end());
    changed = entries.size() != previousSize;
  }

  auto it = std::find_if(entries.begin(), entries.end(),
                         [key](const FinishedBookEntry& entry) { return entry.pathKey == key; });

  if (!stats.isCompleted) {
    if (it == entries.end()) {
      return changed || needsInitialWrite ? writeIndex(entries) : true;
    }
    entries.erase(it);
    return writeIndex(entries);
  }

  const FinishedBookEntry updated{key, title.substr(0, boundedUtf8Size(title, MAX_TITLE_BYTES)),
                                   author.substr(0, boundedUtf8Size(author, MAX_AUTHOR_BYTES)),
                                   stats.totalReadingSeconds, stats.startDate, stats.finishedDate};
  if (it != entries.end()) {
    if (it->pathKey == updated.pathKey && it->title == updated.title &&
        it->author == updated.author &&
        it->totalReadingSeconds == updated.totalReadingSeconds &&
        compareReadingStatsDate(it->startDate, updated.startDate) == 0 &&
        compareReadingStatsDate(it->finishedDate, updated.finishedDate) == 0) {
      return changed || needsInitialWrite ? writeIndex(entries) : true;
    }
    *it = updated;
  } else {
    entries.push_back(updated);
  }

  std::sort(entries.begin(), entries.end(), isNewer);
  if (entries.size() > MAX_ENTRIES) {
    entries.resize(MAX_ENTRIES);
  }
  return writeIndex(entries);
}

bool FinishedBooksIndex::canMigratePath(const std::string& oldPath, const std::string& newPath) {
  const bool needsSnapshot = !Storage.exists(INDEX_PATH) && !Storage.exists(INDEX_BACKUP_PATH);
  const auto entries = load();
  if (blockSave) return false;
  const auto oldKey = pathKey(oldPath), newKey = pathKey(newPath);
  bool source = false, target = false;
  for (const auto& entry : entries) {
    source |= entry.pathKey == oldKey;
    target |= entry.pathKey == newKey;
  }
  if (target && oldKey != newKey) return false;
  // load() can reconstruct old history from recent-book cache paths. Preserve
  // that reconstruction before moving the cache out from under those paths.
  return !needsSnapshot || !source || writeIndex(entries);
}

bool FinishedBooksIndex::migratePath(const std::string& oldPath, const std::string& newPath) {
  if (oldPath == newPath) {
    return true;
  }

  auto entries = load();
  if (blockSave) return false;
  const uint64_t oldKey = pathKey(oldPath);
  const uint64_t newKey = pathKey(newPath);
  auto oldEntry = std::find_if(entries.begin(), entries.end(),
                               [oldKey](const FinishedBookEntry& entry) { return entry.pathKey == oldKey; });
  if (oldEntry == entries.end()) {
    return true;
  }

  const auto duplicate = std::find_if(entries.begin(), entries.end(),
                                      [newKey](const FinishedBookEntry& entry) { return entry.pathKey == newKey; });
  if (duplicate != entries.end() && duplicate != oldEntry) {
    LOG_ERR("FBI", "Destination already has finished-book history; neither entry replaced");
    return false;
  }

  oldEntry->pathKey = newKey;
  return writeIndex(entries);
}
