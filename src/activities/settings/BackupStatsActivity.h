#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class BackupStatsActivity final : public Activity {
  OptionPopup popup;
  bool backupRequested = false;

 public:
  BackupStatsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("BackupStats", renderer, input) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
