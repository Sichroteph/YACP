#include "DictionaryHistoryActivity.h"

#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "activities/UiListActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/DictionaryHistory.h"

namespace {

class DictionaryHistoryPicker final : public UiListActivity {
 public:
  DictionaryHistoryPicker(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("DictionaryHistoryPicker", renderer, input) {}

  void onEnter() override {
    dictionary = SETTINGS.dictionaryName;
    reloadWords();
    UiListActivity::onEnter();
  }

  void onCovered() override {
    // Free the list buffer before allocating a definition and its layout.
    if (openingDefinition) {
      words.text.reset();
      words.count = 0;
      openingDefinition = false;
    }
  }

 private:
  int listCount() const override { return words.count; }
  const char* headerTitle() const override { return tr(STR_DICT_HISTORY); }

  void reloadWords() { readFailed = !dictionary.empty() && !DictionaryHistory::readList(dictionary.c_str(), words); }

  static void provideWord(void* ctx, uint16_t index, freeink::ui::ListItem& item) {
    const auto& words = static_cast<DictionaryHistoryPicker*>(ctx)->words;
    item.label = words.text.get() + words.offsets[index];
    item.actionValue = static_cast<int16_t>(index);
  }

  void buildScreen(UiScreen& screen) override {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    constexpr int headerListGap = 4;
    screen.setContentMarginFromScreen(freeink::ui::Insets{
        static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + headerListGap),
        static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
        static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height + metrics.buttonHintsHeight),
        static_cast<int16_t>(safe.x)});
    if (words.count == 0) return;
    freeink::ui::ListProps props;
    props.rowProvider = &provideWord;
    props.rowProviderCtx = this;
    props.count = static_cast<uint16_t>(words.count);
    props.action = ACTION_ROW;
    props.inputMask = freeink::ui::InputTouch;
    syncListViewport(screen, props);
    screen.list(props);
  }

  void drawChrome() override {
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   headerTitle(), dictionary.empty() ? nullptr : dictionary.c_str());
    if (words.count == 0) {
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const char* message = dictionary.empty() ? tr(STR_DICT_NO_DICT_SET)
                            : readFailed       ? tr(STR_DICT_ERROR)
                                               : tr(STR_NO_ENTRIES);
      UITheme::drawCenteredWrappedText(renderer, safe, UI_12_FONT_ID, message, 4);
    }
  }

  void activateIndex(int index) override {
    if (index < 0 || index >= listCount()) return;
    app.clearTapFlash();
    auto history = makeUniqueNoThrow<DictionaryDefinitionActivity::History>(dictionary, index);
    if (history) {
      auto viewer = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(history));
      if (viewer) {
        openingDefinition = true;
        startActivityForResult(std::move(viewer), [this, index](const ActivityResult& result) {
          RenderLock lock;
          reloadWords();
          const auto* selection = std::get_if<MenuResult>(&result.data);
          const int selected = selection ? selection->action : index;
          moveSelectionTo(std::clamp(selected, 0, std::max(0, listCount() - 1)));
        });
        return;
      }
    }
    LOG_ERR("DICT", "Cannot allocate dictionary history viewer");
  }

  DictionaryHistory::WordList words;
  std::string dictionary;
  bool readFailed = false;
  bool openingDefinition = false;
};

}  // namespace

std::unique_ptr<Activity> createDictionaryHistoryActivity(GfxRenderer& renderer, MappedInputManager& input) {
  auto activity = makeUniqueNoThrow<DictionaryHistoryPicker>(renderer, input);
  if (!activity) LOG_ERR("DICT", "Cannot allocate dictionary history picker");
  return activity;
}
