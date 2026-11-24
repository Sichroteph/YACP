#pragma once

#include <BoardConfig.h>
#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include <cctype>
#include <string_view>

#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "ReaderRefreshPolicy.h"
#include "activities/ActivityManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

inline bool gestureAllowsSwipe(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::SWIPE_ONLY;
}

inline bool gestureAllowsTap(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::TAP_ONLY ||
         gesture == CrossPointSettings::INVERTED_TAP;
}

inline bool isRtlBookLanguage(std::string_view tag) {
  if (tag.size() < 2 || (tag.size() > 2 && tag[2] != '-' && tag[2] != '_')) return false;
  const auto first = std::tolower(static_cast<unsigned char>(tag[0]));
  const auto second = std::tolower(static_cast<unsigned char>(tag[1]));
  return (first == 'h' && second == 'e') || (first == 'i' && second == 'w') || (first == 'a' && second == 'r') ||
         (first == 'f' && second == 'a');
}

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
  bool fromSide;
};

inline uint8_t sideLongPressMode() { return SETTINGS.sideButtonLongPress; }

inline bool handleSideSleep(const MappedInputManager& input, ActivityManager& manager) {
  if (!BoardConfig::isX4Pro()) return false;
  const auto layout = SETTINGS.sideButtonLayout;
  const bool topSleeps = layout == CrossPointSettings::SLEEP_NEXT || layout == CrossPointSettings::SLEEP_SLEEP;
  const bool bottomSleeps = layout == CrossPointSettings::NEXT_SLEEP || layout == CrossPointSettings::SLEEP_SLEEP;
  if (input.getHeldTime() >= SKIP_HOLD_MS) {
    if (topSleeps && input.isPressed(MappedInputManager::Button::Up))
      input.suppressNextRelease(MappedInputManager::Button::Up);
    if (bottomSleeps && input.isPressed(MappedInputManager::Button::Down))
      input.suppressNextRelease(MappedInputManager::Button::Down);
    return false;
  }
  const bool top = topSleeps && input.wasReleased(MappedInputManager::Button::Up);
  const bool bottom = bottomSleeps && input.wasReleased(MappedInputManager::Button::Down);
  if (!top && !bottom) return false;
  // Let goToSleep run the outgoing reader's save hooks immediately.
  manager.requestReaderSleep();
  return true;
}

inline uint8_t pageLongPressBehavior(const bool fromSide) {
  if (!fromSide || sideLongPressMode() == CrossPointSettings::SIDE_LONG_INHERIT)
    return SETTINGS.longPressButtonBehavior;
  return sideLongPressMode() == CrossPointSettings::SIDE_LONG_CHAPTER_SKIP ? CrossPointSettings::CHAPTER_SKIP
                                                                           : CrossPointSettings::OFF;
}

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedBack();
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const auto pageButtonTriggered = [&](const MappedInputManager::Button button) {
    if (usePress) return input.wasPressed(button);
    return input.wasLongPressed(button, SKIP_HOLD_MS) || input.wasReleased(button);
  };
  const auto sideMode = sideLongPressMode();
  const auto sideTriggered = [&](const MappedInputManager::Button button) {
    if (sideMode == CrossPointSettings::SIDE_LONG_INHERIT) return pageButtonTriggered(button);
    if (sideMode == CrossPointSettings::SIDE_LONG_OFF) return input.wasPressed(button);
    if (sideMode == CrossPointSettings::SIDE_LONG_CHAPTER_SKIP)
      return input.wasLongPressed(button, SKIP_HOLD_MS) || input.wasReleased(button);
    // Font/rotation holds are handled by the EPUB reader using physical Up/Down.
    return input.wasReleased(button);
  };
  const bool previousMode = sideMode == CrossPointSettings::SIDE_LONG_PREVIOUS_PAGE;
  // Sleep and disabled short actions have no PageBack/PageForward identity.
  // Directional holds on those keys use Up=previous and Down=next.
  const bool unmappedHold = (sideMode == CrossPointSettings::SIDE_LONG_CHAPTER_SKIP ||
                             (sideMode == CrossPointSettings::SIDE_LONG_INHERIT && !usePress)) &&
                            !input.isPressed(MappedInputManager::Button::PageBack) &&
                            !input.isPressed(MappedInputManager::Button::PageForward);
  bool topHold = false;
  bool bottomHold = false;
  if (previousMode || unmappedHold) {
    topHold = input.wasLongPressed(MappedInputManager::Button::Up, SKIP_HOLD_MS);
    bottomHold = input.wasLongPressed(MappedInputManager::Button::Down, SKIP_HOLD_MS);
  }
  const bool previousHold = previousMode && (topHold || bottomHold);
  const bool sidePrev =
      previousHold || (unmappedHold && topHold) || sideTriggered(MappedInputManager::Button::PageBack);
  const bool sideNext =
      !previousHold && ((unmappedHold && bottomHold) || sideTriggered(MappedInputManager::Button::PageForward));
  const bool frontPrev = pageButtonTriggered(prevButton);
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool frontNext =
      input.homeButtonAction() == HomeButtonAction::NextPage || powerTurn || pageButtonTriggered(nextButton);
  return {tiltPrev || sidePrev || frontPrev, tiltNext || sideNext || frontNext, tiltPrev || tiltNext,
          (sidePrev || sideNext) && !(frontPrev || frontNext)};
}

struct TouchPageTurn {
  bool prev;
  bool next;
  unsigned long heldMs;
};

inline TouchPageTurn detectTouchPageTurn(const GfxRenderer& renderer, const MappedInputManager& input,
                                         const bool rtlBook = false) {
  TouchPageTurn result{false, false, 0};
  if (!SETTINGS.touchReaderControls || !input.hasTouch()) {
    return result;
  }

  // A slow swipe never becomes a long-press chapter skip.
  const auto dir = input.wasSwipe();
  if (dir != MappedInputManager::SwipeDir::None) {
    result.next = dir == (rtlBook ? MappedInputManager::SwipeDir::Right : MappedInputManager::SwipeDir::Left) &&
                  gestureAllowsSwipe(SETTINGS.pageTurnGesture);
    result.prev = dir == (rtlBook ? MappedInputManager::SwipeDir::Left : MappedInputManager::SwipeDir::Right) &&
                  gestureAllowsSwipe(SETTINGS.previousPageGesture);
    return result;
  }

  const bool nextTaps = gestureAllowsTap(SETTINGS.pageTurnGesture);
  const bool prevTaps = gestureAllowsTap(SETTINGS.previousPageGesture);
  if (!nextTaps && !prevTaps) {
    return result;
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) {
    return result;
  }

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  // The centered reader-menu tap target (isTouchMenuTap below) keeps priority
  // over the page-turn zones.
  if (SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_TAP && x >= width / 3 && x < width - width / 3 &&
      y >= height / 3 && y < height - height / 3) {
    return result;
  }

  // Give the whole page to the sole tap-enabled direction. When both accept
  // taps, split at the left third. RTL books and Inverted Tap each reverse
  // the shared zones.
  const bool inverted = (SETTINGS.pageTurnGesture == CrossPointSettings::INVERTED_TAP ||
                         SETTINGS.previousPageGesture == CrossPointSettings::INVERTED_TAP) != rtlBook;
  const bool nextZone = inverted ? x < (width * 2) / 3 : x >= width / 3;
  result.next = nextTaps && (!prevTaps || nextZone);
  result.prev = prevTaps && (!nextTaps || !nextZone);
  result.heldMs = gpio.lastTouchHeldMs();
  return result;
}

// Tap in the center third of the screen: the tap path into the reader menu on
// every touch board. detectTouchPageTurn() excludes this centered rectangle,
// so it remains free in tap mode. The Off/Swipe Up
// alternatives are only surfaced on home-key boards (SettingsList), where the
// menu stays reachable through the key's long-press function.
inline bool isTouchMenuTap(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (SETTINGS.showReaderMenu != CrossPointSettings::READER_MENU_TAP) return false;
  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) return false;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int zoneWidth = width / 3;
  const int zoneHeight = height / 3;
  return x >= zoneWidth && x < width - zoneWidth && y >= zoneHeight && y < height - zoneHeight;
}

// Reader menu opens on the menu edge-swipe or a center-third tap. Home-key
// actions are configured separately from screen gestures.
// Menu gestures honor showReaderMenu independently of touchReaderControls,
// which only gates page-turn touch zones in detectTouchPageTurn().
inline bool isTouchMenuGesture(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (input.wasMenuGesture()) return true;
  // Bottom-edge up-swipe variant: only selectable on home-key boards, where
  // Home is the capacitive key and the bottom edge is otherwise unused.
  if (SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_SWIPE_UP && input.wasReaderMenuSwipeUp()) {
    return true;
  }
  return isTouchMenuTap(renderer, input);
}

// One helper, blocking or deferred: the async form starts the refresh and
// returns so the caller can overlap CPU work with the panel's refresh time.
// Async callers must not touch the framebuffer until
// renderer.waitRefreshComplete() and must rebuild the differential baseline
// before the next page turn (the tiled grayscale cleanup does).
inline bool needsPeriodicReinforcement(const GfxRenderer& renderer, int remaining, bool textPage = true) {
  // A live mode change must not wait out the previous Classic countdown.
  const int deadline = SETTINGS.isMagicFlash() && remaining > 0 ? 1 : remaining;
  return reader_refresh::select(deadline, SETTINGS.supportsMagicFlash(), renderer.grayscaleCapabilities().supported(),
                                textPage, SETTINGS.isMagicFlash()) == reader_refresh::Action::Reinforce;
}

inline void displayNoFlashReinforcement(const GfxRenderer& renderer, bool grayscaleFollows = false,
                                        bool periodic = true,
                                        const reader_refresh::ReinforcementInterrupt* interrupt = nullptr) {
  if (SETTINGS.supportsUc8279MagicFlash()) {
    const int completed =
        reader_refresh::reinforceMonochrome([&renderer]() { renderer.displayMonochromeReinforcement(); }, interrupt);
    LOG_DBG("READER", "Magic Flash UC8279 mono passes=%d/3", completed);
    return;
  }
  const int passes = periodic && SETTINGS.isMagicFlash() ? 3 : 1;
  LOG_DBG("READER", "Magic Flash passes=%d grayscale=%d periodic=%d", passes, grayscaleFollows, periodic);
  // Reuse the intact BW framebuffer. The first pass synchronizes old/new
  // controller planes; subsequent passes reinforce the now-unchanged page. Grays
  // are written only afterward, with no extra framebuffer or SD access.
  for (int pass = 1; pass < passes; ++pass) {
    renderer.displayGrayscaleBaseIntermediate(HalDisplay::FAST_REFRESH);
  }
  if (grayscaleFollows)
    renderer.displayGrayscaleBaseIntermediate(HalDisplay::FAST_REFRESH);
  else
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh, bool async = false,
                                    bool textPage = true, bool grayscaleFollows = false,
                                    const reader_refresh::ReinforcementInterrupt* interrupt = nullptr) {
  if (needsPeriodicReinforcement(renderer, pagesUntilFullRefresh, textPage)) {
    LOG_DBG("READER", "Periodic X3 reinforcement");
    displayNoFlashReinforcement(renderer, grayscaleFollows, true, interrupt);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return;
  }
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  if (pagesUntilFullRefresh <= 1) LOG_DBG("READER", "Reader cleanup (remaining=%d)", pagesUntilFullRefresh);
  if (async) {
    renderer.displayBufferAsync(mode);
  } else {
    renderer.displayBuffer(mode);
  }
  pagesUntilFullRefresh = reader_refresh::advance(pagesUntilFullRefresh, SETTINGS.getRefreshFrequency());
}

// Display the B/W base of a page whose grayscale pass follows. Panels that
// combine the base (Paper Mono) defer the activation so base + gray planes go
// out as one waveform â€” displaying the base separately makes the gray pass
// re-drive the whole text body (a visible flash). Other panels display
// normally. Same refresh-cadence bookkeeping as displayWithRefreshCycle.
inline void displayBaseWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (renderer.grayscaleCapabilities().base != HalDisplay::GrayscaleBase::Combined) {
    displayWithRefreshCycle(renderer, pagesUntilFullRefresh, /*async=*/false, /*textPage=*/true,
                            /*grayscaleFollows=*/true);
    return;
  }
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  renderer.displayGrayscaleBase(mode);
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered â€” status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    // A combined-base panel may still hold a deferred B/W activation; flush it
    // so the page reaches the panel even without its grays.
    if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined)
      renderer.cleanupGrayscaleWithFrameBuffer();
    else if (gpio.deviceIsX3())
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);  // Finish any powered-on reinforcement base.
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};

// Returns true if the back button was consumed (caller should return).
// Long press (>= GO_BACK_OR_HOME_MS):
// - default: go to file browser
// - with backShortToFileBrowser: go home
// Short press (< GO_BACK_OR_HOME_MS):
// - default: go home
// - with backShortToFileBrowser: go to file browser.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager,
                                 const char* filePath, BackNavCallback goHome) {
  // The reading surface deliberately has no left-edge swipe-to-exit path: in
  // swipe page-turn mode a right swipe must page back instead. Home remains
  // available through the board's dedicated Home gesture/key. Back swipes stay
  // available in menus and other activities; only this reader-surface handler
  // ignores them. Physical Back buttons are unaffected: isPressed() is
  // button-only, and this guard skips just the gesture's own release frame.
  if (mappedInput.wasBackGesture()) {
    return false;
  }

  const bool backTriggered = mappedInput.wasLongPressed(MappedInputManager::Button::Back, GO_BACK_OR_HOME_MS) ||
                             mappedInput.wasReleased(MappedInputManager::Button::Back);
  if (!backTriggered) return false;

  const bool longPress = mappedInput.getHeldTime() >= GO_BACK_OR_HOME_MS;
  if (longPress != SETTINGS.backShortToFileBrowser) {
    activityManager.goToFileBrowser(filePath);
  } else {
    goHome.fn(goHome.ctx);
  }
  return true;
}

}  // namespace ReaderUtils
