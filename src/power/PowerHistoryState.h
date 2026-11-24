#pragma once
#include <cstdint>

struct PowerHistoryState {
  static constexpr uint8_t SAMPLE_CAPACITY = 21;

  uint32_t activeSeconds = 0;
  uint32_t cycleStartDay = 0;
  uint32_t readerPageDisplays = 0;
  uint16_t sampleActiveMinutes[SAMPLE_CAPACITY] = {};
  uint16_t sleepSamples = 0;
  uint8_t samplePercents[SAMPLE_CAPACITY] = {};
  uint8_t sampleCount = 0;
  uint8_t lastPercent = UINT8_MAX;
  uint8_t chargePendingPercent = UINT8_MAX;
  bool hasCycleStartDay = false;
  bool cycleConfirmed = false;
  bool chargePending = false;
};

static_assert(sizeof(PowerHistoryState) <= 96, "Power history must remain a small fixed-size state");

