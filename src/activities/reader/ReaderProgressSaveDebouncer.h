#pragma once

#include <Arduino.h>

#include <cstdint>

// Small, allocation-free policy shared by reader activities. It records only
// position changes; callers retain ownership of the actual persistence format.
class ReaderProgressSaveDebouncer {
  static constexpr uint8_t PAGE_CHANGE_INTERVAL = 10;
  static constexpr unsigned long MAX_SAVE_INTERVAL_MS = 5UL * 60UL * 1000UL;

  uint32_t lastPositionKey = 0;
  unsigned long lastPersistedAtMs = 0;
  uint8_t pendingPageChanges = 0;
  bool initialized = false;
  bool pending = false;

 public:
  bool observe(const uint32_t positionKey) {
    const unsigned long now = millis();
    if (!initialized) {
      initialized = true;
      lastPositionKey = positionKey;
      lastPersistedAtMs = now;
      return false;
    }
    if (positionKey == lastPositionKey) {
      return false;
    }

    lastPositionKey = positionKey;
    pending = true;
    if (pendingPageChanges < UINT8_MAX) {
      ++pendingPageChanges;
    }
    return pendingPageChanges >= PAGE_CHANGE_INTERVAL || now - lastPersistedAtMs >= MAX_SAVE_INTERVAL_MS;
  }

  bool hasPending() const { return pending; }
  uint32_t lastObservedPosition() const { return lastPositionKey; }

  void markPersisted(const uint32_t positionKey) {
    if (!initialized) {
      initialized = true;
      lastPositionKey = positionKey;
    } else if (positionKey != lastPositionKey) {
      return;
    }

    pending = false;
    pendingPageChanges = 0;
    lastPersistedAtMs = millis();
  }
};
