#pragma once

#include <cstdint>

inline constexpr uint8_t MAX_PARAGRAPH_INDENT_SPACES = 5;
inline constexpr uint8_t FORCE_PARAGRAPH_INDENTS = 6;

constexpr uint8_t paragraphIndentWidth(const uint8_t mode, const bool extraParagraphSpacing) {
  if (mode != FORCE_PARAGRAPH_INDENTS) return mode;
  return extraParagraphSpacing ? 0 : 3;  // Legacy fallback for text outside a styled paragraph.
}

constexpr uint8_t migrateParagraphIndentSpaces(const bool hasSavedWidth, const int savedWidth,
                                               const bool extraParagraphSpacing) {
  if (!hasSavedWidth) return extraParagraphSpacing ? 0 : 2;
  if (savedWidth < 0) return 0;
  if (savedWidth > FORCE_PARAGRAPH_INDENTS) return MAX_PARAGRAPH_INDENT_SPACES;
  return static_cast<uint8_t>(savedWidth);
}
