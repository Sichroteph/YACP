#include "BookStatsView.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <numeric>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kStatsButtonHintTopGap = 10;
constexpr int kStandaloneNoRtcMaxTopCardHeightDivisor = 2;
constexpr int kStandaloneNoRtcMaxVerticalOffset = 32;
constexpr int kPerBookRtcTopCardMaxExtra = 84;
constexpr int kReadingRhythmWeekCount = 13;
constexpr size_t kReadingRhythmMonthCount = 12;
constexpr uint8_t kMediumReadingMinutes = 15;
constexpr uint8_t kHighReadingMinutes = 30;

struct StatsLayout {
  int headerHeight;
  int headerDrawHeight;
  int topGap;
  int cardGap;
  int topCardTitleH;
  int topCardH;
  int globalCardH;
  int sectionTitleH;
  int sectionTitleFontId;
  int chartLabelFontId;
  int chartLabelW;
  int barH;
  int barGap;
  int chartTopPadding;
  int chartBottomPadding;
};

constexpr StatsLayout kDefaultLayout = {
    .headerHeight = 78,
    .headerDrawHeight = 67,
    .topGap = 8,
    .cardGap = 26,
    .topCardTitleH = 36,
    .topCardH = 214,
    .globalCardH = 154,
    .sectionTitleH = 34,
    .sectionTitleFontId = UI_10_FONT_ID,
    .chartLabelFontId = UI_10_FONT_ID,
    .chartLabelW = 88,
    .barH = 22,
    .barGap = 12,
    .chartTopPadding = 14,
    .chartBottomPadding = 14,
};

constexpr StatsLayout kCompactLayout = {
    .headerHeight = 67,
    .headerDrawHeight = 67,
    .topGap = 6,
    .cardGap = 8,
    .topCardTitleH = 30,
    .topCardH = 156,
    .globalCardH = 110,
    .sectionTitleH = 30,
    .sectionTitleFontId = UI_10_FONT_ID,
    .chartLabelFontId = SMALL_FONT_ID,
    .chartLabelW = 78,
    .barH = 16,
    .barGap = 8,
    .chartTopPadding = 8,
    .chartBottomPadding = 8,
};

constexpr std::array<StrId, READING_TIME_BUCKET_COUNT> TIME_BUCKET_LABELS = {
    StrId::STR_STATS_MORNING, StrId::STR_STATS_AFTERNOON, StrId::STR_STATS_EVENING, StrId::STR_STATS_NIGHT};
constexpr std::array<StrId, READING_DAY_OF_WEEK_COUNT> DAY_LABELS = {
    StrId::STR_STATS_MON, StrId::STR_STATS_TUE, StrId::STR_STATS_WED, StrId::STR_STATS_THU,
    StrId::STR_STATS_FRI, StrId::STR_STATS_SAT, StrId::STR_STATS_SUN};

const char* dayCountText(const uint16_t days) { return days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS); }

void readingIntensityStyle(const uint8_t minutes, const int activeDotSize, const int inactiveDotSize, int& dotSize,
                           Color& color) {
  if (minutes == 0) {
    dotSize = inactiveDotSize;
    color = Color::LightGray;
  } else if (minutes < kMediumReadingMinutes) {
    dotSize = std::max(inactiveDotSize + 1, activeDotSize - 4);
    color = Color::LightGray;
  } else if (minutes < kHighReadingMinutes) {
    dotSize = std::max(inactiveDotSize + 2, activeDotSize - 2);
    color = Color::DarkGray;
  } else {
    dotSize = activeDotSize;
    color = Color::Black;
  }
}

uint16_t weeklyChartScaleMinutes(const uint16_t maxMinutes) {
  if (maxMinutes <= 60) return 60;
  if (maxMinutes <= 180) return 180;
  if (maxMinutes <= 360) return 360;
  if (maxMinutes <= 600) return 600;
  return static_cast<uint16_t>(((static_cast<uint32_t>(maxMinutes) + 119u) / 120u) * 120u);
}

void formatRhythmMinutes(const uint16_t minutes, char* buf, const size_t len) {
  if (!buf || len == 0) {
    return;
  }
  if (minutes < 60) {
    snprintf(buf, len, "%u min", static_cast<unsigned>(minutes));
    return;
  }

  const uint16_t hours = minutes / 60u;
  const uint16_t remainder = minutes % 60u;
  if (remainder == 0) {
    snprintf(buf, len, "%uh", static_cast<unsigned>(hours));
  } else {
    snprintf(buf, len, "%uh %u min", static_cast<unsigned>(hours), static_cast<unsigned>(remainder));
  }
}

int sectionCardHeight(const StatsLayout& layout, const int rowCount) {
  if (rowCount <= 0) {
    return layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding;
  }
  const int rowStride = layout.barH + layout.barGap;
  return layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + layout.barH +
         (rowCount - 1) * rowStride;
}

bool shouldShowRtcBasedStats() { return halClock.isAvailable(); }

int noRtcCardBaseHeight(const StatsLayout& layout) { return layout.globalCardH; }

int statsContentHeight(const StatsLayout& layout, const bool globalPage, const bool showRtcStats) {
  const int topCardH = globalPage ? layout.globalCardH : layout.topCardH;
  if (!showRtcStats) {
    return layout.headerHeight + layout.topGap + topCardH;
  }
  const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
  const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
  return layout.headerHeight + layout.topGap + topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
}

int noRtcCombinedContentHeight(const StatsLayout& layout, const bool showAllDevicesStats) {
  const int cardBaseH = noRtcCardBaseHeight(layout);
  return layout.headerHeight + layout.topGap + cardBaseH + layout.cardGap + layout.globalCardH +
         (showAllDevicesStats ? layout.cardGap + layout.globalCardH : 0);
}

int statsBottomInset(const ThemeMetrics& metrics, const bool showButtonHints) {
  return metrics.verticalSpacing + (showButtonHints ? metrics.buttonHintsHeight + kStatsButtonHintTopGap : 0);
}

int perBookRtcTopCardHeight(const StatsLayout& layout, const int extraHeight) {
  return layout.topCardH + std::min(extraHeight, kPerBookRtcTopCardMaxExtra);
}

int globalRtcCardHeightForPerBookRowSpacing(const StatsLayout& layout, const int perBookExtraHeight) {
  constexpr int perBookDataRowCount = 3;
  constexpr int globalDataRowCount = 2;
  const int perBookDataRowH =
      (perBookRtcTopCardHeight(layout, perBookExtraHeight) - layout.topCardTitleH) / perBookDataRowCount;
  return std::max(layout.globalCardH, layout.topCardTitleH + perBookDataRowH * globalDataRowCount);
}

const StatsLayout& getStatsLayout(const GfxRenderer& renderer, const bool globalPage, const bool showButtonHints,
                                  const bool showRtcStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  if (statsContentHeight(kDefaultLayout, globalPage, showRtcStats) <= availableHeight) {
    return kDefaultLayout;
  }
  return kCompactLayout;
}

const StatsLayout& getNoRtcCombinedLayout(const GfxRenderer& renderer, const bool showButtonHints,
                                          const bool showAllDevicesStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  if (noRtcCombinedContentHeight(kDefaultLayout, showAllDevicesStats) <= availableHeight) {
    return kDefaultLayout;
  }
  return kCompactLayout;
}

void formatCompactEstimate(const uint32_t seconds, char* buf, const size_t len) {
  if (seconds < 60) {
    snprintf(buf, len, "<1m");
    return;
  }
  const uint32_t minutes = (seconds + 30u) / 60u;
  if (minutes < 60) {
    snprintf(buf, len, "%lum", static_cast<unsigned long>(minutes));
    return;
  }
  const uint32_t hours = minutes / 60u;
  const uint32_t remainder = minutes % 60u;
  if (remainder == 0) {
    snprintf(buf, len, "%luh", static_cast<unsigned long>(hours));
  } else {
    snprintf(buf, len, "%luh %lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(remainder));
  }
}

bool fallbackEstimatedTimeLeft(const BookReadingStats& stats, const float progressPercent, uint32_t& seconds) {
  seconds = 0;
  if (progressPercent <= 0.0f || progressPercent >= 100.0f || stats.totalReadingSeconds < 120) {
    return false;
  }

  const float progress = progressPercent / 100.0f;
  const float estimate = (static_cast<float>(stats.totalReadingSeconds) * (1.0f - progress)) / progress;
  if (estimate <= 0.0f) {
    return false;
  }
  seconds = static_cast<uint32_t>(estimate + 0.5f);
  return seconds > 0;
}

bool cachedEstimatedTimeLeft(const BookReadingStats& stats, uint32_t& seconds) {
  seconds = stats.estimatedTimeLeftSeconds;
  return seconds > 0;
}

bool estimateFinishDateFromDailyPace(const BookReadingStats& stats, const ReadingStatsDateTime& today,
                                     const uint32_t estimatedReadingSeconds, ReadingStatsDate& outDate) {
  outDate = {};
  if (!today.isValid() || !stats.startDate.isValid() || estimatedReadingSeconds == 0 ||
      stats.totalReadingSeconds == 0) {
    return false;
  }

  const uint16_t elapsedDays = readingSpanDaysElapsed(stats.startDate, today.date);
  const uint16_t readingDays = std::max<uint16_t>(1, elapsedDays);

  // Convert remaining reading time into calendar time using the book's average reading seconds per calendar day.
  const uint64_t estimatedCalendarSeconds =
      (static_cast<uint64_t>(estimatedReadingSeconds) * static_cast<uint64_t>(readingDays) * 86400ULL +
       static_cast<uint64_t>(stats.totalReadingSeconds) / 2ULL) /
      static_cast<uint64_t>(stats.totalReadingSeconds);
  if (estimatedCalendarSeconds == 0) {
    return false;
  }

  ReadingStatsDateTime estimatedFinish = today;
  addSecondsToReadingStatsDateTime(estimatedFinish,
                                   static_cast<uint32_t>(std::min<uint64_t>(estimatedCalendarSeconds, UINT32_MAX)));
  outDate = estimatedFinish.date;
  return outDate.isValid();
}

float pagesPerMinute(const uint32_t totalPagesTurned, const uint32_t totalReadingSeconds) {
  if (totalReadingSeconds <= 60) {
    return 0.0f;
  }
  return static_cast<float>(totalPagesTurned) * 60.0f / static_cast<float>(totalReadingSeconds);
}

void drawCenteredLabel(const GfxRenderer& renderer, const int fontId, const int x, const int w, const int y,
                       const char* text, const bool bold = false) {
  const int textWidth = renderer.getTextWidth(fontId, text, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  renderer.drawText(fontId, x + (w - textWidth) / 2, y, text, true,
                    bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

void drawStatCell(const GfxRenderer& renderer, const int x, const int w, const int y, const int h, const char* value,
                  const char* label) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int totalTextH = valueLineH + 4 + labelLineH;
  const int textY = y + (h - totalTextH) / 2;
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, w, textY, value, true);
  drawCenteredLabel(renderer, SMALL_FONT_ID, x, w, textY + valueLineH + 4, label);
}

void drawSectionCard(const GfxRenderer& renderer, const int x, const int y, const int w, const int h, const char* title,
                     const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.sectionTitleH, x + w, y + layout.sectionTitleH);
  drawCenteredLabel(renderer, layout.sectionTitleFontId, x, w,
                    y + (layout.sectionTitleH - renderer.getLineHeight(layout.sectionTitleFontId)) / 2, title, true);
}

template <size_t N>
void drawHorizontalBars(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                        const std::array<uint32_t, N>& values, const std::array<StrId, N>& labels,
                        const StatsLayout& layout) {
  constexpr int labelLeftPadding = 10;
  constexpr int labelRightPadding = 18;
  constexpr int barLeftGap = 8;
  constexpr int rightPadding = 18;
  const uint32_t maxValue = *std::max_element(values.begin(), values.end());
  const int labelLineH = renderer.getLineHeight(layout.chartLabelFontId);
  const int rowContentH = std::max(labelLineH, layout.barH);
  const int baseContentH = layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + rowContentH +
                           (static_cast<int>(N) - 1) * (rowContentH + layout.barGap);
  const int extraHeight = std::max(0, h - baseContentH);
  const int spacingSlotCount = static_cast<int>(N) + 1;
  const int extraPerSlot = spacingSlotCount > 0 ? extraHeight / spacingSlotCount : 0;
  const int extraRemainder = spacingSlotCount > 0 ? extraHeight % spacingSlotCount : 0;
  const int topPadding = layout.chartTopPadding + extraPerSlot + (extraRemainder > 0 ? 1 : 0);
  const int rowGap = layout.barGap + extraPerSlot;
  const int contentTop = y + layout.sectionTitleH + topPadding;
  const int rowStride = rowContentH + rowGap;
  int maxLabelW = 0;
  for (size_t i = 0; i < N; ++i) {
    maxLabelW = std::max(maxLabelW, renderer.getTextWidth(layout.chartLabelFontId, I18N.get(labels[i])));
  }
  const int labelColumnW = std::max(layout.chartLabelW, labelLeftPadding + maxLabelW + labelRightPadding);
  const int barX = x + labelColumnW + barLeftGap;
  const int barW = std::max(0, w - labelColumnW - barLeftGap - rightPadding);
  for (size_t i = 0; i < N; ++i) {
    const int rowTop = contentTop + static_cast<int>(i) * rowStride;
    const int labelY = rowTop + (rowContentH - labelLineH) / 2;
    const int barY = rowTop + (rowContentH - layout.barH) / 2;
    renderer.drawText(layout.chartLabelFontId, x + labelLeftPadding, labelY, I18N.get(labels[i]));
    if (maxValue > 0 && values[i] > 0) {
      const int fillW = std::max(2, static_cast<int>((static_cast<uint64_t>(barW) * values[i]) / maxValue));
      renderer.fillRect(barX, barY, fillW, layout.barH, true);
    }
  }
}

void drawPerBookStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                          const std::string& bookTitle, const BookReadingStats& stats, const float progressPercent,
                          const bool hasEstimatedTimeLeft, const uint32_t estimatedTimeLeftSeconds,
                          const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const std::string visibleTitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), w - 20, EpdFontFamily::BOLD);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, visibleTitle.c_str(), true);

  const bool showRtcStats = shouldShowRtcBasedStats();
  const int thirdW = w / 3;
  const int halfW = w / 2;
  const int rowCount = showRtcStats ? 3 : 2;
  const int rowH = (h - layout.topCardTitleH) / rowCount;
  char buf[40];

  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(stats.sessionCount));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_SESSIONS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  if (progressPercent >= 0.0f) {
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(progressPercent + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PROGRESS_LBL));

  const uint32_t avgSecs = stats.sessionCount > 0 ? stats.totalReadingSeconds / stats.sessionCount : 0;
  BookReadingStats::formatDuration(avgSecs, buf, sizeof(buf));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));

  uint32_t fallbackEstimateSeconds = 0;
  uint32_t cachedEstimateSeconds = 0;
  const bool hasCachedEstimate = cachedEstimatedTimeLeft(stats, cachedEstimateSeconds);
  const bool hasFallbackEstimate = fallbackEstimatedTimeLeft(stats, progressPercent, fallbackEstimateSeconds);
  if (!stats.isCompleted && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    formatCompactEstimate(hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                          : hasCachedEstimate  ? cachedEstimateSeconds
                                               : fallbackEstimateSeconds,
                          buf, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_TIME_LEFT));

  snprintf(buf, sizeof(buf), "%.1f", pagesPerMinute(stats.totalPagesTurned, stats.totalReadingSeconds));
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH + rowH, rowH, buf,
               tr(STR_STATS_PAGES_PER_MIN));

  if (!showRtcStats) {
    return;
  }

  ReadingStatsDateTime today;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
  const ReadingStatsDate endDate = stats.isCompleted && stats.finishedDate.isValid()
                                       ? stats.finishedDate
                                       : (hasToday ? today.date : ReadingStatsDate{});
  const bool hasDaySpan = stats.startDate.isValid() && endDate.isValid();
  const uint16_t daysReading = hasDaySpan ? readingSpanDaysElapsed(stats.startDate, endDate) : 0;
  if (hasDaySpan) {
    snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(daysReading), dayCountText(daysReading));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  char startedLabel[32];
  char dateBuf[24];
  formatReadingStatsShortDate(stats.startDate, dateBuf, sizeof(dateBuf));
  snprintf(startedLabel, sizeof(startedLabel), "%s %s", tr(STR_STATS_STARTED), dateBuf);
  drawStatCell(renderer, x, halfW, y + layout.topCardTitleH + rowH * 2, rowH, buf, startedLabel);

  ReadingStatsDate finishDisplayDate;
  bool finished = stats.isCompleted;
  if (finished) {
    finishDisplayDate = stats.finishedDate;
  } else if (hasToday && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    const uint32_t remainingReadingSeconds = hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                             : hasCachedEstimate  ? cachedEstimateSeconds
                                                                  : fallbackEstimateSeconds;
    if (!estimateFinishDateFromDailyPace(stats, today, remainingReadingSeconds, finishDisplayDate)) {
      ReadingStatsDateTime estimatedFinish = today;
      addSecondsToReadingStatsDateTime(estimatedFinish, remainingReadingSeconds);
      finishDisplayDate = estimatedFinish.date;
    }
  }
  formatReadingStatsShortDate(finishDisplayDate, buf, sizeof(buf));
  drawStatCell(renderer, x + halfW, halfW, y + layout.topCardTitleH + rowH * 2, rowH, buf,
               finished ? tr(STR_STATS_FINISHED_DATE) : tr(STR_STATS_EST_FINISH_DATE));
}

void drawGlobalStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h, const char* title,
                         const GlobalReadingStats& stats, const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const bool showRtcStats = shouldShowRtcBasedStats();
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, title, true);

  const int thirdW = w / 3;
  const int halfW = w / 2;
  const int rowH = (h - layout.topCardTitleH) / 2;
  char buf[40];

  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.totalSessions));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_SESSIONS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  snprintf(buf, sizeof(buf), "%.1f", pagesPerMinute(stats.totalPagesTurned, stats.totalReadingSeconds));
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PAGES_PER_MIN));

  const uint32_t avgSecs = stats.totalSessions > 0 ? stats.totalReadingSeconds / stats.totalSessions : 0;
  BookReadingStats::formatDuration(avgSecs, buf, sizeof(buf));
  if (showRtcStats) {
    drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));
  } else {
    drawStatCell(renderer, x, halfW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));
  }

  if (showRtcStats) {
    ReadingStatsDateTime today;
    const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
    const uint16_t currentStreak = hasToday ? stats.currentReadingStreak(&today.date) : 0;
    if (currentStreak > 0) {
      snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(currentStreak), dayCountText(currentStreak));
    } else {
      snprintf(buf, sizeof(buf), "-");
    }
    drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH + rowH, rowH, buf,
                 tr(STR_STATS_READING_STREAK_LBL));
  }

  if (stats.completedBooks > 0) {
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.completedBooks));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, showRtcStats ? x + thirdW * 2 : x + halfW, showRtcStats ? thirdW : halfW,
               y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_COMPLETED_LBL));
}

void drawCombinedBookStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                               const std::string& bookTitle, const BookReadingStats& stats,
                               const float progressPercent, const bool hasEstimatedTimeLeft,
                               const uint32_t estimatedTimeLeftSeconds, const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const std::string visibleTitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), w - 20, EpdFontFamily::BOLD);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, visibleTitle.c_str(), true);

  const int halfW = w / 2;
  const int rowH = std::max(1, (h - layout.topCardTitleH) / 3);
  char buf[40];

  if (progressPercent >= 0.0f) {
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(progressPercent + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x, halfW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PROGRESS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + halfW, halfW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  uint32_t fallbackEstimateSeconds = 0;
  uint32_t cachedEstimateSeconds = 0;
  const bool hasCachedEstimate = cachedEstimatedTimeLeft(stats, cachedEstimateSeconds);
  const bool hasFallbackEstimate = fallbackEstimatedTimeLeft(stats, progressPercent, fallbackEstimateSeconds);
  const uint32_t remainingReadingSeconds = hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                          : hasCachedEstimate  ? cachedEstimateSeconds
                                                               : fallbackEstimateSeconds;
  if (!stats.isCompleted && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    formatCompactEstimate(remainingReadingSeconds, buf, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x, halfW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_TIME_LEFT));

  const uint32_t averageSessionSeconds = stats.sessionCount > 0 ? stats.totalReadingSeconds / stats.sessionCount : 0;
  BookReadingStats::formatDuration(averageSessionSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + halfW, halfW, y + layout.topCardTitleH + rowH, rowH, buf,
               tr(STR_STATS_AVG_SESSION_LBL));

  formatReadingStatsShortDate(stats.startDate, buf, sizeof(buf));
  drawStatCell(renderer, x, halfW, y + layout.topCardTitleH + rowH * 2, rowH, buf, tr(STR_STATS_STARTED));

  ReadingStatsDateTime today;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
  ReadingStatsDate finishDisplayDate;
  if (stats.isCompleted) {
    finishDisplayDate = stats.finishedDate;
  } else if (hasToday && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    if (!estimateFinishDateFromDailyPace(stats, today, remainingReadingSeconds, finishDisplayDate)) {
      ReadingStatsDateTime estimatedFinish = today;
      addSecondsToReadingStatsDateTime(estimatedFinish, remainingReadingSeconds);
      finishDisplayDate = estimatedFinish.date;
    }
  }
  formatReadingStatsShortDate(finishDisplayDate, buf, sizeof(buf));
  drawStatCell(renderer, x + halfW, halfW, y + layout.topCardTitleH + rowH * 2, rowH, buf,
               stats.isCompleted ? tr(STR_STATS_FINISHED_DATE) : tr(STR_STATS_EST_FINISH_DATE));
}

void drawCombinedGlobalStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                                 const GlobalReadingStats& stats, const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, tr(STR_STATS_ALL_TIME),
                    true);

  const int halfW = w / 2;
  const int rowH = std::max(1, (h - layout.topCardTitleH) / 2);
  char buf[40];

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x, halfW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  if (stats.completedBooks > 0) {
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.completedBooks));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x + halfW, halfW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_COMPLETED_LBL));

  ReadingStatsDateTime today;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
  const uint16_t currentStreak = hasToday ? stats.currentReadingStreak(&today.date) : 0;
  if (currentStreak > 0) {
    snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(currentStreak), dayCountText(currentStreak));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x, halfW, y + layout.topCardTitleH + rowH, rowH, buf,
               tr(STR_STATS_READING_STREAK_LBL));

  const uint32_t averageSessionSeconds =
      stats.totalSessions > 0 ? stats.totalReadingSeconds / stats.totalSessions : 0;
  BookReadingStats::formatDuration(averageSessionSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + halfW, halfW, y + layout.topCardTitleH + rowH, rowH, buf,
               tr(STR_STATS_AVG_SESSION_LBL));
}

void drawDateField(const GfxRenderer& renderer, const int x, const int y, const int w, const char* text,
                   const bool selected) {
  const int h = renderer.getLineHeight(UI_12_FONT_ID) + 10;
  renderer.fillRectDither(x, y, w, h, selected ? Color::LightGray : Color::White);
  renderer.drawRect(x, y, w, h, true);
  if (selected) {
    renderer.drawRect(x + 1, y + 1, w - 2, h - 2, true);
  }
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, w, y + 5, text);
}
}  // namespace

void renderPerBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                            const BookReadingStats& stats, const float progressPercent, const bool hasEstimatedTimeLeft,
                            const uint32_t estimatedTimeLeftSeconds, const bool showButtonHints,
                            const bool showEditButton, const bool showMoreButton) {
  renderer.clearScreen();
  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, false, showButtonHints, showRtcStats);
  CompactHeader::drawTitle(renderer, tr(STR_READING_STATS), true);
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  int topCardH = layout.topCardH;
  int y = metrics.topPadding + std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap;

  if (showRtcStats) {
    const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
    const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
    const int compactContentHeight = std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap +
                                     layout.topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    const int extraTopCardHeight = std::min(extraHeight, kPerBookRtcTopCardMaxExtra);
    const int remainingExtraHeight = extraHeight - extraTopCardHeight;
    const int timeOfDayExtraHeight = (remainingExtraHeight * 4) / 11;
    const int dayOfWeekExtraHeight = remainingExtraHeight - timeOfDayExtraHeight;
    const int timeOfDayCardH = timeOfDayH + timeOfDayExtraHeight;
    const int dayOfWeekCardH = dayOfWeekH + dayOfWeekExtraHeight;
    topCardH += extraTopCardHeight;

    drawPerBookStatsCard(renderer, cardX, y, cardW, topCardH, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                         estimatedTimeLeftSeconds, layout);
    y += topCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, timeOfDayCardH, tr(STR_STATS_TIME_OF_DAY), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, timeOfDayCardH, stats.timeOfDaySeconds, TIME_BUCKET_LABELS, layout);
    y += timeOfDayCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, dayOfWeekCardH, tr(STR_STATS_DAY_OF_WEEK), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, dayOfWeekCardH, stats.dayOfWeekSeconds, DAY_LABELS, layout);
  } else {
    const int compactContentHeight =
        std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap + layout.topCardH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    if (showButtonHints) {
      topCardH += extraHeight;
    } else {
      // The sleep-screen variant has no footer controls, so on tall portrait displays the
      // single card can balloon and create huge internal gaps between the two stat rows.
      // Cap the card growth and spend the rest as outer margin instead.
      const int maxStandaloneCardHeight =
          std::max(layout.topCardH, renderer.getScreenHeight() / kStandaloneNoRtcMaxTopCardHeightDivisor);
      topCardH = std::min(layout.topCardH + extraHeight, maxStandaloneCardHeight);
      const int unusedExtraHeight = extraHeight - (topCardH - layout.topCardH);
      y += std::min(unusedExtraHeight / 3, kStandaloneNoRtcMaxVerticalOffset);
    }
    drawPerBookStatsCard(renderer, cardX, y, cardW, topCardH, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                         estimatedTimeLeftSeconds, layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_BACK), "", showEditButton ? tr(STR_EDIT) : "",
                                               showMoreButton ? tr(STR_MORE) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderGlobalStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* screenTitle,
                           const GlobalReadingStats& stats, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, true, showButtonHints, showRtcStats);
  CompactHeader::drawTitle(renderer, screenTitle);
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  int globalCardH = layout.globalCardH;
  int y = metrics.topPadding + std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap;

  if (showRtcStats) {
    const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
    const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
    const int compactContentHeight = std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap +
                                     layout.globalCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    const int perBookCompactContentHeight = std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap +
                                            layout.topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int perBookExtraHeight = std::max(0, availableHeight - perBookCompactContentHeight);
    const int targetGlobalCardH = globalRtcCardHeightForPerBookRowSpacing(layout, perBookExtraHeight);
    const int extraTopCardHeight = std::min(extraHeight, std::max(0, targetGlobalCardH - layout.globalCardH));
    const int remainingExtraHeight = extraHeight - extraTopCardHeight;
    const int timeOfDayExtraHeight = (remainingExtraHeight * 4) / 11;
    const int dayOfWeekExtraHeight = remainingExtraHeight - timeOfDayExtraHeight;
    const int timeOfDayCardH = timeOfDayH + timeOfDayExtraHeight;
    const int dayOfWeekCardH = dayOfWeekH + dayOfWeekExtraHeight;
    globalCardH += extraTopCardHeight;

    drawGlobalStatsCard(renderer, cardX, y, cardW, globalCardH, tr(STR_STATS_ALL_TIME), stats, layout);
    y += globalCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, timeOfDayCardH, tr(STR_STATS_TIME_OF_DAY), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, timeOfDayCardH, stats.timeOfDaySeconds, TIME_BUCKET_LABELS, layout);
    y += timeOfDayCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, dayOfWeekCardH, tr(STR_STATS_DAY_OF_WEEK), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, dayOfWeekCardH, stats.dayOfWeekSeconds, DAY_LABELS, layout);
  } else {
    const int compactContentHeight =
        std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap + layout.globalCardH;
    globalCardH += std::max(0, availableHeight - compactContentHeight);
    drawGlobalStatsCard(renderer, cardX, y, cardW, globalCardH, tr(STR_STATS_ALL_TIME), stats, layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels =
        mappedInput->mapLabels(tr(STR_HOME), tr(STR_HOME), tr(STR_BACK), showMoreButton ? tr(STR_MORE) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                             const std::string& bookTitle, const BookReadingStats& bookStats,
                             const float progressPercent, const bool hasEstimatedTimeLeft,
                             const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                             const bool showButtonHints, const bool showEditButton, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_READING_STATS), true);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const StatsLayout& layout =
      renderer.getScreenHeight() >= 700 && renderer.getScreenWidth() >= 500 ? kDefaultLayout : kCompactLayout;
  const int cardX = metrics.contentSidePadding;
  const int cardW = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int contentY = metrics.topPadding + std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);
  const int cardSpace = std::max(2, contentBottom - contentY - layout.cardGap);
  const int bookCardH = std::max(layout.topCardTitleH + 3, (cardSpace * 3) / 5);
  const int globalCardH = std::max(layout.topCardTitleH + 2, cardSpace - bookCardH);

  drawCombinedBookStatsCard(renderer, cardX, contentY, cardW, bookCardH, bookTitle, bookStats, progressPercent,
                            hasEstimatedTimeLeft, estimatedTimeLeftSeconds, layout);
  drawCombinedGlobalStatsCard(renderer, cardX, contentY + bookCardH + layout.cardGap, cardW, globalCardH, globalStats,
                              layout);

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_HOME), showEditButton ? tr(STR_EDIT) : tr(STR_HOME), "",
                                               showMoreButton ? tr(STR_MORE) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderReadingRhythmPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                             const DailyReadingHistory& dailyHistory, const GlobalReadingStats& stats,
                             const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_READING_RHYTHM));

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const bool compactWidth = screenW < 500;
  const int titleH = compactWidth ? 34 : 40;
  const int cardPadding = compactWidth ? 10 : 14;
  const int weekdayLabelW = compactWidth ? 38 : 46;
  const int monthLabelH = 25;
  const int rowStride = compactWidth ? 24 : 27;
  const int activeDotSize = compactWidth ? 12 : 14;
  const int inactiveDotSize = compactWidth ? 5 : 6;
  const int legendH = 42;
  const int chartTitleH = kCompactLayout.sectionTitleH;
  const int rhythmCardH = titleH + monthLabelH + rowStride * 7 + legendH + cardPadding;
  const int chartGap = compactWidth ? 10 : 18;
  const int weeklyChartH = chartTitleH + (compactWidth ? 86 : 98);
  const int monthlyChartH = weeklyChartH;
  const int headerBottom = metrics.topPadding + std::min(metrics.headerHeight, kDefaultLayout.headerHeight);
  const int bottomInset = statsBottomInset(metrics, showButtonHints);
  const int availableH = renderer.getScreenHeight() - headerBottom - bottomInset;
  const int baseContentH = rhythmCardH + chartGap + weeklyChartH;
  const int fullContentH = baseContentH + chartGap + monthlyChartH;
  const bool showMonthlyChart = availableH >= fullContentH + 12;
  const int contentH = showMonthlyChart ? fullContentH : baseContentH;
  int rhythmY = headerBottom + std::max(6, (availableH - contentH) / 2);

  renderer.drawRect(cardX, rhythmY, cardW, rhythmCardH);
  renderer.drawLine(cardX, rhythmY + titleH, cardX + cardW, rhythmY + titleH);
  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW,
                    rhythmY + (titleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, tr(STR_STATS_RECENT_ACTIVITY),
                    true);

  ReadingStatsDateTime now;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t displayDay = hasToday ? readingStatsDayIndex(now.date) : stats.readingHistoryAnchorDay;
  ReadingStatsDate displayDate;
  const bool hasDisplayDate = readingStatsDateFromDayIndex(displayDay, displayDate);
  const uint8_t displayDow = hasDisplayDate ? readingStatsDayOfWeekIndex(displayDate) : 0;
  const uint32_t currentWeekStart = displayDay >= displayDow ? displayDay - displayDow : 0;
  const uint32_t historySpanBeforeCurrentWeek = static_cast<uint32_t>(kReadingRhythmWeekCount - 1) * 7u;
  const uint32_t firstDay =
      currentWeekStart >= historySpanBeforeCurrentWeek ? currentWeekStart - historySpanBeforeCurrentWeek : 0;

  const int gridX = cardX + cardPadding + weekdayLabelW;
  const int gridW = cardW - cardPadding * 2 - weekdayLabelW;
  const int columnStride = std::max(1, gridW / kReadingRhythmWeekCount);
  const int gridTop = rhythmY + titleH + monthLabelH;
  uint8_t previousMonth = 0;

  for (int week = 0; week < kReadingRhythmWeekCount; ++week) {
    const uint32_t weekStartDay = firstDay + static_cast<uint32_t>(week) * 7u;
    ReadingStatsDate weekDate;
    if (readingStatsDateFromDayIndex(weekStartDay, weekDate) && weekDate.month != previousMonth) {
      char monthToken[8];
      formatReadingStatsMonthToken(weekDate, monthToken, sizeof(monthToken));
      const int monthX = gridX + week * columnStride;
      renderer.drawText(SMALL_FONT_ID, monthX, rhythmY + titleH + 3, monthToken);
      previousMonth = weekDate.month;
    }
  }

  for (int weekday = 0; weekday < 7; ++weekday) {
    const int rowCenterY = gridTop + weekday * rowStride + rowStride / 2;
    const char* weekdayText = I18N.get(DAY_LABELS[weekday]);
    const int labelY = rowCenterY - renderer.getLineHeight(SMALL_FONT_ID) / 2;
    renderer.drawText(SMALL_FONT_ID, cardX + cardPadding, labelY, weekdayText);

    for (int week = 0; week < kReadingRhythmWeekCount; ++week) {
      const uint32_t dayIndex = firstDay + static_cast<uint32_t>(week) * 7u + static_cast<uint32_t>(weekday);
      if (dayIndex > displayDay) {
        continue;
      }

      const int centerX = gridX + week * columnStride + columnStride / 2;
      const uint8_t readingMinutes = dailyHistory.minutesOnDay(dayIndex);
      int dotSize = inactiveDotSize;
      Color dotColor = Color::LightGray;
      readingIntensityStyle(readingMinutes, activeDotSize, inactiveDotSize, dotSize, dotColor);
      const int dotX = centerX - dotSize / 2;
      const int dotY = rowCenterY - dotSize / 2;
      renderer.fillRoundedRect(dotX, dotY, dotSize, dotSize, dotSize / 2, dotColor);

      if (hasToday && dayIndex == displayDay) {
        constexpr int todayRingGap = 3;
        const int ringSize = activeDotSize + todayRingGap * 2;
        renderer.drawRoundedRect(centerX - ringSize / 2, rowCenterY - ringSize / 2, ringSize, ringSize, 2, ringSize / 2,
                                 true);
      }
    }
  }

  const int legendCenterY = gridTop + rowStride * 7 + legendH / 2;
  const int legendTextY = legendCenterY - renderer.getLineHeight(SMALL_FONT_ID) / 2;
  int legendX = cardX + cardPadding;
  renderer.drawText(SMALL_FONT_ID, legendX, legendTextY, tr(STR_STATS_ACTIVE_DAY));
  legendX += renderer.getTextWidth(SMALL_FONT_ID, tr(STR_STATS_ACTIVE_DAY)) + 9;

  constexpr std::array<uint8_t, 3> legendMinutes = {1, kMediumReadingMinutes, kHighReadingMinutes};
  constexpr std::array<const char*, 3> legendLabels = {"1", "15", "30+"};
  for (size_t i = 0; i < legendMinutes.size(); ++i) {
    int dotSize = inactiveDotSize;
    Color dotColor = Color::LightGray;
    readingIntensityStyle(legendMinutes[i], activeDotSize, inactiveDotSize, dotSize, dotColor);
    renderer.fillRoundedRect(legendX, legendCenterY - dotSize / 2, dotSize, dotSize, dotSize / 2, dotColor);
    legendX += dotSize + 4;
    renderer.drawText(SMALL_FONT_ID, legendX, legendTextY, legendLabels[i]);
    legendX += renderer.getTextWidth(SMALL_FONT_ID, legendLabels[i]) + 8;
  }

  const int todayRingSize = activeDotSize + 6;
  const int todayLegendW = todayRingSize + 6 + renderer.getTextWidth(SMALL_FONT_ID, tr(STR_STATS_TODAY));
  const int todayLegendX = cardX + cardW - cardPadding - todayLegendW;
  renderer.drawRoundedRect(todayLegendX, legendCenterY - todayRingSize / 2, todayRingSize, todayRingSize, 2,
                           todayRingSize / 2, true);
  renderer.drawText(SMALL_FONT_ID, todayLegendX + todayRingSize + 6, legendTextY, tr(STR_STATS_TODAY));

  std::array<uint16_t, kReadingRhythmWeekCount> weeklyMinutes{};
  uint32_t previousWeeksMinutes = 0;
  for (int week = 0; week < kReadingRhythmWeekCount; ++week) {
    for (int weekday = 0; weekday < 7; ++weekday) {
      const uint32_t dayIndex = firstDay + static_cast<uint32_t>(week) * 7u + static_cast<uint32_t>(weekday);
      if (dayIndex <= displayDay) {
        const uint8_t minutes = dailyHistory.minutesOnDay(dayIndex);
        weeklyMinutes[static_cast<size_t>(week)] += minutes;
      }
    }
    if (week < kReadingRhythmWeekCount - 1) {
      previousWeeksMinutes += weeklyMinutes[static_cast<size_t>(week)];
    }
  }

  const uint16_t maxWeeklyMinutes = *std::max_element(weeklyMinutes.begin(), weeklyMinutes.end());
  const uint16_t chartScaleMinutes = weeklyChartScaleMinutes(maxWeeklyMinutes);
  const uint16_t averageWeeklyMinutes =
      static_cast<uint16_t>(previousWeeksMinutes / static_cast<uint32_t>(kReadingRhythmWeekCount - 1));
  const uint16_t currentWeekMinutes = weeklyMinutes.back();

  const int chartY = rhythmY + rhythmCardH + chartGap;
  drawSectionCard(renderer, cardX, chartY, cardW, weeklyChartH, tr(STR_STATS_WEEKLY_TIME), kCompactLayout);
  const int chartTop = chartY + chartTitleH + (compactWidth ? 7 : 9);
  const int footerH = renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const int chartBottom = chartY + weeklyChartH - footerH - 5;
  const int chartHeight = std::max(1, chartBottom - chartTop);
  const int chartRight = gridX + columnStride * kReadingRhythmWeekCount;

  {
    char scaleLabel[16];
    formatRhythmMinutes(chartScaleMinutes, scaleLabel, sizeof(scaleLabel));
    renderer.drawText(SMALL_FONT_ID, cardX + cardPadding, chartTop, scaleLabel);
  }
  renderer.drawLine(gridX, chartBottom, chartRight, chartBottom);

  if (averageWeeklyMinutes > 0) {
    const int averageY =
        chartBottom - static_cast<int>((static_cast<uint32_t>(chartHeight) * averageWeeklyMinutes) / chartScaleMinutes);
    for (int dashX = gridX; dashX < chartRight; dashX += 8) {
      renderer.drawLine(dashX, averageY, std::min(dashX + 3, chartRight), averageY);
    }
  }

  const int barWidth = compactWidth ? 10 : 14;
  for (int week = 0; week < kReadingRhythmWeekCount; ++week) {
    const uint16_t minutes = weeklyMinutes[static_cast<size_t>(week)];
    if (minutes == 0) {
      continue;
    }
    const int barHeight =
        std::max(2, static_cast<int>((static_cast<uint32_t>(chartHeight) * minutes) / chartScaleMinutes));
    const int barCenterX = gridX + week * columnStride + columnStride / 2;
    const int barX = barCenterX - barWidth / 2;
    const int barY = chartBottom - std::min(barHeight, chartHeight);
    if (week == kReadingRhythmWeekCount - 1) {
      renderer.fillRect(barX, barY, barWidth, chartBottom - barY);
    } else {
      renderer.fillRectDither(barX, barY, barWidth, chartBottom - barY, Color::DarkGray);
    }
  }

  char averageValue[16];
  char currentValue[16];
  formatRhythmMinutes(averageWeeklyMinutes, averageValue, sizeof(averageValue));
  formatRhythmMinutes(currentWeekMinutes, currentValue, sizeof(currentValue));
  const int footerY = chartY + weeklyChartH - footerH + 2;
  const char* averageText = tr(STR_STATS_WEEKLY_AVERAGE);
  const int averageX = cardX + cardPadding;
  renderer.drawText(SMALL_FONT_ID, averageX, footerY, averageText);
  renderer.drawText(SMALL_FONT_ID, averageX + renderer.getTextWidth(SMALL_FONT_ID, averageText) + 4, footerY,
                    averageValue);

  const char* currentText = tr(STR_STATS_THIS_WEEK);
  const int currentTextW = renderer.getTextWidth(SMALL_FONT_ID, currentText);
  const int currentValueW = renderer.getTextWidth(SMALL_FONT_ID, currentValue);
  const int currentX = cardX + cardW - cardPadding - currentTextW - 4 - currentValueW;
  renderer.drawText(SMALL_FONT_ID, currentX, footerY, currentText);
  renderer.drawText(SMALL_FONT_ID, currentX + currentTextW + 4, footerY, currentValue);

  if (showMonthlyChart) {
    std::array<ReadingStatsDate, kReadingRhythmMonthCount> monthDates{};
    std::array<uint8_t, kReadingRhythmMonthCount> monthlyReadingDays{};

    ReadingStatsDate monthCursor = displayDate;
    monthCursor.day = 1;
    for (size_t offset = 0; offset < kReadingRhythmMonthCount; ++offset) {
      const size_t monthIndex = kReadingRhythmMonthCount - 1 - offset;
      monthDates[monthIndex] = monthCursor;
      addDaysToReadingStatsDate(monthCursor, -1);
      monthCursor.day = 1;
    }

    for (size_t monthIndex = 0; monthIndex < kReadingRhythmMonthCount; ++monthIndex) {
      const uint32_t monthStartDay = readingStatsDayIndex(monthDates[monthIndex]);
      const uint8_t monthDayCount = daysInMonth(monthDates[monthIndex].year, monthDates[monthIndex].month);
      for (uint8_t dayOffset = 0; dayOffset < monthDayCount; ++dayOffset) {
        if (dailyHistory.minutesOnDay(monthStartDay + dayOffset) > 0) {
          ++monthlyReadingDays[monthIndex];
        }
      }
    }

    const uint16_t totalReadingDays =
        std::accumulate(monthlyReadingDays.begin(), monthlyReadingDays.end(), uint16_t{0});
    const uint8_t averageMonthlyReadingDays = static_cast<uint8_t>(
        (totalReadingDays + static_cast<uint16_t>(kReadingRhythmMonthCount / 2)) / kReadingRhythmMonthCount);

    const int monthlyY = chartY + weeklyChartH + chartGap;
    drawSectionCard(renderer, cardX, monthlyY, cardW, monthlyChartH, tr(STR_STATS_READING_DAYS_BY_MONTH),
                    kCompactLayout);

    const int monthlyLineH = renderer.getLineHeight(SMALL_FONT_ID);
    const int monthlyValueY = monthlyY + chartTitleH + 3;
    const int monthlyFooterY = monthlyY + monthlyChartH - monthlyLineH - 3;
    const int monthlyLabelY = monthlyFooterY - monthlyLineH;
    const int monthlyChartTop = monthlyValueY + monthlyLineH + 2;
    const int monthlyChartBottom = monthlyLabelY - 3;
    const int monthlyChartHeight = std::max(1, monthlyChartBottom - monthlyChartTop);
    const int monthlyGridX = cardX + cardPadding;
    const int monthlyGridW = cardW - cardPadding * 2;
    const int monthlyColumnStride = std::max(1, monthlyGridW / static_cast<int>(kReadingRhythmMonthCount));
    const int monthlyRight = monthlyGridX + monthlyColumnStride * static_cast<int>(kReadingRhythmMonthCount);
    const int monthlyBarWidth = compactWidth ? 9 : 13;

    renderer.drawLine(monthlyGridX, monthlyChartBottom, monthlyRight, monthlyChartBottom);

    for (size_t monthIndex = 0; monthIndex < kReadingRhythmMonthCount; ++monthIndex) {
      const int columnX = monthlyGridX + static_cast<int>(monthIndex) * monthlyColumnStride;
      const int centerX = columnX + monthlyColumnStride / 2;
      const uint8_t readingDays = monthlyReadingDays[monthIndex];

      char valueLabel[4];
      if (readingDays == 0) {
        snprintf(valueLabel, sizeof(valueLabel), "-");
      } else {
        snprintf(valueLabel, sizeof(valueLabel), "%u", static_cast<unsigned>(readingDays));
      }
      drawCenteredLabel(renderer, SMALL_FONT_ID, columnX, monthlyColumnStride, monthlyValueY, valueLabel);

      if (readingDays > 0) {
        const int barHeight =
            std::max(2, static_cast<int>((static_cast<uint32_t>(monthlyChartHeight) * readingDays) / 31u));
        const int barY = monthlyChartBottom - std::min(barHeight, monthlyChartHeight);
        renderer.fillRoundedRect(centerX - monthlyBarWidth / 2, barY, monthlyBarWidth, monthlyChartBottom - barY, 2,
                                 Color::DarkGray);
      }

      char monthLabel[8];
      formatReadingStatsMonthToken(monthDates[monthIndex], monthLabel, sizeof(monthLabel));
      drawCenteredLabel(renderer, SMALL_FONT_ID, columnX, monthlyColumnStride, monthlyLabelY, monthLabel);
    }

    char totalValue[20];
    snprintf(totalValue, sizeof(totalValue), "%u %s", static_cast<unsigned>(totalReadingDays),
             dayCountText(totalReadingDays));
    const char* totalText = tr(STR_STATS_MONTHLY_TOTAL);
    renderer.drawText(SMALL_FONT_ID, cardX + cardPadding, monthlyFooterY, totalText);
    renderer.drawText(SMALL_FONT_ID, cardX + cardPadding + renderer.getTextWidth(SMALL_FONT_ID, totalText) + 4,
                      monthlyFooterY, totalValue);

    char monthlyAverageValue[20];
    snprintf(monthlyAverageValue, sizeof(monthlyAverageValue), "%u %s",
             static_cast<unsigned>(averageMonthlyReadingDays), dayCountText(averageMonthlyReadingDays));
    const char* monthlyAverageText = tr(STR_STATS_MONTHLY_AVERAGE);
    const int monthlyAverageW = renderer.getTextWidth(SMALL_FONT_ID, monthlyAverageText);
    const int monthlyAverageValueW = renderer.getTextWidth(SMALL_FONT_ID, monthlyAverageValue);
    const int monthlyAverageX = cardX + cardW - cardPadding - monthlyAverageW - 4 - monthlyAverageValueW;
    renderer.drawText(SMALL_FONT_ID, monthlyAverageX, monthlyFooterY, monthlyAverageText);
    renderer.drawText(SMALL_FONT_ID, monthlyAverageX + monthlyAverageW + 4, monthlyFooterY, monthlyAverageValue);
  }

  if (showButtonHints && mappedInput) {
    const auto labels =
        mappedInput->mapLabels(tr(STR_HOME), tr(STR_HOME), tr(STR_BACK), showMoreButton ? tr(STR_MORE) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderFinishedBooksPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                             const std::vector<FinishedBookEntry>& finishedBooks,
                             const GlobalReadingStats& globalStats, const size_t pageIndex,
                             const bool showButtonHints, const bool showPreviousPage,
                             const bool showNextPage, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_ACHIEVEMENT_BOOKS_COMPLETED));

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  const int buttonTop = screenH - (showButtonHints ? metrics.buttonHintsHeight : metrics.verticalSpacing);
  const int totalCardY = headerBottom + metrics.verticalSpacing;
  const int totalCardH = std::max(62, renderer.getLineHeight(UI_12_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + 20);

  renderer.drawRoundedRect(cardX, totalCardY, cardW, totalCardH, 2, 10, true);
  char duration[32];
  BookReadingStats::formatDuration(globalStats.totalReadingSeconds, duration, sizeof(duration));
  renderer.drawCenteredText(UI_12_FONT_ID, totalCardY + 10, duration, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, totalCardY + totalCardH - renderer.getLineHeight(SMALL_FONT_ID) - 8,
                            tr(STR_STATS_TOTAL_READING_TIME_LBL));

  const int listY = totalCardY + totalCardH + metrics.verticalSpacing;
  const int availableListH = std::max(0, buttonTop - listY - metrics.verticalSpacing);
  const int rowGap = std::max(4, metrics.verticalSpacing / 2);
  const int desiredRowH = std::max(54, renderer.getLineHeight(UI_10_FONT_ID) +
                                           renderer.getLineHeight(SMALL_FONT_ID) + 18);
  const int maxRows =
      std::min<int>(FINISHED_BOOKS_ENTRIES_PER_PAGE, (availableListH + rowGap) / (desiredRowH + rowGap));
  const size_t firstEntry = pageIndex * FINISHED_BOOKS_ENTRIES_PER_PAGE;
  const size_t remainingEntries = firstEntry < finishedBooks.size() ? finishedBooks.size() - firstEntry : 0;

  if (remainingEntries == 0 || maxRows <= 0) {
    const int emptyY = listY + std::max(0, (availableListH - renderer.getLineHeight(UI_12_FONT_ID) -
                                          renderer.getLineHeight(UI_10_FONT_ID) - 8) /
                                                 2);
    renderer.drawCenteredText(UI_12_FONT_ID, emptyY, "0", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, emptyY + renderer.getLineHeight(UI_12_FONT_ID) + 8,
                              tr(STR_ACHIEVEMENT_BOOKS_COMPLETED));
  } else {
    const int visibleRows = std::min<int>(maxRows, remainingEntries);
    const int rowH = std::min(desiredRowH, (availableListH - rowGap * (visibleRows - 1)) / visibleRows);
    for (int index = 0; index < visibleRows; ++index) {
      const auto& entry = finishedBooks[firstEntry + index];
      const int rowY = listY + index * (rowH + rowGap);
      renderer.drawRoundedRect(cardX, rowY, cardW, rowH, 1, 8, true);

      const auto title =
          renderer.truncatedText(UI_10_FONT_ID, entry.title.c_str(), cardW - metrics.contentSidePadding * 2,
                                 EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, cardX + metrics.contentSidePadding, rowY + 7, title.c_str(), true,
                        EpdFontFamily::BOLD);

      char date[24];
      char readingTime[24];
      char subtitle[56];
      formatReadingStatsShortDate(entry.finishedDate, date, sizeof(date));
      BookReadingStats::formatDuration(entry.totalReadingSeconds, readingTime, sizeof(readingTime));
      snprintf(subtitle, sizeof(subtitle), "%s - %s", date, readingTime);
      renderer.drawText(SMALL_FONT_ID, cardX + metrics.contentSidePadding,
                        rowY + rowH - renderer.getLineHeight(SMALL_FONT_ID) - 7, subtitle);
    }
  }

  if (showButtonHints && mappedInput) {
    const char* previousLabel = showPreviousPage ? tr(STR_PREVIOUS_SHORT) : tr(STR_BACK);
    const char* nextLabel = showNextPage ? tr(STR_NEXT_SHORT) : (showMoreButton ? tr(STR_MORE) : "");
    const auto labels = mappedInput->mapLabels(tr(STR_HOME), tr(STR_HOME), previousLabel, nextLabel);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderNoRtcCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& bookStats,
                                  const float progressPercent, const bool hasEstimatedTimeLeft,
                                  const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& deviceStats,
                                  const GlobalReadingStats* allDevicesStats, const bool showButtonHints) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getNoRtcCombinedLayout(renderer, showButtonHints, allDevicesStats != nullptr);
  CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  const int compactContentHeight = noRtcCombinedContentHeight(layout, allDevicesStats != nullptr);
  const int extraHeight = std::max(0, availableHeight - compactContentHeight);
  const int visibleCardCount = allDevicesStats ? 3 : 2;
  const int extraPerCard = visibleCardCount > 0 ? extraHeight / visibleCardCount : 0;
  const int extraRemainder = visibleCardCount > 0 ? extraHeight % visibleCardCount : 0;
  const int perBookExtraHeight = extraPerCard + (extraRemainder > 0 ? 1 : 0);
  const int deviceExtraHeight = extraPerCard + (extraRemainder > 1 ? 1 : 0);
  const int allDevicesExtraHeight = allDevicesStats ? extraPerCard : 0;
  const int perBookCardH = noRtcCardBaseHeight(layout) + perBookExtraHeight;
  const int deviceCardH = layout.globalCardH + deviceExtraHeight;
  const int allDevicesCardH = layout.globalCardH + allDevicesExtraHeight;

  int y = metrics.topPadding + std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap;
  drawPerBookStatsCard(renderer, cardX, y, cardW, perBookCardH, bookTitle, bookStats, progressPercent,
                       hasEstimatedTimeLeft, estimatedTimeLeftSeconds, layout);
  y += perBookCardH + layout.cardGap;

  drawGlobalStatsCard(renderer, cardX, y, cardW, deviceCardH, tr(STR_STATS_THIS_DEVICE_SCREEN), deviceStats, layout);
  y += deviceCardH;

  if (allDevicesStats) {
    y += layout.cardGap;
    drawGlobalStatsCard(renderer, cardX, y, cardW, allDevicesCardH, tr(STR_STATS_ALL_DEVICES_SCREEN), *allDevicesStats,
                        layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_HOME), tr(STR_HOME), "", tr(STR_MORE));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderEditBookDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                             const BookReadingStats& stats, const int selectedField, const bool showButtonHints) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int cardW = pageWidth - 120;
  const int cardH = 250;
  const int cardX = (pageWidth - cardW) / 2;
  const int cardY = 138;

  const std::string visibleTitle =
      renderer.truncatedText(UI_12_FONT_ID, bookTitle.c_str(), pageWidth - 80, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, 96, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawRect(cardX, cardY, cardW, cardH);

  const int sectionGap = 104;
  const int row1Y = cardY + 66;
  const int row2Y = row1Y + sectionGap;
  const int monthW = 52;
  const int dayW = 46;
  const int yearW = 68;
  const int gap = 14;
  const int totalFieldW = monthW + gap + dayW + gap + yearW;
  const int fieldStartX = cardX + (cardW - totalFieldW) / 2;

  char monthBuf[8];
  char dayBuf[8];
  char yearBuf[8];

  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, cardY + 24, tr(STR_STATS_START_DATE), true);
  formatReadingStatsMonthToken(stats.startDate, monthBuf, sizeof(monthBuf));
  snprintf(dayBuf, sizeof(dayBuf), "%s", stats.startDate.isValid() ? "" : "-");
  if (stats.startDate.isValid()) {
    snprintf(dayBuf, sizeof(dayBuf), "%02u", static_cast<unsigned>(stats.startDate.day));
    snprintf(yearBuf, sizeof(yearBuf), "%u", static_cast<unsigned>(stats.startDate.year));
  } else {
    snprintf(dayBuf, sizeof(dayBuf), "-");
    snprintf(yearBuf, sizeof(yearBuf), "-");
  }
  drawDateField(renderer, fieldStartX, row1Y, monthW, monthBuf, selectedField == 0);
  drawDateField(renderer, fieldStartX + monthW + gap, row1Y, dayW, dayBuf, selectedField == 1);
  drawDateField(renderer, fieldStartX + monthW + gap + dayW + gap, row1Y, yearW, yearBuf, selectedField == 2);

  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, cardY + 24 + sectionGap, tr(STR_STATS_FINISHED_DATE), true);
  const bool showFinishedFields = stats.isCompleted && stats.finishedDate.isValid();
  formatReadingStatsMonthToken(showFinishedFields ? stats.finishedDate : ReadingStatsDate{}, monthBuf,
                               sizeof(monthBuf));
  if (showFinishedFields) {
    snprintf(dayBuf, sizeof(dayBuf), "%02u", static_cast<unsigned>(stats.finishedDate.day));
    snprintf(yearBuf, sizeof(yearBuf), "%u", static_cast<unsigned>(stats.finishedDate.year));
  } else {
    snprintf(dayBuf, sizeof(dayBuf), "-");
    snprintf(yearBuf, sizeof(yearBuf), "-");
  }
  drawDateField(renderer, fieldStartX, row2Y, monthW, monthBuf, selectedField == 3);
  drawDateField(renderer, fieldStartX + monthW + gap, row2Y, dayW, dayBuf, selectedField == 4);
  drawDateField(renderer, fieldStartX + monthW + gap + dayW + gap, row2Y, yearW, yearBuf, selectedField == 5);

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_BACK), tr(STR_NEXT_FIELD), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderReadingAchievementPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& stats,
                                  const GlobalReadingStats& globalStats, const bool showButtonHints) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_ACHIEVEMENT_TITLE));

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const bool compact = screenH < 620;
  const int centerX = screenW / 2;
  const int medalSize = compact ? 62 : 82;
  const int medalY = metrics.topPadding + (compact ? 76 : 88);
  const int medalX = centerX - medalSize / 2;

  const int ray = compact ? 18 : 24;
  renderer.drawLine(centerX, medalY - ray, centerX, medalY - 5, 2, true);
  renderer.drawLine(centerX, medalY + medalSize + 5, centerX, medalY + medalSize + ray, 2, true);
  renderer.drawLine(medalX - ray, medalY + medalSize / 2, medalX - 5, medalY + medalSize / 2, 2, true);
  renderer.drawLine(medalX + medalSize + 5, medalY + medalSize / 2, medalX + medalSize + ray,
                    medalY + medalSize / 2, 2, true);
  renderer.drawLine(medalX - 13, medalY - 13, medalX - 4, medalY - 4, 2, true);
  renderer.drawLine(medalX + medalSize + 4, medalY - 4, medalX + medalSize + 13, medalY - 13, 2, true);

  renderer.fillRoundedRect(medalX, medalY, medalSize, medalSize, medalSize / 2, Color::Black);
  renderer.fillRoundedRect(medalX + 7, medalY + 7, medalSize - 14, medalSize - 14, (medalSize - 14) / 2,
                           Color::White);
  renderer.drawLine(medalX + medalSize / 4, medalY + medalSize / 2,
                    medalX + medalSize * 2 / 5, medalY + medalSize * 2 / 3, compact ? 4 : 5, true);
  renderer.drawLine(medalX + medalSize * 2 / 5, medalY + medalSize * 2 / 3,
                    medalX + medalSize * 3 / 4, medalY + medalSize / 3, compact ? 4 : 5, true);

  const int celebrationY = medalY + medalSize + (compact ? 12 : 18);
  renderer.drawCenteredText(UI_12_FONT_ID, celebrationY, tr(STR_ACHIEVEMENT_SUBTITLE), true,
                            EpdFontFamily::BOLD);
  const std::string visibleTitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), screenW - metrics.contentSidePadding * 4,
                             EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, celebrationY + renderer.getLineHeight(UI_12_FONT_ID) + 7,
                            visibleTitle.c_str(), true, EpdFontFamily::BOLD);

  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int cardY = celebrationY + renderer.getLineHeight(UI_12_FONT_ID) +
                    renderer.getLineHeight(UI_10_FONT_ID) + (compact ? 14 : 24);
  const int rowH = compact ? 74 : 94;
  const int cardH = rowH * 2;
  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 2, 12, true);
  renderer.drawLine(cardX, cardY + rowH, cardX + cardW, cardY + rowH);
  const int thirdW = cardW / 3;
  for (int column = 1; column < 3; ++column) {
    renderer.drawLine(cardX + thirdW * column, cardY, cardX + thirdW * column, cardY + cardH);
  }

  char value[32];
  BookReadingStats::formatDuration(stats.totalReadingSeconds, value, sizeof(value));
  drawStatCell(renderer, cardX, thirdW, cardY, rowH, value, tr(STR_STATS_TIME_LBL));

  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(stats.sessionCount));
  drawStatCell(renderer, cardX + thirdW, thirdW, cardY, rowH, value, tr(STR_STATS_SESSIONS_LBL));

  const uint32_t averageSeconds = stats.sessionCount > 0 ? stats.totalReadingSeconds / stats.sessionCount : 0;
  BookReadingStats::formatDuration(averageSeconds, value, sizeof(value));
  drawStatCell(renderer, cardX + thirdW * 2, thirdW, cardY, rowH, value, tr(STR_STATS_AVG_SESSION_LBL));

  size_t favoriteBucket = 0;
  for (size_t bucket = 1; bucket < stats.timeOfDaySeconds.size(); ++bucket) {
    if (stats.timeOfDaySeconds[bucket] > stats.timeOfDaySeconds[favoriteBucket]) {
      favoriteBucket = bucket;
    }
  }
  const char* favoriteValue =
      stats.totalReadingSeconds > 0 ? I18N.get(TIME_BUCKET_LABELS[favoriteBucket]) : tr(STR_STATS_NEW_READER);
  drawStatCell(renderer, cardX, thirdW, cardY + rowH, rowH, favoriteValue, tr(STR_ACHIEVEMENT_FAVORITE_TIME));

  const uint16_t readingDays =
      stats.startDate.isValid() && stats.finishedDate.isValid()
          ? readingSpanDaysInclusive(stats.startDate, stats.finishedDate)
          : 0;
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(readingDays));
  drawStatCell(renderer, cardX + thirdW, thirdW, cardY + rowH, rowH, value, dayCountText(readingDays));

  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(globalStats.completedBooks));
  drawStatCell(renderer, cardX + thirdW * 2, thirdW, cardY + rowH, rowH, value,
               tr(STR_ACHIEVEMENT_BOOKS_COMPLETED));

  const int totalReadingCardY = cardY + cardH + (compact ? 10 : 44);
  if (!compact && stats.startDate.isValid() && stats.finishedDate.isValid()) {
    char startDate[20];
    char finishDate[20];
    formatReadingStatsShortDate(stats.startDate, startDate, sizeof(startDate));
    formatReadingStatsShortDate(stats.finishedDate, finishDate, sizeof(finishDate));
    char timeline[64];
    snprintf(timeline, sizeof(timeline), tr(STR_ACHIEVEMENT_TIMELINE_FORMAT), startDate, finishDate,
             static_cast<unsigned>(readingDays));
    renderer.drawCenteredText(SMALL_FONT_ID, cardY + cardH + 18, timeline);
  }

  const int buttonHintsTop =
      screenH - (showButtonHints ? metrics.buttonHintsHeight : metrics.contentSidePadding);
  const int totalReadingCardH = std::min(compact ? 54 : 68, buttonHintsTop - totalReadingCardY - 10);
  if (totalReadingCardH >= 44) {
    BookReadingStats::formatDuration(globalStats.totalReadingSeconds, value, sizeof(value));
    renderer.drawRoundedRect(cardX, totalReadingCardY, cardW, totalReadingCardH, 2, 12, true);
    drawStatCell(renderer, cardX, cardW, totalReadingCardY, totalReadingCardH, value,
                 tr(STR_STATS_TOTAL_READING_TIME_LBL));
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_HOME), tr(STR_HOME), "", tr(STR_HOME));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}
