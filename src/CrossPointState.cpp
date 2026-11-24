#include "CrossPointState.h"

namespace {

void getLCG(uint32_t packedState, uint32_t packedCount, uint32_t& seed, uint32_t& state, uint16_t& indexesLeft,
            uint16_t& indexesTotal) {
  indexesLeft = packedCount >> 16;
  indexesTotal = packedCount & 0xffff;
  seed = packedState >> 16;
  state = packedState & 0xffff;
}

void setLCG(uint32_t& packedState, uint32_t& packedCount, uint32_t seedVal, uint32_t stateVal, uint16_t indexesLeftVal,
            uint16_t indexesTotalVal) {
  packedCount = (static_cast<uint32_t>(indexesLeftVal) << 16) | indexesTotalVal;
  packedState = (seedVal << 16) | (stateVal & 0xffff);
}

}  // namespace

void CrossPointState::getSleepLCG(uint32_t& seed, uint32_t& state, uint16_t& indexesLeft,
                                  uint16_t& indexesTotal) const {
  getLCG(sleepLcgState, sleepLcgCount, seed, state, indexesLeft, indexesTotal);
}

void CrossPointState::getSleepOverlayLCG(uint32_t& seed, uint32_t& state, uint16_t& indexesLeft,
                                         uint16_t& indexesTotal) const {
  getLCG(sleepOverlayLcgState, sleepOverlayLcgCount, seed, state, indexesLeft, indexesTotal);
}

void CrossPointState::setSleepLCG(uint32_t seed, uint32_t state, uint16_t indexesLeft, uint16_t indexesTotal) {
  setLCG(sleepLcgState, sleepLcgCount, seed, state, indexesLeft, indexesTotal);
}

void CrossPointState::setSleepOverlayLCG(uint32_t seed, uint32_t state, uint16_t indexesLeft, uint16_t indexesTotal) {
  setLCG(sleepOverlayLcgState, sleepOverlayLcgCount, seed, state, indexesLeft, indexesTotal);
}

void CrossPointState::toJson(JsonDocument& doc) const {
  doc["openEpubPath"] = openEpubPath;
  doc["sleepLcgCount"] = sleepLcgCount;
  doc["sleepLcgState"] = sleepLcgState;
  doc["sleepOverlayLcgCount"] = sleepOverlayLcgCount;
  doc["sleepOverlayLcgState"] = sleepOverlayLcgState;
  doc["readerActivityLoadCount"] = readerActivityLoadCount;
  doc["lastSleepFromReader"] = lastSleepFromReader;
  doc["sleepFrameInverted"] = sleepFrameInverted;
  doc["showBootScreen"] = showBootScreen;
  const auto& powerHistoryState = powerHistory;
  JsonObject powerHistory = doc["powerHistory"].to<JsonObject>();
  powerHistory["activeSeconds"] = powerHistoryState.activeSeconds;
  powerHistory["cycleStartDay"] = powerHistoryState.cycleStartDay;
  powerHistory["readerPageDisplays"] = powerHistoryState.readerPageDisplays;
  powerHistory["sleepSamples"] = powerHistoryState.sleepSamples;
  powerHistory["lastPercent"] = powerHistoryState.lastPercent;
  powerHistory["chargePendingPercent"] = powerHistoryState.chargePendingPercent;
  powerHistory["hasCycleStartDay"] = powerHistoryState.hasCycleStartDay;
  powerHistory["cycleConfirmed"] = powerHistoryState.cycleConfirmed;
  powerHistory["chargePending"] = powerHistoryState.chargePending;
  JsonArray powerSamples = powerHistory["samples"].to<JsonArray>();
  for (uint8_t i = 0; i < powerHistoryState.sampleCount; ++i) {
    JsonArray sample = powerSamples.add<JsonArray>();
    sample.add(powerHistoryState.sampleActiveMinutes[i]);
    sample.add(powerHistoryState.samplePercents[i]);
  }
}

bool CrossPointState::fromJson(JsonVariantConst doc) {
  openEpubPath = doc["openEpubPath"] | "";

  sleepLcgCount = doc["sleepLcgCount"] | static_cast<uint32_t>(0);
  sleepLcgState = doc["sleepLcgState"] | static_cast<uint32_t>(0);

  sleepOverlayLcgCount = doc["sleepOverlayLcgCount"] | static_cast<uint32_t>(0);
  sleepOverlayLcgState = doc["sleepOverlayLcgState"] | static_cast<uint32_t>(0);

  readerActivityLoadCount = doc["readerActivityLoadCount"] | static_cast<uint8_t>(0);
  lastSleepFromReader = doc["lastSleepFromReader"] | false;
  sleepFrameInverted = doc["sleepFrameInverted"] | false;
  showBootScreen = doc["showBootScreen"] | true;
  auto& powerHistoryState = powerHistory;
  powerHistoryState = {};
  JsonObjectConst powerHistory = doc["powerHistory"];
  if (!powerHistory.isNull()) {
    powerHistoryState.activeSeconds = powerHistory["activeSeconds"] | static_cast<uint32_t>(0);
    powerHistoryState.cycleStartDay = powerHistory["cycleStartDay"] | static_cast<uint32_t>(0);
    powerHistoryState.readerPageDisplays = powerHistory["readerPageDisplays"] | static_cast<uint32_t>(0);
    powerHistoryState.sleepSamples = powerHistory["sleepSamples"] | static_cast<uint16_t>(0);
    powerHistoryState.lastPercent = powerHistory["lastPercent"] | static_cast<uint8_t>(UINT8_MAX);
    if (powerHistoryState.lastPercent > 100) powerHistoryState.lastPercent = UINT8_MAX;
    powerHistoryState.chargePendingPercent = powerHistory["chargePendingPercent"] | static_cast<uint8_t>(UINT8_MAX);
    if (powerHistoryState.chargePendingPercent > 100) powerHistoryState.chargePendingPercent = UINT8_MAX;
    powerHistoryState.hasCycleStartDay = powerHistory["hasCycleStartDay"] | false;
    powerHistoryState.cycleConfirmed = powerHistory["cycleConfirmed"] | false;
    powerHistoryState.chargePending =
        (powerHistory["chargePending"] | false) && powerHistoryState.chargePendingPercent <= 100;

    JsonArrayConst powerSamples = powerHistory["samples"];
    uint16_t previousMinute = 0;
    for (JsonArrayConst sample : powerSamples) {
      if (powerHistoryState.sampleCount >= PowerHistoryState::SAMPLE_CAPACITY || sample.size() != 2) break;
      if (!sample[0].is<uint16_t>() || !sample[1].is<uint8_t>()) continue;
      const uint16_t minute = sample[0] | static_cast<uint16_t>(0);
      const uint8_t percent = sample[1] | static_cast<uint8_t>(UINT8_MAX);
      if (percent > 100 || (powerHistoryState.sampleCount > 0 && minute < previousMinute)) continue;

      const uint8_t index = powerHistoryState.sampleCount++;
      powerHistoryState.sampleActiveMinutes[index] = minute;
      powerHistoryState.samplePercents[index] = percent;
      previousMinute = minute;
    }
  }
  return true;
}
