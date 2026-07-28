#include "DailyReadingHistory.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "GlobalReadingStats.h"

namespace {
constexpr char LOG_TAG[] = "DRHIST";
constexpr char HISTORY_PATH[] = "/.crosspoint/daily_reading.bin";
constexpr char HISTORY_BACKUP_PATH[] = "/.crosspoint/daily_reading.bin.bak";
constexpr char HISTORY_TMP_PATH[] = "/.crosspoint/daily_reading.bin.tmp";
constexpr std::array<uint8_t, 4> FILE_MAGIC = {'C', 'R', 'H', 'M'};
constexpr std::array<uint8_t, 32> ZERO_CHUNK = {};

enum class LoadResult : uint8_t { MissingOrInvalid, Ok, NewerFormat };

uint32_t readUint32LE(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8u) |
         (static_cast<uint32_t>(data[2]) << 16u) | (static_cast<uint32_t>(data[3]) << 24u);
}

void writeUint32LE(uint8_t* data, const uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFu);
  data[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
  data[2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
  data[3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
}

bool verifyFileSize(const char* path) {
  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, path, file)) {
    return false;
  }
  const size_t actualSize = file.fileSize();
  file.close();
  return actualSize == DailyReadingHistory::CURRENT_FILE_SIZE;
}

LoadResult loadFromFile(const char* path, uint32_t& anchorDay, std::array<uint8_t, READING_HISTORY_DAYS>& minutes,
                        bool& hasData) {
  hasData = false;
  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, path, file)) {
    return LoadResult::MissingOrInvalid;
  }

  if (file.fileSize() != DailyReadingHistory::CURRENT_FILE_SIZE) {
    file.close();
    return LoadResult::MissingOrInvalid;
  }

  std::array<uint8_t, DailyReadingHistory::FILE_HEADER_SIZE> header{};
  const int headerRead = file.read(header.data(), header.size());
  if (headerRead != static_cast<int>(header.size()) ||
      !std::equal(FILE_MAGIC.begin(), FILE_MAGIC.end(), header.begin())) {
    file.close();
    return LoadResult::MissingOrInvalid;
  }
  if (header[4] > DailyReadingHistory::CURRENT_FILE_VERSION) {
    file.close();
    return LoadResult::NewerFormat;
  }
  if (header[4] != DailyReadingHistory::CURRENT_FILE_VERSION) {
    file.close();
    return LoadResult::MissingOrInvalid;
  }

  const int historyRead = file.read(minutes.data(), minutes.size());
  file.close();
  if (historyRead != static_cast<int>(minutes.size())) {
    minutes.fill(0);
    return LoadResult::MissingOrInvalid;
  }

  anchorDay = readUint32LE(header.data() + 5);
  hasData = std::any_of(minutes.begin(), minutes.end(), [](const uint8_t storedMinutes) { return storedMinutes != 0; });
  ReadingStatsDate anchorDate;
  if (hasData && !readingStatsDateFromDayIndex(anchorDay, anchorDate)) {
    anchorDay = 0;
    minutes.fill(0);
    hasData = false;
    return LoadResult::MissingOrInvalid;
  }
  if (!hasData) {
    anchorDay = 0;
  }
  return LoadResult::Ok;
}

bool writePayload(FsFile& file, const std::array<uint8_t, READING_HISTORY_DAYS>* minutes) {
  if (minutes != nullptr) {
    return file.write(minutes->data(), minutes->size()) == minutes->size();
  }

  size_t remaining = READING_HISTORY_DAYS;
  while (remaining > 0) {
    const size_t chunkSize = std::min(remaining, ZERO_CHUNK.size());
    if (file.write(ZERO_CHUNK.data(), chunkSize) != chunkSize) {
      return false;
    }
    remaining -= chunkSize;
  }
  return true;
}

bool saveToFile(const uint32_t anchorDay, const std::array<uint8_t, READING_HISTORY_DAYS>* minutes,
                const bool rotateBackup) {
  if (Storage.exists(HISTORY_TMP_PATH) && !Storage.remove(HISTORY_TMP_PATH)) {
    LOG_ERR(LOG_TAG, "Could not remove stale daily history temp file");
    return false;
  }

  FsFile file;
  if (!Storage.openFileForWrite(LOG_TAG, HISTORY_TMP_PATH, file)) {
    LOG_ERR(LOG_TAG, "Could not open daily history temp file");
    return false;
  }

  std::array<uint8_t, DailyReadingHistory::FILE_HEADER_SIZE> header{};
  std::copy(FILE_MAGIC.begin(), FILE_MAGIC.end(), header.begin());
  header[4] = DailyReadingHistory::CURRENT_FILE_VERSION;
  writeUint32LE(header.data() + 5, anchorDay);

  if (file.write(header.data(), header.size()) != header.size() || !writePayload(file, minutes)) {
    LOG_ERR(LOG_TAG, "Short write for daily history temp file");
    file.close();
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }

  file.flush();
  if (!file.sync()) {
    LOG_ERR(LOG_TAG, "Could not sync daily history temp file");
    file.close();
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }
  if (!file.close()) {
    LOG_ERR(LOG_TAG, "Could not close daily history temp file");
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }
  if (!verifyFileSize(HISTORY_TMP_PATH)) {
    LOG_ERR(LOG_TAG, "Daily history temp file has an unexpected size");
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }

  if (rotateBackup) {
    if (Storage.exists(HISTORY_BACKUP_PATH) && !Storage.remove(HISTORY_BACKUP_PATH)) {
      LOG_ERR(LOG_TAG, "Could not remove old daily history backup");
      Storage.remove(HISTORY_TMP_PATH);
      return false;
    }
    if (Storage.exists(HISTORY_PATH) && !Storage.rename(HISTORY_PATH, HISTORY_BACKUP_PATH)) {
      LOG_ERR(LOG_TAG, "Could not rotate daily history backup");
      Storage.remove(HISTORY_TMP_PATH);
      return false;
    }
  } else if (Storage.exists(HISTORY_PATH) && !Storage.remove(HISTORY_PATH)) {
    LOG_ERR(LOG_TAG, "Could not replace daily history");
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }

  if (!Storage.rename(HISTORY_TMP_PATH, HISTORY_PATH)) {
    LOG_ERR(LOG_TAG, "Could not publish daily history");
    if (rotateBackup && Storage.exists(HISTORY_BACKUP_PATH) && !Storage.exists(HISTORY_PATH)) {
      Storage.rename(HISTORY_BACKUP_PATH, HISTORY_PATH);
    }
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }
  return true;
}
}  // namespace

void DailyReadingHistory::load(const GlobalReadingStats& legacyStats) {
  anchorDay_ = 0;
  minutes_.fill(0);
  hasData_ = false;
  dirty_ = false;
  saveBlocked_ = false;

  const LoadResult primary = loadFromFile(HISTORY_PATH, anchorDay_, minutes_, hasData_);
  if (primary == LoadResult::Ok) {
    return;
  }
  if (primary == LoadResult::NewerFormat) {
    saveBlocked_ = true;
    LOG_ERR(LOG_TAG, "Daily history uses a newer format; refusing to overwrite it");
    return;
  }

  const LoadResult backup = loadFromFile(HISTORY_BACKUP_PATH, anchorDay_, minutes_, hasData_);
  if (backup == LoadResult::Ok) {
    dirty_ = true;
    LOG_DBG(LOG_TAG, "Recovered daily reading history from backup");
    return;
  }
  if (backup == LoadResult::NewerFormat) {
    saveBlocked_ = true;
    LOG_ERR(LOG_TAG, "Daily history backup uses a newer format; refusing to overwrite it");
    return;
  }

  seedFromLegacy(legacyStats);
}

bool DailyReadingHistory::save() {
  if (!dirty_) {
    return true;
  }
  if (saveBlocked_) {
    LOG_ERR(LOG_TAG, "Refusing to overwrite newer-format daily history");
    return false;
  }
  if (!saveToFile(anchorDay_, &minutes_, true)) {
    return false;
  }
  dirty_ = false;
  return true;
}

bool DailyReadingHistory::reset() { return saveToFile(0, nullptr, false); }

void DailyReadingHistory::seedFromLegacy(const GlobalReadingStats& legacyStats) {
  anchorDay_ = legacyStats.readingHistoryAnchorDay;
  for (size_t age = 0; age < READING_HISTORY_DAYS; ++age) {
    if (anchorDay_ < age) {
      break;
    }
    if (legacyStats.hasReadingOnDay(anchorDay_ - static_cast<uint32_t>(age))) {
      minutes_[age] = 1;
      hasData_ = true;
      dirty_ = true;
    }
  }
}

void DailyReadingHistory::advanceAnchor(const uint32_t dayIndex) {
  if (!hasData_) {
    anchorDay_ = dayIndex;
    minutes_.fill(0);
    return;
  }
  if (dayIndex <= anchorDay_) {
    return;
  }

  const uint32_t shiftDays = dayIndex - anchorDay_;
  if (shiftDays >= READING_HISTORY_DAYS) {
    minutes_.fill(0);
  } else {
    const size_t shift = static_cast<size_t>(shiftDays);
    std::memmove(minutes_.data() + shift, minutes_.data(), READING_HISTORY_DAYS - shift);
    std::fill_n(minutes_.begin(), shift, 0);
  }
  anchorDay_ = dayIndex;
}

void DailyReadingHistory::addMinutes(const uint32_t dayIndex, const uint32_t minutes) {
  if (minutes == 0) {
    return;
  }

  advanceAnchor(dayIndex);
  if (dayIndex > anchorDay_) {
    return;
  }
  const uint32_t age = anchorDay_ - dayIndex;
  if (age >= READING_HISTORY_DAYS) {
    return;
  }

  uint8_t& storedMinutes = minutes_[static_cast<size_t>(age)];
  const uint32_t updatedMinutes = static_cast<uint32_t>(storedMinutes) + minutes;
  storedMinutes = static_cast<uint8_t>(std::min<uint32_t>(updatedMinutes, UINT8_MAX));
  hasData_ = true;
  dirty_ = true;
}

void DailyReadingHistory::recordReadingSpan(const ReadingStatsDateTime& localStart, const uint32_t seconds) {
  if (!localStart.isValid() || seconds == 0) {
    return;
  }

  ReadingStatsDateTime cursor = localStart;
  uint32_t remaining = seconds;
  while (remaining > 0) {
    const uint32_t secondsSinceMidnight =
        static_cast<uint32_t>(cursor.hour) * 3600u + static_cast<uint32_t>(cursor.minute) * 60u + cursor.second;
    const uint32_t secondsUntilMidnight = 24u * 3600u - secondsSinceMidnight;
    const uint32_t segment = std::min(remaining, secondsUntilMidnight);
    addMinutes(readingStatsDayIndex(cursor.date), (segment + 59u) / 60u);
    remaining -= segment;
    addSecondsToReadingStatsDateTime(cursor, segment);
  }
}

uint8_t DailyReadingHistory::minutesOnDay(const uint32_t dayIndex) const {
  if (!hasData_ || dayIndex > anchorDay_) {
    return 0;
  }
  const uint32_t age = anchorDay_ - dayIndex;
  return age < READING_HISTORY_DAYS ? minutes_[static_cast<size_t>(age)] : 0;
}
