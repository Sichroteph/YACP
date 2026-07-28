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

 private:
  enum class Layout : uint8_t { Yacp = 0, CrossInk = 1, Custom = 2 };

  Layout selectedLayout = Layout::Yacp;
  bool customLayoutAvailable = false;

  void applySelection();
  static bool isYacpLayout();
  static bool isCrossInkLayout();
};
