#pragma once

#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class GfxRenderer;

class IntervalSelectionActivity final : public Activity {
 public:
  explicit IntervalSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* activityName,
                                     StrId titleId, int initialValue, int minValue, int maxValue, int smallStep,
                                     int largeStep, StrId valueFormatId = StrId::STR_NONE_OPT,
                                     bool readerActivity = false, bool allowPowerAsConfirm = false,
                                     bool ignoreInitialConfirmRelease = false, bool showPercentValue = false,
                                     StrId maxBoundaryLabelId = StrId::STR_NONE_OPT)
      : Activity(activityName, renderer, mappedInput),
        titleId(titleId),
        valueFormatId(valueFormatId),
        maxBoundaryLabelId(maxBoundaryLabelId),
        value(initialValue),
        minValue(minValue),
        maxValue(maxValue),
        smallStep(smallStep),
        largeStep(largeStep),
        readerActivity(readerActivity),
        allowPowerAsConfirm(allowPowerAsConfirm),
        ignoreConfirmRelease(ignoreInitialConfirmRelease),
        showPercentValue(showPercentValue) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return readerActivity; }
  bool allowPowerAsConfirmInReaderMode() const override { return allowPowerAsConfirm; }

 private:
  StrId titleId;
  StrId valueFormatId;
  StrId maxBoundaryLabelId;
  int value;
  int minValue;
  int maxValue;
  int smallStep;
  int largeStep;
  bool readerActivity;
  bool allowPowerAsConfirm;
  bool ignoreConfirmRelease;
  bool showPercentValue;
  ButtonNavigator buttonNavigator;

  void adjustValue(int delta);
  int clampedValue(int candidate) const;
  void drawStepHintLine(int y, StrId labelId, int step);
};
