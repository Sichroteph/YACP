#include "HeaderDate.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cstddef>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"

namespace {
constexpr int kHeaderDateRightInset = 12;
constexpr int kHeaderDateBottomGap = 10;

bool formatHeaderDate(char* buf, const size_t len) {
  if (!gpio.deviceIsX3()) return false;
  if (!SETTINGS.clockDateHasBeenSynced) return false;
  return halClock.formatDate(buf, len, SETTINGS.clockUtcOffsetQ);
}
}  // namespace

int headerDateReservedWidth(const GfxRenderer& renderer) {
  char dateBuf[13];
  if (!formatHeaderDate(dateBuf, sizeof(dateBuf))) return 0;

  return renderer.getTextWidth(UI_10_FONT_ID, dateBuf) + kHeaderDateRightInset;
}

int headerDateLineBottomY(const GfxRenderer&, const ThemeMetrics& metrics, const int headerHeight) {
  const int effectiveHeaderHeight = headerHeight >= 0 ? headerHeight : metrics.headerHeight;
  return metrics.topPadding + effectiveHeaderHeight - kHeaderDateBottomGap;
}

void drawHeaderDate(const GfxRenderer& renderer, const int pageWidth, const ThemeMetrics& metrics,
                    const int headerHeight) {
  drawHeaderDateAtLineBottom(renderer, pageWidth, headerDateLineBottomY(renderer, metrics, headerHeight));
}

void drawHeaderDateAtLineBottom(const GfxRenderer& renderer, const int pageWidth, const int lineBottomY) {
  constexpr int dateFontId = UI_10_FONT_ID;
  drawHeaderDateAtBaseline(renderer, pageWidth,
                           lineBottomY - renderer.getLineHeight(dateFontId) + renderer.getFontAscenderSize(dateFontId));
}

void drawHeaderDateAtBaseline(const GfxRenderer& renderer, const int pageWidth, const int baselineY) {
  char dateBuf[13];
  if (!formatHeaderDate(dateBuf, sizeof(dateBuf))) return;

  constexpr int dateFontId = UI_10_FONT_ID;
  const int textWidth = renderer.getTextWidth(dateFontId, dateBuf);
  const int dateX = pageWidth - kHeaderDateRightInset - textWidth;
  const int dateY = baselineY - renderer.getFontAscenderSize(dateFontId);
  renderer.drawText(dateFontId, std::max(0, dateX), std::max(0, dateY), dateBuf);
}
