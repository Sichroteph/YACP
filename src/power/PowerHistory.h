#pragma once

#include <cstdint>

#include "CrossPointState.h"

namespace PowerHistory {

// Starts the in-RAM awake-time accumulator. This does not read the battery or
// touch storage.
void begin(bool externalPowerConnected);

// Called only from the USB edge already detected by HalGPIO.
void noteExternalPower(bool connected);

// Counts one successfully displayed reader page while running on battery.
// This is RAM-only; persistence remains part of the existing sleep state save.
void recordReaderPageDisplay();

// Adds this awake session and, at most, one battery sample immediately before
// the existing state-file save performed by enterDeepSleep().
void commitBeforeSleep(bool externalPowerConnected);

// The simulator can expose a deterministic history without changing state.json.
bool isDemoMode();
const PowerHistoryState& historyForDisplay();

// Returns calendar days since the cycle start. The RTC is queried only when the
// user explicitly opens the autonomy screen.
bool elapsedDaysForDisplay(uint32_t& outDays);

}  // namespace PowerHistory
