#pragma once
#include <span>
#include "activities/UiListActivity.h"
#include "components/UITheme.h"

// Labels refer to SettingsList's static tables. The bounded row array belongs
// to this heap-owned screen, never to the input stack or ordinary reader path.
class ReaderCommandSelectionActivity final : public UiListActivity {
  StrId title;
  std::span<const StrId> labels;
  uint8_t initial;
  freeink::ui::ListItem rows[24]{};

 public:
  ReaderCommandSelectionActivity(GfxRenderer& renderer, MappedInputManager& input, StrId title,
                                 std::span<const StrId> labels, uint8_t initial)
      : UiListActivity("ReaderCommandSelection", renderer, input), title(title),
        labels(labels.first(std::min<size_t>(labels.size(), std::size(rows)))), initial(initial) {}
  void onEnter() override {
    UiListActivity::onEnter();
    moveSelectionTo(initial < labels.size() ? initial : 0);
  }

 private:
  int listCount() const override { return labels.size(); }
  const char* headerTitle() const override { return I18N.get(title); }
  void activateIndex(int index) override {
    if (index < 0 || index >= listCount()) return;
    app.clearTapFlash();
    setResult(MenuResult{index});
    finish();
  }
  void onBackButton() override {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }
  void buildScreen(UiScreen& screen) override {
    namespace fui = freeink::ui;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    screen.setContentMarginFromScreen(fui::Insets{
        static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
        static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
        static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
    for (int i = 0; i < listCount(); ++i) {
      rows[i].label = I18N.get(labels[i]);
      rows[i].actionValue = i;
    }
    fui::ListProps props;
    props.items = rows;
    props.count = listCount();
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    syncListViewport(screen, props);
    screen.list(props);
  }
};
