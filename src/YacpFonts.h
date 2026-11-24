#pragma once

#include <cstdint>

class GfxRenderer;

void registerYacpFonts(GfxRenderer& renderer);
int yacpReaderFontId(uint8_t family, uint8_t pointSize);
