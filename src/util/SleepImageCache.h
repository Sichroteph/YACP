#pragma once
#include <cstddef>
#include <cstdint>

// Disposable decoded planes, separate from reading progress and the legacy YACP cache.
// Every operation closes its file before returning; callers close the source BMP first.
class SleepImageCache {
 public:
  struct Spec {
    uint16_t width;
    uint16_t height;
    uint32_t bufferBytes;
    uint8_t controller;
    uint8_t mode;
    uint8_t orientation;
    uint8_t filter;
    uint8_t coverMode;
    uint8_t fadingFix;
  };
  // Scratch is borrowed only during the call; its previous pixels are disposable.
  bool prepare(const char* source, const Spec& spec, uint8_t* scratch = nullptr, size_t scratchSize = 0);
  bool validate(uint8_t* scratch = nullptr, size_t scratchSize = 0);
  bool appendPlane(const uint8_t* pixels, size_t size);
  bool readPlane(uint8_t index, uint8_t* pixels, size_t size) const;
  const char* path() const { return path_; }

 private:
  uint8_t header_[48] = {};
  char path_[64] = {};
  uint8_t nextPlane_ = 0;
  bool prepared_ = false;
  bool writable_ = false;
};
static_assert(sizeof(SleepImageCache) <= 128);
