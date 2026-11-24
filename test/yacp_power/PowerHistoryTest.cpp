#include <Arduino.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include "CrossPointState.h"
#include "CrossPointSettings.h"
#include "power/PowerHistory.h"

static void reset(bool plugged = false) {
  APP_STATE.powerHistory = {};
  testMillis = 0;
  powerManager = {};
  halClock.time = {};
  halClock.time.tm_year = 126;
  halClock.time.tm_mon = 9;
  halClock.time.tm_mday = 1;
  halClock.available = true;
  SETTINGS.clockDateHasBeenSynced = 1;
  PowerHistory::begin(plugged);
}
static void page() { PowerHistory::recordReaderPageDisplay(); }
int main(int argc, char** argv) {
  auto& h = APP_STATE.powerHistory;
  reset(); page(); page(); testMillis = 120000;
  assert(powerManager.reads == 0);
  PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == 120 && h.readerPageDisplays == 2 && h.sleepSamples == 1);
  assert(h.sampleCount == 1 && h.samplePercents[0] == 90 && powerManager.reads == 0);
  PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == 120 && h.sleepSamples == 1);

  // Plugged intervals and pages are excluded; only post-charge work belongs to the new cycle.
  PowerHistory::begin(false); testMillis += 10000; page();
  PowerHistory::noteExternalPower(true); testMillis += 300000; page();
  PowerHistory::noteExternalPower(false); testMillis += 60000; page();
  powerManager.percent = 98; PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == 60 && h.readerPageDisplays == 1 && h.cycleConfirmed);

  // A missing measurement cannot erase the previous cycle or invent a zero-percent point.
  PowerHistory::begin(false); testMillis += 30000; page();
  powerManager.available = false; powerManager.cached = false;
  PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == 90 && h.readerPageDisplays == 2 && h.lastPercent == 98);
  assert(powerManager.reads == 1);

  // A later five-percent drop records a bounded sample, not every page.
  powerManager.available = true; powerManager.percent = 90;
  PowerHistory::begin(false); testMillis += 60000;
  PowerHistory::commitBeforeSleep(false);
  assert(h.sampleCount == 2 && h.samplePercents[1] == 90);

  // Charge across sleep/reboot: preserve baseline while connected, then start a cycle on unplug.
  reset(); h.lastPercent = 30; h.activeSeconds = 100; h.readerPageDisplays = 10;
  PowerHistory::begin(true); powerManager.percent = 40; testMillis = 5000;
  PowerHistory::commitBeforeSleep(true); assert(h.chargePending && h.activeSeconds == 100);
  PowerHistory::begin(false); powerManager.percent = 80; testMillis += 30000; page();
  PowerHistory::commitBeforeSleep(false);
  assert(h.cycleConfirmed && !h.chargePending && h.activeSeconds == 30 && h.readerPageDisplays == 1);

  // Explicit UI snapshot drains the atomic counter once without reading battery or writing SD.
  reset(); page();
  assert(PowerHistory::historyForDisplay().readerPageDisplays == 1);
  assert(PowerHistory::historyForDisplay().readerPageDisplays == 1 && powerManager.reads == 0);
  testMillis = 60000; PowerHistory::commitBeforeSleep(false); assert(h.readerPageDisplays == 1);

  // millis wraps on ESP32 even though the host unsigned long is 64-bit.
  reset(); testMillis = UINT32_MAX - 4999; PowerHistory::begin(false); testMillis = 5000;
  halClock.available = false; PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == 10 && !h.hasCycleStartDay);
  uint32_t days = 99; assert(!PowerHistory::elapsedDaysForDisplay(days));

  reset(); h.lastPercent = 90; h.activeSeconds = UINT32_MAX - 1; h.readerPageDisplays = UINT32_MAX;
  page(); testMillis = 10000; PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == UINT32_MAX && h.readerPageDisplays == UINT32_MAX);

  // A valid-looking RTC date is not trusted after an old time-only sync.
  reset(); SETTINGS.clockDateHasBeenSynced = 0; testMillis = 60000;
  PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == 60 && !h.hasCycleStartDay);
  assert(!PowerHistory::elapsedDaysForDisplay(days));
  // Sync does not invent a start date for an already running cycle.
  SETTINGS.clockDateHasBeenSynced = 1;
  PowerHistory::begin(false); testMillis += 30000;
  PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == 90 && !h.hasCycleStartDay);
  // A subsequent observed charge starts a newly dated cycle.
  PowerHistory::begin(false); PowerHistory::noteExternalPower(true);
  PowerHistory::noteExternalPower(false); powerManager.percent = 100;
  testMillis += 30000; PowerHistory::commitBeforeSleep(false);
  assert(h.activeSeconds == 30 && h.hasCycleStartDay && h.cycleConfirmed);

  // Real JSON import/serialization uses the firmware codec. No output overwrites the fixture.
  if (argc == 2) {
    std::ifstream input(argv[1]); assert(input.good()); JsonDocument original;
    assert(!deserializeJson(original, input));
    assert(APP_STATE.fromJson(original.as<JsonVariantConst>()));
    JsonDocument output; APP_STATE.toJson(output);
    assert(original["powerHistory"].as<JsonVariantConst>() == output["powerHistory"].as<JsonVariantConst>());
  }
  JsonDocument malformed;
  deserializeJson(malformed, R"({"powerHistory":{"lastPercent":999,"samples":[[0,99],[1,999],[65536,50],[2,90],[1,80]]}})");
  APP_STATE.fromJson(malformed.as<JsonVariantConst>());
  assert(h.lastPercent == UINT8_MAX && h.sampleCount == 2 && h.sampleActiveMinutes[1] == 2);
  std::cout << "Autonomy: 9 scenarios and real state round-trip passed\n";
}
