#pragma once
#include <I18n.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "OptionAvailability.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

// Modal option picker drawn over the current screen (no clear) via
// fui::optionDialog. Touch hit-testing is the SDK's InteractionBuffer: each
// render registers the option buttons (plus a chrome guard rect) on the render
// task, and handleInput routes touch snapshots against that table on the loop
// task, gated by the uiReady handshake (same pattern as UiListActivity).
// render() builds into InteractionBuffer's non-published generation
// (beginPublishCycle()) and publishes it only once every hit() call for the
// frame is done (publish()), so handleInput()'s routePublished()/
// publishedData() reads on the loop task always see a complete table, never
// one render is mid-rebuilding. uiReady closes when show() replaces the
// popup's data, then stays open across ordinary repaints after the first
// publication so a release cannot be dropped during a highlight repaint.
class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex, std::function<void(int)> onSelect,
            const StrId* hints = nullptr, uint16_t disabled = 0, const char* availabilityHint = nullptr) {
    optionHints = hints;
    title = I18N.get(titleId);
    headline = availabilityHint ? availabilityHint : "";
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = I18N.get(optionIds[i]);
    }
    activate(currentIndex, std::move(onSelect), disabled);
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect, const StrId* hints = nullptr) {
    optionHints = hints;
    title = titleStr;
    headline.clear();
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = options[i];
    }
    activate(currentIndex, std::move(onSelect));
  }

  // As above, plus a subject line inside the dialog (a book or event title).
  // It wraps to several lines under the caption; the dialog grows to fit.
  void show(const char* titleStr, const char* headlineStr, const char* const* options, int optionCount,
            int currentIndex, std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    headline = headlineStr ? headlineStr : "";
  }

  void showMessage(const char* titleStr, const char* messageStr, const char* const* options, int optionCount,
                   int currentIndex, std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    message = messageStr ? messageStr : "";
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex, std::function<void(int)> onSelect,
            const StrId* hints = nullptr) {
    optionHints = hints;
    title = I18N.get(titleId);
    headline.clear();
    ownedStrings = options;
    activate(currentIndex, std::move(onSelect));
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    const freeink::ui::InputSnapshot snap = touchSnapshotFrom(input);
    if (snap.touchPressed || snap.touchReleased || snap.touchHeld) {
      // Interactions are registered on the render task; only route once the
      // first render after show() has populated the table (uiReady handshake).
      if (uiReady) {
        const freeink::ui::ActionEvent event = interactions.routePublished(snap);
        if (event && event.action == ACTION_OPTION) {
          if (!option_popup::enabled(disabledOptions, event.value)) return true;
          // Tap released on an option: select it, fire, dismiss.
          selectedIndex = event.value;
          active = false;
          if (onSelectCallback) onSelectCallback(selectedIndex);
          haptic_feedback::touchAction();
          requestUpdate();
          return true;
        }
        if (event && event.action == ACTION_CHROME) {
          // Taps on the dialog chrome (title, padding) keep the popup open.
          return true;
        }
        if (snap.touchReleased && snap.touchX >= 0) {
          // Tap released outside the dialog: dismiss without firing. Swipe-end
          // releases arrive with -1,-1 coords and fall through (no dismiss).
          active = false;
          haptic_feedback::touchAction();
          requestUpdate();
          return true;
        }
        if (snap.touchPressed) {
          // Touch-down on an option moves the highlight (route() latched the
          // hit as the active interaction; read it back, no re-hit-testing).
          const int16_t idx = interactions.activeIndex();
          if (idx >= 0) {
            const freeink::ui::Interaction& hit = interactions.publishedData()[idx];
            if (hit.action == ACTION_OPTION && option_popup::enabled(disabledOptions, hit.value) &&
                selectedIndex != hit.value) {
              selectedIndex = hit.value;
              requestUpdate();
            }
          }
        }
      }
      return true;
    }

    return handleButtons(input.wasPressed(MappedInputManager::Button::NavPrevious),
                         input.wasPressed(MappedInputManager::Button::NavNext),
                         input.wasReleased(MappedInputManager::Button::Confirm),
                         input.wasReleased(MappedInputManager::Button::Back), requestUpdate);
  }

  bool handleButtons(const bool previous, const bool next, const bool confirm, const bool back,
                     const std::function<void()>& requestUpdate) {
    if (!active) return false;
    const int count = std::min<int>(ownedStrings.size(), MAX_OPTIONS);
    if (previous || next) {
      selectedIndex = option_popup::nextEnabled(disabledOptions, count, selectedIndex, previous ? -1 : 1);
      requestUpdate();
    } else if (confirm && selectedIndex < count && option_popup::enabled(disabledOptions, selectedIndex)) {
      active = false;
      if (onSelectCallback) onSelectCallback(selectedIndex);
      requestUpdate();
    } else if (back) {
      active = false;
      requestUpdate();
    }
    return true;
  }

  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) const {
    if (!active) return false;
    const auto popupLabels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, popupLabels.btn1, popupLabels.btn2, popupLabels.btn3, popupLabels.btn4);
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

  void render(const GfxRenderer& renderer) const {
    if (!active) return;
    namespace fui = freeink::ui;

    // Per-render target: a GfxRendererTarget is a renderer reference plus
    // three font ids, so rebuilding it here is trivially cheap and always
    // tracks the live orientation and uiScale fonts; a target held across
    // show() would stale-bind both after a rotation or scale change.
    fui::GfxRendererTarget target = makeUiTarget(renderer);
    const fui::ThemeTokens& theme = refreshSharedUiThemeTokens(target);
    // Frame stores a const DeviceContext&; keep it in a local that outlives
    // the frame (a deviceContext() temporary would dangle).
    const fui::DeviceContext device = target.deviceContext();
    // Routing happens on the loop task against the member buffer; the frame
    // itself never dispatches, so it gets an empty snapshot.
    const fui::InputSnapshot noInput{};

    // Builds into the generation handleInput()'s routePublished()/
    // publishedData() aren't currently reading, so the loop task never sees
    // this table mid-rebuild — see publish() below and
    // InteractionBuffer::beginPublishCycle().
    interactions.beginPublishCycle();
    fui::Frame<INTERACTION_CAPACITY> frame(target, device, noInput, interactions);

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int totalOptions = static_cast<int>(ownedStrings.size());
    const uint8_t count = static_cast<uint8_t>(totalOptions > MAX_OPTIONS ? MAX_OPTIONS : totalOptions);

    fui::DialogOption options[MAX_OPTIONS];
    for (uint8_t i = 0; i < count; ++i) {
      options[i].label = ownedStrings[i].c_str();
      options[i].action = ACTION_OPTION;
      options[i].value = static_cast<int16_t>(i);
      options[i].state = (i == selectedIndex) ? fui::StateFocused : fui::StateNormal;
      options[i].enabled = option_popup::enabled(disabledOptions, i);
      if (!options[i].enabled)
        options[i].state = static_cast<fui::State>(options[i].state | fui::StateDisabled);
    }

    fui::OptionDialogProps props;
    props.title = title.empty() ? nullptr : title.c_str();
    props.headline = headline.empty() ? nullptr : headline.c_str();
    if (!message.empty()) {
      props.message = message.c_str();
      props.messageText.font = fui::GfxRendererTarget::FONT_BODY;
      props.messageText.align = fui::TextAlign::Center;
      props.messageText.maxLines = 6;
    }
    props.options = options;
    props.optionCount = count;
    props.verticalOptions = true;
    // Touch only: physical buttons stay on the legacy wrap/confirm path above,
    // so the buffer never competes with it for focus/confirm dispatch.
    props.inputMask = fui::InputTouch;
    props.titleText.font = fui::GfxRendererTarget::FONT_BODY;
    props.titleText.bold = true;
    props.titleText.align = fui::TextAlign::Center;
    // Captions like "Remove from Recent Books?" overflow the narrow portrait
    // dialog in one line; let them wrap and the panel grow.
    props.titleText.maxLines = 2;
    props.headlineText.font = fui::GfxRendererTarget::FONT_BODY;
    props.headlineText.align = fui::TextAlign::Center;
    props.headlineText.maxLines = 3;
    props.buttonText.font = fui::GfxRendererTarget::FONT_BODY;
    const int16_t innerPadding = static_cast<int16_t>(metrics.optionPopupInnerPadding);
    props.padding = fui::Insets{innerPadding, innerPadding, innerPadding, innerPadding};
    props.gap = static_cast<int16_t>(metrics.optionPopupItemSpacing);
    // Rounded invert-fill themes use a black pill, not the default gray focus cursor.
    if (theme.listSelectionStyle == fui::SelectionStyle::InvertFill && theme.listRowRadius > 0) {
      props.buttonStyles = fui::defaultButtonStyles();
      props.buttonStyles.focused = props.buttonStyles.selected;
      fui::setStyleRadius(props.buttonStyles, theme.listRowRadius);
    }
    if (disabledOptions != 0) {
      if (props.buttonStyles.unset()) props.buttonStyles = fui::defaultButtonStyles();
      props.buttonStyles.disabled.background = fui::Paint::dither(fui::Color::LightGray);
      props.buttonStyles.disabled.foreground = fui::Paint::solid(fui::Color::Black);
    }
    // defaultPopupStyles() has no border, so opt in using the per-theme frame metrics.
    props.styles = fui::defaultPopupStyles();
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
    props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
    props.styles.selected = props.styles.normal;
    props.styles.focused = props.styles.normal;
    props.styles.active = props.styles.normal;
    props.styles.disabled = props.styles.normal;
    props.buttonHeight =
        fui::clampI16(target.lineHeight(fui::GfxRendererTarget::FONT_BODY) + metrics.optionPopupSelectionVPadding * 2);

    // Fixed fraction of the screen, clamped by the theme's side margins; the
    // old max-text-width sizing is gone, long labels wrap inside the buttons.
    const fui::Rect screen = device.screen();
    fui::Rect popupArea = screen;
    fui::Rect hintRect{};
    if (optionHints) {
      const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      int top, right, bottom, left;
      renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
      const int safeLeft = std::max(safe.x, left);
      const int safeRight = std::min(safe.x + safe.width, renderer.getScreenWidth() - right);
      const int safeTop = std::max(safe.y, top);
      const int safeBottom = std::min(safe.y + safe.height, renderer.getScreenHeight() - bottom);
      const int hintHeight = 2 * renderer.getLineHeight(UI_10_FONT_ID) + 3 * metrics.verticalSpacing;
      hintRect = fui::Rect{fui::clampI16(safeLeft), fui::clampI16(safeBottom - hintHeight),
                           fui::clampI16(safeRight - safeLeft), fui::clampI16(hintHeight)};
      popupArea = fui::Rect{fui::clampI16(safeLeft), fui::clampI16(safeTop), hintRect.width,
                            fui::clampI16(hintRect.y - metrics.verticalSpacing - safeTop)};
    }
    const int16_t width = fui::clampI16(
        std::min<int>(popupArea.width * 3 / 4, popupArea.width - metrics.optionPopupDialogSideMargin * 2));
    if (optionHints && fui::optionDialogHeight(target, props, width) > popupArea.height) {
      // Seven frequency choices still fit in landscape above the help band.
      props.titleText.font = fui::GfxRendererTarget::FONT_SMALL;
      props.buttonText.font = fui::GfxRendererTarget::FONT_SMALL;
      props.buttonHeight = fui::clampI16(target.lineHeight(fui::GfxRendererTarget::FONT_SMALL) + 8);
      props.padding = fui::Insets{8, 8, 8, 8};
      props.gap = 4;
    }
    const int16_t height = fui::clampI16(fui::optionDialogHeight(target, props, width), 0, popupArea.height);
    const fui::Rect dialogRect = fui::centeredRect(popupArea, fui::Size{width, height});

    // Chrome guard first, options after: route() scans newest-first, so the
    // option buttons win inside the dialog and the guard absorbs the rest.
    frame.hit(dialogRect, ACTION_CHROME, 0, fui::InputTouch);
    fui::optionDialog(frame, dialogRect, props);
    if (optionHints) {
      // Two bounded wrapped lines are laid out only while this picker is open.
      // A second chrome target absorbs help taps without dismissing the picker.
      frame.hit(hintRect, ACTION_CHROME, 0, fui::InputTouch);
      renderer.fillRect(hintRect.x, hintRect.y, hintRect.width, hintRect.height, false);
      const int x = hintRect.x + metrics.contentSidePadding;
      const int textWidth = hintRect.width - 2 * metrics.contentSidePadding;
      renderer.drawLine(x, hintRect.y, x + textWidth, hintRect.y, true);
      const auto lines = renderer.wrappedText(UI_10_FONT_ID, I18N.get(optionHints[selectedIndex]), textWidth, 2);
      int y = hintRect.y + metrics.verticalSpacing;
      for (const auto& line : lines) {
        renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
        y += renderer.getLineHeight(UI_10_FONT_ID);
      }
    }
    // Atomically make this generation the one handleInput() reads, now that
    // every hit() call for this frame is done.
    interactions.publish();
    uiReady = true;
  }

  bool isActive() const { return active; }

  // Close without firing the callback (the surface under the popup is going
  // away, e.g. its host screen closes from outside the popup's own input).
  void dismiss() {
    active = false;
    onSelectCallback = nullptr;
  }

 private:
  // The dialog has no scrolling, so options past MAX_OPTIONS would render off
  // screen anyway; a fixed cap keeps the DialogOption array on the stack and
  // the interaction table small. Two guard slots cover the dialog and optional
  // help band; fixed storage avoids a new heap allocation when hints appear.
  static constexpr int MAX_OPTIONS = 16;
  static constexpr size_t INTERACTION_CAPACITY = MAX_OPTIONS + 2;
  static constexpr freeink::ui::ActionId ACTION_OPTION = 1;
  static constexpr freeink::ui::ActionId ACTION_CHROME = 2;

  void activate(int currentIndex, std::function<void(int)> onSelect, uint16_t disabled = 0) {
    disabledOptions = disabled;
    const int count = std::min<int>(ownedStrings.size(), MAX_OPTIONS);
    selectedIndex = currentIndex >= 0 && currentIndex < count ? currentIndex : 0;
    if (!option_popup::enabled(disabledOptions, selectedIndex))
      selectedIndex = option_popup::nextEnabled(disabledOptions, count, selectedIndex, 1);
    onSelectCallback = std::move(onSelect);
    message.clear();
    uiReady = false;
    active = count > 0;
  }

  bool active = false;
  std::string title;
  std::string headline;
  std::string message;
  const StrId* optionHints = nullptr;  // Optional static flash table, one id per option.
  std::vector<std::string> ownedStrings;
  int selectedIndex = 0;
  uint16_t disabledOptions = 0;
  std::function<void(int)> onSelectCallback;
  // Written by the render task (frame registration), routed by the loop task;
  // uiReady closes the rebuild window exactly like UiListActivity::uiReady.
  mutable freeink::ui::InteractionBuffer<INTERACTION_CAPACITY> interactions;
  mutable std::atomic<bool> uiReady{false};
};
