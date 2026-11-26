#include "X3WakePage.h"

#include <CredentialIntegrity.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/boot_sleep/SleepActivity.h"

namespace x3_wake {
namespace {

using reader_refresh::WakeTransition;

constexpr char RECORD[] = "/.crosspoint/x3-wake-v1.bin";
constexpr char PENDING[] = "/.crosspoint/x3-wake-v1.tmp";
constexpr uint8_t SCHEMA = 1;
constexpr uint8_t CLICK_TO_WAKE = 1;
constexpr uint8_t FADING_FIX = 2;
constexpr uint8_t PREVIEW_ONLY = 4;

struct Header {
  uint64_t book;
  uint32_t buildAndDevice;
  uint32_t frameCrc;
  uint32_t readerData;
  uint32_t offset;
  uint16_t chapter;
  uint16_t page;
  uint8_t orientation;
  uint8_t transition;
  uint8_t flags;
  uint8_t schema;
};
static_assert(sizeof(Header) == 32);

bool binding(uint32_t& result) {
  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) != 0) return false;
  result = credential_integrity::crc32(CROSSPOINT_VERSION) ^
           credential_integrity::crc32({reinterpret_cast<const char*>(mac), sizeof(mac)});
  return true;
}

size_t moonBytes(WakeTransition transition) {
  return transition == WakeTransition::Moon ? SleepActivity::MOON_BACKGROUND_BYTES : 0;
}

uint32_t checksum(const Header& header, const uint8_t* moon, size_t size) {
  return credential_integrity::crc32({reinterpret_cast<const char*>(&header), sizeof(header)}) ^
         credential_integrity::crc32({reinterpret_cast<const char*>(moon), size});
}

bool hashFile(const char* path, uint32_t& hash, bool optional = false) {
  if (optional && !Storage.exists(path)) {
    hash *= 16777619u;
    return true;
  }
  HalFile file;
  if (!Storage.openFileForRead("SLP", path, file)) return false;
  size_t remaining = file.fileSize();
  if (remaining == 0 || remaining > 64 * 1024) return false;
  hash = (hash ^ remaining) * 16777619u;
  uint8_t bytes[128];
  while (remaining != 0) {
    const size_t count = std::min(remaining, sizeof(bytes));
    if (file.read(bytes, count) != static_cast<int>(count)) return false;
    for (size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    remaining -= count;
  }
  return file.close();
}

bool readerData(const reader_refresh::WakeTextPage& page, uint32_t& result) {
  // Progress is flushed by the reader before publishing the snapshot. Verify
  // its native chapter, page and visible-text offset before touching the panel.
  char path[80];
  const auto book = static_cast<unsigned long long>(page.book);
  snprintf(path, sizeof(path), "/.crosspoint/epub_%llu/progress.bin", book);
  HalFile file;
  if (!Storage.openFileForRead("SLP", path, file)) return false;
  uint8_t bytes[10];
  const bool complete = file.fileSize() == sizeof(bytes) && file.read(bytes, sizeof(bytes)) == sizeof(bytes);
  if (!file.close() || !complete) return false;
  const uint16_t chapter = bytes[0] | (bytes[1] << 8);
  const uint16_t number = bytes[2] | (bytes[3] << 8);
  const uint32_t offset = static_cast<uint32_t>(bytes[6]) | (static_cast<uint32_t>(bytes[7]) << 8) |
                          (static_cast<uint32_t>(bytes[8]) << 16) | (static_cast<uint32_t>(bytes[9]) << 24);
  if (page.chapter != chapter || page.page != number || page.offset != offset) return false;

  // Read the stored bytes only. The normal loaders still run after migration.
  uint32_t hash = 2166136261u;
  if (!hashFile(CrossPointState::getFilePath(), hash) || !hashFile(CrossPointSettings::getFilePath(), hash)) return false;
  for (unsigned slot = 0; slot < 2; ++slot) {
    snprintf(path, sizeof(path), "/.crosspoint/book_profiles/epub_%llu/reader_profile.%u", book, slot);
    if (!hashFile(path, hash, true)) return false;
  }
  result = hash;
  return true;
}

}  // namespace

bool save(const Snapshot& snapshot, const uint8_t* moonBackground) {
  const auto& page = snapshot.page;
  Header header{page.book, 0, snapshot.frameCrc, 0, page.offset, page.chapter, page.page, page.orientation,
                static_cast<uint8_t>(page.transition),
                static_cast<uint8_t>((snapshot.clickToWake ? CLICK_TO_WAKE : 0) | (snapshot.fadingFix ? FADING_FIX : 0)),
                SCHEMA};
  if (!binding(header.buildAndDevice)) return false;
  if (!readerData(page, header.readerData)) {
    if (page.transition != WakeTransition::Cover) return false;
    header.flags |= PREVIEW_ONLY;
  }
  const size_t size = moonBytes(page.transition);
  const uint32_t crc = checksum(header, moonBackground, size);
  HalFile file;
  if (!Storage.openFileForWrite("SLP", PENDING, file)) return false;
  const bool complete = file.write(&header, sizeof(header)) == sizeof(header) &&
                        (size == 0 || file.write(moonBackground, size) == size) &&
                        file.write(&crc, sizeof(crc)) == sizeof(crc);
  return file.close() && complete && Storage.rename(PENDING, RECORD);
}

Snapshot consume(bool allowPreview, uint8_t* moonBackground) {
  if (!Storage.exists(RECORD)) return {};
  Header header{};
  uint32_t crc = 0;
  bool valid = false;
  if (allowPreview) {
    HalFile file;
    if (Storage.openFileForRead("SLP", RECORD, file)) {
      if (file.read(&header, sizeof(header)) == sizeof(header)) {
        const size_t size = moonBytes(static_cast<WakeTransition>(header.transition));
        valid = file.fileSize() == sizeof(header) + size + sizeof(crc) &&
                (size == 0 || file.read(moonBackground, size) == static_cast<int>(size)) &&
                file.read(&crc, sizeof(crc)) == sizeof(crc) && checksum(header, moonBackground, size) == crc;
      }
      valid = file.close() && valid;
    }
  }
  // Consume even on a maintenance boot so stale pixels cannot be replayed
  // after a subsequent restart. A failed unlink disqualifies the preview.
  if (!Storage.remove(RECORD)) {
    LOG_ERR("SLP", "Could not consume X3 wake record");
    return {};
  }
  const auto transition = static_cast<WakeTransition>(header.transition);
  uint32_t expectedBinding;
  if (!valid || header.schema != SCHEMA || (header.flags & ~(CLICK_TO_WAKE | FADING_FIX | PREVIEW_ONLY)) != 0 ||
      header.orientation > GfxRenderer::LandscapeCounterClockwise ||
      (transition != WakeTransition::Moon && transition != WakeTransition::Cover &&
       transition != WakeTransition::CustomMono && transition != WakeTransition::ProCoverFast &&
       transition != WakeTransition::ProCustomGray) ||
      !binding(expectedBinding) || header.buildAndDevice != expectedBinding) {
    return {};
  }
  return {{header.book, header.offset, header.chapter, header.page, header.orientation, true, transition},
          header.frameCrc, header.readerData, (header.flags & CLICK_TO_WAKE) != 0, (header.flags & FADING_FIX) != 0,
          (header.flags & PREVIEW_ONLY) != 0};
}

bool matchesReaderData(const Snapshot& snapshot) {
  uint32_t current;
  return !snapshot.previewOnly && readerData(snapshot.page, current) && current == snapshot.readerData;
}

}  // namespace x3_wake
