#pragma once
#include <memory>

#include "activities/Activity.h"
#include "activities/home/FileBrowserActivity.h"

class Epub;
class Xtc;
class Txt;

class ReaderActivity final : public Activity {
  std::string initialBookPath;
  std::string currentBookPath;  // Track current book path for navigation
  bool suppressInitialBackRelease = false;
  bool allowFastInitialRefresh = false;
  // Non-static (unlike the other loaders): draws the first-open indexing popup, which needs the renderer.
  std::unique_ptr<Epub> loadEpub(const std::string& path);
  static std::unique_ptr<Xtc> loadXtc(const std::string& path);
  static std::unique_ptr<Txt> loadTxt(const std::string& path);
  static bool isXtcFile(const std::string& path);
  static bool isTxtFile(const std::string& path);
  // Whether to paint the "Loading" popup on entry. Skipped for already-cached
  // EPUBs, whose open is ~ms and would otherwise cost an extra full e-ink
  // refresh before the reader paints its first page.
  static bool shouldShowLoadingPopup(const std::string& path);
  int initialRefreshCountdown() const;

  void goToLibrary(const std::string& fromBookPath = "");
  void onGoToEpubReader(std::unique_ptr<Epub> epub);
  void onGoToXtcReader(std::unique_ptr<Xtc> xtc);
  void onGoToTxtReader(std::unique_ptr<Txt> txt);
  void onGoToBmpViewer(const std::string& path);

  void onGoBack();

 public:
  explicit ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialBookPath,
                          bool suppressInitialBackRelease = false, bool allowFastInitialRefresh = false)
      : Activity("Reader", renderer, mappedInput),
        initialBookPath(std::move(initialBookPath)),
        suppressInitialBackRelease(suppressInitialBackRelease),
        allowFastInitialRefresh(allowFastInitialRefresh) {}
  void onEnter() override;
  bool isReaderActivity() const override { return true; }
};
