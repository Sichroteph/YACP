#pragma once

#include <cstddef>

// Same global-statistics archive and names as YACP. This is not a full SD backup.
// Call after reader exit has committed its session, on the input task.
bool backupGlobalStats(bool manual, char* outName = nullptr, size_t outSize = 0);
