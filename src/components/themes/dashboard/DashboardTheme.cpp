#include "DashboardTheme.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "components/UITheme.h"
#include "components/icons/afternoon.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/cover.h"
#include "components/icons/evening.h"
#include "components/icons/morning.h"
#include "components/icons/night.h"
#include "components/icons/streak.h"
#include "fontIds.h"
#include "images/Logo120.h"

namespace {
constexpr int kContentInsetX4 = 20;
constexpr int kContentInsetX3 = 75;
constexpr int kTopInset = 20;
constexpr int kCoverCornerRadius = 8;
constexpr int kStatsColumnWidth = 105;
constexpr int kStatsColumnWidthWide = 120;
constexpr int kCoverStatsGap = 15;
constexpr int kPairInwardShiftX3 = 15;
constexpr int kTitleTopGap = 28;
constexpr int kTitleChapterGap = 8;
constexpr int kBookTitleMaxLines = 2;
constexpr int kBookChapterMaxLines = 2;
constexpr int kFooterIconSize = 24;
constexpr int kFooterIconTextGap = 18;
constexpr int kFooterBottomGap = 57;
constexpr int kStatsRowCount = 7;
constexpr int kStatsRowCountX4 = 6;
constexpr int kStatsValueLabelGap = 1;
constexpr int kHomeCardInset = 20;
constexpr int kHomeCardTopGap = 8;
constexpr int kHomeCardBottomGap = 12;
constexpr int kHomeCardRadius = 26;
constexpr int kHomeCardInnerPadding = 24;
constexpr int kHomePillRadius = 18;
constexpr int kHomePanelRadius = 20;
constexpr int kHomeProgressBarHeight = 10;
constexpr int kHomeLogoSize = 120;
constexpr int kHomeMenuSideInset = 24;
constexpr int kHomeMenuRowHeight = 50;
constexpr int kHomeMenuRowGap = 8;
constexpr int kHomeMenuRadius = 24;
constexpr int kHomeHintSideInset = 20;
constexpr int kHomeHintGroupGap = 10;
constexpr int kHomeHintBottomMargin = 10;
constexpr int kHomeHintRadius = 15;
#ifdef OMIT_LARGE_FONT
constexpr int kHomeDisplayFontId = UI_12_FONT_ID;
#else
constexpr int kHomeDisplayFontId = LEXENDDECA_16_FONT_ID;
#endif

bool isWideScreen(const GfxRenderer& renderer) { return renderer.getScreenWidth() >= 560; }

int contentInset(const GfxRenderer& renderer) { return isWideScreen(renderer) ? kContentInsetX3 : kContentInsetX4; }

Rect coverRectForScreen(const GfxRenderer& renderer, const Rect& rect) {
  const int inset = contentInset(renderer);
  const int statsW = isWideScreen(renderer) ? kStatsColumnWidthWide : kStatsColumnWidth;
  const int maxCoverW = renderer.getScreenWidth() - inset * 2 - statsW - kCoverStatsGap;
  const int coverW = std::min(DashboardMetrics::homeCoverImageWidth, maxCoverW);
  const int coverH = std::min(DashboardMetrics::homeCoverImageHeight, (coverW * 3) / 2);
  return Rect{inset + (gpio.deviceIsX3() ? kPairInwardShiftX3 : 0), rect.y + kTopInset, coverW, coverH};
}

Rect fittedBitmapRect(const Bitmap& bitmap, const Rect& target) {
  if (bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0 || target.width <= 0 || target.height <= 0) {
    return target;
  }

  const float widthScale = static_cast<float>(target.width) / static_cast<float>(bitmap.getWidth());
  const float heightScale = static_cast<float>(target.height) / static_cast<float>(bitmap.getHeight());
  const float scale = std::min(1.0f, std::min(widthScale, heightScale));
  const int drawnW = std::min(target.width, std::max(1, static_cast<int>(std::ceil(bitmap.getWidth() * scale))));
  const int drawnH = std::min(target.height, std::max(1, static_cast<int>(std::ceil(bitmap.getHeight() * scale))));
  return Rect{target.x + (target.width - drawnW) / 2, target.y + (target.height - drawnH) / 2, drawnW, drawnH};
}

std::string coverPathForRect(const RecentBook& book, const Rect& imageRect) {
  if (book.coverBmpPath.empty()) {
    return {};
  }
  if (FsHelpers::hasEpubExtension(book.path)) {
    const std::string adaptivePath =
        Epub(book.path, "/.crosspoint").getAdaptiveThumbBmpPath(imageRect.width, imageRect.height);
    if (Storage.exists(adaptivePath.c_str())) {
      return adaptivePath;
    }
  }
  return UITheme::getCoverThumbPath(book.coverBmpPath, imageRect.width, imageRect.height);
}

void drawMissingBookCover(const GfxRenderer& renderer, const Rect& coverRect, const RecentBook& book) {
  renderer.fillRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, kCoverCornerRadius,
                           Color::White);
  renderer.drawRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, 1, kCoverCornerRadius, true);

  constexpr int iconSize = 32;
  renderer.drawIcon(CoverIcon, coverRect.x + (coverRect.width - iconSize) / 2, coverRect.y + 36, iconSize, iconSize);

  constexpr int textPadding = 14;
  const int textW = coverRect.width - textPadding * 2;
  const char* title = book.title.empty() ? book.path.c_str() : book.title.c_str();
  auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title, textW, 4, EpdFontFamily::BOLD);
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  int textY = coverRect.y + (coverRect.height - static_cast<int>(titleLines.size()) * lineH) / 2;
  for (const auto& line : titleLines) {
    const int lineW = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, coverRect.x + (coverRect.width - lineW) / 2, textY, line.c_str(), true,
                      EpdFontFamily::BOLD);
    textY += lineH;
  }
}

void drawBookCover(const GfxRenderer& renderer, const Rect& coverRect, const RecentBook& book,
                   const Color backgroundColor) {
  bool hasCover = false;
  const std::string coverBmpPath = coverPathForRect(book, coverRect);
  if (!coverBmpPath.empty() && Storage.exists(coverBmpPath.c_str())) {
    FsFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const Rect bitmapRect = fittedBitmapRect(bitmap, coverRect);
        renderer.fillRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, kCoverCornerRadius,
                                 backgroundColor);
        renderer.fillRoundedRect(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height, kCoverCornerRadius,
                                 Color::White);
        renderer.drawBitmap(bitmap, bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height);
        renderer.maskRoundedRectOutsideCorners(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height,
                                               kCoverCornerRadius, backgroundColor);
        renderer.drawRoundedRect(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height, 1, kCoverCornerRadius,
                                 true);
        hasCover = true;
      }
      file.close();
    }
  }

  if (!hasCover) {
    drawMissingBookCover(renderer, coverRect, book);
  }
}

void drawRightAlignedText(const GfxRenderer& renderer, const int fontId, const int rightX, const int y,
                          const char* text, const bool bold = false, const bool black = true) {
  const EpdFontFamily::Style style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int width = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, rightX - width, y, text, black, style);
}

void formatCompactDuration(const uint32_t seconds, char* buf, const size_t len) {
  if (seconds < 60) {
    snprintf(buf, len, "%s", tr(STR_STATS_LESS_THAN_MIN));
    return;
  }
  const uint32_t minutes = (seconds + 30u) / 60u;
  if (minutes < 60) {
    snprintf(buf, len, tr(STR_DURATION_MINUTES_SHORT_FORMAT), static_cast<unsigned long>(minutes));
    return;
  }
  const uint32_t hours = minutes / 60u;
  const uint32_t remainder = minutes % 60u;
  if (remainder == 0) {
    snprintf(buf, len, tr(STR_DURATION_HOURS_SHORT_FORMAT), static_cast<unsigned long>(hours));
  } else {
    snprintf(buf, len, tr(STR_DURATION_HOURS_MINUTES_SHORT_FORMAT), static_cast<unsigned long>(hours),
             static_cast<unsigned long>(remainder));
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

bool estimatedTimeLeft(const BookReadingStats& stats, const float progressPercent, uint32_t& seconds) {
  if (stats.estimatedTimeLeftSeconds > 0) {
    seconds = stats.estimatedTimeLeftSeconds;
    return true;
  }
  return fallbackEstimatedTimeLeft(stats, progressPercent, seconds);
}

Rect homeCardRect(const GfxRenderer& renderer, const Rect& rect) {
  (void)renderer;
  const int cardX = rect.x + kHomeCardInset;
  const int cardY = rect.y + kHomeCardTopGap;
  const int cardW = std::max(0, rect.width - kHomeCardInset * 2);
  const int bottomLimit = rect.y + rect.height - kHomeCardBottomGap;
  const int cardH = std::max(0, bottomLimit - cardY);
  return Rect{cardX, cardY, cardW, cardH};
}

void drawHomeSectionPill(const GfxRenderer& renderer, const Rect& card, const char* label) {
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
  constexpr int horizontalPadding = 15;
  constexpr int verticalPadding = 9;
  const int pillW = textW + horizontalPadding * 2;
  const int pillH = lineH + verticalPadding * 2;
  const int pillX = card.x + kHomeCardInnerPadding;
  const int pillY = card.y + kHomeCardInnerPadding;
  renderer.fillRoundedRect(pillX, pillY, pillW, pillH, kHomePillRadius, Color::Black);
  renderer.drawText(UI_10_FONT_ID, pillX + horizontalPadding, pillY + verticalPadding, label, false,
                    EpdFontFamily::BOLD);
}

int drawHomeTitle(const GfxRenderer& renderer, const Rect& card, const char* title, const bool compact) {
  const int titleX = card.x + kHomeCardInnerPadding;
  const int titleW = card.width - kHomeCardInnerPadding * 2;
  const int titleY = card.y + (compact ? 78 : 105);
  const int maxLines = compact ? 2 : 3;
  const auto lines = renderer.wrappedText(kHomeDisplayFontId, title, titleW, maxLines, EpdFontFamily::BOLD);
  const int lineH = renderer.getLineHeight(kHomeDisplayFontId);
  int lineY = titleY;
  for (const auto& line : lines) {
    renderer.drawText(kHomeDisplayFontId, titleX, lineY, line.c_str(), true, EpdFontFamily::BOLD);
    lineY += lineH;
  }
  return lineY;
}

void drawHomeBookIdentity(const GfxRenderer& renderer, const Rect& card, const RecentBook& book, const bool compact) {
  drawHomeSectionPill(renderer, card, tr(STR_CONTINUE_READING));

  constexpr int iconTileSize = 54;
  constexpr int iconSize = 32;
  const int iconTileX = card.x + card.width - kHomeCardInnerPadding - iconTileSize;
  const int iconTileY = card.y + kHomeCardInnerPadding - 5;
  renderer.fillRoundedRect(iconTileX, iconTileY, iconTileSize, iconTileSize, iconTileSize / 2, Color::White);
  renderer.drawIcon(BookIcon, iconTileX + (iconTileSize - iconSize) / 2, iconTileY + (iconTileSize - iconSize) / 2,
                    iconSize);

  const char* title = book.title.empty() ? book.path.c_str() : book.title.c_str();
  int identityBottom = drawHomeTitle(renderer, card, title, compact);
  if (!book.author.empty()) {
    const int authorW = card.width - kHomeCardInnerPadding * 2;
    const std::string author = renderer.truncatedText(UI_12_FONT_ID, book.author.c_str(), authorW);
    renderer.drawText(UI_12_FONT_ID, card.x + kHomeCardInnerPadding, identityBottom + 10, author.c_str());
  }
}

void drawHomeProgress(const GfxRenderer& renderer, const Rect& card, const BookReadingStats* stats,
                      const float progressPercent, const bool compact) {
  const int panelInset = compact ? 14 : 18;
  const int panelH = compact ? 118 : 164;
  const Rect panel{card.x + panelInset, card.y + card.height - panelInset - panelH, card.width - panelInset * 2,
                   panelH};
  if (panel.width <= 0 || panel.height <= 0) {
    return;
  }

  renderer.fillRoundedRect(panel.x, panel.y, panel.width, panel.height, kHomePanelRadius, Color::White);

  const int contentX = panel.x + 18;
  const int contentRight = panel.x + panel.width - 18;
  const int labelY = panel.y + (compact ? 12 : 17);
  renderer.drawText(UI_10_FONT_ID, contentX, labelY, tr(STR_STATS_PROGRESS_LBL), true, EpdFontFamily::BOLD);

  char percentText[8] = "";
  const bool hasProgress = progressPercent >= 0.0f;
  const float clampedProgress = hasProgress ? std::clamp(progressPercent, 0.0f, 100.0f) : 0.0f;
  if (hasProgress) {
    snprintf(percentText, sizeof(percentText), "%d%%", static_cast<int>(clampedProgress + 0.5f));
    drawRightAlignedText(renderer, compact ? UI_12_FONT_ID : kHomeDisplayFontId, contentRight,
                         panel.y + (compact ? 9 : 11), percentText, true);
  }

  const int barY = panel.y + (compact ? 43 : 57);
  const int barW = panel.width - 36;
  renderer.drawRoundedRect(contentX, barY, barW, kHomeProgressBarHeight, 1, kHomeProgressBarHeight / 2, true);
  if (hasProgress) {
    const int innerW = static_cast<int>((barW - 4) * clampedProgress / 100.0f + 0.5f);
    if (innerW > 0) {
      const int innerRadius = std::min((kHomeProgressBarHeight - 4) / 2, innerW / 2);
      renderer.fillRoundedRect(contentX + 2, barY + 2, innerW, kHomeProgressBarHeight - 4,
                               innerRadius, Color::Black);
    }
  }

  if (stats == nullptr || panel.height < 100) {
    return;
  }

  const int statLabelY = barY + kHomeProgressBarHeight + (compact ? 11 : 17);
  const int statValueY = statLabelY + renderer.getLineHeight(SMALL_FONT_ID) + 2;
  if (stats->totalReadingSeconds > 0) {
    char readingTime[32];
    BookReadingStats::formatDuration(stats->totalReadingSeconds, readingTime, sizeof(readingTime));
    renderer.drawText(SMALL_FONT_ID, contentX, statLabelY, tr(STR_STATS_TIME_LBL));
    renderer.drawText(UI_10_FONT_ID, contentX, statValueY, readingTime, true, EpdFontFamily::BOLD);
  }

  uint32_t estimatedSeconds = 0;
  if (!stats->isCompleted && estimatedTimeLeft(*stats, progressPercent, estimatedSeconds)) {
    char remainingTime[32];
    formatCompactDuration(estimatedSeconds, remainingTime, sizeof(remainingTime));
    drawRightAlignedText(renderer, SMALL_FONT_ID, contentRight, statLabelY, tr(STR_TIME_LEFT));
    drawRightAlignedText(renderer, UI_10_FONT_ID, contentRight, statValueY, remainingTime, true);
  }
}

void drawYacpBookHome(const GfxRenderer& renderer, const Rect& card, const RecentBook& book,
                      const BookReadingStats* stats, const float progressPercent) {
  renderer.fillRoundedRect(card.x, card.y, card.width, card.height, kHomeCardRadius, Color::LightGray);
  renderer.drawIcon(Logo120, card.x + (card.width - kHomeLogoSize) / 2,
                    card.y + (card.height - kHomeLogoSize) / 2, kHomeLogoSize);
  const bool compact = card.height < 500;
  drawHomeBookIdentity(renderer, card, book, compact);
  drawHomeProgress(renderer, card, stats, progressPercent, compact);
}

void drawYacpEmptyHome(const GfxRenderer& renderer, const Rect& card) {
  renderer.fillRoundedRect(card.x, card.y, card.width, card.height, kHomeCardRadius, Color::LightGray);

  constexpr int iconTileSize = 76;
  constexpr int iconSize = 40;
  const int iconTileX = card.x + (card.width - iconTileSize) / 2;
  const int iconTileY = card.y + std::max(30, card.height / 5);
  renderer.fillRoundedRect(iconTileX, iconTileY, iconTileSize, iconTileSize, iconTileSize / 2, Color::White);
  renderer.drawIcon(BookIcon, iconTileX + (iconTileSize - iconSize) / 2, iconTileY + (iconTileSize - iconSize) / 2,
                    iconSize);

  const int textW = card.width - kHomeCardInnerPadding * 2;
  const auto headingLines =
      renderer.wrappedText(kHomeDisplayFontId, tr(STR_NO_OPEN_BOOK), textW, 2, EpdFontFamily::BOLD);
  const int headingLineH = renderer.getLineHeight(kHomeDisplayFontId);
  int headingY = iconTileY + iconTileSize + 28;
  for (const auto& line : headingLines) {
    const int lineW = renderer.getTextWidth(kHomeDisplayFontId, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(kHomeDisplayFontId, card.x + (card.width - lineW) / 2, headingY, line.c_str(), true,
                      EpdFontFamily::BOLD);
    headingY += headingLineH;
  }

  const char* action = tr(STR_BROWSE_FILES);
  const int actionTextW = renderer.getTextWidth(UI_12_FONT_ID, action, EpdFontFamily::BOLD);
  constexpr int actionPaddingX = 22;
  constexpr int actionPaddingY = 12;
  const int actionW = actionTextW + actionPaddingX * 2;
  const int actionH = renderer.getLineHeight(UI_12_FONT_ID) + actionPaddingY * 2;
  const int actionX = card.x + (card.width - actionW) / 2;
  const int actionY = std::min(card.y + card.height - actionH - 34, headingY + 30);
  renderer.fillRoundedRect(actionX, actionY, actionW, actionH, actionH / 2, Color::Black);
  renderer.drawText(UI_12_FONT_ID, actionX + actionPaddingX, actionY + actionPaddingY, action, false,
                    EpdFontFamily::BOLD);
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

const char* dayCountText(const uint16_t days) { return days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS); }

int statsBlockHeight(const GfxRenderer& renderer) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(SMALL_FONT_ID);
  return valueLineH + kStatsValueLabelGap + labelLineH;
}

int statsBlockTop(const Rect& coverRect, const int index, const int blockH, const int rowCount) {
  const int remainingH = std::max(0, coverRect.height - blockH * rowCount);
  const int gapCount = rowCount - 1;
  const int gap = gapCount > 0 ? remainingH / gapCount : 0;
  const int remainder = gapCount > 0 ? remainingH % gapCount : 0;
  return coverRect.y + index * (blockH + gap) + std::min(index, remainder);
}

void drawStatsRow(const GfxRenderer& renderer, const int rightX, const int y, const char* value, const char* label,
                  const bool black = true) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  drawRightAlignedText(renderer, UI_12_FONT_ID, rightX, y, value, true, black);
  drawRightAlignedText(renderer, SMALL_FONT_ID, rightX, y + valueLineH + kStatsValueLabelGap, label, false, black);
}

void drawDashboardStats(const GfxRenderer& renderer, const Rect& coverRect, const BookReadingStats* stats,
                        const float progressPercent, const bool black = true) {
  const int rightX = renderer.getScreenWidth() - contentInset(renderer) - (gpio.deviceIsX3() ? kPairInwardShiftX3 : 0);
  const int blockH = statsBlockHeight(renderer);
  const bool showRtcStats = gpio.deviceIsX3();
  const int rowCount = showRtcStats ? kStatsRowCount : kStatsRowCountX4;
  const BookReadingStats emptyStats{};
  const BookReadingStats& bookStats = stats != nullptr ? *stats : emptyStats;
  char value[40];
  char label[40];
  char startedDate[24];
  char finishDate[24];
  uint32_t estimatedSeconds = 0;
  const bool hasEstimate = estimatedTimeLeft(bookStats, progressPercent, estimatedSeconds);
  ReadingStatsDateTime today;
  const bool hasToday = showRtcStats && getCurrentLocalReadingStatsDateTime(today);
  const ReadingStatsDate endDate = bookStats.isCompleted && bookStats.finishedDate.isValid()
                                       ? bookStats.finishedDate
                                       : (hasToday ? today.date : ReadingStatsDate{});
  const bool hasDaySpan = bookStats.startDate.isValid() && endDate.isValid();
  const uint16_t daysReading = hasDaySpan ? readingSpanDaysElapsed(bookStats.startDate, endDate) : 0;

  int rowIndex = 0;
  int rowY = statsBlockTop(coverRect, rowIndex, blockH, rowCount);
  BookReadingStats::formatDuration(bookStats.totalReadingSeconds, value, sizeof(value));
  drawStatsRow(renderer, rightX, rowY, value, tr(STR_STATS_TIME_LBL), black);

  rowY = statsBlockTop(coverRect, ++rowIndex, blockH, rowCount);
  if (hasEstimate && !bookStats.isCompleted) {
    formatCompactDuration(estimatedSeconds, value, sizeof(value));
  } else {
    snprintf(value, sizeof(value), "-");
  }
  drawStatsRow(renderer, rightX, rowY, value, tr(STR_TIME_LEFT), black);

  rowY = statsBlockTop(coverRect, ++rowIndex, blockH, rowCount);
  if (progressPercent >= 0.0f) {
    snprintf(value, sizeof(value), "%d%%", static_cast<int>(progressPercent + 0.5f));
  } else {
    snprintf(value, sizeof(value), "-");
  }
  drawStatsRow(renderer, rightX, rowY, value, tr(STR_STATS_PROGRESS_LBL), black);

  if (showRtcStats) {
    rowY = statsBlockTop(coverRect, ++rowIndex, blockH, rowCount);
    if (hasDaySpan) {
      const uint16_t dailyAverageDays = std::max<uint16_t>(1, daysReading);
      BookReadingStats::formatDuration(bookStats.totalReadingSeconds / dailyAverageDays, value, sizeof(value));
    } else {
      snprintf(value, sizeof(value), "-");
    }
    drawStatsRow(renderer, rightX, rowY, value, tr(STR_STATS_DAILY_AVG_LBL), black);
  }

  rowY = statsBlockTop(coverRect, ++rowIndex, blockH, rowCount);
  snprintf(value, sizeof(value), "%.1f", pagesPerMinute(bookStats.totalPagesTurned, bookStats.totalReadingSeconds));
  drawStatsRow(renderer, rightX, rowY, value, tr(STR_STATS_PAGES_PER_MIN), black);

  if (!showRtcStats) {
    rowY = statsBlockTop(coverRect, ++rowIndex, blockH, rowCount);
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(bookStats.sessionCount));
    drawStatsRow(renderer, rightX, rowY, value, tr(STR_STATS_SESSIONS_LBL), black);

    rowY = statsBlockTop(coverRect, ++rowIndex, blockH, rowCount);
    const uint32_t avgSeconds = bookStats.sessionCount > 0 ? bookStats.totalReadingSeconds / bookStats.sessionCount : 0;
    BookReadingStats::formatDuration(avgSeconds, value, sizeof(value));
    drawStatsRow(renderer, rightX, rowY, value, tr(STR_STATS_AVG_SESSION_LBL), black);
    return;
  }

  rowY = statsBlockTop(coverRect, ++rowIndex, blockH, rowCount);
  if (hasDaySpan) {
    snprintf(value, sizeof(value), "%u %s", static_cast<unsigned>(daysReading), dayCountText(daysReading));
  } else {
    snprintf(value, sizeof(value), "-");
  }
  formatReadingStatsShortDate(bookStats.startDate, startedDate, sizeof(startedDate));
  snprintf(label, sizeof(label), "%s %s", tr(STR_STATS_STARTED), startedDate);
  drawStatsRow(renderer, rightX, rowY, value, label, black);

  rowY = statsBlockTop(coverRect, ++rowIndex, blockH, rowCount);
  ReadingStatsDate finishDisplayDate;
  if (bookStats.isCompleted) {
    finishDisplayDate = bookStats.finishedDate;
  } else if (hasToday && hasEstimate) {
    if (!estimateFinishDateFromDailyPace(bookStats, today, estimatedSeconds, finishDisplayDate)) {
      ReadingStatsDateTime estimatedFinish = today;
      addSecondsToReadingStatsDateTime(estimatedFinish, estimatedSeconds);
      finishDisplayDate = estimatedFinish.date;
    }
  }
  formatReadingStatsShortDate(finishDisplayDate, finishDate, sizeof(finishDate));
  drawStatsRow(renderer, rightX, rowY, finishDate,
               bookStats.isCompleted ? tr(STR_STATS_FINISHED_DATE) : tr(STR_STATS_EST_FINISH_DATE), black);
}

bool dominantReaderTypeBucket(const GlobalReadingStats& globalStats, ReadingTimeBucket& bucketOut) {
  const auto& values = globalStats.timeOfDaySeconds;
  const uint32_t totalSeconds = std::accumulate(values.begin(), values.end(), 0u);
  if (totalSeconds == 0) {
    return false;
  }

  const size_t dominantIndex =
      static_cast<size_t>(std::distance(values.begin(), std::max_element(values.begin(), values.end())));
  bucketOut = static_cast<ReadingTimeBucket>(dominantIndex);
  return true;
}

const char* readerTypeLabel(const GlobalReadingStats* globalStats) {
  if (globalStats == nullptr) {
    return tr(STR_STATS_NEW_READER);
  }

  ReadingTimeBucket bucket = ReadingTimeBucket::Night;
  if (!dominantReaderTypeBucket(*globalStats, bucket)) {
    return tr(STR_STATS_NEW_READER);
  }

  switch (bucket) {
    case ReadingTimeBucket::Morning:
      return tr(STR_STATS_MORNING_READER);
    case ReadingTimeBucket::Afternoon:
      return tr(STR_STATS_AFTERNOON_READER);
    case ReadingTimeBucket::Evening:
      return tr(STR_STATS_EVENING_READER);
    case ReadingTimeBucket::Night:
    default:
      return tr(STR_STATS_NIGHT_READER);
  }
}

const uint8_t* readerTypeIcon(const GlobalReadingStats* globalStats) {
  if (globalStats == nullptr) {
    return Book24Icon;
  }

  ReadingTimeBucket bucket = ReadingTimeBucket::Night;
  if (!dominantReaderTypeBucket(*globalStats, bucket)) {
    return Book24Icon;
  }

  switch (bucket) {
    case ReadingTimeBucket::Morning:
      return MorningReaderIcon;
    case ReadingTimeBucket::Afternoon:
      return AfternoonReaderIcon;
    case ReadingTimeBucket::Evening:
      return EveningReaderIcon;
    case ReadingTimeBucket::Night:
    default:
      return NightReaderIcon;
  }
}

void formatStreakStat(const GlobalReadingStats* globalStats, char* buf, const size_t len) {
  if (len == 0) {
    return;
  }
  if (globalStats == nullptr) {
    snprintf(buf, len, "%s", tr(STR_STATS_NO_STREAK));
    return;
  }

  ReadingStatsDateTime today;
  const uint16_t streak =
      getCurrentLocalReadingStatsDateTime(today) ? globalStats->currentReadingStreak(&today.date) : 0;
  if (streak == 0) {
    snprintf(buf, len, "%s", tr(STR_STATS_NO_STREAK));
    return;
  }
  snprintf(buf, len, tr(STR_STATS_DAY_STREAK_FORMAT), static_cast<unsigned>(streak));
}

void drawIconLabel(const GfxRenderer& renderer, const uint8_t* icon, const int iconX, const int centerY,
                   const char* label, const int maxTextW, const bool inverted = false) {
  const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, maxTextW);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  if (inverted) {
    renderer.drawIconInverted(icon, iconX, centerY - kFooterIconSize / 2, kFooterIconSize, kFooterIconSize);
  } else {
    renderer.drawIcon(icon, iconX, centerY - kFooterIconSize / 2, kFooterIconSize, kFooterIconSize);
  }
  renderer.drawText(UI_10_FONT_ID, iconX + kFooterIconSize + kFooterIconTextGap, centerY - lineH / 2,
                    visibleLabel.c_str(), !inverted);
}

void drawRightAlignedIconLabel(const GfxRenderer& renderer, const uint8_t* icon, const int rightX, const int centerY,
                               const char* label, const int maxTextW, const bool inverted = false) {
  const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, maxTextW);
  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, visibleLabel.c_str());
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textX = rightX - labelW;
  const int iconX = textX - kFooterIconTextGap - kFooterIconSize;
  if (inverted) {
    renderer.drawIconInverted(icon, iconX, centerY - kFooterIconSize / 2, kFooterIconSize, kFooterIconSize);
  } else {
    renderer.drawIcon(icon, iconX, centerY - kFooterIconSize / 2, kFooterIconSize, kFooterIconSize);
  }
  renderer.drawText(UI_10_FONT_ID, textX, centerY - lineH / 2, visibleLabel.c_str(), !inverted);
}

void drawLeftAnchoredFooterStat(const GfxRenderer& renderer, const int labelX, const int centerY, const int maxTextW,
                                const char* value, const char* label, const bool inverted = false) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int totalH = valueLineH + kStatsValueLabelGap + labelLineH;
  const int valueW = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
  const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, maxTextW);
  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, visibleLabel.c_str());
  const int topY = centerY - totalH / 2;
  renderer.drawText(UI_12_FONT_ID, labelX + (labelW - valueW) / 2, topY, value, !inverted, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, labelX, topY + valueLineH + kStatsValueLabelGap, visibleLabel.c_str(), !inverted);
}

void drawRightAnchoredFooterStat(const GfxRenderer& renderer, const int labelRightX, const int centerY,
                                 const int maxTextW, const char* value, const char* label,
                                 const bool inverted = false) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int totalH = valueLineH + kStatsValueLabelGap + labelLineH;
  const int valueW = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
  const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, maxTextW);
  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, visibleLabel.c_str());
  const int labelX = labelRightX - labelW;
  const int topY = centerY - totalH / 2;
  renderer.drawText(UI_12_FONT_ID, labelX + (labelW - valueW) / 2, topY, value, !inverted, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, labelX, topY + valueLineH + kStatsValueLabelGap, visibleLabel.c_str(), !inverted);
}

void drawFooterStats(const GfxRenderer& renderer, const Rect& coverRect, const GlobalReadingStats* globalStats,
                     const bool inverted = false) {
  const int inset = contentInset(renderer);
  const int footerY = renderer.getScreenHeight() - DashboardMetrics::values.buttonHintsHeight - kFooterBottomGap;
  const int centerY = std::max(coverRect.y + coverRect.height + 120, footerY);

  if (gpio.deviceIsX4()) {
    char totalTime[40];
    char booksRead[16];
    const uint32_t totalReadingSeconds = globalStats != nullptr ? globalStats->totalReadingSeconds : 0;
    const uint32_t completedBooks = globalStats != nullptr ? globalStats->completedBooks : 0;
    BookReadingStats::formatDuration(totalReadingSeconds, totalTime, sizeof(totalTime));
    snprintf(booksRead, sizeof(booksRead), "%lu", static_cast<unsigned long>(completedBooks));

    const int halfW = renderer.getScreenWidth() / 2;
    const int maxTextW = std::max(1, halfW - inset * 2);
    drawLeftAnchoredFooterStat(renderer, coverRect.x, centerY, maxTextW, totalTime,
                               tr(STR_STATS_TOTAL_READING_TIME_LBL), inverted);
    const int rightX = renderer.getScreenWidth() - inset;
    drawRightAnchoredFooterStat(renderer, rightX, centerY, maxTextW, booksRead, tr(STR_STATS_COMPLETED_LBL), inverted);
    return;
  }

  char streakBuf[48];
  formatStreakStat(globalStats, streakBuf, sizeof(streakBuf));

  const int leftTextW = renderer.getScreenWidth() / 2 - inset - kFooterIconSize - kFooterIconTextGap;
  drawIconLabel(renderer, StreakIcon, coverRect.x, centerY, streakBuf, leftTextW, inverted);

  const char* readerLabel = readerTypeLabel(globalStats);
  const int rightX = renderer.getScreenWidth() - inset - kPairInwardShiftX3;
  const int maxReaderTextW = std::max(1, renderer.getScreenWidth() / 2 - inset - kFooterIconSize - kFooterIconTextGap);
  drawRightAlignedIconLabel(renderer, readerTypeIcon(globalStats), rightX, centerY, readerLabel, maxReaderTextW,
                            inverted);
}

void drawBookText(const GfxRenderer& renderer, const Rect& coverRect, const RecentBook& book,
                  const char* currentChapterTitle, const bool black = true) {
  const int inset = contentInset(renderer);
  const int textW = renderer.getScreenWidth() - inset * 2;
  const char* title = book.title.empty() ? book.path.c_str() : book.title.c_str();
  auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title, textW, kBookTitleMaxLines, EpdFontFamily::BOLD);
  int textY = coverRect.y + coverRect.height + kTitleTopGap;
  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, coverRect.x, textY, line.c_str(), black, EpdFontFamily::BOLD);
    textY += titleLineH;
  }

  const char* subtitle =
      (currentChapterTitle != nullptr && currentChapterTitle[0] != '\0') ? currentChapterTitle : book.author.c_str();
  if (subtitle != nullptr && subtitle[0] != '\0') {
    auto subtitleLines = renderer.wrappedText(UI_12_FONT_ID, subtitle, textW, kBookChapterMaxLines);
    int subtitleY = textY + kTitleChapterGap;
    for (const auto& line : subtitleLines) {
      renderer.drawText(UI_12_FONT_ID, coverRect.x, subtitleY, line.c_str(), black);
      subtitleY += titleLineH;
    }
  }
}
}  // namespace

void DashboardTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                                const bool readerContext) const {
  if (title != nullptr || readerContext) {
    MinimalTheme::drawHeader(renderer, rect, title, subtitle, readerContext);
    return;
  }

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  const int brandY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, rect.x + kHomeCardInset, brandY, tr(STR_CROSSINK), true, EpdFontFamily::BOLD);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - kHomeCardInset - DashboardMetrics::values.batteryWidth;
  const int batteryY = rect.y + (rect.height - DashboardMetrics::values.batteryHeight) / 2;
  drawBatteryRight(renderer,
                   Rect{batteryX, batteryY, DashboardMetrics::values.batteryWidth,
                        DashboardMetrics::values.batteryHeight},
                   showBatteryPercentage);
  drawTopStatusBarClock(renderer, rect.y, nullptr, false, 3);
}

Rect DashboardTheme::homeCoverCacheRect(const GfxRenderer& renderer, const Rect& homeRect) {
  (void)renderer;
  (void)homeRect;
  // The YACP Home is typographic: it has no cover thumbnail to retain between
  // frames and therefore needs no heap snapshot.
  return Rect{};
}

void DashboardTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                         int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                         bool& bufferRestored, const std::function<bool()>& storeCoverBuffer,
                                         const BookReadingStats* stats, const float progressPercent,
                                         const GlobalReadingStats* globalStats, const char* currentChapterTitle) const {
  (void)selectorIndex;
  (void)bufferRestored;
  (void)storeCoverBuffer;
  (void)globalStats;
  (void)currentChapterTitle;

  coverRendered = false;
  coverBufferStored = false;

  const Rect card = homeCardRect(renderer, rect);
  if (card.width <= 0 || card.height <= 0) {
    return;
  }
  if (recentBooks.empty()) {
    drawYacpEmptyHome(renderer, card);
  } else {
    drawYacpBookHome(renderer, card, recentBooks[0], stats, progressPercent);
  }
}

void DashboardTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, const int buttonCount, const int selectedIndex,
                                    const std::function<const char*(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rowIcon;
  if (buttonCount <= 0) {
    return;
  }

  // HomeActivity already passes the orientation-aware safe area, excluding the
  // physical front-button edge.
  const int availableH = std::max(1, rect.height - 10);
  const int rowStep = kHomeMenuRowHeight + kHomeMenuRowGap;
  const int pageItems = std::max(1, availableH / rowStep);
  const int safeSelected = std::clamp(selectedIndex, 0, buttonCount - 1);
  const int pageStart = (safeSelected / pageItems) * pageItems;
  const int rowX = rect.x + kHomeMenuSideInset;
  const int rowW = rect.width - kHomeMenuSideInset * 2;

  for (int i = pageStart; i < buttonCount && i < pageStart + pageItems; ++i) {
    const int rowY = rect.y + 10 + (i - pageStart) * rowStep;
    const bool selected = i == safeSelected;
    renderer.fillRoundedRect(rowX, rowY, rowW, kHomeMenuRowHeight, kHomeMenuRadius,
                             selected ? Color::Black : Color::White);
    if (!selected) {
      renderer.drawRoundedRect(rowX, rowY, rowW, kHomeMenuRowHeight, 1, kHomeMenuRadius, true);
    }

    const char* label = buttonLabel != nullptr ? buttonLabel(i) : "";
    if (label == nullptr) {
      label = "";
    }
    const int textY = rowY + (kHomeMenuRowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(UI_12_FONT_ID, rowX + 20, textY, label, !selected, EpdFontFamily::BOLD);
  }

  if (buttonCount > pageItems) {
    constexpr int scrollW = 4;
    const int scrollH = std::max(12, availableH * pageItems / buttonCount);
    const int maxStart = std::max(1, buttonCount - pageItems);
    const int maxTravel = std::max(1, availableH - scrollH);
    const int scrollY = rect.y + (std::min(pageStart, maxStart) * maxTravel) / maxStart;
    renderer.fillRoundedRect(rect.x + rect.width - 9, scrollY, scrollW, scrollH, scrollW / 2, Color::Black);
  }
}

void DashboardTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                     const char* btn4, const bool allowInvertedText) const {
  const GfxRenderer::Orientation originalOrientation = renderer.getOrientation();
  const bool invertText =
      allowInvertedText && originalOrientation == GfxRenderer::Orientation::PortraitInverted;
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int hintHeight = DashboardMetrics::values.buttonHintsHeight - kHomeHintBottomMargin;
  const int groupWidth = (pageWidth - kHomeHintSideInset * 2 - kHomeHintGroupGap) / 2;
  const int outlineY = pageHeight - hintHeight - kHomeHintBottomMargin;
  const int leftGroupX = kHomeHintSideInset;
  const int rightGroupX = leftGroupX + groupWidth + kHomeHintGroupGap;

  const char* labels[] = {invertText ? btn4 : btn1, invertText ? btn3 : btn2, invertText ? btn2 : btn3,
                          invertText ? btn1 : btn4};
  for (const char*& label : labels) {
    if (label == nullptr) {
      label = "";
    }
  }

  renderer.fillRoundedRect(leftGroupX, outlineY, groupWidth, hintHeight, kHomeHintRadius, Color::White);
  renderer.drawRoundedRect(leftGroupX, outlineY, groupWidth, hintHeight, 2, kHomeHintRadius, true);
  renderer.fillRoundedRect(rightGroupX, outlineY, groupWidth, hintHeight, kHomeHintRadius, Color::White);
  renderer.drawRoundedRect(rightGroupX, outlineY, groupWidth, hintHeight, 2, kHomeHintRadius, true);

  constexpr int innerPadding = 16;
  const int labelWidths[] = {renderer.getTextWidth(SMALL_FONT_ID, labels[0]),
                             renderer.getTextWidth(SMALL_FONT_ID, labels[1]),
                             renderer.getTextWidth(SMALL_FONT_ID, labels[2]),
                             renderer.getTextWidth(SMALL_FONT_ID, labels[3])};
  const int labelX[] = {leftGroupX + innerPadding, leftGroupX + groupWidth - innerPadding - labelWidths[1],
                        rightGroupX + innerPadding, rightGroupX + groupWidth - innerPadding - labelWidths[3]};

  renderer.setOrientation(invertText ? GfxRenderer::Orientation::PortraitInverted
                                     : GfxRenderer::Orientation::Portrait);
  const int textY =
      (invertText ? kHomeHintBottomMargin : outlineY) +
      (hintHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  for (int i = 0; i < 4; ++i) {
    if (labels[i][0] != '\0') {
      renderer.drawText(SMALL_FONT_ID, labelX[i], textY, labels[i]);
    }
  }

  renderer.setOrientation(originalOrientation);
}

void DashboardTheme::drawSleepScreen(const GfxRenderer& renderer, const RecentBook& book, const BookReadingStats* stats,
                                     const GlobalReadingStats* globalStats, const float progressPercent,
                                     const char* currentChapterTitle, const bool inverted) const {
  renderer.clearScreen(inverted ? 0xFF : 0x00);

  const Rect contentRect{0, DashboardMetrics::values.homeTopPadding, renderer.getScreenWidth(),
                         DashboardMetrics::values.homeCoverTileHeight};
  const Rect coverRect = coverRectForScreen(renderer, contentRect);
  drawBookCover(renderer, coverRect, book, inverted ? Color::White : Color::Black);
  drawDashboardStats(renderer, coverRect, stats, progressPercent, inverted);
  drawBookText(renderer, coverRect, book, currentChapterTitle, inverted);
  drawFooterStats(renderer, coverRect, globalStats, !inverted);
}
