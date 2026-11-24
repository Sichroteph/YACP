#pragma once

#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"

// A bounded recent-book snapshot lives for this screen only. Progress uses
// CrossPoint's cached metadata reader for the selected book, with no library scan.
class YacpHomeActivity final : public Activity {
  std::vector<RecentBook> books;
  size_t selected = 0;
  struct ReadingInfo {
    int progressPercent = -1;
    uint32_t totalReadingSeconds = 0;
    uint32_t estimatedTimeLeftSeconds = 0;
  };
  ReadingInfo readingInfo;
  ReadingInfo loadReadingInfo(size_t index) const;
  bool firstRender = true;
  const bool cleanInitialRefresh;
  Rect cardRect() const;
  Rect statsRect() const;
  Rect autonomyRect() const;
  Rect menuRect() const;
  void openMenu();
  void openStats();
  void openAutonomy();
  void resume();
  void selectBook(size_t index);

 public:
  YacpHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool cleanInitialRefresh)
      : Activity("YacpHome", renderer, mappedInput), cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
