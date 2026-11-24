#pragma once
#include <cstdint>
inline uint32_t esp_random() { static uint32_t value=0x12345678; value=value*1664525u+1013904223u; return value; }
