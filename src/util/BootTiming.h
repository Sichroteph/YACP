#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstdint>

namespace boot_timing {
enum Phase {
  Hardware,
  SdMount,
  SavedPageVisible,
  DataCheck,
  Stores,
  Display,
  ResumeFrame,
  Routed,
  FirstPaint,
  InputLoop,
  PhaseCount
};

inline uint32_t started;
inline std::atomic<uint32_t> stamps[PhaseCount]{};
inline std::atomic<uint8_t> retainedPage{0};

inline void mark(const Phase phase) {
  if (stamps[phase].load(std::memory_order_relaxed) == 0) {
    // Store elapsed+1 to distinguish an unrecorded phase from elapsed zero.
    stamps[phase].store(millis() - started + 1, std::memory_order_release);
  }
}
}  // namespace boot_timing
