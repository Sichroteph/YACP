#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "power/PowerHistoryState.h"

class PowerStatsActivity final : public Activity, private UiAppHost {
  PowerHistoryState history;
  int touchCommand = -1;
  static void navigationScreen(UiScreen& screen, void* user);
  static void navigationAction(const freeink::ui::ActionEvent& event, void* user);
  uint32_t elapsedDays = 0;
  bool hasElapsedDays = false;

 public:
  explicit PowerStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PowerStats", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
