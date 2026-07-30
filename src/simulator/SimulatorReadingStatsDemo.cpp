#ifdef SIMULATOR

#include "SimulatorReadingStatsDemo.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/reader/BookStatsActivity.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

bool startSimulatorReadingStatsDemo() {
  const char* requestedPage = std::getenv("CROSSINK_SIMULATOR_STATS_DEMO");
  if (requestedPage == nullptr || requestedPage[0] == '\0') {
    return false;
  }
  const bool showAchievement = std::strcmp(requestedPage, "achievement") == 0;
  const bool showFinishedBooks = std::strcmp(requestedPage, "finished-books") == 0;
  if (!showAchievement && !showFinishedBooks) {
    LOG_ERR("SIM", "Unknown reading statistics demo: %s", requestedPage);
    return false;
  }

  BookReadingStats stats;
  stats.sessionCount = 23;
  stats.totalReadingSeconds = 11u * 3600u + 48u * 60u;
  stats.totalPagesTurned = 412;
  stats.isCompleted = true;
  stats.startDate = {2026, 7, 5};
  stats.finishedDate = {2026, 7, 28};
  stats.timeOfDaySeconds = {68u * 60u, 142u * 60u, 426u * 60u, 72u * 60u};

  GlobalReadingStats globalStats = GlobalReadingStats::load();
  globalStats.completedBooks = std::max<uint32_t>(globalStats.completedBooks, 12);
  globalStats.totalReadingSeconds = std::max<uint32_t>(globalStats.totalReadingSeconds, 86u * 3600u + 35u * 60u);

  auto activity = makeUniqueNoThrow<BookStatsActivity>(
      renderer, mappedInputManager, "Dune - Frank Herbert", std::string{}, stats, 100.0f, false, 0, globalStats, true,
      showAchievement ? BookStatsActivity::InitialPage::Achievement : BookStatsActivity::InitialPage::Summary);
  if (!activity) {
    LOG_ERR("SIM", "Could not allocate reading statistics demo");
    return false;
  }

  LOG_INF("SIM", "Starting reading statistics demo: %s", requestedPage);
  activityManager.replaceActivity(std::move(activity));
  return true;
}

#endif
