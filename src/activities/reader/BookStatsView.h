#pragma once

#include <string>
#include <vector>

#include "BookReadingStats.h"
#include "DailyReadingHistory.h"
#include "FinishedBooksIndex.h"
#include "GlobalReadingStats.h"

class GfxRenderer;
class MappedInputManager;

constexpr size_t FINISHED_BOOKS_ENTRIES_PER_PAGE = 5;

void renderPerBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                            const BookReadingStats& stats, float progressPercent, bool hasEstimatedTimeLeft,
                            uint32_t estimatedTimeLeftSeconds, bool showButtonHints, bool showEditButton,
                            bool showMoreButton);

void renderGlobalStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* screenTitle,
                           const GlobalReadingStats& stats, bool showButtonHints, bool showMoreButton);

void renderCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                             const BookReadingStats& bookStats, float progressPercent, bool hasEstimatedTimeLeft,
                             uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                             bool showButtonHints, bool showEditButton, bool showMoreButton);

void renderReadingRhythmPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                             const DailyReadingHistory& dailyHistory, const GlobalReadingStats& stats,
                             bool showButtonHints, bool showMoreButton);

void renderFinishedBooksPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                             const std::vector<FinishedBookEntry>& finishedBooks,
                             const GlobalReadingStats& globalStats, size_t pageIndex, bool showButtonHints,
                             bool showPreviousPage, bool showNextPage, bool showMoreButton);

void renderNoRtcCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& bookStats,
                                  float progressPercent, bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds,
                                  const GlobalReadingStats& deviceStats, const GlobalReadingStats* allDevicesStats,
                                  bool showButtonHints);

void renderEditBookDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                             const BookReadingStats& stats, int selectedField, bool showButtonHints);

void renderReadingAchievementPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& stats,
                                  const GlobalReadingStats& globalStats, bool showButtonHints);
