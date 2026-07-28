#define HAL_STORAGE_IMPL
#include "HalStorage.h"

#include <Arduino.h>
#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <HalClock.h>
#include <Logging.h>
#include <Memory.h>
#include <SDCardManager.h>

#include <cassert>

#include "HalSpiBus.h"

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

namespace {
constexpr uint16_t kFallbackYear = 2024;
constexpr uint8_t kFallbackMonth = 1;
constexpr uint8_t kFallbackDay = 1;
constexpr uint8_t kFallbackHour = 0;
constexpr uint8_t kFallbackMinute = 0;
const uint8_t* clockUtcOffsetQ = nullptr;

bool isLeapYear(const uint16_t year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

uint8_t daysInMonth(const uint16_t year, const uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && isLeapYear(year)) return 29;
  return days[month - 1];
}

bool isValidFatDateTime(const uint16_t year, const uint8_t month, const uint8_t day, const uint8_t hour,
                        const uint8_t minute) {
  if (year < 1980 || year > 2107 || hour > 23 || minute > 59) return false;
  const uint8_t monthDays = daysInMonth(year, month);
  return monthDays > 0 && day >= 1 && day <= monthDays;
}

void adjustDateByDays(uint16_t& year, uint8_t& month, uint8_t& day, const int dayDelta) {
  if (dayDelta > 0) {
    const uint8_t monthDays = daysInMonth(year, month);
    if (day < monthDays) {
      day++;
      return;
    }
    day = 1;
    if (month < 12) {
      month++;
    } else {
      month = 1;
      year++;
    }
  } else if (dayDelta < 0) {
    if (day > 1) {
      day--;
      return;
    }
    if (month > 1) {
      month--;
    } else {
      month = 12;
      year--;
    }
    day = daysInMonth(year, month);
  }
}

void setPackedFatDateTime(uint16_t* date, uint16_t* time, const uint16_t year, const uint8_t month, const uint8_t day,
                          const uint8_t hour, const uint8_t minute) {
  *date = FS_DATE(year, month, day);
  *time = FS_TIME(hour, minute, 0);
}

void storageDateTimeCallback(uint16_t* date, uint16_t* time) {
  uint16_t year = kFallbackYear;
  uint8_t month = kFallbackMonth;
  uint8_t day = kFallbackDay;
  uint8_t hour = kFallbackHour;
  uint8_t minute = kFallbackMinute;

  if (halClock.getDateTime(year, month, day, hour, minute) && isValidFatDateTime(year, month, day, hour, minute)) {
    const uint8_t configuredOffsetQ = clockUtcOffsetQ ? *clockUtcOffsetQ : 48;
    const uint8_t offsetQ = configuredOffsetQ > 104 ? 104 : configuredOffsetQ;
    const int offsetQuarterHours = static_cast<int>(offsetQ) - 48;
    int localMinutes = static_cast<int>(hour) * 60 + static_cast<int>(minute) + offsetQuarterHours * 15;
    const int dayDelta = localMinutes < 0 ? -1 : (localMinutes >= 1440 ? 1 : 0);
    localMinutes = ((localMinutes % 1440) + 1440) % 1440;
    adjustDateByDays(year, month, day, dayDelta);
    hour = static_cast<uint8_t>(localMinutes / 60);
    minute = static_cast<uint8_t>(localMinutes % 60);
  }

  if (!isValidFatDateTime(year, month, day, hour, minute)) {
    year = kFallbackYear;
    month = kFallbackMonth;
    day = kFallbackDay;
    hour = kFallbackHour;
    minute = kFallbackMinute;
  }

  setPackedFatDateTime(date, time, year, month, day, hour, minute);
}
}  // namespace

HalStorage::HalStorage() {
  storageMutex = xSemaphoreCreateMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() {
  HalSpiBus::Lock spiLock;
  return SDCard.begin();
}

bool HalStorage::ready() const { return SDCard.ready(); }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock() : spiLock() { xSemaphoreTake(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGive(HalStorage::getInstance().storageMutex); }

 private:
  HalSpiBus::Lock spiLock;
};

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

void HalStorage::installDateTimeCallback(const uint8_t* utcOffsetQuarterHoursBiased) {
  if (!halClock.isAvailable()) return;
  clockUtcOffsetQ = utcOffsetQuarterHoursBiased;
  FsDateTime::setCallback(storageDateTimeCallback);
  LOG_INF("SD", "Installed RTC-backed SD timestamp callback");
}

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
  FsFile file;
};

HalFile::HalFile() = default;

HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

HalFile::~HalFile() { close(); }

HalFile::HalFile(HalFile&&) = default;

HalFile& HalFile::operator=(HalFile&& other) {
  if (this == &other) return *this;
  close();
  impl = std::move(other.impl);
  return *this;
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  FsFile fsFile;
  {
    StorageLock lock;  // ensure thread safety for the duration of this function
    fsFile = SDCard.open(path, oflag);
  }
  if (!fsFile) {
    return HalFile();
  }
  auto impl = makeUniqueNoThrow<HalFile::Impl>(std::move(fsFile));
  if (!impl) {
    LOG_ERR("SD", "OOM: HalFile wrapper for %s (%u free, %u max alloc)", path, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    StorageLock lock;
    fsFile.close();
    return HalFile();
  }
  return HalFile(std::move(impl));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  file.close();
  FsFile fsFile;
  bool ok = false;
  {
    StorageLock lock;  // ensure thread safety for the duration of this function
    ok = SDCard.openFileForRead(moduleName, path, fsFile);
  }
  if (!ok) {
    return false;
  }
  auto impl = makeUniqueNoThrow<HalFile::Impl>(std::move(fsFile));
  if (!impl) {
    LOG_ERR(moduleName, "OOM: HalFile read wrapper for %s (%u free, %u max alloc)", path, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    StorageLock lock;
    fsFile.close();
    return false;
  }
  file = HalFile(std::move(impl));
  return true;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  file.close();
  FsFile fsFile;
  bool ok = false;
  {
    StorageLock lock;  // ensure thread safety for the duration of this function
    ok = SDCard.openFileForWrite(moduleName, path, fsFile);
  }
  if (!ok) {
    return false;
  }
  auto impl = makeUniqueNoThrow<HalFile::Impl>(std::move(fsFile));
  if (!impl) {
    LOG_ERR(moduleName, "OOM: HalFile write wrapper for %s (%u free, %u max alloc)", path, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    StorageLock lock;
    fsFile.close();
    return false;
  }
  file = HalFile(std::move(impl));
  return true;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe, no need to wrap
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
bool HalFile::sync() { HAL_FILE_WRAPPED_CALL(sync, ); }
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() {
  if (!impl) return true;
  HalStorage::StorageLock lock;
  const bool ok = impl->file.close();
  impl.reset();
  return ok;
}
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto fsFile = impl->file.openNextFile();
  if (!fsFile) {
    return HalFile();
  }
  auto childImpl = makeUniqueNoThrow<Impl>(std::move(fsFile));
  if (!childImpl) {
    LOG_ERR("SD", "OOM: HalFile directory entry wrapper (%u free, %u max alloc)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    fsFile.close();
    return HalFile();
  }
  return HalFile(std::move(childImpl));
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
