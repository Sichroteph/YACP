#include "FinishedBooksIndex.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <Xtc.h>

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
  if (Storage.exists(INDEX_TMP_PATH) && !Storage.remove(INDEX_TMP_PATH)) {
    LOG_ERR("FBI", "Could not remove stale finished-books temp file");
    return false;
  }

  FsFile file;
  if (!Storage.openFileForWrite("FBI", INDEX_TMP_PATH, file)) {
    LOG_ERR("FBI", "Could not open finished-books temp file");
    return false;
  }

  const uint8_t count = static_cast<uint8_t>(std::min(entries.size(), FinishedBooksIndex::MAX_ENTRIES));
  bool ok = file.write(reinterpret_cast<const uint8_t*>(MAGIC), sizeof(MAGIC)) == sizeof(MAGIC) &&
            serialization::tryWritePod(file, INDEX_VERSION) && serialization::tryWritePod(file, count);
  const uint16_t reserved = 0;
  ok = ok && serialization::tryWritePod(file, reserved);

  for (size_t i = 0; ok && i < count; ++i) {
    const auto& entry = entries[i];
    const uint16_t titleLength =
        static_cast<uint16_t>(std::min(entry.title.size(), static_cast<size_t>(MAX_TITLE_BYTES)));
    const uint16_t authorLength =
        static_cast<uint16_t>(std::min(entry.author.size(), static_cast<size_t>(MAX_AUTHOR_BYTES)));
    ok = serialization::tryWritePod(file, entry.pathKey) &&
         serialization::tryWritePod(file, entry.totalReadingSeconds) &&
         serialization::tryWritePod(file, entry.startDate.year) &&
         serialization::tryWritePod(file, entry.startDate.month) &&
         serialization::tryWritePod(file, entry.startDate.day) &&
         serialization::tryWritePod(file, entry.finishedDate.year) &&
         serialization::tryWritePod(file, entry.finishedDate.month) &&
         serialization::tryWritePod(file, entry.finishedDate.day) &&
         serialization::tryWritePod(file, titleLength) &&
         file.write(reinterpret_cast<const uint8_t*>(entry.title.data()), titleLength) == titleLength &&
         serialization::tryWritePod(file, authorLength) &&
         file.write(reinterpret_cast<const uint8_t*>(entry.author.data()), authorLength) == authorLength;
  }

  file.flush();
  const bool synced = file.sync();
  const bool closed = file.close();
  ok = ok && synced && closed;
  if (!ok) {
    LOG_ERR("FBI", "Could not finish finished-books temp file");
    Storage.remove(INDEX_TMP_PATH);
    return false;
  }

  if (Storage.exists(INDEX_BACKUP_PATH) && !Storage.remove(INDEX_BACKUP_PATH)) {
    LOG_ERR("FBI", "Could not replace finished-books backup");
    Storage.remove(INDEX_TMP_PATH);
    return false;
  }
  if (Storage.exists(INDEX_PATH) && !Storage.rename(INDEX_PATH, INDEX_BACKUP_PATH)) {
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
  return true;
}

bool loadPath(const char* path, std::vector<FinishedBookEntry>& entries) {
  FsFile file;
  if (!Storage.openFileForRead("FBI", path, file)) {
    return false;
  }

  char magic[sizeof(MAGIC)] = {};
  uint8_t version = 0;
  uint8_t count = 0;
  uint16_t reserved = 0;
  bool ok = file.read(reinterpret_cast<uint8_t*>(magic), sizeof(magic)) == sizeof(magic) &&
            serialization::tryReadPod(file, version) && serialization::tryReadPod(file, count) &&
            serialization::tryReadPod(file, reserved) && std::memcmp(magic, MAGIC, sizeof(MAGIC)) == 0 &&
            (version == INDEX_VERSION || version == START_DATE_INDEX_VERSION || version == LEGACY_INDEX_VERSION) &&
            count <= FinishedBooksIndex::MAX_ENTRIES;

  if (ok) {
    // Bounded activity-lifetime allocation: at most 32 entries, loaded only on
    // the statistics screen or during one completion update.
    entries.reserve(count);
  }
  for (uint8_t i = 0; ok && i < count; ++i) {
    FinishedBookEntry entry;
    uint16_t titleLength = 0;
    ok = serialization::tryReadPod(file, entry.pathKey) && serialization::tryReadPod(file, entry.totalReadingSeconds);
    if (ok && version >= START_DATE_INDEX_VERSION) {
      ok = serialization::tryReadPod(file, entry.startDate.year) &&
           serialization::tryReadPod(file, entry.startDate.month) &&
           serialization::tryReadPod(file, entry.startDate.day);
    }
    ok = ok && serialization::tryReadPod(file, entry.finishedDate.year) &&
         serialization::tryReadPod(file, entry.finishedDate.month) &&
         serialization::tryReadPod(file, entry.finishedDate.day) && serialization::tryReadPod(file, titleLength) &&
         titleLength <= MAX_TITLE_BYTES;
    if (!ok) {
      break;
    }
    entry.title.resize(titleLength);
    ok = titleLength == 0 || file.read(&entry.title[0], titleLength) == titleLength;
    if (ok && version >= INDEX_VERSION) {
      uint16_t authorLength = 0;
      ok = serialization::tryReadPod(file, authorLength) && authorLength <= MAX_AUTHOR_BYTES;
      if (ok) {
        entry.author.resize(authorLength);
        ok = authorLength == 0 || file.read(&entry.author[0], authorLength) == authorLength;
      }
    }
    if (ok && !entry.title.empty()) {
      entries.push_back(std::move(entry));
    }
  }
  file.close();
  if (!ok) {
    entries.clear();
  }
  return ok;
}
}  // namespace

std::vector<FinishedBookEntry> FinishedBooksIndex::load() {
  std::vector<FinishedBookEntry> entries;
  const bool primaryLoaded = loadPath(INDEX_PATH, entries);
  bool loaded = primaryLoaded;
  if (!primaryLoaded && Storage.exists(INDEX_BACKUP_PATH)) {
    LOG_ERR("FBI", "Primary finished-books index invalid, trying backup");
    loaded = loadPath(INDEX_BACKUP_PATH, entries);
  }
  if (!loaded && !Storage.exists(INDEX_PATH) && !Storage.exists(INDEX_BACKUP_PATH)) {
    const auto& recentBooks = RECENT_BOOKS.getBooks();
    entries.reserve(std::min(recentBooks.size(), MAX_ENTRIES));
    for (const auto& book : recentBooks) {
      std::string cachePath;
      if (FsHelpers::hasEpubExtension(book.path)) {
        cachePath = Epub::cachePathForFilePath(book.path, "/.crosspoint");
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
    if (!writeIndex(entries)) {
      LOG_ERR("FBI", "Could not persist one-time recent-books migration");
    }
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
  auto entries = load();
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
      return changed ? writeIndex(entries) : true;
    }
    entries.erase(it);
    return writeIndex(entries);
  }

  const FinishedBookEntry updated{key, title, author, stats.totalReadingSeconds, stats.startDate, stats.finishedDate};
  if (it != entries.end()) {
    if (it->pathKey == updated.pathKey && it->title == updated.title &&
        it->author == updated.author &&
        it->totalReadingSeconds == updated.totalReadingSeconds &&
        compareReadingStatsDate(it->startDate, updated.startDate) == 0 &&
        compareReadingStatsDate(it->finishedDate, updated.finishedDate) == 0) {
      return changed ? writeIndex(entries) : true;
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

bool FinishedBooksIndex::migratePath(const std::string& oldPath, const std::string& newPath) {
  if (oldPath == newPath) {
    return true;
  }

  auto entries = load();
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
    entries.erase(duplicate);
    oldEntry = std::find_if(entries.begin(), entries.end(),
                            [oldKey](const FinishedBookEntry& entry) { return entry.pathKey == oldKey; });
  }

  oldEntry->pathKey = newKey;
  return writeIndex(entries);
}
