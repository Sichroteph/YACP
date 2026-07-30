#pragma once

#include <cstdint>

#include "activities/Activity.h"

class ButtonLayoutSetupActivity final : public Activity {
 public:
  explicit ButtonLayoutSetupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ButtonLayoutSetup", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handlesPowerButtonLocally() const override { return true; }

 private:
  enum class Layout : uint8_t { Yacp = 0, CrossInk = 1, KeepCurrent = 2 };
  static constexpr uint8_t LAYOUT_COUNT = 3;

  Layout selectedLayout = Layout::Yacp;

  void applySelection();
};
