#pragma once
#include <cstdint>
struct TestSettings { uint8_t clockDateHasBeenSynced = 0; };
inline TestSettings testSettings;
#define SETTINGS testSettings
