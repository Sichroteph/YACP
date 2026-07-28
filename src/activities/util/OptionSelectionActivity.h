#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class OptionSelectionActivity final : public Activity {
 public:
  OptionSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string activityName,
                          StrId titleId, std::vector<std::string> options, uint8_t selectedIndex,
                          bool readerMode = false);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool allowPowerAsConfirmInReaderMode() const override { return readerMode_; }

 private:
  void cancel();
  void select();

  ButtonNavigator buttonNavigator_;
  StrId titleId_;
  std::vector<std::string> options_;
  int currentIndex_ = 0;
  int selectedIndex_ = 0;
  bool readerMode_ = false;
};
