#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ClipSelectionActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/Dictionary.h"

class EpubReaderActivity;

// Paged viewer for one dictionary definition. HTML definitions are laid out
// through the EPUB chapter parser into styled Pages; anything else (plain
// text, or HTML too damaged to parse) is word-wrapped once on entry and each
// page renders spans of the original string, so no per-line copies are held.
class DictionaryDefinitionActivity final : public Activity {
 public:
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string headword,
                                        std::string definition, bool htmlDefinition = false,
                                        const Page* backgroundPage = nullptr, int backgroundLeft = 0,
                                        int backgroundTop = 0, const EpubReaderActivity* backgroundReader = nullptr)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        content{.headword = std::move(headword), .text = std::move(definition)},
        htmlDefinition(htmlDefinition), backgroundPage(backgroundPage),
        backgroundLeft(backgroundLeft), backgroundTop(backgroundTop), backgroundReader(backgroundReader) {}

  struct History {
    History(std::string name, int selectedIndex) : dictionaryName(std::move(name)), index(selectedIndex) {}
    std::string dictionaryName;
    Dictionary dictionary;
    std::string removedWord;
    int removedIndex = 0;
    int index = 0;
    int count = 0;
    bool writeFailed = false;
  };

  DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<History> history);
  const std::string& getHeadword() const { return content.headword; }
  bool enableLookup(Dictionary& dictionary, std::string&& query);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onUncovered() override { backgroundPainted = false; }
  bool usesReaderColors() const override { return backgroundPage != nullptr; }

 private:
  // One wrapped display line: a byte span of `definition`. Wrapping keeps
  // lines under the screen width, so uint16_t length is ample.
  struct PlainSelectionLine;

  struct Line {
    uint32_t start;
    uint16_t len;
    bool bold;
  };

  // Usable body-text area between the header and the button hints.
  struct BodyArea {
    int x;
    int y;
    int width;
    int height;
  };

  BodyArea bodyArea() const;
  struct PopupArea {
    int x, y, width, height;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + width && py < y + height; }
  };
  PopupArea popupArea() const;
  bool isPopup() const { return backgroundPage || history; }
  void renderPopup();
  void loadHistoryWord(int index);
  void changeHistoryWord(int direction);
  void editHistory();
  void changeDefinitionPage(int direction);

  struct Definition {
    std::string headword;
    std::string text;
    std::vector<std::unique_ptr<Page>> pages;
    std::vector<Line> lines;
    int currentPage = 0;
    int totalPages = 1;
    int linesPerPage = 1;
    int selectionPage = 0;
    ClipSelectionActivity::Selection selection;

    void releasePayload() {
      std::string{}.swap(text);
      std::vector<std::unique_ptr<Page>>{}.swap(pages);
      std::vector<Line>{}.swap(lines);
    }
  };

  // Replay the original query: the displayed headword may be a stem or synonym.
  struct HistoryEntry {
    std::string query;
    int page = 0;
    int selectionPage = 0;
    ClipSelectionActivity::Selection selection;
    bool styled = false;
  };

  struct LookupState {
    explicit LookupState(Dictionary& dictionary) : dictionary(dictionary) {}
    Dictionary& dictionary;  // Owned by the retained DictionaryWordSelectActivity parent.
    std::array<HistoryEntry, 16> history;
    size_t depth = 0;
    std::string query;
    std::string pendingWord;
    std::vector<std::unique_ptr<Page>> selectionPages;
  };

  bool prepareDefinition(Definition& next, bool forcePlain = false);
  bool layoutHtmlPages(Definition& next);
  bool wrapText(Definition& next);
  bool reserveLines(Definition& next, size_t count);
  int measureSpan(int fontId, const char* text, size_t len, EpdFontFamily::Style family = EpdFontFamily::REGULAR) const;
  void drawBody(int fontId, int x, int startY, int currentPage) const;
  void startSelection(int x = -1, int y = -1);
  bool buildPlainSelectionPages();
  void navigateDefinition(bool back);
  bool restoreCurrentDefinition(bool styled);
  void showMessage(StrId message);

  Definition content;
  bool htmlDefinition;
  const Page* backgroundPage;
  const int backgroundLeft;
  const int backgroundTop;
  const EpubReaderActivity* backgroundReader = nullptr;
  bool backgroundPainted = false;
  std::unique_ptr<History> history;
  std::unique_ptr<LookupState> lookup;
  StrId message = StrId::STR_DICT_NOT_FOUND;
  unsigned long messageTime = 0;
  bool messageVisible = false;
  bool initialLayoutFailed = false;
  ButtonNavigator buttonNavigator;
};
