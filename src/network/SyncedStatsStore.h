#pragma once

#include "NearbyStatsProtocol.h"
#include "../activities/reader/SyncedStatsFileName.h"

namespace nearby_stats {
bool readLocalSnapshot(uint8_t* data, size_t& size);
bool savePeerSnapshot(const Mac& peer, const uint8_t* data, size_t size);
}  // namespace nearby_stats
