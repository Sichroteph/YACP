#pragma once

#include <cstdint>

constexpr uint8_t legacyYacpLineHeightPercent(const uint8_t preset, const uint8_t fontFamily,
                                              const bool hasSdFont) {
  constexpr uint8_t LEXEND[] = {95, 100, 110, 120};
  constexpr uint8_t ATKINSON[] = {90, 95, 100, 105};
  constexpr uint8_t SD[] = {95, 100, 130, 160};
  if (preset > 3) return 0;
  if (hasSdFont) return SD[preset];
  return fontFamily == 1 ? ATKINSON[preset] : LEXEND[preset];
}
