#include "StatsBackup.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "GlobalReadingStats.h"
#include "ReadingStatsUtils.h"

namespace {
constexpr char TAG[] = "SBACK";
constexpr char DIRECTORY[] = "/.crossink-stats-backup";
constexpr char SOURCE[] = "/.crosspoint/global_stats.bin";
constexpr unsigned KEEP = 7;

bool counterFromName(const char* name, uint32_t& index) {
  if (strncmp(name, "stats_backup_", 13) != 0) return false;
  const char* p = name + 13;
  if (*p < '0' || *p > '9') return false;
  uint32_t value = 0;
  while (*p >= '0' && *p <= '9') {
    const unsigned digit = *p++ - '0';
    if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
    value = value * 10 + digit;
  }
  if (strcmp(p, ".bin") != 0 || value == 0) return false;
  index = value;
  return true;
}

bool archiveName(const char* name) {
  uint32_t index = 0;
  if (counterFromName(name, index)) return true;
  const size_t size = strlen(name);
  if ((size != 20 && size != 25) || strncmp(name, "stats_", 6) || strcmp(name + size - 4, ".bin")) return false;
  for (size_t i = 6; i < size - 4; ++i) {
    if (i == 10 || i == 13) { if (name[i] != '-') return false; }
    else if (i == 16) { if (name[i] != '_') return false; }
    else if (name[i] < '0' || name[i] > '9') return false;
  }
  return true;
}

bool makePath(const char* name, char* path, size_t capacity) {
  const int count = snprintf(path, capacity, "%s/%s", DIRECTORY, name);
  return count > 0 && static_cast<size_t>(count) < capacity;
}

bool olderName(const char* lhs, const char* rhs) {
  uint32_t left = 0, right = 0;
  if (counterFromName(lhs, left) && counterFromName(rhs, right)) return left < right;
  return strcmp(lhs, rhs) < 0;
}

bool chooseName(bool manual, char* name, size_t capacity) {
  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now)) {
    const int count = manual
        ? snprintf(name, capacity, "stats_%04u-%02u-%02u_%02u%02u.bin", now.date.year, now.date.month,
                   now.date.day, now.hour, now.minute)
        : snprintf(name, capacity, "stats_%04u-%02u-%02u.bin", now.date.year, now.date.month, now.date.day);
    return count > 0 && static_cast<size_t>(count) < capacity;
  }
  uint32_t highest = 0;
  HalFile dir = Storage.open(DIRECTORY);
  if (dir && !dir.isDirectory()) { dir.close(); return false; }
  if (dir) {
    char candidate[64];
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      const bool isDirectory = entry.isDirectory();
      const size_t size = entry.getName(candidate, sizeof(candidate));
      entry.close();
      uint32_t index = 0;
      if (!isDirectory && size > 0 && size < sizeof(candidate) && counterFromName(candidate, index)) {
        highest = std::max(highest, index);
      }
    }
    dir.close();
  }
  if (highest == std::numeric_limits<uint32_t>::max()) return false;
  const int count = snprintf(name, capacity, "stats_backup_%03lu.bin", static_cast<unsigned long>(highest + 1));
  return count > 0 && static_cast<size_t>(count) < capacity;
}

bool readSource(uint8_t* data, size_t& size) {
  HalFile file;
  if (!Storage.openFileForRead(TAG, SOURCE, file)) return false;
  size = file.fileSize();
  const bool sizeValid = size == 13 || size == 17 || size == GlobalReadingStats::CURRENT_FILE_SIZE;
  const bool read = sizeValid && file.read(data, size) == static_cast<int>(size);
  const bool closed = file.close();
  return read && closed && ((data[0] == 1 && size == 13) || (data[0] == 2 && size == 17) ||
                           (data[0] == GlobalReadingStats::CURRENT_FILE_VERSION &&
                            size == GlobalReadingStats::CURRENT_FILE_SIZE));
}

bool matches(const char* path, const uint8_t* data, size_t size) {
  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) return false;
  bool equal = file.fileSize() == size;
  uint8_t chunk[48];
  for (size_t offset = 0; equal && offset < size;) {
    const size_t count = std::min(sizeof(chunk), size - offset);
    equal = file.read(chunk, count) == static_cast<int>(count) && memcmp(chunk, data + offset, count) == 0;
    offset += count;
  }
  return file.close() && equal;
}

bool publish(const char* path, const uint8_t* data, size_t size) {
  char temporary[104], previous[104];
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  snprintf(previous, sizeof(previous), "%s.previous", path);
  // Recover a power interruption between the two promotion renames.
  if (!Storage.exists(path) && Storage.exists(previous) && !Storage.rename(previous, path)) return false;
  HalFile file;
  if (!Storage.openFileForWrite(TAG, temporary, file)) return false;
  const bool written = file.write(data, size) == size;
  file.flush();
  const bool closed = file.close();
  if (!written || !closed || !matches(temporary, data, size)) return false;
  if (Storage.exists(previous) && !Storage.remove(previous)) return false;
  const bool hadOriginal = Storage.exists(path);
  if (hadOriginal && !Storage.rename(path, previous)) return false;
  if (!Storage.rename(temporary, path)) {
    if (hadOriginal && !Storage.rename(previous, path)) LOG_ERR(TAG, "Previous backup retained for recovery");
    return false;
  }
  if (hadOriginal && !Storage.remove(previous)) LOG_ERR(TAG, "Backup saved; prior copy cleanup pending");
  return true;
}

bool prune(const char* protectedName) {
  // Seven tiny archives normally means one pass. Keep fixed memory even when a
  // copied card contains thousands of archives, and never prune the new copy.
  for (;;) {
    HalFile dir = Storage.open(DIRECTORY);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return false; }
    unsigned count = 0;
    char oldest[64] = {}, name[64];
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      const bool isDirectory = entry.isDirectory();
      const size_t size = entry.getName(name, sizeof(name));
      entry.close();
      if (isDirectory || size == 0 || size >= sizeof(name) || !archiveName(name)) continue;
      ++count;
      if (strcmp(name, protectedName) != 0 && (!oldest[0] || olderName(name, oldest))) {
        memcpy(oldest, name, size + 1);
      }
    }
    dir.close();
    if (count <= KEEP) return true;
    char path[96];
    if (!oldest[0] || !makePath(oldest, path, sizeof(path)) || !Storage.remove(path)) return false;
  }
}
}  // namespace

bool backupGlobalStats(const bool manual, char* outName, const size_t outSize) {
  // Bounded maintenance-only stack storage: 159 data bytes plus 160 name/path
  // bytes. Called after reader exit, never by the per-page render path.
  uint8_t data[GlobalReadingStats::CURRENT_FILE_SIZE];
  char name[64], path[96];
  size_t size = 0;
  if (outName && outSize) outName[0] = '\0';
  if (!readSource(data, size) || !Storage.ensureDirectoryExists(DIRECTORY) ||
      !chooseName(manual, name, sizeof(name)) || !makePath(name, path, sizeof(path))) {
    LOG_ERR(TAG, "Could not read supported statistics or choose archive path");
    return false;
  }
  if (!matches(path, data, size) && !publish(path, data, size)) {
    LOG_ERR(TAG, "Statistics archive write or verification failed");
    return false;
  }
  if (!prune(name)) LOG_ERR(TAG, "Statistics saved; archive pruning incomplete");
  if (outName && outSize) snprintf(outName, outSize, "%s", name);
  LOG_DBG(TAG, "Statistics backup verified: %s", name);
  return true;
}
