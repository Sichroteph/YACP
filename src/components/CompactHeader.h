#pragma once

class GfxRenderer;
struct ThemeMetrics;

namespace CompactHeader {
int headerBottomY(const ThemeMetrics& metrics);
int contentTop(const ThemeMetrics& metrics);
void drawTitle(const GfxRenderer& renderer, const char* title, bool showDate = false);
}  // namespace CompactHeader
