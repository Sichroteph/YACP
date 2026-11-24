#include "SleepImageCache.h"
#include <HalStorage.h>
#include <Logging.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
constexpr uint32_t BASIS = 2166136261u;
constexpr char DIRECTORY[] = "/.crosspoint/sleep_planes_v2";
uint32_t hashBytes(uint32_t hash, const uint8_t* bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
void put32(uint8_t* bytes, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t get32(const uint8_t* bytes) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(bytes[i]) << (i * 8);
  return value;
}
void put16(uint8_t* bytes, uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}
bool hashFile(HalFile& file, size_t count, uint32_t& hash, uint8_t* scratch, size_t scratchSize) {
  // The sleep path borrows its existing framebuffer. Other callers retain a bounded fallback.
  uint8_t fallback[192];
  if (!scratch || !scratchSize) { scratch = fallback; scratchSize = sizeof(fallback); }
  hash = BASIS;
  while (count) {
    const size_t chunk = std::min(count, scratchSize);
    if (file.read(scratch, chunk) != static_cast<int>(chunk)) return false;
    hash = hashBytes(hash, scratch, chunk);
    count -= chunk;
  }
  return true;
}
}  // namespace

bool SleepImageCache::prepare(const char* source, const Spec& spec, uint8_t* scratch, size_t scratchSize) {
  prepared_ = writable_ = false;
  nextPlane_ = 0;
  if (!source || !source[0] || !spec.width || !spec.height || !spec.bufferBytes ||
      spec.bufferBytes > (std::numeric_limits<uint32_t>::max() - sizeof(header_)) / 3) return false;
  HalFile file;
  if (!Storage.openFileForRead("SLC", source, file)) return false;
  const auto size = file.fileSize64();
  uint32_t sourceHash = 0;
  const bool valid = size > 0 && size <= std::numeric_limits<uint32_t>::max() &&
                     hashFile(file, static_cast<size_t>(size), sourceHash, scratch, scratchSize);
  const bool closed = file.close();
  if (!valid || !closed) {
    LOG_ERR("SLC", "Could not fingerprint sleep image");
    return false;
  }
  memset(header_, 0, sizeof(header_));
  memcpy(header_, "YSC2", 4);
  header_[4] = 2;
  header_[5] = 1;  // Commit marker; provisional writes clear this byte.
  header_[6] = spec.filter;
  header_[7] = spec.coverMode;
  header_[8] = spec.orientation;
  header_[9] = spec.controller;
  header_[10] = spec.mode;
  header_[11] = spec.fadingFix;
  put16(header_ + 12, spec.width);
  put16(header_ + 14, spec.height);
  put32(header_ + 16, spec.bufferBytes);
  put32(header_ + 20, static_cast<uint32_t>(size));
  put32(header_ + 24, sourceHash);
  const uint32_t pathHash = hashBytes(BASIS, reinterpret_cast<const uint8_t*>(source), strlen(source));
  put32(header_ + 28, pathHash);
  snprintf(path_, sizeof(path_), "%s/%08lx.bin", DIRECTORY, static_cast<unsigned long>(pathHash));
  prepared_ = writable_ = true;
  return true;
}

bool SleepImageCache::validate(uint8_t* scratch, size_t scratchSize) {
  if (!prepared_) return false;
  HalFile file;
  if (!Storage.openFileForRead("SLC", path_, file)) return false;
  uint8_t stored[sizeof(header_)];
  const size_t size = get32(header_ + 16);
  bool valid = file.fileSize64() == sizeof(header_) + 3ULL * size &&
               file.read(stored, sizeof(stored)) == static_cast<int>(sizeof(stored)) &&
               memcmp(stored, header_, 32) == 0 && get32(stored + 44) == hashBytes(BASIS, stored, 44);
  for (unsigned plane = 0; valid && plane < 3; ++plane) {
    uint32_t hash = 0;
    valid = hashFile(file, size, hash, scratch, scratchSize) && hash == get32(stored + 32 + 4 * plane);
  }
  const bool closed = file.close();
  if (!valid || !closed) {
    LOG_ERR("SLC", "Sleep cache rejected; decoding original image");
    return false;
  }
  memcpy(header_ + 32, stored + 32, 16);
  return true;
}

bool SleepImageCache::appendPlane(const uint8_t* pixels, size_t size) {
  if (!prepared_ || !writable_ || nextPlane_ >= 3 || !pixels || size != get32(header_ + 16)) return false;
  bool valid = true;
  if (nextPlane_ == 0) {
    HalFile file;
    if (!Storage.ensureDirectoryExists(DIRECTORY) || !Storage.openFileForWrite("SLC", path_, file)) {
      writable_ = false;
      LOG_ERR("SLC", "Cannot create sleep cache");
      return false;
    }
    header_[5] = 0;
    valid = file.write(header_, sizeof(header_)) == sizeof(header_);
    header_[5] = 1;
    const bool closed = file.close();
    valid = valid && closed;
  }
  if (valid) {
    auto file = Storage.open(path_, O_RDWR);
    if (file) {
      valid = file.seek(sizeof(header_) + nextPlane_ * size) && file.write(pixels, size) == size;
      if (valid) {
        put32(header_ + 32 + 4 * nextPlane_, hashBytes(BASIS, pixels, size));
        if (nextPlane_ == 2) {
          put32(header_ + 44, hashBytes(BASIS, header_, 44));
          valid = file.seek(0) && file.write(header_, sizeof(header_)) == sizeof(header_);
        }
      }
      const bool closed = file.close();
      valid = valid && closed;
    } else {
      valid = false;
    }
  }
  if (!valid) {
    writable_ = false;
    LOG_ERR("SLC", "Sleep cache write failed; ordinary rendering continues");
    return false;
  }
  ++nextPlane_;
  if (nextPlane_ == 3) LOG_INF("SLC", "Stored three sleep planes");
  return true;
}

bool SleepImageCache::readPlane(uint8_t index, uint8_t* pixels, size_t size) const {
  if (!prepared_ || index >= 3 || !pixels || size != get32(header_ + 16)) return false;
  HalFile file;
  if (!Storage.openFileForRead("SLC", path_, file)) return false;
  const bool valid = file.seek(sizeof(header_) + index * size) && file.read(pixels, size) == static_cast<int>(size) &&
                     hashBytes(BASIS, pixels, size) == get32(header_ + 32 + 4 * index);
  const bool closed = file.close();
  if (!valid || !closed) LOG_ERR("SLC", "Sleep cache plane read failed");
  return valid && closed;
}
