#pragma once
#include <array>
#include <cstdint>
inline std::array<uint8_t, 6> testFactoryMac{};
inline int esp_efuse_mac_get_default(uint8_t* mac) {
  for (size_t i = 0; i < testFactoryMac.size(); ++i) mac[i] = testFactoryMac[i];
  return 0;
}
