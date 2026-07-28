#pragma once

#include <cstdint>

#include "activities/Activity.h"

class PowerStatsActivity final : public Activity {
  uint32_t elapsedDays = 0;
  bool hasElapsedDays = false;

 public:
  explicit PowerStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PowerStats", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
