#pragma once
#include <cstdint>
struct CrossPointSettings {
  uint8_t fontFamily = 0, fontPointSize = 12, lineSpacing = 1, lineHeightPercent = 0;
  uint8_t orientation = 0, screenMargin = 5, paragraphAlignment = 0, embeddedStyle = 1;
  uint8_t hyphenationEnabled = 0, textAntiAliasing = 1, readerDarkMode = 0, imageRendering = 0;
  uint8_t extraParagraphSpacing = 1, paragraphIndentSpaces = 0, focusReadingEnabled = 0;
  uint8_t wordSpacing = 100, characterSpacing = 2;
  uint8_t autoPageTurnSeconds = 30;
  uint8_t guideReadingEnabled = 0, publisherPageNumbers = 0;
  char sdFontFamilyName[32]{};
};
