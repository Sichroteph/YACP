#include "PowerStatsActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "power/PowerHistory.h"

namespace {
void drawCenteredText(const GfxRenderer& renderer, const int fontId, const int x, const int width, const int y,
                      const char* text, const bool bold = false) {
  const EpdFontFamily::Style style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int textWidth = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, x + (width - textWidth) / 2, y, text, true, style);
}

void formatActiveTime(const uint32_t seconds, char* buffer, const size_t length) {
  const unsigned long hours = static_cast<unsigned long>(seconds / 3600U);
  const unsigned long minutes = static_cast<unsigned long>((seconds % 3600U) / 60U);
  snprintf(buffer, length, tr(STR_POWER_ACTIVE_TIME_FORMAT), hours, minutes);
}

void formatDays(const uint32_t days, char* buffer, const size_t length) {
  snprintf(buffer, length, "%lu %s", static_cast<unsigned long>(days),
           days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS));
}

void formatHours(const uint32_t minutes, char* buffer, const size_t length) {
  const unsigned long roundedHours = static_cast<unsigned long>((minutes + 30U) / 60U);
  snprintf(buffer, length, tr(STR_POWER_HOURS_SHORT_FORMAT), roundedHours);
}

void drawSummary(const GfxRenderer& renderer, const PowerHistoryState& history, const int x, const int y, const int w,
                 const int h, const uint32_t elapsedDays, const bool hasElapsedDays) {
  renderer.drawRoundedRect(x, y, w, h, 1, 8, true);

  const int columnW = w / 3;
  renderer.drawLine(x + columnW, y + 12, x + columnW, y + h - 12);
  renderer.drawLine(x + columnW * 2, y + 12, x + columnW * 2, y + h - 12);

  char battery[8];
  if (history.lastPercent <= 100) {
    snprintf(battery, sizeof(battery), "%u%%", static_cast<unsigned>(history.lastPercent));
  } else {
    snprintf(battery, sizeof(battery), "--");
  }

  char active[24];
  formatActiveTime(history.activeSeconds, active, sizeof(active));

  char days[20];
  if (hasElapsedDays) {
    formatDays(elapsedDays, days, sizeof(days));
  } else {
    snprintf(days, sizeof(days), "--");
  }

  const int valueY = y + 17;
  const int labelY = y + h - renderer.getLineHeight(SMALL_FONT_ID) - 12;
  drawCenteredText(renderer, UI_12_FONT_ID, x, columnW, valueY, battery, true);
  drawCenteredText(renderer, UI_12_FONT_ID, x + columnW, columnW, valueY, active, true);
  drawCenteredText(renderer, UI_12_FONT_ID, x + columnW * 2, w - columnW * 2, valueY, days, true);

  drawCenteredText(renderer, SMALL_FONT_ID, x, columnW, labelY, tr(STR_POWER_LAST_SAMPLE));
  drawCenteredText(renderer, SMALL_FONT_ID, x + columnW, columnW, labelY, tr(STR_POWER_ACTIVE_TIME));
  drawCenteredText(renderer, SMALL_FONT_ID, x + columnW * 2, w - columnW * 2, labelY,
                   history.cycleConfirmed ? tr(STR_POWER_SINCE_CHARGE) : tr(STR_POWER_SINCE_MONITORING));
}

void drawGraph(const GfxRenderer& renderer, const PowerHistoryState& history, const int x, const int y, const int w,
               const int h) {
  renderer.drawRoundedRect(x, y, w, h, 1, 8, true);
  const char* graphTitle = tr(STR_POWER_ACTIVITY_GRAPH);
  renderer.drawText(UI_10_FONT_ID, x + 12, y + 10, graphTitle, true, EpdFontFamily::BOLD);

  if (PowerHistory::isDemoMode()) {
    const char* demoLabel = tr(STR_POWER_DEMO_DATA);
    const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, graphTitle, EpdFontFamily::BOLD);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, demoLabel);
    if (titleWidth + labelWidth + 36 <= w) {
      renderer.drawText(SMALL_FONT_ID, x + w - labelWidth - 12, y + 13, demoLabel);
    }
  }

  if (history.sampleCount == 0 || history.lastPercent > 100) {
    drawCenteredText(renderer, UI_10_FONT_ID, x + 16, w - 32, y + h / 2, tr(STR_POWER_WAITING_FOR_SAMPLE));
    return;
  }

  const int titleHeight = renderer.getLineHeight(UI_10_FONT_ID) + 14;
  const int axisLabelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int plotLeft = x + 42;
  const int plotRight = x + w - 14;
  const int plotTop = y + titleHeight + 7;
  const int plotBottom = y + h - axisLabelHeight - 16;
  const int plotWidth = std::max(1, plotRight - plotLeft);
  const int plotHeight = std::max(1, plotBottom - plotTop);

  constexpr uint8_t levels[] = {100, 75, 50, 25, 0};
  for (const uint8_t level : levels) {
    const int levelY = plotBottom - static_cast<int>((static_cast<uint32_t>(plotHeight) * level) / 100U);
    char label[8];
    snprintf(label, sizeof(label), "%u", static_cast<unsigned>(level));
    renderer.drawText(SMALL_FONT_ID, x + 10, levelY - axisLabelHeight / 2, label);
    for (int dashX = plotLeft; dashX < plotRight; dashX += 8) {
      renderer.drawLine(dashX, levelY, std::min(dashX + 3, plotRight), levelY);
    }
  }

  const uint32_t currentMinutes = history.activeSeconds / 60U;
  const uint32_t lastStoredMinutes = history.sampleActiveMinutes[history.sampleCount - 1];
  const uint32_t maxMinutes = std::max<uint32_t>(1, std::max(currentMinutes, lastStoredMinutes));

  char xLabel[12];
  formatHours(0, xLabel, sizeof(xLabel));
  renderer.drawText(SMALL_FONT_ID, plotLeft, plotBottom + 5, xLabel);
  formatHours(maxMinutes / 2U, xLabel, sizeof(xLabel));
  drawCenteredText(renderer, SMALL_FONT_ID, plotLeft + plotWidth / 4, plotWidth / 2, plotBottom + 5, xLabel);
  formatHours(maxMinutes, xLabel, sizeof(xLabel));
  const int maxLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, xLabel);
  renderer.drawText(SMALL_FONT_ID, plotRight - maxLabelWidth, plotBottom + 5, xLabel);

  auto pointX = [plotLeft, plotWidth, maxMinutes](const uint32_t minutes) {
    return plotLeft + static_cast<int>((static_cast<uint64_t>(plotWidth) * minutes) / maxMinutes);
  };
  auto pointY = [plotBottom, plotHeight](const uint8_t percent) {
    return plotBottom - static_cast<int>((static_cast<uint32_t>(plotHeight) * percent) / 100U);
  };

  int previousX = pointX(history.sampleActiveMinutes[0]);
  int previousY = pointY(history.samplePercents[0]);
  renderer.fillRect(previousX - 2, previousY - 2, 5, 5);
  for (uint8_t i = 1; i < history.sampleCount; ++i) {
    const int sampleX = pointX(history.sampleActiveMinutes[i]);
    const int sampleY = pointY(history.samplePercents[i]);
    renderer.drawLine(previousX, previousY, sampleX, sampleY, 2, true);
    renderer.fillRect(sampleX - 2, sampleY - 2, 5, 5);
    previousX = sampleX;
    previousY = sampleY;
  }

  if (history.lastPercent != history.samplePercents[history.sampleCount - 1] ||
      currentMinutes != history.sampleActiveMinutes[history.sampleCount - 1]) {
    const int currentX = pointX(currentMinutes);
    const int currentY = pointY(history.lastPercent);
    renderer.drawLine(previousX, previousY, currentX, currentY, 2, true);
    renderer.fillRect(currentX - 2, currentY - 2, 5, 5);
  }
}

void drawPageDisplays(const GfxRenderer& renderer, const PowerHistoryState& history, const int x, const int y,
                      const int w, const int h) {
  renderer.drawRoundedRect(x, y, w, h, 1, 8, true);
  const char* title = tr(STR_POWER_PAGES_DISPLAYED);
  renderer.drawText(UI_10_FONT_ID, x + 12, y + 9, title, true, EpdFontFamily::BOLD);

  const char* periodLabel = history.cycleConfirmed ? tr(STR_POWER_SINCE_CHARGE) : tr(STR_POWER_SINCE_MONITORING);
  const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, title, EpdFontFamily::BOLD);
  const int periodWidth = renderer.getTextWidth(SMALL_FONT_ID, periodLabel);
  if (titleWidth + periodWidth + 36 <= w) {
    renderer.drawText(SMALL_FONT_ID, x + w - periodWidth - 12, y + 12, periodLabel);
  }

  char count[16];
  snprintf(count, sizeof(count), "%lu", static_cast<unsigned long>(history.readerPageDisplays));
  const int valueY = y + h - renderer.getLineHeight(UI_12_FONT_ID) - 8;
  drawCenteredText(renderer, UI_12_FONT_ID, x + 8, w - 16, valueY, count, true);
}
}  // namespace

void PowerStatsActivity::onEnter() {
  Activity::onEnter();
  hasElapsedDays = PowerHistory::elapsedDaysForDisplay(elapsedDays);
  requestUpdate();
}

void PowerStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onGoHome();
  }
}

void PowerStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  CompactHeader::drawTitleWithoutStatus(renderer, tr(STR_POWER_STATS_TITLE));

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& history = PowerHistory::historyForDisplay();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);

  const int sidePadding = std::max(metrics.contentSidePadding, marginLeft + 8);
  const int rightPadding = std::max(metrics.contentSidePadding, marginRight + 8);
  const int contentX = sidePadding;
  const int contentW = screenWidth - sidePadding - rightPadding;
  const int contentTop = std::max(CompactHeader::contentTop(metrics), marginTop);
  const int contentBottom =
      std::min(screenHeight - marginBottom, screenHeight - metrics.buttonHintsHeight - metrics.verticalSpacing);
  const int gap = std::max(7, metrics.verticalSpacing);
  const int summaryH = 86;
  const int costH = 62;
  const int graphY = contentTop + summaryH + gap;
  const int graphH = std::max(120, contentBottom - graphY - costH - gap);
  const int costY = graphY + graphH + gap;

  drawSummary(renderer, history, contentX, contentTop, contentW, summaryH, elapsedDays, hasElapsedDays);
  drawGraph(renderer, history, contentX, graphY, contentW, graphH);
  drawPageDisplays(renderer, history, contentX, costY, contentW, costH);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_HOME), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
