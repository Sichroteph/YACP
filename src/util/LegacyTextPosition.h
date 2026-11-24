#pragma once

#include <cstdint>
#include <optional>

namespace legacy::txt {

// CrossPoint 2.0 TxtToHtml emits one newline inside <body>, drops a leading
// BOM and CR, and converts LF to <br>. The EPUB parser counts decoded Unicode
// code points, including spaces and entities, but no characters for <br>.
// Read one byte at a time from the caller's bounded buffer. Look past the
// anchor only when needed to resolve deferred spaces at the end of a line.
template <typename ReadByte>
std::optional<uint32_t> visibleOffset(uint32_t anchor, uint32_t fileSize, ReadByte&& readByte) {
  if (anchor > fileSize || fileSize == UINT32_MAX) return std::nullopt;
  if (anchor == 0) return 0;

  uint32_t pos = 0;
  uint32_t visible = 1;
  uint32_t spaces = 0;
  bool lineStart = true;
  auto next = [&]() {
    if (pos == fileSize) return -1;
    ++pos;
    return readByte();
  };
  while (pos < anchor) {
    const int lead = next();
    if (lead < 0 || lead > 0xFF) return std::nullopt;
    if (lead == '\r') continue;
    if (lead == '\n') {
      spaces = 0;
      lineStart = true;
      continue;
    }
    if (lead == ' ' && !lineStart) {
      ++spaces;
      continue;
    }

    if (lead >= 0x80) {
      const unsigned length = lead >= 0xF0 && lead <= 0xF4 ? 4
                              : lead >= 0xE0 && lead <= 0xEF ? 3
                              : lead >= 0xC2 && lead <= 0xDF ? 2 : 0;
      if (!length || length - 1 > anchor - pos) return std::nullopt;
      uint32_t cp = static_cast<unsigned>(lead) & (0x7Fu >> length);
      for (unsigned i = 1; i < length; ++i) {
        const int tail = next();
        if (tail < 0x80 || tail > 0xBF) return std::nullopt;
        cp = (cp << 6) | (static_cast<unsigned>(tail) & 0x3F);
      }
      constexpr uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
      if (cp < minimum[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
          cp == 0xFFFE || cp == 0xFFFF)
        return std::nullopt;
      if (pos == 3 && cp == 0xFEFF) continue;
    }
    visible += spaces + 1;
    spaces = 0;
    if (lead != ' ') lineStart = false;
  }
  if (spaces) {
    while (pos < fileSize) {
      const int ch = next();
      if (ch < 0 || ch > 0xFF) return std::nullopt;
      if (ch == ' ' || ch == '\r') continue;
      if (ch != '\n') visible += spaces;
      break;
    }
  }
  return visible;
}

}  // namespace legacy::txt
