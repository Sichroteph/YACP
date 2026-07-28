#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class XtcReaderMenuActivity final : public Activity {
 public:
  enum class MenuAction {
    SELECT_CHAPTER,
    READING_STATS,
    TOGGLE_COMPLETED,
    DELETE_STATS,
    DELETE_CACHE,
  };

  XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, bool hasChapters,
                        bool isBookCompleted);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static std::vector<MenuItem> buildMenuItems(bool hasChapters, bool isBookCompleted);

  ButtonNavigator buttonNavigator;
  std::string title;
  std::vector<MenuItem> items;
  int selectedIndex = 0;
};
