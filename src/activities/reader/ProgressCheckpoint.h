#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstring>
#include "ProgressFile.h"

// One small, fixed snapshot per reader, serialized under ActivityManager's
// render lock. Retains CrossPoint's exact bytes even after a section is freed.
class ProgressCheckpoint {
  uint8_t bytes[10] = {};
  uint8_t length = 0;
  uint8_t changes = 0;
  uint32_t position = 0;
  uint32_t persistedAt = 0;
  bool pending = false;

 public:
  bool observe(const std::string& cache, const uint8_t* data, const size_t size,
               const uint32_t key, const bool force = false) {
    if (!data || size == 0 || size > sizeof(bytes)) {
      LOG_ERR("PRG", "Invalid checkpoint size: %u", static_cast<unsigned>(size));
      return false;
    }
    const bool first = length == 0;
    const bool changed = first || size != length || std::memcmp(bytes, data, size) != 0;
    if (changed) {
      if (!first && key != position && changes < UINT8_MAX) ++changes;
      std::memcpy(bytes, data, size);
      length = static_cast<uint8_t>(size);
      position = key;
      pending = true;
    }
    // Persist the first displayed page immediately, as CrossPoint does. After
    // that, retain YACP's ten changes / five minutes on a page-change policy.
    const uint32_t elapsed = static_cast<uint32_t>(millis()) - persistedAt;
    if (first || force || changes >= 10 || (changed && elapsed >= 300000)) return flush(cache);
    return true;
  }

  bool flush(const std::string& cache) {
    if (!pending) return true;
    if (!ProgressFile::writeAtomic(cache, bytes, length)) return false;
    pending = false;
    changes = 0;
    persistedAt = millis();
    LOG_DBG("PRG", "Checkpoint saved: position=%lu bytes=%u", static_cast<unsigned long>(position), length);
    return true;
  }
};
static_assert(sizeof(ProgressCheckpoint) <= 32, "Progress checkpoints must remain small and allocation-free");
