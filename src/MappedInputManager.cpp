#include "MappedInputManager.h"

#include <BoardConfig.h>
#include <FreeInkUICore.h>
#include <GfxRenderer.h>
#include <HalHaptics.h>

#include <algorithm>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void MappedInputManager::update(const bool deferHomeButtonAction) const {
  remoteInput.reset();
  gpio.update();
  const bool pagePressed =
      SETTINGS.vibration == CrossPointSettings::VIBRATION_TOUCH_PAGE && gpio.wasCapacitivePagePressed();
  HalHaptics::feedback(SETTINGS.vibration != CrossPointSettings::VIBRATION_OFF, pagePressed, SETTINGS.hapticIntensity);
  homeAction = HomeButtonAction::Ignore;
  if (gpio.hasHomeKey()) {
    homeAction = homeButtonInput.update(millis(), gpio.wasHomeKeyTapped(), gpio.wasHomeKeyLongPressed(),
                                        wasSwipe() != SwipeDir::None, gpio.wasHomeKeyPressed(),
                                        static_cast<HomeButtonAction>(SETTINGS.homeButtonTapAction),
                                        static_cast<HomeButtonAction>(SETTINGS.homeButtonDoubleTapAction),
                                        static_cast<HomeButtonAction>(SETTINGS.homeButtonLongPressAction));
    if (gpio.wasHomeKeyLongPressed() && homeAction != HomeButtonAction::Ignore) {
      HalHaptics::longPress(SETTINGS.vibration != CrossPointSettings::VIBRATION_OFF, SETTINGS.hapticIntensity);
    } else if (homeAction != HomeButtonAction::Ignore) {
      HalHaptics::feedback(SETTINGS.vibration != CrossPointSettings::VIBRATION_OFF, true, SETTINGS.hapticIntensity);
    }
  }
  // Touch boards without a home key: a short power click can carry a reader
  // shortcut (see SHORT_PWRBTN).
  if (homeAction == HomeButtonAction::Ignore && mapButton(Button::Power, &HalGPIO::wasReleased)) {
    switch (SETTINGS.shortPwrBtn) {
      case CrossPointSettings::PWR_BOOKMARK:
        homeAction = HomeButtonAction::Bookmark;
        break;
      case CrossPointSettings::PWR_SYNC:
        homeAction = HomeButtonAction::Sync;
        break;
      case CrossPointSettings::PWR_READER_MENU:
        homeAction = HomeButtonAction::ReaderMenu;
        break;
      default:
        break;
    }
  }
  if (deferHomeButtonAction) {
    // Keep the first action observed during a synchronous transfer. Home must
    // still be visible now so the transfer can cancel and unwind promptly.
    if (homeAction != HomeButtonAction::Ignore && deferredHomeAction == HomeButtonAction::Ignore) {
      deferredHomeAction = homeAction;
    }
  } else if (deferredHomeAction != HomeButtonAction::Ignore) {
    homeAction = deferredHomeAction;
    deferredHomeAction = HomeButtonAction::Ignore;
  }
  for (uint8_t raw = HalGPIO::BTN_BACK; raw <= HalGPIO::BTN_POWER; ++raw) {
    if (!gpio.isPressed(raw)) longPressFiredPhysicalButtons &= ~(1u << raw);
  }
}

bool MappedInputManager::usesYacpFrontOrientation() const {
#if FREEINK_MCU_C3
  return readerMode && SETTINGS.frontButtonOrientationAware < 3;
#else
  return false;
#endif
}

bool MappedInputManager::usesYacpSideOrientation() const {
#if FREEINK_MCU_C3
  return readerMode && SETTINGS.sideButtonOrientationAware < 2;
#else
  return false;
#endif
}

bool MappedInputManager::isNavDirectionSwapped() const {
  return !usesYacpFrontOrientation() && crossPointNavDirectionSwapped();
}

bool MappedInputManager::isSideNavDirectionSwapped() const {
  return !usesYacpSideOrientation() && crossPointNavDirectionSwapped();
}

bool MappedInputManager::isSidePageDirectionSwapped() const {
  return usesYacpSideOrientation()
             ? SETTINGS.sideButtonOrientationAware && SETTINGS.orientation != CrossPointSettings::PORTRAIT
             : crossPointNavDirectionSwapped();
}

bool MappedInputManager::crossPointNavDirectionSwapped() const {
  // Touch boards always follow the rendered orientation; button-only boards keep the user toggle.
  // Home and settings render in portrait, so neither path swaps them.
  const auto orientation = renderer.getOrientation();
  return (gpio.hasTouch() || SETTINGS.frontButtonFollowOrientation) &&
         (orientation == GfxRenderer::PortraitInverted || orientation == GfxRenderer::LandscapeCounterClockwise);
}

MappedInputManager::Button MappedInputManager::mapScreenDirection(const Button button) const {
  // Rows follow GfxRenderer::Orientation's declared order.
  static constexpr Button directions[][4] = {
      {Button::Left, Button::Right, Button::Up, Button::Down},
      {Button::Down, Button::Up, Button::Left, Button::Right},
      {Button::Right, Button::Left, Button::Down, Button::Up},
      {Button::Up, Button::Down, Button::Right, Button::Left},
  };

  uint8_t direction = 0;
  switch (button) {
    case Button::ScreenLeft:
      direction = 0;
      break;
    case Button::ScreenRight:
      direction = 1;
      break;
    case Button::ScreenUp:
      direction = 2;
      break;
    case Button::ScreenDown:
      direction = 3;
      break;
    default:
      return button;
  }

  const uint8_t orientation = SETTINGS.frontButtonFollowOrientation && !usesYacpFrontOrientation()
                                  ? static_cast<uint8_t>(renderer.getOrientation())
                                  : 0;
  return directions[orientation][direction];
}

uint8_t MappedInputManager::frontButtonForRole(const Button role) const {
  // Only the C3 readers have YACP's four-button reader profile. S3 keeps the
  // upstream Home/touch mapping even when importing settings from a C3 card.
#if FREEINK_MCU_C3
  const bool custom = readerMode && SETTINGS.readerFrontButtonsEnabled;
#else
  constexpr bool custom = false;
#endif
  const auto rawForRole = [&](const Button selected) -> uint8_t {
    switch (selected) {
      case Button::Back:
        return custom ? SETTINGS.readerFrontButtonBack : SETTINGS.frontButtonBack;
      case Button::Confirm:
        return custom ? SETTINGS.readerFrontButtonConfirm : SETTINGS.frontButtonConfirm;
      case Button::Left:
        return custom ? SETTINGS.readerFrontButtonLeft : SETTINGS.frontButtonLeft;
      case Button::Right:
        return custom ? SETTINGS.readerFrontButtonRight : SETTINGS.frontButtonRight;
      default:
        return UINT8_MAX;
    }
  };
  if (usesYacpFrontOrientation()) {
    const auto mode = SETTINGS.frontButtonOrientationAware;
    const auto orientation = SETTINGS.orientation;
    if (mode == 2 && orientation == CrossPointSettings::INVERTED) {
      const auto raw = rawForRole(role);
      return raw <= HalGPIO::BTN_RIGHT ? HalGPIO::BTN_RIGHT - raw : raw;
    }
    const bool swap = mode != 0 && (orientation == CrossPointSettings::LANDSCAPE_CW ||
                                    orientation == CrossPointSettings::LANDSCAPE_CCW ||
                                    (mode == 1 && orientation == CrossPointSettings::INVERTED));
    if (swap && role == Button::Left) return rawForRole(Button::Right);
    if (swap && role == Button::Right) return rawForRole(Button::Left);
  }
  return rawForRole(role);
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const,
                                   uint8_t* physicalButtons) const {
  const auto sideLayout = SETTINGS.sideButtonLayout;
  const auto readButton = [&](const uint8_t raw) {
    const bool matched = (gpio.*fn)(raw);
    if (physicalButtons && matched && raw <= HalGPIO::BTN_POWER) *physicalButtons |= 1u << raw;
    return matched;
  };

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return readButton(frontButtonForRole(Button::Back));
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return readButton(frontButtonForRole(Button::Confirm));
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return readButton(frontButtonForRole(Button::Left));
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return readButton(frontButtonForRole(Button::Right));
    case Button::Up:
      return readButton(usesYacpSideOrientation() && isSidePageDirectionSwapped() ? HalGPIO::BTN_DOWN
                                                                                  : HalGPIO::BTN_UP);
    case Button::Down:
      return readButton(usesYacpSideOrientation() && isSidePageDirectionSwapped() ? HalGPIO::BTN_UP
                                                                                  : HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return readButton(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return readButton(isSidePageDirectionSwapped() ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_PREV:
          return readButton(isSidePageDirectionSwapped() ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN);
        case CrossPointSettings::PREV_PREV:
          return readButton(HalGPIO::BTN_UP) || readButton(HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_NEXT:
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::NEXT_SLEEP:
          return BoardConfig::isX4Pro() && readButton(HalGPIO::BTN_UP);
        case CrossPointSettings::SLEEP_NEXT:
          return BoardConfig::isX4Pro() && readButton(HalGPIO::BTN_DOWN);
        case CrossPointSettings::PREV_NEXT:
          return readButton(isSidePageDirectionSwapped() ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_PREV:
          return readButton(isSidePageDirectionSwapped() ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_NEXT:
          return readButton(HalGPIO::BTN_UP) || readButton(HalGPIO::BTN_DOWN);
        case CrossPointSettings::PREV_PREV:
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::NavNext:
      // Logical "next item" navigation: side Down + front Right, with the control axis flipped in
      // INVERTED / LANDSCAPE_CCW under the live orientation policy, matching the rotated hint labels.
      return mapButton(isSideNavDirectionSwapped() ? Button::Up : Button::Down, fn, physicalButtons) ||
             mapButton(isNavDirectionSwapped() ? Button::Left : Button::Right, fn, physicalButtons);
    case Button::NavPrevious:
      // Logical "previous item" navigation: side Up + front Left, axis-flipped in the same orientations.
      return mapButton(isSideNavDirectionSwapped() ? Button::Down : Button::Up, fn, physicalButtons) ||
             mapButton(isNavDirectionSwapped() ? Button::Right : Button::Left, fn, physicalButtons);
    case Button::ScreenLeft:
    case Button::ScreenRight:
    case Button::ScreenUp:
    case Button::ScreenDown:
      return mapButton(mapScreenDirection(button), fn, physicalButtons);
  }

  return false;
}

namespace {
constexpr unsigned long TOUCH_DOWN_SELECT_DELAY_MS = 90;
constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS = 250;
}  // namespace

bool MappedInputManager::hasTouch() const { return gpio.hasTouch(); }

void MappedInputManager::rememberTouchHeldTime() const {
  touchHeldOverrideValid = true;
  touchHeldOverrideMs = gpio.lastTouchHeldMs();
  touchHeldOverrideAt = millis();
}

bool MappedInputManager::wasScreenTapped(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchTap(nx, ny)) return false;
  int tapX = 0;
  int tapY = 0;
  renderer.tapToLogical(nx, ny, tapX, tapY);
  // A tap on the header back button is Button::Back (wasBackGesture), not a
  // screen tap: screens that route every tap (the keyboard's key router)
  // would otherwise swallow it before their Back check.
  if (HeaderBackTapTarget::contains(tapX, tapY)) return false;
  x = tapX;
  y = tapY;
  rememberTouchHeldTime();
  return true;
}

bool MappedInputManager::wasScreenTouchPressed(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchDown(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasScreenTouchDown(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  unsigned long heldMs = 0;
  if (!gpio.isTouchTapCandidate(nx, ny, heldMs)) return false;
  if (heldMs < TOUCH_DOWN_SELECT_DELAY_MS) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasScreenLongPress(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchLongPress(nx, ny)) return false;
  // Consuming the long-press implies acting on it: suppress the rest of the
  // contact so the finger lift can't also tap whatever the action opened.
  gpio.suppressTouchContact();
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::isScreenTouchHeld(int& x, int& y) const {
  // Live contact position while the finger is down (no tap-slop gate) — drag tracking.
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.isTouchHeldAt(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasScreenTouchReleased() const { return gpio.wasTouchReleased(); }

MappedInputManager::RowTouch MappedInputManager::rowTouch(int& row, const int top, const int rowStep,
                                                          const int rowCount, const int xStart, const int xEnd,
                                                          const int rowHeight) const {
  if (rowStep <= 0 || rowCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (x < xStart || x >= xEnd || y < top) return false;
    const int r = (y - top) / rowStep;
    if (r >= rowCount) return false;
    if (rowHeight > 0 && (y - top) % rowStep >= rowHeight) return false;
    row = r;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

MappedInputManager::RowTouch MappedInputManager::colTouch(int& col, const int left, const int colStep,
                                                          const int colCount, const int yStart, const int yEnd,
                                                          const int colWidth) const {
  if (colStep <= 0 || colCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (y < yStart || y >= yEnd || x < left) return false;
    const int c = (x - left) / colStep;
    if (c >= colCount) return false;
    if (colWidth > 0 && (x - left) % colStep >= colWidth) return false;
    col = c;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

bool MappedInputManager::wasTapInRect(const int x, const int y, const int width, const int height) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTapped(tx, ty) && tx >= x && tx < x + width && ty >= y && ty < y + height;
}

bool MappedInputManager::decodeSwipe(int& sx, int& sy, int& ex, int& ey) const {
  float nxs = 0.0f;
  float nys = 0.0f;
  float nxe = 0.0f;
  float nye = 0.0f;
  if (!gpio.wasSwipe(nxs, nys, nxe, nye)) return false;
  renderer.tapToLogical(nxs, nys, sx, sy);
  renderer.tapToLogical(nxe, nye, ex, ey);
  return true;
}

MappedInputManager::SwipeDir MappedInputManager::wasSwipe(int* startX, int* startY) const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return SwipeDir::None;
  if (startX) *startX = sx;
  if (startY) *startY = sy;
  switch (fui::swipeDirection(sx, sy, ex, ey)) {
    case fui::SwipeDir::Left:
      return SwipeDir::Left;
    case fui::SwipeDir::Right:
      return SwipeDir::Right;
    case fui::SwipeDir::Up:
      return SwipeDir::Up;
    case fui::SwipeDir::Down:
      return SwipeDir::Down;
    default:
      return SwipeDir::None;
  }
}

// Edge classification (which swipe counts as an edge gesture) lives in the
// SDK; only the MEANING of each edge — back, menu, home, light panel, and the
// home-key remap — is decided here.
bool MappedInputManager::wasEdgeSwipe(const freeink::ui::ScreenEdge edge) const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return false;
  const bool hit = fui::edgeSwipe(edge, sx, sy, ex, ey, renderer.getScreenWidth(), renderer.getScreenHeight());
  if (hit) rememberTouchHeldTime();
  return hit;
}

bool MappedInputManager::wasBackGesture() const {
  // Tap on the header back button (rect recorded by BaseTheme::drawHeader;
  // empty on screens without one). Folded into Button::Back alongside the
  // swipe so every activity's existing Back handling picks it up.
  float nx = 0.0f;
  float ny = 0.0f;
  if (gpio.wasTouchTap(nx, ny)) {
    int tapX = 0;
    int tapY = 0;
    renderer.tapToLogical(nx, ny, tapX, tapY);
    if (HeaderBackTapTarget::contains(tapX, tapY)) {
      rememberTouchHeldTime();
      return true;
    }
  }
  // Back = left-to-right swipe starting near the left edge. Edge-anchored so that
  // mid-screen horizontal swipes stay available to activities that consume
  // SwipeDir::Left/Right (e.g. percent selection, image viewer).
  return wasEdgeSwipe(fui::ScreenEdge::Left);
}

bool MappedInputManager::wasTopEdgeDownSwipe() const { return wasEdgeSwipe(fui::ScreenEdge::Top); }

bool MappedInputManager::wasBottomEdgeUpSwipe() const { return wasEdgeSwipe(fui::ScreenEdge::Bottom); }

bool MappedInputManager::wasMenuGesture() const { return wasTopEdgeDownSwipe(); }

bool MappedInputManager::wasReaderMenuSwipeUp() const { return gpio.hasHomeKey() && wasBottomEdgeUpSwipe(); }

bool MappedInputManager::wasHomeGesture() const {
  return gpio.hasHomeKey() ? homeAction == HomeButtonAction::Home : wasBottomEdgeUpSwipe();
}

bool MappedInputManager::wasLightPanelGesture() const { return wasTopEdgeDownSwipe(); }

#if FREEINK_CAP_TOUCH
bool MappedInputManager::wasPowerConfirmClick() const {
  if (!gpio.hasTouch() || SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM) return false;
  // Wait out the X4 Pro's frontlight double-click window before treating its
  // first release as Confirm. With the shortcut disabled, and on other touch
  // boards, the release counts directly.
  if (BoardConfig::isX4Pro() && SETTINGS.doubleClickPwrLight) return powerConfirmClickFrame;
  return gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() <= SETTINGS.getPowerButtonDuration();
}
#endif

bool MappedInputManager::injectRemoteClick(const RemoteButton button) {
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || isPressed(Button::Confirm) || isPressed(Button::Back))
    return false;
  return remoteInput.click(button);
}

bool MappedInputManager::wasPressed(const Button button) const {
  if (button == Button::Confirm && remoteInput.button == RemoteButton::Confirm) return true;
  if (button == Button::Back && remoteInput.button == RemoteButton::Back) return true;
  if ((button == Button::NavNext || button == Button::Right) && remoteInput.button == RemoteButton::Next) return true;
  if ((button == Button::NavPrevious || button == Button::Left) && remoteInput.button == RemoteButton::Previous)
    return true;
  if (button == Button::Confirm && homeAction == HomeButtonAction::Confirm) return true;
  if (button == Button::Back && wasBackGesture()) return true;
#if FREEINK_CAP_TOUCH
  if (button == Button::Confirm && wasPowerConfirmClick()) return true;
#endif
  return mapButton(button, &HalGPIO::wasPressed);
}

bool MappedInputManager::wasReleased(const Button button) const {
  if (button == Button::Confirm && remoteInput.button == RemoteButton::Confirm) return true;
  if (button == Button::Back && remoteInput.button == RemoteButton::Back) return true;
  if ((button == Button::NavNext || button == Button::Right) && remoteInput.button == RemoteButton::Next) return true;
  if ((button == Button::NavPrevious || button == Button::Left) && remoteInput.button == RemoteButton::Previous)
    return true;
  if (button == Button::Confirm && homeAction == HomeButtonAction::Confirm) return true;
  if (button == Button::Back && wasBackGesture()) return true;
#if FREEINK_CAP_TOUCH
  if (button == Button::Confirm && wasPowerConfirmClick()) return true;
#endif
  return mapButton(button, &HalGPIO::wasReleased);
}

bool MappedInputManager::wasLongPressed(const Button button, const unsigned long thresholdMs) const {
  uint8_t physicalButtons = 0;
  if (!mapButton(button, &HalGPIO::isPressed, &physicalButtons)) return false;
  if ((longPressFiredPhysicalButtons & physicalButtons) != 0 || getHeldTime() < thresholdMs) return false;
  longPressFiredPhysicalButtons |= physicalButtons;
  suppressNextRelease(button);
  if (mapButton(button, &HalGPIO::isCapacitivePagePressed)) {
    HalHaptics::longPress(SETTINGS.vibration == CrossPointSettings::VIBRATION_TOUCH_PAGE, SETTINGS.hapticIntensity);
  }
  return true;
}

void MappedInputManager::suppressNextRelease(const Button button) const {
  // Capture the contact before reader profiles or orientation change its role.
  mapButton(button, &HalGPIO::isPressed, &suppressedPhysicalReleases);
  mapButton(button, &HalGPIO::wasReleased, &suppressedPhysicalReleases);
}

void MappedInputManager::waitForButtonReleaseAfterRemap() const {
  for (uint8_t raw = HalGPIO::BTN_BACK; raw <= HalGPIO::BTN_DOWN; ++raw)
    if (gpio.isPressed(raw)) remapHeldButtons |= 1u << raw;
}

bool MappedInputManager::consumeSuppressedRelease() const {
  if (remapHeldButtons) {
    for (uint8_t raw = HalGPIO::BTN_BACK; raw <= HalGPIO::BTN_DOWN; ++raw)
      if ((remapHeldButtons & (1u << raw)) && gpio.isPressed(raw)) return true;
    remapHeldButtons = 0;
    suppressedPhysicalReleases &= 1u << HalGPIO::BTN_POWER;
    longPressFiredPhysicalButtons &= 1u << HalGPIO::BTN_POWER;
    return true;
  }
  uint8_t released = 0;
  for (uint8_t raw = HalGPIO::BTN_BACK; raw <= HalGPIO::BTN_POWER; ++raw) {
    const uint8_t bit = 1u << raw;
    if ((suppressedPhysicalReleases & bit) != 0 && gpio.wasReleased(raw)) {
      released |= bit;
    }
  }
  suppressedPhysicalReleases &= ~released;
  return released != 0;
}

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

unsigned long MappedInputManager::getHeldTime() const {
  // A mapped action has its own meaning, independent of the contact duration.
  if (hasRemoteClick() || homeAction != HomeButtonAction::Ignore) return 0;
  if (!gpio.wasAnyPressed() && !gpio.wasAnyReleased() && touchHeldOverrideValid &&
      millis() - touchHeldOverrideAt <= TOUCH_HELD_OVERRIDE_WINDOW_MS) {
    return touchHeldOverrideMs;
  }
  touchHeldOverrideValid = false;
  return gpio.getHeldTime();
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Swap previous/next labels to match the page turn direction swap in INVERTED and LANDSCAPE_CCW.
  const bool swapLabels = isNavDirectionSwapped();
  const char* leftLabel = swapLabels ? next : previous;
  const char* rightLabel = swapLabels ? previous : next;

  return mapFrontLabels(back, confirm, leftLabel, rightLabel);
}

MappedInputManager::Labels MappedInputManager::mapDirectionalLabels(const char* back, const char* confirm,
                                                                    const char* left, const char* right, const char* up,
                                                                    const char* down) const {
  const auto labelForButton = [&](const Button rawButton) {
    if (mapScreenDirection(Button::ScreenLeft) == rawButton) return left;
    if (mapScreenDirection(Button::ScreenRight) == rawButton) return right;
    if (mapScreenDirection(Button::ScreenUp) == rawButton) return up;
    if (mapScreenDirection(Button::ScreenDown) == rawButton) return down;
    return "";
  };
  return mapFrontLabels(back, confirm, labelForButton(Button::Left), labelForButton(Button::Right));
}

MappedInputManager::Labels MappedInputManager::mapFrontLabels(const char* back, const char* confirm, const char* left,
                                                              const char* right) const {
  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == frontButtonForRole(Button::Back)) {
      return back;
    }
    if (hw == frontButtonForRole(Button::Confirm)) {
      return confirm;
    }
    if (hw == frontButtonForRole(Button::Left)) {
      return left;
    }
    if (hw == frontButtonForRole(Button::Right)) {
      return right;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
