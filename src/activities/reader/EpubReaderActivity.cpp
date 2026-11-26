#include "EpubReaderActivity.h"

#include "util/WakeRefreshTrace.h"
#include "BookStatsActivity.h"
#include "TimeLeftEstimate.h"
#include "util/BootTiming.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/network/NetworkModeSelectionActivity.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "power/PowerHistory.h"

#include <BlePageTurner.h>
#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <limits>

#include "../../util/BookmarkFile.h"
#include "BookmarkEntry.h"
#include "ChapterBuildGate.h"
#include "ClipSelectionActivity.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderClippingListActivity.h"
#include "EpubReaderFootnoteSelectActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "FinishedBooksIndex.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "ReaderActivity.h"
#include "ReaderFontSizes.h"
#include "ReaderLineSpacing.h"
#include "ReaderToolbarUi.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFont.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "clippings/ClippingText.h"
#include "clippings/ClippingsManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookPathMove.h"
#include "util/BookmarkUtil.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

namespace {
// The X4 Pro and X4 Classic carry the X4's panel but sit outside isXteinkDevice()
// (that helper also gates power management). Overlay refresh choices are per-panel:
// this family runs the grayscale anti-aliasing pass, so chrome painted over a
// fresh page needs the HALF ghost-cleanup and closing re-renders the page.
bool xteinkClassPanel() {
  return gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic() || BoardConfig::isEegoA4();
}

constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
constexpr uint8_t CUSTOM_AUTO_TURN = std::size(PAGE_TURN_RATES);
constexpr float bookmarkProgressEpsilon = 0.0001f;
constexpr int CLIP_SELECTION_PAGE_WINDOW = 3;

// Word i's font: headings and CSS font-size runs can differ from the section font.
int clippingWordFont(const GfxRenderer& renderer, const int fontId, const TextBlock& block, const uint16_t i) {
  return TextBlock::renderFontId(renderer, block.wordFontId(i, fontId), fontId);
}

Rect clippingWordRect(const GfxRenderer& renderer, const int fontId, const PageLine& line, const uint16_t i,
                      const int top, const int left, const int wordWidth) {
  const auto& block = line.getBlock();
  const int wordFont = clippingWordFont(renderer, fontId, *block, i);
  const char* text = block->wordText(i);
  const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
  const int skipX =
      clippingText::hasEmSpacePrefix(text) ? renderer.getTextAdvanceX(wordFont, "\xe2\x80\x83", style) : 0;
  int width = wordWidth - skipX;
  if (i + 1 < block->wordCount() && block->wordXpos(i + 1) > block->wordXpos(i)) {
    width = std::min(width, static_cast<int>(block->wordXpos(i + 1) - block->wordXpos(i) - skipX));
  }
  const int lineFont = TextBlock::renderFontId(renderer, block->blockFontId(fontId), fontId);
  return Rect{left + line.xPos + block->wordXpos(i) + skipX,
              top + line.yPos + block->getRubyShift(renderer.getFontAscenderSize(lineFont)) +
                  block->wordYOffset(renderer, fontId, i),
              width, renderer.getLineHeight(wordFont)};
}

class ReferencePageStream final : public Print {
 public:
  ReferencePageParser parser;
  explicit ReferencePageStream(uint16_t index) : parser(index) {}
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    parser.feed(reinterpret_cast<const char*>(data), length);
    return length;
  }
};

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

constexpr char READ_FOLDER[] = "/read";

bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

struct ProgressRange {
  float start;
  float end;
};

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page, const int pageCount,
                             const ProgressRange& pageRange) {
  if (bookmark.computedSpineIndex == spineIndex && bookmark.computedChapterPageCount == pageCount &&
      bookmark.computedChapterProgress == page) {
    return true;
  }

  const float bookmarkProgress = std::clamp(bookmark.percentage, 0.0f, 1.0f);
  return bookmarkProgress + bookmarkProgressEpsilon >= pageRange.start &&
         bookmarkProgress - bookmarkProgressEpsilon <= pageRange.end;
}

std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  if (!Storage.ensureDirectoryExists(READ_FOLDER)) {
    LOG_ERR("ERS", "Cannot create read folder");
    return {};
  }
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  if (Storage.exists(dstPath.c_str())) {
    LOG_ERR("ERS", "Read-folder destination names exhausted");
    return {};
  }
  return dstPath;
}

void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath) {
  if (dstPath.empty() || !BookPathMove::move(srcPath, dstPath)) {
    LOG_ERR("ERS", "Failed to move finished book and associated data to '/Read'");
  }
}

// A button contact is closed right now. Reads the Xteink ADC ladder, power pin, and T5S3 expander
// key only; on other boards a press stops the build once the loop has debounced it.
bool buttonContactClosed() { return gpio.rawInputActive(); }

}  // namespace

EpubReaderActivity::~EpubReaderActivity() {
  ImageBlock::setExtractor(nullptr, nullptr);
  // ActivityManager destroys activities with its RenderLock already held;
  // taking another here self-deadlocks (renderingMutex is non-recursive).
  settleOverlayRefresh();
  discardOverlayPage();  // free the overlay's page snapshot if one is held
  // A radio still stopped for this book's build may start again in the next one.
  if (radioReleasedForBuild.load()) bleturner::afterPaint();

  if (footnoteDepth > 0 && epub) saveLinkStack();

  section.reset();
  CLIPPINGS.unload();
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string dstPath = buildReadFolderDestination(srcPath);
    epub.reset();
    moveFinishedBookToReadFolder(srcPath, dstPath);
  } else {
    epub.reset();
  }
}

bool EpubReaderActivity::loadBook() {
  auto loadedEpub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
  if (!loadedEpub) {
    LOG_ERR("ERS", "Failed to allocate EPUB object");
    return false;
  }

  const bool uncached = !Storage.exists((loadedEpub->getCachePath() + "/book.bin").c_str());
  if (uncached) {
    disableFastInitialRefresh();
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  bool loaded;
  {
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = loadedEpub->load(true, SETTINGS.embeddedStyle == 0);
  }
  if (!loaded) {
    // Surfaced by handleLoadFailure() as a dialog; loadedEpub dies with this
    // scope, so carry the reason out in a member.
    loadProtectionError = loadedEpub->getProtectionError();
    LOG_ERR("ERS", "Failed to load EPUB%s%s", loadProtectionError.empty() ? "" : ": ", loadProtectionError.c_str());
    return false;
  }
  epub = std::move(loadedEpub);

  ImageBlock::clearRenderFailures();
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

  epub->setupCacheDir();

  bool hasSavedPosition = false;
  HalFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[10];
    const auto fileSize = f.size();
    const int dataSize = f.read(data, sizeof(data));
    if (dataSize == fileSize && (dataSize == 4 || dataSize == 6 || dataSize == 10)) {
      hasSavedPosition = true;
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (hasSavedPosition && dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    } else if (hasSavedPosition && dataSize == 10) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
      cachedVisibleTextOffset = static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
                                (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
    }
    f.close();
  }

  if (!hasSavedPosition) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      cachedVisibleTextOffset.reset();
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  loadLinkStack();
  loadCachedBookmarks();
  if (!CLIPPINGS.loadForBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), "epub")) {
    LOG_ERR("CLIP", "Failed to load clippings for %s", epub->getPath().c_str());
  }
  if (SETTINGS.trackReadingStats && !wakeTextPage.valid) ensureReadingSession();
  return true;
}

void EpubReaderActivity::ensureReadingSession() {
  if (!readingSession && epub) {
    // Allocate once for the open book. The 730-day history must not sit on
    // the small input/render task stacks or be reallocated at each page turn.
    readingSession = makeUniqueNoThrow<ReadingSession>();
    if (readingSession) {
      readingSession->load(epub->getCachePath(), SETTINGS.getReadingIdleTimeThresholdSeconds());
      publishTimeLeftInputs();
    } else {
      LOG_ERR("STATS", "OOM: reading session; reading remains available");
    }
  }
}

void EpubReaderActivity::publishTimeLeftInputs() {
  if (!readingSession) return;
  readingPaceSeconds.store(readingSession->estimatePace(), std::memory_order_relaxed);
  readingElapsedSeconds.store(readingSession->elapsedForEstimate(), std::memory_order_relaxed);
}

uint32_t EpubReaderActivity::timeLeftSeconds(const bool wholeBook) const {
  if (!epub || !section || currentSpineIndex >= epub->getSpineItemsCount()) return 0;
  TimeLeftInput input;
  input.paceSeconds = readingPaceSeconds.load(std::memory_order_relaxed);
  if (!input.paceSeconds) return 0;
  input.elapsedSeconds = readingElapsedSeconds.load(std::memory_order_relaxed);
  input.page = section->currentPage;
  input.chapterPages = section->estimatedTotalPages();
  input.bookBytes = epub->getBookSize();
  input.previousChapterBytes = currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  const size_t cumulative = epub->getCumulativeSpineItemSize(currentSpineIndex);
  if (cumulative < input.previousChapterBytes) return 0;
  input.chapterBytes = cumulative - input.previousChapterBytes;
  return estimateReadingTimeLeft(input, wholeBook);
}

void EpubReaderActivity::recordBookCompletion() {
  // Completion still works with tracking disabled. Allocate its snapshot only
  // when the end is actually reached, keeping ordinary disabled reading light.
  ensureReadingSession();
  if (!readingSession || !epub) return;
  ReadingStatsDateTime now;
  getCurrentLocalReadingStatsDateTime(now);
  if (readingSession->setCompleted(true, now)) {
    readingSession->book.save(epub->getCachePath());
    readingSession->global.save();
    if (!FinishedBooksIndex::recordCanonical(bookPath, epub->getCachePath(), epub->getTitle(), epub->getAuthor(),
                                             readingSession->book)) {
      LOG_ERR("STATS", "Could not record completed book");
    }
  }
}

void EpubReaderActivity::goReaderHome() {
  pauseReadingSession();
  if (!epub || footnoteDepth > 0) {
    onGoHome();
    return;
  }
  ensureReadingSession();
  if (!readingSession || readingSession->book.saveBlocked) {
    onGoHome();
    return;
  }

  auto& book = readingSession->book;
  bool completionCandidate = false;
  {
    RenderLock lock;
    const int spineCount = epub->getSpineItemsCount();
    const bool completeSection = section && !section->isBuilding() && !section->isPartial();
    const bool finalPage = completeSection && spineCount > 0 && currentSpineIndex == spineCount - 1 &&
                           section->pageCount > 0 && section->currentPage >= section->pageCount - 1;
    completionCandidate = isAtEndOfBook() || finalPage || (completeSection && bookPercentFor(chapterPosition()) >= 100);
    if ((!book.isCompleted && !book.completionPromptDismissedAtHundred && completionCandidate) ||
        (book.isCompleted && book.completionAchievementPending)) {
      // This path leaves the reader. Release pagination before allocating
      // the explicitly requested dialog or summary activity.
      section.reset();
    }
  }

  if (!book.isCompleted && !book.completionPromptDismissedAtHundred && completionCandidate) {
    auto prompt = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_MARK_FINISHED_PROMPT_TITLE),
                                                          epub->getTitle());
    if (!prompt) {
      LOG_ERR("STATS", "OOM: final-page confirmation");
      onGoHome();
      return;
    }
    startActivityForResult(std::move(prompt), [this](const ActivityResult& result) {
      if (result.isCancelled) {
        readingSession->book.completionPromptDismissedAtHundred = true;
        readingSession->book.save(epub->getCachePath());
        onGoHome();
        return;
      }
      recordBookCompletion();
      goReaderHome();
    });
    return;
  }

  if (!book.isCompleted || !book.completionAchievementPending) {
    onGoHome();
    return;
  }
  auto summary = makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInput, epub->getTitle(), std::string{}, book,
                                                      100.0f, false, 0u, readingSession->global, true,
                                                      BookStatsActivity::InitialPage::Achievement);
  if (!summary) {
    LOG_ERR("STATS", "OOM: completion summary");
    onGoHome();
    return;
  }
  summary->setSessionPreview(*readingSession);
  book.completionAchievementPending = false;
  if (!book.save(epub->getCachePath())) {
    book.completionAchievementPending = true;
    LOG_ERR("STATS", "Could not acknowledge completion summary");
  }
  activityManager.replaceActivity(std::move(summary));
}

void EpubReaderActivity::acceptReadingPage() {
  if (!readingPagePending.exchange(false, std::memory_order_acquire)) return;
  if (readingSession && SETTINGS.trackReadingStats && !readingCovered && overlay == Overlay::None &&
      footnoteDepth == 0) {
    readingSession->pageShown(readingPageShownAt.load(std::memory_order_relaxed));
  }
}

void EpubReaderActivity::pauseReadingSession() {
  acceptReadingPage();
  if (readingSession) readingSession->pause(millis());
  publishTimeLeftInputs();
}

void EpubReaderActivity::onCovered() {
  pauseReadingSession();
  readingCovered = true;
}

void EpubReaderActivity::onUncovered() {
  readingPagePending.store(false, std::memory_order_release);
  readingCovered = false;
  // ActivityManager requests a render after the result handler. Wait for its
  // completion before timing the returned page.
}

void EpubReaderActivity::onEnter() {
  // Same cache identity as Epub; load before ReaderActivity loads fonts or orientation.
  char cachePath[64];
  snprintf(cachePath, sizeof(cachePath), "/.crosspoint/epub_%llu",
           static_cast<unsigned long long>(std::hash<std::string>{}(bookPath)));
  SETTINGS.beginBookProfile(bookProfile, cachePath);
  ReaderActivity::onEnter();
}

void EpubReaderActivity::prepareForSleep() {
  if (epub && !progressCheckpoint.flush(epub->getCachePath())) {
    LOG_ERR("ERS", "Could not flush reader progress before sleep");
  }
  ReaderActivity::prepareForSleep();
}

void EpubReaderActivity::onExit() {
  if (epub && !progressCheckpoint.flush(epub->getCachePath())) {
    LOG_ERR("ERS", "Could not flush reader progress on exit");
  }
  pauseReadingSession();
  if (readingSession && epub) {
    if (section || isAtEndOfBook()) readingSession->cacheTimeLeft(timeLeftSeconds(true));
    readingSession->finish(millis());
    readingSession->save(epub->getCachePath());
    if (readingSession->book.isCompleted &&
        !FinishedBooksIndex::recordCanonical(bookPath, epub->getCachePath(), epub->getTitle(), epub->getAuthor(),
                                             readingSession->book)) {
      LOG_ERR("STATS", "Could not update completed-book reading time");
    }
    LOG_INF("STATS", "Session ended: active=%lu seconds, pace samples=%u",
            static_cast<unsigned long>(readingSession->activeSeconds()), readingSession->paceSamples());
  }
  SETTINGS.endBookProfile(bookProfile);
  ReaderActivity::onExit();
}

ChapterPosition EpubReaderActivity::chapterPosition() const {
  if (section) return {section->currentPage, section->estimatedTotalPages()};
  return {nextPageNumber, cachedChapterTotalPageCount};
}

int EpubReaderActivity::bookPercentFor(const ChapterPosition& position) const {
  if (!epub || epub->getBookSize() == 0 || !position.hasTotal()) return 0;
  // The page index can run past the chapter's estimated total while it is still
  // building, so the fraction is clamped before the cast.
  const float fraction = epub->calculateProgress(currentSpineIndex, position.chapterFraction());
  return static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f + 0.5f);
}

void EpubReaderActivity::openReaderMenu() {
  loadRetainedPageMetadata();
  pauseReadingSession();
  pendingManualTurn = 0;
  reinforcementInterrupt.clear();
  if (usesToolbarMenu()) {
    // Reached from a child activity's result handler (footnotes, bookmarks,
    // go-to-percent... cancelled back to the menu), so the framebuffer holds
    // that screen, not the page: re-render the page and let renderBook() put
    // the toolbar on top. The in-reader fast path is openOverlay().
    overlay = Overlay::Toolbar;
    focusedTool = 0;
    panelHoldJumped = false;
    panelCursorShown = !mappedInput.hasTouch();
    if (!toolbarUi) toolbarUi = std::make_unique<ReaderToolbarUi>(renderer);
    toolbarUi->begin();
    discardOverlayPage();
    requestUpdate();
    return;
  }
  // Child screens (chapter list, text settings) release the section to free its
  // pagination buffers; chapterPosition() covers that with the cached position.
  const ChapterPosition position = chapterPosition();
  const int bookProgressPercent = bookPercentFor(position);

  startActivityForResult(std::make_unique<EpubReaderMenuActivity>(
                             renderer, mappedInput, epub->getTitle(), position.displayPage(), position.totalPages,
                             bookProgressPercent, SETTINGS.orientation, !currentPageFootnotes.empty(),
                             !cachedBookmarks.empty(), CLIPPINGS.hasClippings(), selectedMenuAction),
                         [this](const ActivityResult& result) {
                           const auto& menu = std::get<MenuResult>(result.data);

                           if (SETTINGS.orientation != menu.orientation) {
                             applyOrientation(menu.orientation);
                           }

                           toggleAutoPageTurn(menu.pageTurnOption);
                           if (!result.isCancelled) {
                             onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
                           }
                         });
}

// Runs on the render task, under its lock, right before every chapter build. The page turner's
// radio holds about 50 KB while it runs, and no heap figure taken before a build tells whether the
// build fits next to it (X3: with the radio up, 55 KB free and a 47 KB block, a 6 KB chapter ran
// out of memory). So the radio stops for every build and starts again once the chapter is built
// and its page is shown. A radio that did not stop within the module's 3 s, or a start still
// settling, usually lets go soon after: the build waits up to BUILD_ROOM_WAIT_MS more, then is
// refused rather than run next to it. The stop finishes on the main loop (the module's tick) and
// the start on its own task; neither needs the render lock this task holds.
bool EpubReaderActivity::makeRoomForChapterBuild() {
  bool asked = false;
  const auto roomMade = [this, &asked] {
    // The first ask stops the radio. A later one asks again only once a start that was still
    // settling has left the radio up.
    if (!radioReleasedForBuild.load() && (!asked || !bleturner::status().starting)) {
      radioReleasedForBuild = bleturner::beforeChapterBuild() != bleturner::BuildRelease::NotHeld;
    }
    asked = true;
    return chapterBuildMayRun(bleturner::status());
  };
  const auto nowMs = [] { return static_cast<uint32_t>(millis()); };
  const auto nap = [](const uint32_t ms) { delay(ms); };
  if (waitForBuildRoom(roomMade, nowMs, nap, BUILD_ROOM_WAIT_MS, BUILD_ROOM_STEP_MS)) return true;
  LOG_ERR("ERS", "Bluetooth radio still holds the heap after %u ms; chapter build refused",
          static_cast<unsigned>(BUILD_ROOM_WAIT_MS));
  return false;
}

bool EpubReaderActivity::buildTickHeapGate() {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxBlock = ESP.getMaxAllocHeap();
  buildHeapPaused = freeHeap < BACKGROUND_BUILD_MIN_FREE_HEAP || maxBlock < BACKGROUND_BUILD_MIN_MAX_ALLOC;
  return !buildHeapPaused;
}

void EpubReaderActivity::showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (!buildPopupPending || !renderer.hasFrameBuffer()) return;
  GUI.drawPopup(renderer, tr(STR_INDEXING));
  pagesUntilFullRefresh = 0;
  buildPopupPending = false;
}

// Debounced state of every button. Covers the frame between a raw release and its debounced
// release edge, which would otherwise run a build step before the page turn.
bool EpubReaderActivity::anyButtonPressed() const {
  using Button = MappedInputManager::Button;
  for (const Button button :
       {Button::Back, Button::Confirm, Button::Left, Button::Right, Button::Up, Button::Down, Button::Power}) {
    if (mappedInput.isPressed(button)) return true;
  }
  return false;
}

void EpubReaderActivity::backgroundBuildTick() {
  RenderLock lock(RenderLock::Mode::Try);
  // Not under an open toolbar or panel: those repaint straight onto the page in the framebuffer.
  if (!lock.ownsLock() || overlay != Overlay::None || !backgroundBuildWanted()) return;
  // A button is down or a page turn is queued: the next loop frames act on it first.
  if (buttonContactClosed() || anyButtonPressed() || pendingManualTurn != 0 || !buildTickHeapGate()) return;
  // A build step can lend the framebuffer (image probes), which hands it back white while the
  // panel still shows the page; redraw so nothing is later painted over the blank buffer.
  const uint32_t loansBefore = renderer.frameBufferLoanCount();
  const bool built = section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK, &buttonContactClosed);
  if (renderer.frameBufferLoanCount() != loansBefore) {
    pageBufferStale = true;
    requestUpdate();
  }
  if (!built) {
    LOG_ERR("ERS", "Background section build failed");
    backgroundBuildFailedSpine = currentSpineIndex;
    nextPageNumber = section->currentPage;
    section.reset();
    requestUpdate();
  } else if (section->isBuildComplete() && applyDeferredReposition()) {
    requestUpdate();
  }
}

void EpubReaderActivity::openDictionaryWordSelect(const int touchX, const int touchY) {
  if (SETTINGS.dictionaryName[0] == '\0') {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  startClipSelection(touchX, touchY, ClipSelectionActivity::Mode::Lookup);
}

void EpubReaderActivity::startClipSelection(const int initialX, const int initialY,
                                            const ClipSelectionActivity::Mode mode) {
  if (!section || !epub || section->currentPage < 0 || section->currentPage >= section->pageCount) return;

  const int pageNumber = section->currentPage;
  const uint16_t pageCount = section->pageCount;
  const uint16_t spineIndex = static_cast<uint16_t>(currentSpineIndex);
  const int pageWindow = mode == ClipSelectionActivity::Mode::Lookup && initialX >= 0 ? 1 : CLIP_SELECTION_PAGE_WINDOW;
  std::vector<std::unique_ptr<Page>> pages;
  pages.reserve(pageWindow);
  for (int pageOffset = 0; pageOffset < pageWindow && pageNumber + pageOffset < pageCount; ++pageOffset) {
    auto page = section->loadPage(pageNumber + pageOffset);
    if (!page) break;
    if (pageOffset == 0 && initialX >= 0 && mode == ClipSelectionActivity::Mode::Clip && CLIPPINGS.hasClippings()) {
      RenderLock lock;
      const int clippingIndex = clippingAtPoint(*page, initialX, initialY);
      if (clippingIndex >= 0) {
        const bool removed = CLIPPINGS.removeClippingAt(static_cast<size_t>(clippingIndex));
        if (!removed) LOG_ERR("CLIP", "Failed to delete highlighted clipping %d", clippingIndex);
        clippingMessage = removed ? StrId::STR_CLIPPING_REMOVED : StrId::STR_CLIPPING_DELETE_FAILED;
        showClippingMessage = true;
        clippingMessageTime = millis();
        requestUpdate();
        return;
      }
    }
    pages.push_back(std::move(page));
  }
  if (pages.empty()) {
    LOG_ERR("CLIP", "Failed to load page %d for clipping selection", pageNumber);
    requestUpdate();
    return;
  }

  if (buildViewportWidth == 0 || buildViewportHeight == 0) {
    LOG_ERR("CLIP", "Cannot anchor clipping before the reader viewport is initialized");
    requestUpdate();
    return;
  }
  const uint32_t layoutSignature =
      readerRenderSpecSignature(SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight));

  std::string chapterTitle;
  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex >= 0) chapterTitle = epub->getTocItem(tocIndex).title;
  std::string bookTitle = epub->getTitle();
  std::string author = epub->getAuthor();

  auto activity = makeUniqueNoThrow<ClipSelectionActivity>(renderer, mappedInput, std::move(pages),
                                                            currentPageMarginLeft, currentPageMarginTop, initialX,
                                                            initialY, mode);
  if (!activity) {
    LOG_ERR("CLIP", "Failed to allocate clipping selection activity");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(activity), [this, spineIndex, pageNumber, pageCount, layoutSignature,
                                               bookTitle = std::move(bookTitle), author = std::move(author),
                                               chapterTitle = std::move(chapterTitle)](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    const auto& clipping = std::get<ClippingResult>(result.data);
    const uint16_t startPage = static_cast<uint16_t>(pageNumber + clipping.startPageOffset);
    const uint16_t endPage = static_cast<uint16_t>(pageNumber + clipping.endPageOffset);
    if (section && currentSpineIndex == spineIndex && endPage < section->pageCount) {
      section->currentPage = endPage;
      currentPageVisibleOffset = section->getVisibleTextOffsetForPage(endPage);
    }
    if (clipping.action == ClippingResult::Action::Bookmark) {
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      requestUpdate();
      return;
    }
    if (clipping.action == ClippingResult::Action::Lookup) {
      auto page = section ? section->loadPage(section->currentPage) : nullptr;
      if (!page) {
        LOG_ERR("CLIP", "Failed to load lookup page");
        requestUpdate();
        return;
      }
      auto lookup = makeUniqueNoThrow<DictionaryWordSelectActivity>(
          renderer, mappedInput, *this, std::move(page), currentPageMarginLeft, currentPageMarginTop, clipping.text);
      if (!lookup) {
        LOG_ERR("CLIP", "OOM: dictionary lookup activity");
        requestUpdate();
        return;
      }
      startActivityForResult(std::move(lookup), [this](const ActivityResult& result) {
        if (!result.isCancelled && !mappedInput.hasTouch()) openDictionaryWordSelect();
      });
      return;
    }
    const uint16_t paragraphIndex =
        section ? section->getParagraphIndexForPage(startPage).value_or(UINT16_MAX) : UINT16_MAX;
    const auto addResult = CLIPPINGS.addClipping(
        spineIndex, startPage, endPage, pageCount, clipping.startWordIndex, clipping.endWordIndex, clipping.wordCount,
        chapterTitle.c_str(), paragraphIndex, clipping.text, layoutSignature, clipping.startOffset, clipping.endOffset);
    bool exported = false;
    if (addResult == ClippingStore::AddResult::Added) {
      exported = ClippingsManager::saveClipping(bookTitle, author, chapterTitle, startPage + 1, clipping.text);
    }
    clippingMessage = addResult == ClippingStore::AddResult::LimitReached ? StrId::STR_CLIPPING_LIMIT_REACHED
                      : exported                                          ? StrId::STR_CLIPPING_SAVED
                      : addResult == ClippingStore::AddResult::Added      ? StrId::STR_CLIPPING_EXPORT_FAILED
                                                                          : StrId::STR_CLIPPING_FAILED;
    showClippingMessage = true;
    clippingMessageTime = millis();
    requestUpdate();
  });
}

void EpubReaderActivity::openFootnoteSelect(const bool reopenMenuOnCancel) {
  loadRetainedPageMetadata();
  if (!section || currentPageFootnotes.empty()) return;
  if (currentPageFootnotes.size() == 1) {
    navigateToHref(currentPageFootnotes[0].href, true);
    return;
  }

  auto page = section->loadPage(section->currentPage);
  if (!page) return;

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += UITheme::getReaderTopInset(renderer, orientedMarginTop);
  orientedMarginLeft +=
      SETTINGS.publisherPageNumbers ? std::max<int>(15, SETTINGS.screenMargin) : SETTINGS.screenMargin;
  auto selector = makeUniqueNoThrow<EpubReaderFootnoteSelectActivity>(renderer, mappedInput, std::move(page),
                                                                      orientedMarginLeft, orientedMarginTop);
  if (!selector) {
    LOG_ERR("ERA", "OOM: EpubReaderFootnoteSelectActivity");
    return;
  }
  startActivityForResult(std::move(selector), [this, reopenMenuOnCancel](const ActivityResult& result) {
    if (result.isCancelled) {
      if (reopenMenuOnCancel) {
        openReaderMenu();
      } else {
        requestUpdate();
      }
      return;
    }
    const auto& footnoteResult = std::get<FootnoteResult>(result.data);
    navigateToHref(footnoteResult.href, true);
    requestUpdate();
  });
}

void EpubReaderActivity::openClippings() {
  if (!CLIPPINGS.hasClippings()) {
    requestUpdate();
    return;
  }
  auto list = makeUniqueNoThrow<EpubReaderClippingListActivity>(renderer, mappedInput);
  if (!list) {
    LOG_ERR("CLIP", "OOM: clipping list");
    return;
  }
  startActivityForResult(std::move(list), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      if (const auto* progress = std::get_if<ProgressChangeResult>(&result.data)) {
        applyProgressChange(*progress);
        return;
      }
    }
    requestUpdate();
  });
}

void EpubReaderActivity::loop() {
  if (loadFailurePopup.isActive()) {
    loadFailurePopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }
  if (!epub) {
    finish();
    return;
  }

  rememberBookOnceRendered();
  acceptReadingPage();
  if (ReaderUtils::handleSideSleep(mappedInput, activityManager)) return;
  if (pendingReaderAction != ReaderAction::Upstream) {
    const auto action = pendingReaderAction;
    pendingReaderAction = ReaderAction::Upstream;
    executeReaderAction(action);
    return;
  }

  // The radio stopped for a chapter build: once the render task has let go of the lock, that page
  // is on screen; once the build tick below has finished the chapter, no parser is left alive and
  // the radio may start again.
  if (radioReleasedForBuild.load()) {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock() && !(section && section->isBuilding())) {
      radioReleasedForBuild = false;
      bleturner::afterPaint();
    }
  }

  // The remote linked, or failed to, after the status bar showed its note: paint the page again.
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock() && linkNoteTitle.repaint(bleturner::linkNote())) requestUpdate();
  }

  // Someone else turned the screen while this reader was stacked (the control
  // center's orientation tile). Reflow before the next render, or the page
  // would be drawn with a layout built for the previous frame size.
  if (appliedOrientation != SETTINGS.orientation) {
    pauseReadingSession();
    applyOrientation(SETTINGS.orientation);
    requestUpdate();
    return;
  }

  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS = 400;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock() && section && !section->isBuilding() && renderer.hasFrameBuffer() &&
        lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS &&
        ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP && ESP.getMaxAllocHeap() > BACKGROUND_BUILD_MIN_MAX_ALLOC &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      const int nextPage = section->currentPage + 1;
      if (nextPage < static_cast<int>(section->pageCount)) {
        if (const auto p = section->loadPage(nextPage)) {
          if (auto* fcm = renderer.getFontCacheManager()) {
            const auto t0 = millis();
            auto scope = fcm->createPrewarmScope();
            p->render(renderer, SETTINGS.getReaderFontId(), 0, 0);
            scope.endScanAndPrewarm();
            LOG_DBG("ERS", "Idle prewarm: page %d in %lums", nextPage, millis() - t0);
          }
        }
      }
    }
  }

  {
    RenderLock lock(RenderLock::Mode::Try);
    // Never next to the radio: once the reader reaches the watermark, the build starts in
    // renderBook(), which stops the radio first.
    if (lock.ownsLock() && section && !section->isBuilding() && section->isPartial() && buildViewportWidth > 0 &&
        !partialRebuildStartFailed && !bleturner::holdsHeap() &&
        section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount)) {
      const ReaderRenderSpec buildSpec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight);
      if (!section->startBuild(buildSpec)) {
        partialRebuildStartFailed = true;
        LOG_ERR("ERS", "Failed to start deferred partial extension build");
      } else {
        LOG_DBG("ERS", "Reader near partial watermark (%d/%d), resuming extension build", section->currentPage,
                section->pageCount);
      }
    }
  }

  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();
  clearEndOfBookOptionsIfNeeded();

  const bool completedBook = atEndOfBook || (readingSession && readingSession->book.isCompleted);
  if (SETTINGS.removeReadBooksFromRecents) {
    if (completedBook && !recentsEntryRemoved) {
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!completedBook && recentsEntryRemoved) {
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
      recentsEntryRemoved = false;
    }
  }

  if (completedBook) {
    pendingReadFolderMove = SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else {
    pendingReadFolderMove = false;
  }

  const auto touch =
      ReaderUtils::detectTouchPageTurn(renderer, mappedInput, ReaderUtils::isRtlBookLanguage(epub->getLanguage()));

  if (showBookmarkMessage && (millis() - bookmarkMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showBookmarkMessage = false;
    requestUpdate();
  }

  if (showDictionaryMessage && (millis() - dictionaryMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showDictionaryMessage = false;
    requestUpdate();
  }

  if (showFontDamagedMessage && (millis() - fontDamagedMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showFontDamagedMessage = false;
    requestUpdate();
  }

  // The toolbar reader menu owns all input while shown, ahead of the automatic page turn
  // below: the More panel's rate popup switches automatic turning on and leaves the panel
  // open, so the timer must neither flip the page under it nor eat the panel's next
  // Confirm/Back release.
  if (overlay != Overlay::None) {
    if (usesToolbarMenu()) {
      // Hold the interval at zero elapsed so closing the panel starts a fresh one.
      lastPageTurnTime = millis();
      handleOverlayInput();
      return;
    }
    // The style was switched off while an overlay was up (Settings reached via
    // the More panel); fall back to the clean page.
    overlay = Overlay::None;
    discardOverlayPage();
    requestUpdate();
    return;
  }

  int selectionX = 0;
  int selectionY = 0;
  if (!atEndOfBook && SETTINGS.touchReaderControls && mappedInput.wasScreenLongPress(selectionX, selectionY)) {
    automaticPageTurnActive = false;
    pendingManualTurn = 0;
    if (SETTINGS.dictionaryName[0]) {
      startClipSelection(selectionX, selectionY, ClipSelectionActivity::Mode::Lookup);
    }
    return;
  }

  switch (mappedInput.homeButtonAction()) {
    case HomeButtonAction::ReaderMenu:
    case HomeButtonAction::Bookmark:
    case HomeButtonAction::Sync:
    case HomeButtonAction::Dictionary:
    case HomeButtonAction::Footnotes:
    case HomeButtonAction::CreateClipping:
      automaticPageTurnActive = false;
      break;
    default:
      break;
  }

  if (automaticPageTurnActive) {
    const bool touchStopsAutoTurn = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) || touchStopsAutoTurn) {
      automaticPageTurnActive = false;
      if (touchStopsAutoTurn) haptic_feedback::touchAction();
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      const bool succeeded = pageTurn(true);
      notePageTurn(true, succeeded);
      requestUpdate();
      return;
    }
  }

  if (showClippingMessage && (millis() - clippingMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showClippingMessage = false;
    requestUpdate();
  }

  // While the end-of-book suggestion menu is up it owns Confirm/Back/navigation, so it
  // gets this tick's input first and the long-press shortcuts below stay inert behind it
  // -- a hold there must not drop a bookmark onto the suggestion screen or paint the
  // dictionary word picker over it. Anything the menu does not handle (long-press Back to
  // the file browser, say) still falls through to the regular handlers.
  if (handleEndOfBookMenu()) {
    return;
  }
  const bool endOfBookMenuOpen = endOfBookMenuActive();

  // Do not load a page, measure words or open a dictionary on ordinary taps.
  int wordX = 0;
  int wordY = 0;
  if (!endOfBookMenuOpen && !atEndOfBook && SETTINGS.dictionaryName[0] != '\0' && SETTINGS.touchReaderControls &&
      !RenderLock::peek() && mappedInput.wasScreenLongPress(wordX, wordY)) {
    automaticPageTurnActive = false;
    openDictionaryWordSelect(wordX, wordY);
    return;
  }

#if FREEINK_MCU_C3
  const auto menuAction = readerMenuAction(SETTINGS.longPressMenuAction);
  const auto backAction = readerMenuAction(SETTINGS.longPressBackAction);
  const bool hasMenuOverride = supportedReaderAction(menuAction);
  // A disabled Menu hold keeps the ordinary release-to-menu behavior.
  if (!endOfBookMenuOpen && hasMenuOverride && menuAction != ReaderAction::Ignore &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 600)) {
    executeReaderAction(menuAction);
    return;
  }
  if (!endOfBookMenuOpen && supportedReaderAction(backAction) &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Back, 1000)) {
    executeReaderAction(backAction);
    return;
  }
#else
  constexpr bool hasMenuOverride = false;
#endif
  const unsigned long confirmHoldMs = confirmLongPressThreshold();
  // wasLongPressed() suppresses the release that follows it, so leave it unpolled while
  // the end-of-book menu or the configured YACP shortcut owns Confirm.
  const bool confirmLongPressed = !endOfBookMenuOpen && !hasMenuOverride && confirmHoldMs != 0 &&
                                  mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, confirmHoldMs);
  const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  if (confirmLongPressed) {
    switch (SETTINGS.longPressMenuFunction) {
      case CrossPointSettings::LP_MENU_BOOKMARK:
        addBookmark();
        showBookmarkMessage = true;
        bookmarkMessageTime = millis();
        requestUpdate();
        break;
      case CrossPointSettings::LP_MENU_KOSYNC:
        if (launchKOReaderSync()) {
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_DICTIONARY:
        openDictionaryWordSelect();
        return;
      case CrossPointSettings::LP_MENU_CREATE_CLIPPING:
        startClipSelection();
        return;
      case CrossPointSettings::LP_MENU_FOOTNOTES:
        if (footnoteDepth > 0) {
          restoreSavedPosition();
        } else {
          openFootnoteSelect(false);
        }
        return;
      case CrossPointSettings::LP_MENU_READER_MENU:
      case CrossPointSettings::LP_MENU_DISABLED:
      default:
        break;
    }
  }

  if (!endOfBookMenuOpen) {
    switch (mappedInput.homeButtonAction()) {
      case HomeButtonAction::Bookmark:
        if (!showBookmarkMessage) {
          addBookmark();
          showBookmarkMessage = true;
          bookmarkMessageTime = millis();
          requestUpdate();
        }
        return;
      case HomeButtonAction::Sync:
        launchKOReaderSync();
        return;
      case HomeButtonAction::Dictionary:
        if (!showDictionaryMessage) openDictionaryWordSelect();
        return;
      case HomeButtonAction::CreateClipping:
        startClipSelection();
        return;
      case HomeButtonAction::ReaderMenu:
        if (usesToolbarMenu() && section)
          openOverlay(Overlay::Toolbar);
        else
          openReaderMenu();
        return;
      default:
        break;
    }
  }

  // Link taps take priority over the reader-menu and page-turn zones.
  if (!atEndOfBook && !currentPageLinks.empty() && SETTINGS.touchReaderControls && mappedInput.hasTouch()) {
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      const auto* link = EpubReaderUtils::linkAtPoint(currentPageLinks, touchX, touchY, currentPageMarginLeft,
                                                      currentPageMarginTop);
      if (link) {
        navigateToHref(link->href, true);
        haptic_feedback::touchAction();
        return;
      }
    }
  }

  const bool touchMenu = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
  if (confirmReleased || touchMenu) {
    if (touchMenu) haptic_feedback::touchAction();
    // Toolbar style: the page is on screen and in the framebuffer, so paint the
    // toolbar over it (one refresh) instead of pushing a full-screen menu.
    if (usesToolbarMenu() && section) {
      pendingManualTurn = 0;
      reinforcementInterrupt.clear();
      openOverlay(Overlay::Toolbar);
    } else {
      openReaderMenu();
    }
    return;  // no build step before the menu shows
  }

  if (footnoteDepth > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }

  if (handleBackNavigation()) {
    return;
  }

  if (!endOfBookMenuOpen && SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::CREATE_CLIPPING &&
      mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    startClipSelection();
    return;
  }

  if ((!endOfBookMenuOpen && mappedInput.homeButtonAction() == HomeButtonAction::Footnotes) ||
      (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES &&
       mappedInput.wasReleased(MappedInputManager::Button::Power) &&
       !mappedInput.wasReleased(MappedInputManager::Button::Down))) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
    } else {
      openFootnoteSelect(false);
    }
    return;
  }

  constexpr unsigned long kMinManualTurnGapMs = 200;
  const bool turnGuardActive = RenderLock::peek() || (millis() - lastPageTurnTime) < kMinManualTurnGapMs;
  if (pendingManualTurn != 0 && !turnGuardActive) {
    if (!section) {
      pendingManualTurn = 0;
      reinforcementInterrupt.clear();
      return;
    }
    const bool forward = pendingManualTurn > 0;
    pendingManualTurn = 0;
    reinforcementInterrupt.clear();
    const bool succeeded = pageTurn(forward);
    notePageTurn(forward, succeeded);
    if (succeeded && pendingManualTurnTouch) haptic_feedback::touchAction();
    pendingManualTurnTouch = false;
    requestUpdate();
    return;
  }

  const auto sideMode = ReaderUtils::sideLongPressMode();
  if (sideMode == CrossPointSettings::SIDE_LONG_FONT_SIZE ||
      sideMode == CrossPointSettings::SIDE_LONG_ORIENTATION_CHANGE) {
    const bool top = mappedInput.wasLongPressed(MappedInputManager::Button::Up, ReaderUtils::SKIP_HOLD_MS);
    const bool bottom = mappedInput.wasLongPressed(MappedInputManager::Button::Down, ReaderUtils::SKIP_HOLD_MS);
    if (top || bottom) {
      if (sideMode == CrossPointSettings::SIDE_LONG_FONT_SIZE) {
        stepReaderFontSize(top);
      } else {
        mappedInput.waitForButtonReleaseAfterRemap();
        applyOrientation((SETTINGS.orientation + (top ? SETTINGS.ORIENTATION_COUNT - 1 : 1)) %
                         SETTINGS.ORIENTATION_COUNT);
        requestUpdate();
      }
      return;
    }
  }

  auto [prevTriggered, nextTriggered, fromTilt, fromSide] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    // Nothing to act on in this frame: build more of the chapter.
    backgroundBuildTick();
    return;
  }

  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool longPress = !fromTilt && heldMs >= ReaderUtils::SKIP_HOLD_MS;
  const auto longBehavior = ReaderUtils::pageLongPressBehavior(fromSide && !(touch.prev || touch.next));
  if (longPress && longBehavior == SETTINGS.FONT_SIZE_CHANGE) {
    stepReaderFontSize(nextTriggered);
    return;
  }
  if (longPress && longBehavior == SETTINGS.CHAPTER_SKIP) {
    const bool succeeded = skipPages(nextTriggered ? 1 : -1);
    notePageTurn(false, succeeded);
    if (succeeded && (touch.prev || touch.next)) haptic_feedback::touchAction(true);
    requestUpdate();
    return;
  }

  if (longPress && longBehavior == SETTINGS.ORIENTATION_CHANGE) {
    mappedInput.waitForButtonReleaseAfterRemap();
    const uint8_t newOrientation =
        nextTriggered ? (SETTINGS.orientation - 1 + SETTINGS.ORIENTATION_COUNT) % SETTINGS.ORIENTATION_COUNT
                      : (SETTINGS.orientation + 1) % SETTINGS.ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    if (touch.prev || touch.next) haptic_feedback::touchAction(true);
    requestUpdate();
    return;
  }

  if (!section) {
    requestUpdate();
    return;
  }

  if (turnGuardActive) {
    pendingManualTurn = prevTriggered ? -1 : 1;
    pendingManualTurnTouch = touch.prev || touch.next;
    if (SETTINGS.isMagicFlash() && SETTINGS.supportsUc8279MagicFlash()) reinforcementInterrupt.request();
    return;
  }

  const bool succeeded = pageTurn(!prevTriggered);
  notePageTurn(!prevTriggered, succeeded);
  if (succeeded && (touch.prev || touch.next)) haptic_feedback::touchAction();
  requestUpdate();
}

void EpubReaderActivity::jumpToPercent(int percent) {
  pauseReadingSession();
  if (!epub) return;
  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) return;

  percent = clampPercent(percent);

  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) targetSize = bookSize - 1;

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) return;

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  const int found = epub->getSpineIndexForSize(targetSize);
  if (found >= 0) {
    targetSpineIndex = found;
    prevCumulative = (found > 0) ? epub->getCumulativeSpineItemSize(found - 1) : 0;
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  pendingSpineProgress = std::clamp(pendingSpineProgress, 0.0f, 1.0f);

  {
    RenderLock lock;
    clearDeferredReposition();
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::applyProgressChange(const ProgressChangeResult& sync) {
  if (sync.hasVisibleTextOffset && sync.spineIndex >= 0 && sync.spineIndex < epub->getSpineItemsCount()) {
    RenderLock lock;
    clearDeferredReposition();
    const auto page = section && currentSpineIndex == sync.spineIndex
                          ? section->getPageForVisibleTextOffset(sync.visibleTextOffset)
                          : std::nullopt;
    if (page) {
      section->currentPage = *page;
    } else {
      currentSpineIndex = sync.spineIndex;
      pendingOffsetJump = sync.visibleTextOffset;
      nextPageNumber = std::max(0, sync.page);
      section.reset();
    }
    requestUpdate();
    return;
  }

  int targetSpineIndex = sync.spineIndex;
  int targetPage = sync.page;
  const int activeTotalPages = section ? section->estimatedTotalPages() : 0;
  const bool cachedPageMatchesActiveSection = section && sync.totalPages > 0 && currentSpineIndex == sync.spineIndex &&
                                              sync.page >= 0 && sync.page < sync.totalPages &&
                                              activeTotalPages == sync.totalPages;

  if (!cachedPageMatchesActiveSection && sync.hasSavedProgress) {
    const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
    CrossPointPosition fallback =
        ProgressMapper::toCrossPoint(epub, {sync.xpath, sync.percentage}, renderer, currentSpineIndex, totalPages);
    targetSpineIndex = fallback.spineIndex;
    targetPage = fallback.pageNumber;
  }

  RenderLock lock;
  clearDeferredReposition();

  if (currentSpineIndex != targetSpineIndex) {
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = targetPage;
    section.reset();
  } else if (section && section->currentPage != targetPage) {
    const int clampedTargetPage = std::max(0, targetPage);
    section->currentPage = clampedTargetPage;
  } else if (!section) {
    nextPageNumber = targetPage;
  }
  requestUpdate();
}

int EpubReaderActivity::currentTocIndex() const {
  if (!epub) return 0;
  if (section) return std::max(0, section->getTocIndexForPage(section->currentPage));
  // Child screens may have released the section; its saved anchor map remains readable.
  Section cachedSection(epub, currentSpineIndex, renderer);
  return std::max(0, cachedSection.getTocIndexForPage(nextPageNumber));
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  auto progressChangeResultHandler = [this](const ActivityResult& result) {
    loadCachedBookmarks();
    if (result.isCancelled) {
      openReaderMenu();
    } else {
      const auto& sync = std::get<ProgressChangeResult>(result.data);

      applyProgressChange(sync);
    }
  };

  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      int tocIndex;
      // Release the section while the chapter list is up (mirrors the
      // TEXT_SETTINGS path): picking a chapter resets it anyway, and its
      // tens-of-KB footprint is the difference between the chapter list
      // holding its CJK glyph arena (RAM-only repaints) and re-reading
      // glyphs from SD on every row step. Cancel restores via the same
      // cached-position rebuild TEXT_SETTINGS uses.
      {
        RenderLock lock;
        tocIndex = currentTocIndex();
        if (section) {
          rememberCurrentContentOffset();
          cachedSpineIndex = currentSpineIndex;
          cachedChapterTotalPageCount = section->pageCount;
          nextPageNumber = section->currentPage;
        }
        section.reset();
      }
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, tocIndex),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
              return;
            }
            const auto& chapterResult = std::get<ChapterResult>(result.data);
            RenderLock lock;
            clearDeferredReposition();
            currentSpineIndex = chapterResult.spineIndex;
            pendingAnchor = chapterResult.anchor;
            nextPageNumber = 0;
            section.reset();
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      openFootnoteSelect(true);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS: {
      startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                    TextSettingsActivity::Tab::Family),
                             [this](const ActivityResult&) {
                               {
                                 RenderLock lock;
                                 if (section) {
                                   rememberCurrentContentOffset();
                                   cachedSpineIndex = currentSpineIndex;
                                   cachedChapterTotalPageCount = section->pageCount;
                                   nextPageNumber = section->currentPage;
                                 }
                                 section.reset();
                               }
                               openReaderMenu();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BLUETOOTH:
    case EpubReaderMenuActivity::MenuAction::NIGHT_MODE:
      // Handled in-place by EpubReaderMenuActivity so its On/Off value updates
      // without closing the menu.
      break;
    case EpubReaderMenuActivity::MenuAction::FRONTLIGHT:
      // Handled in-place by EpubReaderMenuActivity using the live frontlight HAL.
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      const int initialPercent = bookPercentFor(chapterPosition());
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
            } else {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READING_STATS: {
      pauseReadingSession();
      ensureReadingSession();
      if (!readingSession || !epub) break;
      const float progress = static_cast<float>(bookPercentFor(chapterPosition()));
      uint32_t timeLeft = 0;
      {
        RenderLock lock;
        timeLeft = timeLeftSeconds(true);
        readingSession->cacheTimeLeft(timeLeft);
        // As for the chapter list, release pagination buffers before allocating
        // the stats screen. CrossPoint restores the saved content offset on return.
        if (section) {
          rememberCurrentContentOffset();
          cachedSpineIndex = currentSpineIndex;
          cachedChapterTotalPageCount = section->pageCount;
          nextPageNumber = section->currentPage;
        }
        section.reset();
      }
      auto screen = makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInput, epub->getTitle(), epub->getCachePath(),
                                                         readingSession->book, progress, timeLeft > 0, timeLeft,
                                                         readingSession->global);
      if (!screen) {
        LOG_ERR("STATS", "OOM: statistics screen");
        break;
      }
      // The snapshot lives in the already allocated screen, keeping the 730-day
      // table off the input stack. Neither preview nor edits split the session.
      screen->setSessionPreview(*readingSession);
      screen->setEditHandler(
          [](const BookReadingStats& edited, void* context) {
            auto* reader = static_cast<EpubReaderActivity*>(context);
            auto& session = *reader->readingSession;
            session.applyEdits(edited);
            // Persist only the actual counters and metadata. Preview-only time is
            // added exactly once by finish(), including when Home clears the stack.
            session.book.save(reader->epub->getCachePath());
            session.global.save();
            if (!FinishedBooksIndex::recordCanonical(reader->bookPath, reader->epub->getCachePath(),
                                                     reader->epub->getTitle(), reader->epub->getAuthor(),
                                                     session.book)) {
              LOG_ERR("STATS", "Could not update edited completion");
            }
          },
          this);
      startActivityForResult(std::move(screen), [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY: {
      openDictionaryWordSelect();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                 [this](const ActivityResult&) { openReaderMenu(); });
          break;
        }
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      goReaderHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock;
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          } else {
            section.reset();
            if (!clearBookCachePreservingUserState(epub->getPath()))
              LOG_ERR("ERS", "Could not completely clear generated cache; user state retained");
          }
        }
      }
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock;
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      launchKOReaderSync();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS: {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, epub, epub->getPath()),
          progressChangeResultHandler);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SAVE_CLIPPING: {
      startClipSelection();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::VIEW_CLIPPINGS: {
      openClippings();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK: {
      addBookmark();
      break;
    }
  }
}

uint8_t EpubReaderActivity::readerPowerAction(bool longPress) const {
#if !FREEINK_MCU_C3
  if (!longPress) return static_cast<uint8_t>(ReaderAction::Upstream);
#endif
  const auto action = static_cast<ReaderAction>(longPress ? SETTINGS.longPwrBtn : SETTINGS.readerShortPowerAction);
  if (overlay == Overlay::None && !endOfBookMenuActive() && supportedReaderAction(action))
    return static_cast<uint8_t>(action);
  return static_cast<uint8_t>(ReaderAction::Upstream);
}

void EpubReaderActivity::executeReaderAction(ReaderAction action) {
  automaticPageTurnActive = false;
  LOG_DBG("ERS", "Reader action %u", static_cast<unsigned>(action));
  switch (action) {
    case ReaderAction::Ignore:
      return;
    case ReaderAction::Sleep:
      activityManager.requestReaderSleep();
      return;
    case ReaderAction::NextPage:
      pageTurn(true);
      requestUpdate();
      return;
    case ReaderAction::Refresh:
      handleForcedRefresh();
      return;
    case ReaderAction::Reinforce: {
      RenderLock lock;
      reinforcementRequested = true;
      forcedRefreshPending = true;
      pagesUntilFullRefresh = 0;
      requestUpdate();
      return;
    }
    case ReaderAction::Font:
      SETTINGS.clearSdFontFamily();
      SETTINGS.fontFamily = (SETTINGS.fontFamily + 1) % CrossPointSettings::BUILTIN_FONT_COUNT;
      applyReaderTextSettings();
      return;
    case ReaderAction::Guide:
      SETTINGS.guideReadingEnabled = !SETTINGS.guideReadingEnabled;
      applyReaderTextSettings();
      return;
    case ReaderAction::Focus:
      SETTINGS.focusReadingEnabled = !SETTINGS.focusReadingEnabled;
      applyReaderTextSettings();
      return;
    case ReaderAction::Bookmark:
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      requestUpdate();
      return;
    case ReaderAction::Sync: {
      if (launchKOReaderSync()) return;
      // The child activity outlives this input frame, so it must be owned on the heap.
      auto screen = makeUniqueNoThrow<KOReaderSettingsActivity>(renderer, mappedInput);
      if (!screen) {
        LOG_ERR("ERS", "OOM: sync settings");
        return;
      }
      startActivityForResult(std::move(screen), [this](const ActivityResult&) { requestUpdate(); });
      return;
    }
    case ReaderAction::Completed: {
      pauseReadingSession();
      ensureReadingSession();
      if (!readingSession || !epub) return;
      ReadingStatsDateTime now;
      getCurrentLocalReadingStatsDateTime(now);
      if (readingSession->setCompleted(!readingSession->book.isCompleted, now)) {
        readingSession->book.save(epub->getCachePath());
        readingSession->global.save();
        if (!FinishedBooksIndex::recordCanonical(bookPath, epub->getCachePath(), epub->getTitle(), epub->getAuthor(),
                                                 readingSession->book))
          LOG_ERR("STATS", "Could not record completion command");
      }
      // Show the resulting status using the existing translated statistics screen.
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::READING_STATS);
      return;
    }
    case ReaderAction::Dictionary:
      openDictionaryWordSelect();
      return;
    case ReaderAction::Clipping:
      startClipSelection();
      return;
    case ReaderAction::Stats:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::READING_STATS);
      return;
    case ReaderAction::Screenshot:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SCREENSHOT);
      return;
    case ReaderAction::Interval:
      openAutoTurnIntervalPicker();
      return;
    case ReaderAction::Tilt:
      if (SETTINGS.tiltPageTurn) {
        SETTINGS.tiltPageTurnLastMode = SETTINGS.tiltPageTurn;
        SETTINGS.tiltPageTurn = CrossPointSettings::TILT_OFF;
      } else {
        SETTINGS.tiltPageTurn = SETTINGS.tiltPageTurnLastMode;
      }
      SETTINGS.saveToFile();
      requestUpdate();
      return;
    case ReaderAction::Dark:
      SETTINGS.readerDarkMode = !SETTINGS.readerDarkMode;
      SETTINGS.saveToFile();
      requestUpdate();
      return;
    case ReaderAction::Footnotes:
      if (footnoteDepth > 0 && SETTINGS.pwrBtnFootnoteBack)
        restoreSavedPosition();
      else
        openFootnoteSelect(false);
      return;
    case ReaderAction::Browser:
      activityManager.goToFileBrowser(bookPath);
      return;
    case ReaderAction::Transfer:
    case ReaderAction::Calibre:
    case ReaderAction::Join:
    case ReaderAction::Hotspot: {
      const auto mode = action == ReaderAction::Calibre   ? NetworkMode::CONNECT_CALIBRE
                        : action == ReaderAction::Hotspot ? NetworkMode::CREATE_HOTSPOT
                                                          : NetworkMode::JOIN_NETWORK;
      // Activity-owned network state; the reader is released before Wi-Fi starts.
      auto screen =
          makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInput, action != ReaderAction::Transfer, mode);
      if (!screen) {
        LOG_ERR("ERS", "OOM: file transfer");
        return;
      }
      activityManager.replaceActivity(std::move(screen));
      return;
    }
    default:
      return;
  }
}

unsigned long EpubReaderActivity::confirmLongPressThreshold() const {
  // The setting is hidden on these boards, so a stale stored value must not act.
  if (BoardConfig::hasTouch()) return 0;
  switch (SETTINGS.longPressMenuFunction) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
    case CrossPointSettings::LP_MENU_DICTIONARY:
    case CrossPointSettings::LP_MENU_CREATE_CLIPPING:
    case CrossPointSettings::LP_MENU_FOOTNOTES:
      return ReaderUtils::BOOKMARK_HOLD_MS;
    case CrossPointSettings::LP_MENU_KOSYNC:
      return KOREADER_STORE.hasCredentials() ? ReaderUtils::GO_HOME_MS : 0;
    case CrossPointSettings::LP_MENU_READER_MENU:
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return 0;
  }
}

bool EpubReaderActivity::launchKOReaderSync() {
  flushReaderSession();

  // Without a KOReader server the sync activity only brings the device online
  // and drains that outbox; a failed join exits back to the reader with the
  // events still queued.
  if (!KOREADER_STORE.hasCredentials()) {
    activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
        renderer, mappedInput, epub->getPath(), getCurrentPosition(), SavedProgressPosition{}, "",
        /*pluginEventsOnly=*/true));
    return true;
  }

  RenderLock renderLock;

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;

  CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoPos;
  const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
  const std::string savedEpubPath = epub->getPath();

  if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
    LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
    pendingSyncSaveError = true;
    requestUpdate();
    return true;
  }

  LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
  {
    if (section) {
      nextPageNumber = section->currentPage;
    }
    discardOverlayPage();
    ImageBlock::releaseRenderCache();
    ImageBlock::setExtractor(nullptr, nullptr);
    section.reset();
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->releaseSdFontCaches();
    }
    // No rendering may run while the chapter mapper borrows the framebuffer.
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      localKoPos = ProgressMapper::toSavedProgress(epub, localPos);
    }
    // The destructor can no longer save the back-stack once epub is gone.
    if (footnoteDepth > 0) saveLinkStack();
    epub.reset();
  }
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
      renderer, mappedInput, savedEpubPath, localPos, std::move(localKoPos), std::move(localChapterName)));
  return true;
}

void EpubReaderActivity::applyInitialOrientation() {
  ReaderActivity::applyInitialOrientation();
  appliedOrientation = SETTINGS.orientation;
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // Also runs when SETTINGS already holds the new value but this layout was
  // built for the old one — that is what an external change looks like here.
  if (SETTINGS.orientation == orientation && appliedOrientation == orientation) {
    return;
  }

  RenderLock lock(*this);
  if (section) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
  }

  if (SETTINGS.orientation != orientation) {
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();
  }
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  appliedOrientation = orientation;
  section.reset();
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  if (selectedPageTurnOption == 0 || selectedPageTurnOption > CUSTOM_AUTO_TURN) {
    automaticPageTurnActive = false;
    return;
  }

  lastPageTurnTime = millis();
  pageTurnDuration = selectedPageTurnOption == CUSTOM_AUTO_TURN
                         ? static_cast<unsigned long>(SETTINGS.autoPageTurnSeconds) * 1000
                         : (1UL * 60 * 1000) / PAGE_TURN_RATES[selectedPageTurnOption];
  automaticPageTurnActive = true;
  autoTurnOption = selectedPageTurnOption;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    RenderLock lock;
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
}

bool EpubReaderActivity::pageTurn(bool isForwardTurn) {
  if (!section) return false;
  // A "Bluetooth failed" note has been read: the page this turn paints shows the title again.
  bleturner::acknowledgeLinkNote();
  {
    RenderLock lock;
    acceptReadingPage();
    const bool canTurn = isForwardTurn || section->currentPage > 0 || currentSpineIndex > 0;
    if (canTurn && readingSession && SETTINGS.trackReadingStats) {
      readingSession->turn(millis(), isForwardTurn,
                           section->currentPage >= section->pageCount - 1 && !section->isBuilding());
      publishTimeLeftInputs();
    }
    clearDeferredReposition();
  }
  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1 || section->isBuilding()) {
      section->currentPage++;
#if defined(YACP_PERSONAL_GO)
      GoDgs::recordReadPage();
#endif
      lastPageTurnTime = millis();
      return true;
    } else if (currentSpineIndex + 1 < epub->getSpineItemsCount()) {
      RenderLock lock;
      nextPageNumber = 0;
      currentSpineIndex++;
      section.reset();
#if defined(YACP_PERSONAL_GO)
      GoDgs::recordReadPage();
#endif
      lastPageTurnTime = millis();
      return true;
    } else {
      currentSpineIndex = epub->getSpineItemsCount();
      if (footnoteDepth == 0 && !section->isPartial() && !section->isBuilding()) recordBookCompletion();
      lastPageTurnTime = millis();
      return true;
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
      lastPageTurnTime = millis();
      return true;
    } else if (currentSpineIndex > 0) {
      RenderLock lock;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      currentSpineIndex--;
      section.reset();
      lastPageTurnTime = millis();
      return true;
    }
  }
  return false;
}

bool EpubReaderActivity::skipPages(int amount) {
  if (!section) return false;
  bleturner::acknowledgeLinkNote();
  if (amount > 0) {
    RenderLock lock;
    nextPageNumber = 0;
    currentSpineIndex++;
    section.reset();
    return true;
  } else {
    if (section->currentPage > 0) {
      section->currentPage = 0;
      return true;
    } else if (currentSpineIndex > 0) {
      RenderLock lock;
      nextPageNumber = 0;
      currentSpineIndex--;
      section.reset();
      return true;
    }
  }
  return false;
}

// Failed protected open: show the standard option dialog (wrapped message)
// instead of silently falling back to the previous screen. Exact error
// strings are set by openProtectedBook (ContentProtection.cpp).
bool EpubReaderActivity::handleLoadFailure() {
  if (loadProtectionError.empty()) return false;
  const std::string& perr = loadProtectionError;
  StrId msg = StrId::STR_DRM_PROTECTED_FILE;
  bool offerSync = false;
  if (perr == "access expired") {
    msg = StrId::STR_LOAN_EXPIRED;
  } else if (perr == "loan date unverified") {
    msg = StrId::STR_LOAN_TIME_UNVERIFIED;
    offerSync = true;
  }
  const char* options[2] = {I18N.get(offerSync ? StrId::STR_CLOCK_SYNC_NOW : StrId::STR_OK_BUTTON),
                            I18N.get(StrId::STR_OK_BUTTON)};
  loadFailurePopup.showMessage("", I18N.get(msg), options, offerSync ? 2 : 1, 0, [this, offerSync](const int index) {
    if (offerSync && index == 0) {
      beginLoanTimeSync();
      return;
    }
    finish();
  });
  requestUpdate();
  return true;  // stay alive; the popup's Back dismiss lands in loop()'s !epub finish
}

void EpubReaderActivity::beginLoanTimeSync() {
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("ERS", "OOM: Wi-Fi selection for loan time sync");
    finish();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
      finish();
      return;
    }
    GUI.drawPopup(renderer, tr(STR_SYNCING_TIME));
    const bool synced = trustedtime::syncNow(5000);
    WiFi.disconnect(false);
    delay(30);
    if (!synced) {
      // Reopening would hit the same unverified-time refusal: offer a retry.
      const char* options[2] = {I18N.get(StrId::STR_RETRY), I18N.get(StrId::STR_OK_BUTTON)};
      loadFailurePopup.showMessage("", I18N.get(StrId::STR_CLOCK_SYNC_FAIL), options, 2, 0, [this](const int index) {
        if (index == 0) {
          beginLoanTimeSync();
          return;
        }
        finish();
      });
      requestUpdate();
      return;
    }
    APP_STATE.openEpubPath = bookPath;
    APP_STATE.saveToFile();
    // Reboot straight back into this book with a clean heap (no-op on touch
    // boards, which fall through to the in-place relaunch below).
    silentRestartToReader();
    activityManager.goToReader(bookPath);
  });
}

void EpubReaderActivity::render(RenderLock&& lock) {
  if (loadFailurePopup.isActive()) {
    renderer.clearScreen();
    loadFailurePopup.processRender(renderer, mappedInput);
    return;
  }
  ReaderActivity::render(std::move(lock));
}

bool EpubReaderActivity::remoteShortcut(const bool readerMenu) {
  if (!section) return false;
  automaticPageTurnActive = false;
  if (!readerMenu) {
    startClipSelection();
  } else if (usesToolbarMenu()) {
    openOverlay(Overlay::Toolbar);
  } else {
    openReaderMenu();
  }
  return true;
}

bool EpubReaderActivity::isAtEndOfBook() const { return epub && currentSpineIndex >= epub->getSpineItemsCount(); }

void EpubReaderActivity::onReturnFromEndOfBook() {
  if (epub && epub->getSpineItemsCount() > 0) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = 0;
    pendingPageJump = std::numeric_limits<uint16_t>::max();
  }
}

// A started build runs to the end of the chapter, one bounded step per idle loop frame, so the
// parser and its page LUT are freed soon (and a radio stopped for the build comes back with no
// parser alive). Not while the radio holds the heap, and not in a spine whose background build
// failed.
bool EpubReaderActivity::backgroundBuildWanted() const {
  return section && section->isBuilding() && !bleturner::holdsHeap() && currentSpineIndex != backgroundBuildFailedSpine;
}

bool EpubReaderActivity::skipLoopDelay() {
  // The main loop holds the render lock while querying this hint. It keeps yielding while a button
  // is down: a press that starts inside a long build step gets its second debounce sample ~5 ms
  // after the step instead of after a 10 ms sleep.
  return overlay == Overlay::None && !buildHeapPaused && backgroundBuildWanted();
}

reader_refresh::WakeTextPage EpubReaderActivity::retainedTextPage() const {
  const bool positionChanged = section &&
      (paintedTextPage.chapter != currentSpineIndex || paintedTextPage.page != section->currentPage);
  const bool messagePending = showBookmarkMessage || showClippingMessage || showDictionaryMessage ||
                              showFontDamagedMessage || pendingSyncSaveError;
  if (!section || pageBufferStale || overlay != Overlay::None || footnoteDepth != 0 || positionChanged ||
      messagePending || !paintedTextPage.valid) {
    wake_refresh_trace::event("sleep.reject.page",
                              (!section ? 1 : 0) | (pageBufferStale ? 2 : 0) | (positionChanged ? 4 : 0),
                              static_cast<int>(overlay), (footnoteDepth != 0 ? 1 : 0) | (messagePending ? 2 : 0) |
                              (!paintedTextPage.valid ? 4 : 0));
    return {};
  }
  auto page = paintedTextPage;
  if (page.valid) page.book = std::hash<std::string>{}(bookPath);
  return page;
}

void EpubReaderActivity::loadRetainedPageMetadata() {
  if (!retainedPageMetadataPending.load(std::memory_order_acquire)) return;
  RenderLock lock(*this);
  if (!retainedPageMetadataPending.exchange(false) || !section) return;
  if (auto page = section->loadPage(section->currentPage)) {
    currentPageFootnotes = std::move(page->footnotes);
  }
}

bool EpubReaderActivity::tryAdoptRetainedPage() {
  const auto retained = std::exchange(wakeTextPage, {});
  if (!retained.valid || retained.transition == reader_refresh::WakeTransition::None || !gpio.supportsReaderWakePreview() ||
      display.isInverted() || SETTINGS.isTextAntiAliasingEnabled() || overlay != Overlay::None || forcedRefreshPending ||
      !section || section->isPartial() || section->isBuilding() || section->currentPage < 0 ||
      section->currentPage >= section->pageCount) {
    return false;
  }
  const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage));
  const reader_refresh::WakeTextPage resolved{
      std::hash<std::string>{}(bookPath), offset.value_or(0),
      static_cast<uint16_t>(currentSpineIndex), static_cast<uint16_t>(section->currentPage),
      appliedOrientation, offset.has_value(), reader_refresh::WakeTransition::None};
  if (!retained.matches(resolved)) {
    wake_refresh_trace::event("reader.reject.position");
    return false;
  }

  // Boot already painted this frame. Adopt it only after the current section
  // and reading position resolve to the same page.
  pagesUntilFullRefresh = std::max(SETTINGS.getRefreshFrequency(), 2);
  currentPageVisibleOffset = *offset;
  currentPageFootnotes.clear();
  updateBookmarkFlag();
  paintedTextPage = retained;
  lastSavedSpineIndex = currentSpineIndex;
  lastSavedPage = section->currentPage;
  lastSavedPageCount = section->pageCount;
  pageBufferStale = false;
  reusedWakePage = true;
  retainedPageMetadataPending.store(true, std::memory_order_release);
  const auto now = millis();
  lastRenderCompleteMs = now;
  readingPageShownAt.store(now, std::memory_order_relaxed);
  readingPagePending.store(true, std::memory_order_release);
  markPageRendered();
  PowerHistory::recordReaderPageDisplay();
  boot_timing::retainedPage.store(static_cast<uint8_t>(retained.transition), std::memory_order_relaxed);
#ifdef SIMULATOR
  LOG_INF("ERS", "Restored retained wake frame: mode=%u chapter=%d page=%d",
          static_cast<unsigned>(retained.transition), currentSpineIndex, section->currentPage);
#endif
  return true;
}

void EpubReaderActivity::renderBook() {
  currentPageLinks.clear();
  if (!epub) return;
  // A radio start in flight takes ~50 KB while the stack comes up: the page waits for it rather than
  // take the heap beside it (X3: abort() in a page restore at free 22,676 B). About 20 ms, once a visit.
  bleturner::settleStart();
  // Runs under the render task's RenderLock; catches every requestUpdate()
  // exit from the overlay while its deferred chrome refresh is still pending.
  settleOverlayRefresh();

  const auto showPendingSyncSaveError = [this]() {
    if (!pendingSyncSaveError) return;
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  };

  const auto showBuildError = [this](const char* message = nullptr) {
    renderer.clearScreen();
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawPopup(renderer, message ? message : tr(STR_INDEX_FAILED));
    automaticPageTurnActive = false;
  };

  if (currentSpineIndex < 0) currentSpineIndex = 0;
  if (currentSpineIndex > epub->getSpineItemsCount()) currentSpineIndex = epub->getSpineItemsCount();

  if (currentSpineIndex == epub->getSpineItemsCount()) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += UITheme::getReaderTopInset(renderer, orientedMarginTop);
  orientedMarginLeft +=
      SETTINGS.publisherPageNumbers ? std::max<int>(15, SETTINGS.screenMargin) : SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin));
  } else {
    orientedMarginBottom += std::max(SETTINGS.screenMargin, statusBarHeight);
  }

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;

  const ReaderRenderSpec renderSpec = SETTINGS.readerRenderSpec(viewportWidth, viewportHeight);

  if (!section) {
    loadReferencePages();
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));
    partialRebuildStartFailed = false;

    const bool cacheLoaded = section->loadSectionFile(renderSpec);
    if (cacheLoaded) {
      cachedChapterTotalPageCount = 0;
      cachedVisibleTextOffset.reset();
    }
    const bool cacheComplete = cacheLoaded && !section->isPartial();
    const bool explicitOffsetJump = pendingOffsetJump.has_value();
    const std::optional<uint32_t> offsetJump =
        explicitOffsetJump ? pendingOffsetJump
        : (pendingPageJump.has_value() || !pendingAnchor.empty() || currentSpineIndex != cachedSpineIndex)
            ? std::nullopt
            : cachedVisibleTextOffset;
    if (!cacheComplete) {
      wakeTextPage = {};
      if (section->isPartial()) {
        LOG_DBG("ERS", "Partial cache found (%d pages), resuming build...", section->pageCount);
      } else {
        LOG_DBG("ERS", "Cache not found, building...");
      }

      const bool needsFullBuild = pendingPercentJump;
      if (needsFullBuild) {
        GUI.drawPopup(renderer, tr(STR_INDEXING));
        pagesUntilFullRefresh = 0;
        const auto popupFn = [this]() {
          if (renderer.hasFrameBuffer()) GUI.drawPopup(renderer, tr(STR_INDEXING));
        };
        if (!makeRoomForChapterBuild()) {
          section.reset();
          showBuildError(tr(STR_BT_BUILD_REFUSED));
          return;
        }
        GfxRenderer::FrameBufferLoan loan(renderer);
        if (!section->createSectionFile(renderSpec, popupFn)) {
          LOG_ERR("ERS", "Failed to persist page data to SD");
          section.reset();
          loan.end();
          showBuildError();
          return;
        }
        loan.end();
      } else {
        const int target = pendingPageJump.has_value() ? *pendingPageJump : (nextPageNumber < 0 ? 0 : nextPageNumber);
        const bool anchorJump = !pendingAnchor.empty();

        if (section->isPartial() &&
            (anchorJump               ? section->getPageForAnchor(pendingAnchor).has_value()
             : offsetJump.has_value() ? section->getPageForVisibleTextOffset(*offsetJump).has_value()
                                      : target + PARTIAL_REBUILD_START_MARGIN < static_cast<int>(section->pageCount))) {
          LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", target, section->pageCount);
        } else {
          const size_t spineBytes =
              epub->getCumulativeSpineItemSize(currentSpineIndex) -
              (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
          const bool willInflate = !section->hasHtmlCache();
          bool showPopup;
          if (anchorJump) {
            showPopup = !section->findAnchor(pendingAnchor).has_value() && spineBytes > BUILD_POPUP_BYTE_THRESHOLD;
          } else {
            const bool targetAvailable = target < static_cast<int>(section->pageCount);
            showPopup = !targetAvailable && ((spineBytes > BUILD_POPUP_BYTE_THRESHOLD && willInflate) ||
                                             target > BUILD_POPUP_PAGE_THRESHOLD);
          }
          if (showPopup) {
            GUI.drawPopup(renderer, tr(STR_INDEXING));
            pagesUntilFullRefresh = 0;
          }
          buildPopupPending = !showPopup;
          // Section (re)builds are the heap-hungriest path (per-word
          // allocations for the whole section). Under TTF heap pressure, shed
          // every rebuildable font cache first — dropped glyphs re-fault on
          // demand after the build. Skipped for cpfont/builtin reading: the
          // release drops the persistent advance table and mini tables, which
          // would force SD metric re-reads on every fresh chapter for no gain.
          if (!renderer.getTtfFonts().empty()) {
            if (auto* fcm = renderer.getFontCacheManager()) {
              fcm->releaseSdFontCaches();
            }
          }
          if (!makeRoomForChapterBuild()) {
            section.reset();
            buildPopupPending = false;
            showBuildError(tr(STR_BT_BUILD_REFUSED));
            return;
          }
          LOG_DBG("ERS", "Heap before section build: %u (max block %u)", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
          const unsigned long buildStartMs = millis();
          bool started;
          {
            GfxRenderer::FrameBufferLoan loan(renderer);
            started = section->startBuild(renderSpec, [this] { showBuildPopup(renderer, pagesUntilFullRefresh); });
          }
          if (!started) {
            LOG_ERR("ERS", "Failed to start section build");
            section.reset();
            buildPopupPending = false;
            showBuildError();
            return;
          }
          while (!section->isBuildComplete() &&
                 (anchorJump               ? !section->findAnchor(pendingAnchor)
                  : offsetJump.has_value() ? !section->buildReachedVisibleTextOffset(*offsetJump)
                                           : static_cast<int>(section->pageCount) <= target)) {
            if (buildPopupPending && millis() - buildStartMs >= BUILD_POPUP_DEADLINE_MS) {
              showBuildPopup(renderer, pagesUntilFullRefresh);
            }
            if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
              LOG_ERR("ERS", "Failed during incremental section build");
              section.reset();
              buildPopupPending = false;
              showBuildError();
              return;
            }
          }
          buildPopupPending = false;
        }
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      section->currentPage = *pendingPageJump;
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) section->currentPage = 0;
    }

    if (offsetJump.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*offsetJump)) {
        section->currentPage = *offsetPage;
        clearDeferredReposition();
      }
    }
    if (explicitOffsetJump) {
      clearDeferredReposition();
    }
    pendingOffsetJump.reset();

    if (!pendingAnchor.empty()) {
      const auto page = section->findAnchor(pendingAnchor);
      if (page) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      }
      pendingAnchor.clear();
    }

    if (pendingPercentJump && section->pageCount > 0) {
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) newPage = section->pageCount - 1;
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  // The two loops below build on until the page to show exists.
  if ((section->isPartial() || section->isBuilding()) && section->currentPage >= static_cast<int>(section->pageCount) &&
      !makeRoomForChapterBuild()) {
    section.reset();
    showBuildError(tr(STR_BT_BUILD_REFUSED));
    return;
  }
  if (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    pagesUntilFullRefresh = 0;
  }
  while (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    if (!section->isBuilding() && !section->startBuild(renderSpec)) {
      LOG_ERR("ERS", "Failed to start partial extension build");
      section.reset();
      showBuildError();
      return;
    }
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        section.reset();
        showBuildError();
        return;
      }
    }
  }
  if (section->isBuilding()) {
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        section.reset();
        showBuildError();
        return;
      }
    }
  }

  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }

  applyDeferredReposition();

  if (wakeTextPage.valid) {
    const bool adopted = tryAdoptRetainedPage();
    wake_refresh_trace::event("reader.adopt", adopted);
    if (adopted) return;
  }
  retainedPageMetadataPending.store(false, std::memory_order_release);
  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    pageBufferStale = false;
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  updateBookmarkFlag();

  {
    auto p = section->loadPage(section->currentPage);
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      automaticPageTurnActive = false;
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      section->abandonBuild();
      section->clearCache();
      section.reset();
      if (giveUp) {
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
        renderer.displayBuffer();
        showPendingSyncSaveError();
        return;
      }
      requestUpdate();
      showPendingSyncSaveError();
      return;
    }
    pageLoadRetryCount = 0;

    currentPageVisibleOffset = p->visibleTextOffset;
    currentPageFootnotes = std::move(p->footnotes);
    currentPageLinks = std::move(p->links);
    currentPageMarginLeft = orientedMarginLeft;
    currentPageMarginTop = orientedMarginTop;

    // The overlay and non-tiled grayscale renderer share the renderer's single
    // stored-BW slot. Release the old page snapshot before renderContents()
    // needs that slot, then snapshot the newly rendered page below.
    discardOverlayPage();

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    lastRenderCompleteMs = millis();
    markPageRendered();
    PowerHistory::recordReaderPageDisplay();
    if (overlay == Overlay::None && footnoteDepth == 0) {
      readingPageShownAt.store(lastRenderCompleteMs, std::memory_order_relaxed);
      readingPagePending.store(true, std::memory_order_release);
    }
  }

  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage ||
      section->pageCount != lastSavedPageCount) {
    if (saveProgress(currentSpineIndex, section->currentPage, section->estimatedTotalPages(), true)) {
      lastSavedSpineIndex = currentSpineIndex;
      lastSavedPage = section->currentPage;
      lastSavedPageCount = section->estimatedTotalPages();
    }
  }

  showPendingSyncSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  if (showBookmarkMessage) {
    GUI.drawPopup(renderer, bookmarkRemoved ? tr(STR_BOOKMARK_REMOVED) : tr(STR_BOOKMARK_ADDED));
  }

  if (showDictionaryMessage) {
    GUI.drawPopup(renderer, tr(STR_DICT_NO_DICT_SET));
  }

  // Glyphs of a damaged .cpfont draw as blanks; say why, once per loaded font.
  if (SdCardFont::takeCorruptGlyphNotice()) {
    showFontDamagedMessage = true;
    fontDamagedMessageTime = millis();
  }
  if (showFontDamagedMessage) {
    GUI.drawPopup(renderer, tr(STR_FONT_DAMAGED));
  }

  if (showClippingMessage) {
    GUI.drawPopup(renderer, I18N.get(clippingMessage));
  }

  pageBufferStale = false;  // the page is back in the framebuffer

  // Toolbar menu: overlay the toolbar / panel on top of the freshly rendered page.
  if (overlay != Overlay::None && usesToolbarMenu()) {
    // The page just re-rendered under the overlay: refresh the snapshot that
    // backs panel->toolbar restores (any previous copy is stale).
    overlayPageStored = renderer.storeBwBuffer();
    renderOverlay();
    // An open option picker rides on top of the freshly drawn panel.
    if (overlayPopup.isActive()) overlayPopup.render(renderer);
    // FAST, same as openOverlay: HALF's inverting pass flashes the sheet
    // (white, in night mode) on every repaint under an open panel. Any AA
    // residue a FAST differential leaves under the chrome has not shown in
    // practice; restore a HALF cleanup here if text ever visibly ghosts
    // through the sheet (see #2190 for the mechanism).
    pushOverlayRefresh();
  }
}

void EpubReaderActivity::onEndOfBookRendered() {
  automaticPageTurnActive = false;
  if (pendingSyncSaveError) {
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  }
}

bool EpubReaderActivity::applyDeferredReposition() {
  if ((!cachedVisibleTextOffset.has_value() && cachedChapterTotalPageCount == 0) || !section || section->isBuilding()) {
    return false;
  }
  bool changed = false;
  if (currentSpineIndex == cachedSpineIndex) {
    int newPage = section->currentPage;
    bool mappedOffset = false;
    if (cachedVisibleTextOffset.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*cachedVisibleTextOffset)) {
        newPage = *offsetPage;
        mappedOffset = true;
      }
    }
    if (!mappedOffset && cachedChapterTotalPageCount > 0 && section->pageCount != cachedChapterTotalPageCount) {
      const float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
      newPage = static_cast<int>(progress * static_cast<float>(section->pageCount));
    }
    if (newPage < 0) newPage = 0;
    if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
      newPage = section->pageCount - 1;
    }
    if (newPage != section->currentPage) {
      section->currentPage = newPage;
      changed = true;
    }
  }
  clearDeferredReposition();
  return changed;
}

void EpubReaderActivity::clearDeferredReposition() {
  cachedChapterTotalPageCount = 0;
  cachedVisibleTextOffset.reset();
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount, const bool defer) {
  if (!epub) return false;
  std::optional<uint32_t> offset;
  if (section && spineIndex == currentSpineIndex && currentPage >= 0 && currentPage < section->pageCount) {
    offset = (currentPage == section->currentPage && currentPageVisibleOffset.has_value())
                 ? currentPageVisibleOffset
                 : section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage));
  }
  return EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount, offset, &progressCheckpoint, defer);
}

void EpubReaderActivity::rememberCurrentContentOffset() {
  cachedVisibleTextOffset.reset();
  if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
    cachedVisibleTextOffset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage));
  }
}

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  const int fontId = SETTINGS.getReaderFontId();
  ImageBlock::clearRenderFailures();

  struct PxcSlotGuard {
    ~PxcSlotGuard() { ImageBlock::releaseRenderCache(); }
  } pxcSlotGuard;

  const auto drawPublisherMarkers = [&]() {
    if (!SETTINGS.publisherPageNumbers || !page->publisherMarkerCount) return;
    int top, right, bottom, left;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    page->renderPublisherMarkers(renderer, SMALL_FONT_ID, left + 5, orientedMarginTop,
                                 renderer.getScreenHeight() - orientedMarginBottom);
  };
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  drawPublisherMarkers();
  // Scan the status bar too: a CJK book/chapter title redirected to the SD
  // fallback font joins the page's single batch prewarm instead of triggering
  // its own SD pass after the scope ends.
  renderStatusBar();
  scope.endScanAndPrewarm();
  const auto tPrewarm = millis();

  // Black & White images: the page turns as plain B/W, images dither in the
  // B/W pass and text keeps its usual anti-aliasing.
  const bool imageGrayscale = SETTINGS.imageRendering != CrossPointSettings::IMAGES_DISPLAY_BW;
  struct DitherScope {
    GfxRenderer& renderer;
    ~DitherScope() { renderer.setDitherImagesInBw(false); }
  } ditherScope{renderer};
  renderer.setDitherImagesInBw(!imageGrayscale);
  const bool pageHasImages = imageGrayscale && page->hasImages();
  const bool pageHasImagesNeedingDecode = pageHasImages && page->hasImagesNeedingDecode();
  const bool requestedReinforcement = reinforcementRequested;
  reinforcementRequested = false;
  const bool manualRefreshPending = forcedRefreshPending;
  const bool needsTextGrayscale =
      gpio.supportsReaderWakePreview() ? SETTINGS.isTextAntiAliasingEnabled() : SETTINGS.textAntiAliasing != 0;
  paintedTextPage = {};
  if (gpio.supportsReaderWakePreview() && !needsTextGrayscale && !page->hasImages() && overlay == Overlay::None &&
      footnoteDepth == 0) {
    paintedTextPage = {0, page->visibleTextOffset, static_cast<uint16_t>(currentSpineIndex),
                       static_cast<uint16_t>(section->currentPage), appliedOrientation, true,
                       reader_refresh::WakeTransition::None};
  }
  const bool manualReinforcement =
      requestedReinforcement && gpio.deviceIsX3() && !pageHasImages && renderer.grayscaleCapabilities().supported();
  forcedRefreshPending = false;
  wake_refresh_trace::event("reader.paint", pagesUntilFullRefresh, pageHasImages, manualRefreshPending);
  const bool cleanImageBasePending = manualRefreshPending || pagesUntilFullRefresh <= 1;
  const bool needsAnyGrayscale = needsTextGrayscale || (pageHasImages && !BoardConfig::isEegoA4());
  const bool absoluteImageGrayscale = pageHasImages && !gpio.deviceIsX3() &&
                                      display.getController() == HalDisplay::Controller::UC8279 &&
                                      renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  const auto grayscale = renderer.grayscaleCapabilities(absoluteImageGrayscale ? HalDisplay::GrayscaleMode::Absolute
                                                                               : HalDisplay::GrayscaleMode::Overlay);
  const bool tiledGrayscale = needsAnyGrayscale && grayscale.stripUploads;
  // Paper Mono only (no other panel combines): defer the B/W base activation so
  // the gray planes join it in a single waveform. Displaying the base
  // separately makes the gray pass re-drive the whole text body — a visible
  // flash on every AA page.
  const bool combinedGrayscaleBase =
      tiledGrayscale && !pageHasImages && grayscale.base == HalDisplay::GrayscaleBase::Combined;
  const bool overlapRefresh = tiledGrayscale && grayscale.asyncBase && !pageHasImages;
  auto renderGrayscalePass = [&]() {
    // A4 gray planes replace the full page, including its clipping highlights.
    if (BoardConfig::isEegoA4()) drawClippingHighlights(*page, fontId, orientedMarginTop, orientedMarginLeft);
    if (absoluteImageGrayscale || needsTextGrayscale) {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
      drawPublisherMarkers();
    } else {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    }
    // A4 replaces the whole frame with its gray planes, including the status bar.
    if (absoluteImageGrayscale || BoardConfig::isEegoA4()) renderStatusBar();
  };

  if (pageHasImagesNeedingDecode) {
    page->renderWithImagePlaceholders(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    drawPublisherMarkers();
    renderStatusBar();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    renderer.clearScreen();
  }

  drawClippingHighlights(*page, fontId, orientedMarginTop, orientedMarginLeft);
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  drawPublisherMarkers();
  renderStatusBar();
  const auto tBwRender = millis();

  if (std::exchange(proMagicWakePending, false) && !pageHasImages && !manualRefreshPending &&
      ReaderUtils::needsPeriodicReinforcement(renderer, pagesUntilFullRefresh)) {
    // Complete the ordinary differential paint before the first custom waveform.
    // The completed paint commits the displayed page as the panel's old image.
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  if (absoluteImageGrayscale) {
    const auto baseMode = cleanImageBasePending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, baseMode)) {
      LOG_ERR("ERS", "Could not start absolute image page; displaying B/W");
      ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, /*async=*/false, /*textPage=*/false);
      return;
    }
    LOG_DBG("ERS", "UC8279 image page: absolute quality waveform");
    pagesUntilFullRefresh = 0;
  } else if (pageHasImages) {
    // Image pages use one base refresh before the grayscale pass. FAST leaves
    // the panel receptive to the gray waveform; pending cleanup still honors
    // the scheduled/manual HALF refresh.
    renderer.displayBuffer(cleanImageBasePending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    pagesUntilFullRefresh = 0;
  } else if (combinedGrayscaleBase) {
    // Stash the base without activating; displayGrayBuffer() below commits
    // base + grays as one waveform.
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
  } else if (needsAnyGrayscale) {
    if (pagesUntilFullRefresh <= 1) {
      // A cleanup refresh settles X3 correctly only when its grayscale
      // preconditioning waveform runs before the gray planes are written.
      if (manualReinforcement || ReaderUtils::needsPeriodicReinforcement(renderer, pagesUntilFullRefresh)) {
        LOG_DBG("ERS", "Periodic X3 reinforcement");
        ReaderUtils::displayNoFlashReinforcement(renderer, /*grayscaleFollows=*/true,
                                                 /*periodic=*/!manualReinforcement);
      } else {
        LOG_DBG("ERS", "Reader cleanup (remaining=%d)", pagesUntilFullRefresh);
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        renderer.preconditionGrayscale();
      }
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else if (overlapRefresh) {
      ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, /*async=*/true);
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }
  } else {
    if (manualReinforcement) {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, false, true, false,
                                           &reinforcementInterrupt);
    }
  }
  const auto tDisplay = millis();

  if (tiledGrayscale) {
    constexpr int STRIP_ROWS = 80;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();
    const size_t planeBytes = static_cast<size_t>(gwBytes) * gh;

    auto renderPlaneToBuffer = [&](const bool lsbPlane, uint8_t* buf) {
      renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < gh; y += STRIP_ROWS) {
        const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
        renderer.beginStripTarget(buf + static_cast<size_t>(y) * gwBytes, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
      }
    };

    constexpr size_t PLANE_BUF_HEADROOM = 60000;
    constexpr size_t PLANE_BUF_MAX_ALLOC_RESERVE = 16 * 1024;
    const auto planeBufFits = [planeBytes] {
      return ESP.getFreeHeap() >= planeBytes + PLANE_BUF_HEADROOM &&
             ESP.getMaxAllocHeap() >= planeBytes + PLANE_BUF_MAX_ALLOC_RESERVE;
    };
    auto lsbPlaneBuf = (overlapRefresh && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;
    auto msbPlaneBuf = (lsbPlaneBuf && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;

    if (lsbPlaneBuf) {
      renderPlaneToBuffer(true, lsbPlaneBuf.get());
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      const auto tGrayRender = millis();

      renderer.waitRefreshComplete();
      const auto tWait = millis();

      renderer.writeGrayscalePlaneStrip(true, lsbPlaneBuf.get(), 0, gh);
      if (msbPlaneBuf) {
        renderer.writeGrayscalePlaneStrip(false, msbPlaneBuf.get(), 0, gh);
      } else {
        renderPlaneToBuffer(false, lsbPlaneBuf.get());
        renderer.writeGrayscalePlaneStrip(false, lsbPlaneBuf.get(), 0, gh);
      }
      const auto tGrayWrite = millis();

      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();

      renderer.cleanupGrayscaleWithFrameBuffer();
      const auto tEnd = millis();

      LOG_DBG("ERS",
              "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
              "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayRender - tDisplay, tWait - tGrayRender,
              tGrayWrite - tWait, tGrayDisplay - tGrayWrite, tEnd - tGrayDisplay, tEnd - t0, msbPlaneBuf ? 2 : 1);
    } else {
      auto scratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(gwBytes) * STRIP_ROWS);
      renderer.waitRefreshComplete();
      if (!scratch) {
        LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page", gwBytes * STRIP_ROWS);
        if (overlapRefresh || combinedGrayscaleBase) {
          // The BW refresh ran the shadow-free async path, so controller RAM's
          // differential baseline was never rebuilt. Even with AA skipped it must
          // be re-synced from the intact BW framebuffer, or the next differential
          // update diffs against stale contents. On the combined-base path the
          // base activation is still deferred; this cleanup commits it so the
          // page reaches the panel even without its grays.
          renderer.cleanupGrayscaleWithFrameBuffer();
        } else if (gpio.deviceIsX3()) {
          // A reinforcement base stayed powered for AA. Finish the intact BW
          // page even when the strip allocation failed, respecting power saving.
          renderer.displayBuffer(HalDisplay::FAST_REFRESH);
          LOG_DBG("ERS", "X3 skipped grayscale: finished BW refresh");
        }
      } else {
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(true, scratch.get(), y, rows);
        }
        const auto tGrayLsb = millis();

        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(false, scratch.get(), y, rows);
        }
        const auto tGrayMsb = millis();

        renderer.setRenderMode(GfxRenderer::BW);
        renderer.displayGrayBuffer();
        const auto tGrayDisplay = millis();

        renderer.cleanupGrayscaleWithFrameBuffer();
        const auto tCleanup = millis();

        const auto tEnd = millis();
        LOG_DBG("ERS",
                "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
                tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayLsb - tDisplay, tGrayMsb - tGrayLsb,
                tGrayDisplay - tGrayMsb, tCleanup - tGrayDisplay, tEnd - t0);
      }
    }
  } else {
    if (needsAnyGrayscale) {
      if (!renderer.storeBwBuffer()) {
        LOG_ERR("ERS", "Failed to store BW buffer for grayscale render; skipping grayscale this page");
        if (gpio.deviceIsX3()) renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        if (absoluteImageGrayscale) renderer.setRenderMode(GfxRenderer::BW);
        return;
      }
      const auto tBwStore = millis();

      renderer.clearScreen(absoluteImageGrayscale ? 0xFF : 0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderGrayscalePass();
      renderer.copyGrayscaleLsbBuffers();
      const auto tGrayLsb = millis();

      renderer.clearScreen(absoluteImageGrayscale ? 0xFF : 0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderGrayscalePass();
      renderer.copyGrayscaleMsbBuffers();
      const auto tGrayMsb = millis();

      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.restoreBwBuffer();
      const auto tBwRestore = millis();

      const auto tEnd = millis();
      LOG_DBG("ERS",
              "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
              "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
              tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
    } else {
      const auto tEnd = millis();
      LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
              tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
    }
  }
}

int EpubReaderActivity::clippingAtPoint(const Page& page, const int x, const int y) const {
  const int fontId = SETTINGS.getReaderFontId();
  const uint32_t signature =
      readerRenderSpecSignature(SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight));
  uint16_t wordIndex = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block || !block->valid()) continue;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const char* text = block->wordText(i);
      if (!clippingText::hasVisibleText(text)) continue;
      const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
      const int width = renderer.getTextAdvanceX(clippingWordFont(renderer, fontId, *block, i), text, style,
                                                 block->getBlockStyle().characterSpacing);
      if (width <= 0) continue;
      const uint16_t index = wordIndex++;
      const Rect rect =
          clippingWordRect(renderer, fontId, line, i, currentPageMarginTop, currentPageMarginLeft, width);
      if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) continue;
      const auto range = block->wordSourceRange(i);
      for (size_t j = 0; j < CLIPPINGS.clippingCount(); ++j) {
        if (clippingContainsWord(*CLIPPINGS.clippingAt(j), currentSpineIndex, section->currentPage, section->pageCount,
                                 signature, index, range.start, range.end))
          return static_cast<int>(j);
      }
    }
  }
  return -1;
}

void EpubReaderActivity::drawClippingHighlights(const Page& page, const int fontId, const int orientedMarginTop,
                                                const int orientedMarginLeft) const {
  if (!section || !CLIPPINGS.hasClippings() || section->currentPage < 0 || section->currentPage >= section->pageCount) {
    return;
  }

  size_t chapterClippings = 0;
  for (size_t i = 0; i < CLIPPINGS.clippingCount(); ++i) {
    if (CLIPPINGS.clippingAt(i)->spineIndex == currentSpineIndex) ++chapterClippings;
  }
  if (!chapterClippings) return;

  const auto started = millis();
  size_t highlightedWords = 0;
  const uint16_t currentPage = static_cast<uint16_t>(section->currentPage);
  const uint32_t layoutSignature =
      readerRenderSpecSignature(SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight));
  const auto isHighlighted = [&](const uint16_t wordIndex, const TextBlock::SourceRange range) {
    // ponytail: scan at most 256 C3 / 1024 S3 clippings per word; index by chapter if this becomes slow.
    for (size_t index = 0; index < CLIPPINGS.clippingCount(); ++index) {
      const Clipping& clipping = *CLIPPINGS.clippingAt(index);
      if (clippingContainsWord(clipping, static_cast<uint16_t>(currentSpineIndex), currentPage, section->pageCount,
                               layoutSignature, wordIndex, range.start, range.end))
        return true;
    }
    return false;
  };

  const auto fillHighlight = [&](const int x, const int y, const int width, const int height) {
    if (BoardConfig::isEegoA4() && renderer.getRenderMode() != GfxRenderer::BW) {
      // Gray planes start at zero and use set bits for ink, opposite to B/W.
      for (int py = y + (y & 1); py < y + height; py += 2) {
        for (int px = x + (x & 1); px < x + width; px += 2) renderer.drawPixel(px, py, false);
      }
    } else {
      renderer.fillRectDither(x, y, width, height, Color::LightGray);
    }
  };

  uint16_t pageWordIndex = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block || !block->valid()) continue;
    bool hasPreviousHighlight = false;
    int previousHighlightX = 0;
    int previousHighlightWidth = 0;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const char* text = block->wordText(i);
      if (!clippingText::hasVisibleText(text)) continue;
      const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
      int width = renderer.getTextAdvanceX(clippingWordFont(renderer, fontId, *block, i), text, style,
                                           block->getBlockStyle().characterSpacing);
      if (width <= 0) continue;
      const uint16_t wordIndex = pageWordIndex++;
      if (!isHighlighted(wordIndex, block->wordSourceRange(i))) {
        hasPreviousHighlight = false;
        continue;
      }

      const Rect rect = clippingWordRect(renderer, fontId, line, i, orientedMarginTop, orientedMarginLeft, width);
      const int x = rect.x;
      const int y = rect.y;
      width = rect.width;
      if (width > 0) {
        const int highlightRight = x + width;
        const int previousHighlightRight = previousHighlightX + previousHighlightWidth;
        if (hasPreviousHighlight && previousHighlightRight < x) {
          fillHighlight(previousHighlightRight, y, x - previousHighlightRight, renderer.getLineHeight(fontId));
        } else if (hasPreviousHighlight && highlightRight < previousHighlightX) {
          fillHighlight(highlightRight, y, previousHighlightX - highlightRight, renderer.getLineHeight(fontId));
        }
        fillHighlight(x, y, width, renderer.getLineHeight(fontId));
        previousHighlightX = x;
        previousHighlightWidth = width;
        hasPreviousHighlight = true;
        ++highlightedWords;
      } else {
        hasPreviousHighlight = false;
      }
    }
  }
  LOG_DBG("CLIP", "Highlight pass: chapter_clips=%u highlighted_words=%u time=%lums",
          static_cast<unsigned>(chapterClippings), static_cast<unsigned>(highlightedWords), millis() - started);
}

void EpubReaderActivity::renderStatusBar() const {
  const int currentPage = section ? section->currentPage + 1 : 1;
  const float pageCount = section ? section->estimatedTotalPages() : 1;
  const float sectionChapterProg = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) : 0;
  const float bookProgress = epub ? (epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100) : 0;

  std::string title;
  int textYOffset = 0;
  const auto sb = SETTINGS.statusBarSpec();

  const auto note = linkNoteTitle.draw(sb.showsTitle(), bleturner::linkNote());
  if (note == bleturner::LinkNote::Connected) {
    const auto peer = bleturner::linked();
    char connected[64];
    snprintf(connected, sizeof(connected), tr(STR_BT_CONNECTED_TO), peer.name[0] != '\0' ? peer.name : peer.addr);
    title = connected;
  } else if (note == bleturner::LinkNote::LowMemory) {
    title = tr(STR_BT_LOW_MEMORY);
  } else if (note != bleturner::LinkNote::None) {
    title = note == bleturner::LinkNote::Connecting ? tr(STR_BT_CONNECTING) : tr(STR_BT_FAILED);
  } else if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    if (epub) {
      const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
      if (tocIndex != -1) {
        const auto tocItem = epub->getTocItem(tocIndex);
        title = tocItem.title;
      }
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub ? epub->getTitle() : "";
  }

  char timeLeft[48]{};
  if (sb.timeLeftMode != CrossPointSettings::TIME_LEFT_HIDE) {
    const uint32_t seconds = timeLeftSeconds(sb.timeLeftMode == CrossPointSettings::TIME_LEFT_BOOK);
    if (seconds)
      BookReadingStats::formatDuration(seconds, timeLeft, sizeof(timeLeft));
    else if (SETTINGS.trackReadingStats && !readingPaceSeconds.load(std::memory_order_relaxed)) {
      snprintf(timeLeft, sizeof(timeLeft), "%s", tr(STR_TIME_LEFT_CALCULATING));
    }
  }
  uint32_t referencePage = 0, referenceCount = 0;
  if (SETTINGS.stablePageNumbers && section && referenceSpine == currentSpineIndex && pageCount > 0) {
    referencePages.resolve(static_cast<float>(section->currentPage) / pageCount, referencePage, referenceCount);
  }
  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, true, currentPageBookmarked,
                    section ? section->isBuilding() : false, timeLeft, -1, referencePage, referenceCount);
  GUI.drawReaderTopClock(renderer);
}

bool EpubReaderActivity::renderFrontlightIndicator(RenderLock&&) {
  // A toolbar or sheet owns the lower edge while open. Let its normal render
  // path redraw the complete overlay rather than painting the indicator over it.
  if (overlay != Overlay::None || !epub || !section || isAtEndOfBook()) return false;
  renderStatusBar();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

// ---------------------------------------------------------------------------
// Toolbar reader menu
// ---------------------------------------------------------------------------

namespace {
constexpr StrId kTextRowNames[] = {StrId::STR_FONT, StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING,
                                   StrId::STR_PARA_ALIGNMENT, StrId::STR_FOCUS_READING};
constexpr StrId kAlignIds[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                               StrId::STR_BOOK_S_STYLE};
constexpr int kTextRowCount = static_cast<int>(std::size(kTextRowNames));
static_assert(std::size(kAlignIds) == CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, "alignment labels");
}  // namespace

bool EpubReaderActivity::usesToolbarMenu() const {
  // Both board classes drive the same chrome: touch through the FreeInkUI tap
  // targets, buttons through the focused-tool pill and the panel cursor.
  return SETTINGS.readerMenuStyle == CrossPointSettings::READER_MENU_TOOLBAR;
}

std::string EpubReaderActivity::currentChapterTitle() const {
  if (!epub) return "";
  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return tr(STR_UNNAMED);
}

std::string EpubReaderActivity::textRowName(int row) const {
  return row >= 0 && row < kTextRowCount ? I18N.get(kTextRowNames[row]) : "";
}

std::string EpubReaderActivity::textRowValue(int row) const {
  static constexpr StrId kFamily[] = {StrId::STR_LEXEND_DECA, StrId::STR_ATKINSON_HYPERLEGIBLE_NEXT};
  static_assert(std::size(kFamily) == CrossPointSettings::FONT_FAMILY_COUNT);
  switch (row) {
    case 0:
      if (SETTINGS.sdFontFamilyName[0] != '\0') return SETTINGS.sdFontFamilyName;
      return I18N.get(kFamily[SETTINGS.fontFamily % CrossPointSettings::FONT_FAMILY_COUNT]);
    case 1:
      return std::to_string(SETTINGS.fontPointSize) + " pt";
    case 2:
      if (SETTINGS.lineHeightPercent) return std::to_string(SETTINGS.lineHeightPercent) + "%";
      return I18N.get(LINE_SPACING_IDS[SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT]);
    case 3:
      return I18N.get(kAlignIds[SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT]);
    case 4:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

// Live apply: persist, re-paginate, and let renderBook() redraw the page with
// the open panel back on top -- the book itself is the preview.
void EpubReaderActivity::applyTextSettingLive() {
  applyReaderTextSettings();
  discardOverlayPage();  // the stored page is laid out with the old settings
  requestUpdate();
}

// Settings-style option pickers for the Text panel's enum rows. Every
// selection applies immediately to the page under the sheet.
void EpubReaderActivity::showTextRowPopup(const int row) {
  switch (row) {
    case 1: {
      // The point sizes the active family actually ships.
      const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
      if (sizes.empty()) return;
      std::vector<std::string> labels;
      labels.reserve(sizes.size());
      for (const uint8_t size : sizes) labels.push_back(std::to_string(size) + " pt");
      const uint8_t cur = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
      int curIdx = 0;
      for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i] == cur) curIdx = static_cast<int>(i);
      }
      overlayPopup.show(StrId::STR_FONT_SIZE, labels, curIdx, [this, sizes](int idx) {
        if (idx < 0 || idx >= static_cast<int>(sizes.size())) return;
        SETTINGS.fontPointSize = sizes[idx];
        applyTextSettingLive();
      });
      break;
    }
    case 2: {
      const int selected = SETTINGS.lineHeightPercent ? CUSTOM_LINE_SPACING_INDEX : SETTINGS.lineSpacing;
      overlayPopup.show(StrId::STR_LINE_SPACING, LINE_SPACING_PICKER_IDS.data(), LINE_SPACING_PICKER_IDS.size(),
                        selected, [this](int idx) {
                          if (idx == CUSTOM_LINE_SPACING_INDEX) {
                            lineHeightPickerRequested = true;
                            return;
                          }
                          SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                          SETTINGS.lineHeightPercent = 0;
                          applyTextSettingLive();
                        });
      break;
    }
    case 3:
      overlayPopup.show(StrId::STR_PARA_ALIGNMENT, kAlignIds, static_cast<int>(std::size(kAlignIds)),
                        SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, [this](int idx) {
                          SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    default:
      return;
  }
  paintOverlayPopup();
}

void EpubReaderActivity::discardOverlayPage() {
  if (!overlayPageStored) return;
  renderer.discardStoredBwBuffer();
  overlayPageStored = false;
}

void EpubReaderActivity::openAutoTurnIntervalPicker() {
  // The slider is allocated only while the user adjusts this book's cadence.
  auto picker = makeUniqueNoThrow<IntervalSelectionActivity>(
      renderer, mappedInput, "AutoTurnInterval", StrId::STR_AUTO_TURN_INTERVAL_SECONDS, SETTINGS.autoPageTurnSeconds, 5,
      120, 1, 5, StrId::STR_SECONDS_VALUE_FORMAT, true);
  if (!picker) {
    LOG_ERR("EREAD", "OOM: automatic-turn interval picker");
    requestUpdate();
    return;
  }
  const auto returnOverlay = overlay;
  overlayPopup.dismiss();
  {
    RenderLock lock;
    settleOverlayRefresh();
    discardOverlayPage();
  }
  overlay = Overlay::None;
  requestUpdateAndWait();
  startActivityForResult(std::move(picker), [this, returnOverlay](const ActivityResult& result) {
    if (!result.isCancelled) {
      SETTINGS.setAutoPageTurnSeconds(static_cast<uint8_t>(std::get<IntervalResult>(result.data).value));
      SETTINGS.saveToFile();
      toggleAutoPageTurn(CUSTOM_AUTO_TURN);
    } else {
      overlay = returnOverlay;
      if (toolbarUi) toolbarUi->begin();
    }
    requestUpdate();
  });
}

void EpubReaderActivity::openLineHeightPicker() {
  // The activity owns its slider state until return; no allocation on page turns.
  auto picker = makeUniqueNoThrow<IntervalSelectionActivity>(
      renderer, mappedInput, "LineHeight", StrId::STR_LINE_SPACING,
      static_cast<int>(SETTINGS.getReaderLineCompression() * 100.0f + 0.5f), CrossPointSettings::LINE_HEIGHT_MIN,
      CrossPointSettings::LINE_HEIGHT_MAX, 1, 10, StrId::STR_PERCENT_VALUE_FORMAT, true);
  if (!picker) {
    LOG_ERR("EREAD", "OOM: line height picker");
    requestUpdate();
    return;
  }
  overlayPopup.dismiss();
  bool needsPage = false;
  {
    RenderLock lock;
    settleOverlayRefresh();
    if (overlayPageStored) {
      renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      overlayPageStored = false;
      renderOverlay();
    } else {
      needsPage = true;
    }
  }
  if (needsPage) requestUpdateAndWait();
  overlay = Overlay::None;
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      SETTINGS.lineHeightPercent = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
      applyReaderTextSettings();
    }
    overlay = Overlay::Text;
    panelIndex = 2;
    if (toolbarUi) toolbarUi->begin();
    requestUpdate();
  });
}

// Push freshly painted overlay chrome. Where the panel supports it the refresh
// is fired deferred: the loop keeps polling input while the waveform runs, so
// the chrome answers taps and buttons the moment it is visible instead of only
// after a blocking displayBuffer() returns. Caller must hold the RenderLock.
void EpubReaderActivity::pushOverlayRefresh() {
  if (renderer.supportsAsyncRefresh()) {
    renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
    overlayRefreshPending = true;
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

// Wait out a pending deferred overlay refresh and reseed the panel's
// differential baseline from the framebuffer: the shadow-free async path skips
// the post-refresh resync, so without this the next FAST diff would run
// against the frame from before the chrome and leave stale pixels on the
// glass. Caller must hold the RenderLock.
void EpubReaderActivity::settleOverlayRefresh() {
  if (!overlayRefreshPending) return;
  overlayRefreshPending = false;
  renderer.cleanupGrayscaleWithFrameBuffer();  // waits, then reseeds the baseline
}

void EpubReaderActivity::openOverlay(Overlay target) {
  pauseReadingSession();
  mappedInput.resetHomeButtonInput();
  const Overlay previous = overlay;
  overlay = target;
  if (!toolbarUi) toolbarUi = std::make_unique<ReaderToolbarUi>(renderer);
  if (previous == Overlay::None) toolbarUi->begin();
  // Buttons show a cursor from the start; touch boards only once a button moves it.
  panelCursorShown = !mappedInput.hasTouch();
  switch (target) {
    case Overlay::Toolbar:
      focusedTool = 0;
      break;
    case Overlay::Contents: {
      RenderLock lock;
      panelIndex = currentTocIndex();
      // Fresh viewport opening on the current chapter, cursor shown or not.
      toolbarUi->nav().reset(panelIndex);
      toolbarUi->nav().top = panelIndex;
      break;
    }
    case Overlay::Text:
      panelIndex = 0;
      toolbarUi->nav().reset();
      break;
    case Overlay::More:
      panelIndex = 0;
      buildMoreActions();
      toolbarUi->nav().reset();
      break;
    default:
      break;
  }
  panelHoldJumped = false;

  // The page is already on screen and still in the framebuffer, so paint the
  // chrome straight onto it and push one refresh. requestUpdate() would
  // re-render the whole page first: slow, and visibly wrong, since that repaint
  // lands before the overlay does.
  //
  // Refresh mode: FAST for every overlay paint, first open included. The AA
  // pass only grays glyph edges, and residue a FAST differential leaves under
  // the sheet has not shown in practice; it also self-heals on the
  // Xteink-class panels, whose close path re-renders the page. If text or
  // images ever visibly ghost through the chrome, restore a HALF cleanup on
  // the first open (see #2190 for the mechanism).
  if (section && !pageBufferStale) {
    // Serialize against the render task: renderBook may be mid-page (status
    // bar included) in the shared framebuffer, and painting the chrome from
    // the loop task at the same time interleaves the two frames.
    RenderLock lock;
    settleOverlayRefresh();
    if (previous == Overlay::None) {
      // Snapshot the clean page so stepping back from a panel to the toolbar
      // (and closing, where supported) can restore it without a re-render.
      overlayPageStored = renderer.storeBwBuffer();
    } else if (overlayPageStored) {
      // Overlay -> overlay: wipe the previous chrome (toolbar header, sheet,
      // progress row) back to the clean page so none of it shows around or
      // through the new sheet; re-store for the next transition. No baseline
      // resync: the glass still shows the old chrome, and the differential
      // must keep diffing against it to erase it.
      renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      overlayPageStored = renderer.storeBwBuffer();
    }
    renderOverlay();
    pushOverlayRefresh();
  } else {
    requestUpdate();  // no page in the framebuffer: renderBook() draws the overlay once it is
  }
}

// Grayscale panels re-render to restore AA; other boards restore the B/W snapshot.
void EpubReaderActivity::closeOverlayToPage() {
  mappedInput.resetHomeButtonInput();
  overlay = Overlay::None;
  overlayPopup.dismiss();  // an option picker cannot outlive its panel
  toolbarUi.reset();       // ~1 KB of interaction table + props, only needed while open
  if (BoardConfig::isEegoA4()) pagesUntilFullRefresh = 1;
  if (!xteinkClassPanel() && overlayPageStored) {
    RenderLock lock;  // the render task shares the framebuffer
    settleOverlayRefresh();
    // No baseline resync: the glass shows the chrome, and erasing it needs
    // the differential to keep diffing against the last pushed frame.
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = false;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    if (readingSession && SETTINGS.trackReadingStats && footnoteDepth == 0) readingSession->pageShown(millis());
    return;
  }
  discardOverlayPage();
  requestUpdate();  // redraw the clean page
}

void EpubReaderActivity::renderOverlay() {
  if (!epub || !section || !toolbarUi) return;

  ReaderToolbarUi::Model model;
  // The toolbar's tool pill is the button-navigation cursor: tap-first (same
  // convention as the panel lists), it only shows once a button has moved it.
  // Panels override below: there the pill marks the open panel on every board.
  model.activeTool = (overlay == Overlay::Toolbar && !panelCursorShown) ? -1 : focusedTool;
  // Strings the model points at live here until render() returns.
  std::string chapterTitle, pageInfo;

  if (overlay == Overlay::Toolbar) {
    chapterTitle = currentChapterTitle();
    const int pageCount = section->estimatedTotalPages();
    const float chapterProgress =
        pageCount > 0 ? static_cast<float>(section->currentPage + 1) / static_cast<float>(pageCount) : 0.0f;
    const float bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress);
    pageInfo = std::to_string(section->currentPage + 1) + "/" + std::to_string(pageCount) + "   " +
               std::to_string(clampPercent(static_cast<int>(bookProgress * 100.0f + 0.5f))) + "%";
    model.chapterTitle = chapterTitle.c_str();
    model.pageInfo = pageInfo.c_str();
    model.progressPermille = static_cast<int>(bookProgress * 1000.0f + 0.5f);
    toolbarUi->setModel(model);
    toolbarUi->render();
    return;
  }

  // Panels (Contents / Text / More): a bottom sheet over the page.
  model.panel = true;
  if (!mappedInput.hasTouch()) {
    model.denseRows = true;
  }
  // Tap-first: the cursor is only drawn once a button has moved it, so a
  // tapped row does not stay inverted after its action.
  model.selectedIndex = panelCursorShown ? panelIndex : -1;
  if (overlay == Overlay::Contents) {
    model.panelTitle = tr(STR_TOOL_CONTENTS);
    model.itemCount = epub->getTocItemsCount();
    model.rowText = [this](int i) {
      const auto item = epub->getTocItem(i);
      const int depth = item.level > 1 ? (item.level - 1) * 2 : 0;
      return std::string(depth, ' ') + item.title;
    };
  } else if (overlay == Overlay::Text) {
    model.panelTitle = tr(STR_TOOL_TEXT);
    model.itemCount = kTextRowCount;
    model.rowText = [this](int i) { return textRowName(i); };
    model.rowValue = [this](int i) { return textRowValue(i); };
    model.rowCheckbox = [](void*, int i, freeink::ui::ListItem& item) {
      if (i == 4) GUI.setCheckboxRow(item, SETTINGS.focusReadingEnabled);
    };
  } else {
    model.panelTitle = tr(STR_TOOL_MORE);
    model.itemCount = static_cast<int>(moreItems.size());
    model.rowText = [this](int i) { return moreRowName(i); };
    model.rowValue = [this](int i) { return moreRowValue(i); };
    model.rowCheckboxContext = this;
    model.rowCheckbox = [](void* ctx, int i, freeink::ui::ListItem& item) {
      const auto* self = static_cast<EpubReaderActivity*>(ctx);
      using MA = EpubReaderMenuActivity::MenuAction;
      const auto action = self->moreItems[i].action;
      if (action == MA::NIGHT_MODE) GUI.setCheckboxRow(item, SETTINGS.screenInverted);
      if (action == MA::BLUETOOTH) GUI.setCheckboxRow(item, SETTINGS.ble.enabled);
      if (action == MA::FRONTLIGHT) GUI.setCheckboxRow(item, Frontlight.isOn());
    };
  }
  toolbarUi->setModel(model);
  toolbarUi->render();
}

void EpubReaderActivity::handleOverlayInput() {
  if (!toolbarUi) return;

  // A modal option picker over the panel owns all input while open.
  if (overlayPopup.isActive()) {
    overlayPopup.handleInput(mappedInput, [this] {
      if (lineHeightPickerRequested || autoTurnIntervalRequested) return;
      if (overlayPopup.isActive()) {
        paintOverlayPopup();  // highlight moved
        return;
      }
      // Dismissed or selected: erase the dialog -- clean page back, then the
      // panel over it (the dialog can overhang the sheet onto the page).
      RenderLock lock;
      settleOverlayRefresh();
      if (overlayPageStored) {
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
        renderOverlay();
        pushOverlayRefresh();
      } else {
        requestUpdate();
      }
    });
    if (lineHeightPickerRequested) {
      lineHeightPickerRequested = false;
      openLineHeightPicker();
    }
    if (autoTurnIntervalRequested) {
      autoTurnIntervalRequested = false;
      openAutoTurnIntervalPicker();
    }
    return;
  }
  const auto fastRedraw = [this] {
    RenderLock lock;  // the render task shares the framebuffer
    settleOverlayRefresh();
    renderOverlay();
    pushOverlayRefresh();
  };

  // Jump to another spine item (chapter scrub). The overlay stays up and is
  // re-drawn over the new page by renderBook().
  const auto gotoSpine = [this](int target) {
    const int spineCount = epub->getSpineItemsCount();
    target = std::clamp(target, 0, spineCount - 1);
    if (target != currentSpineIndex) {
      RenderLock lock;
      clearDeferredReposition();
      nextPageNumber = 0;
      currentSpineIndex = target;
      section.reset();
    }
    requestUpdate();
  };
  const auto toolOverlay = [](int tool) {
    return tool == 0 ? Overlay::Contents : (tool == 1 ? Overlay::Text : Overlay::More);
  };

  // Touch first: FreeInkUI routes the frame against the tap targets the last
  // render registered and hands back the action it mapped to.
  const auto routed = toolbarUi->route(mappedInput);

  // --- Toolbar ---
  if (overlay == Overlay::Toolbar) {
    switch (routed.event) {
      case ReaderToolbarUi::Event::Dismiss:
        closeOverlayToPage();
        return;
      case ReaderToolbarUi::Event::Tool:
        focusedTool = routed.value;
        openOverlay(toolOverlay(focusedTool));
        return;
      case ReaderToolbarUi::Event::PrevChapter:
        gotoSpine(currentSpineIndex - 1);
        return;
      case ReaderToolbarUi::Event::NextChapter:
        gotoSpine(currentSpineIndex + 1);
        return;
      case ReaderToolbarUi::Event::Scrub:
        gotoSpine(static_cast<int>((static_cast<float>(routed.permille) / 1000.0f) *
                                       static_cast<float>(epub->getSpineItemsCount() - 1) +
                                   0.5f));
        return;
      default:
        break;
    }
    if (routed.routed) return;  // a touch frame the chrome consumed (or dead space)

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeOverlayToPage();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      focusedTool = (focusedTool + 2) % 3;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      focusedTool = (focusedTool + 1) % 3;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openOverlay(toolOverlay(focusedTool));
      return;
    }
    const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
    const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (prev || next) {
      gotoSpine(currentSpineIndex + (next ? 1 : -1));
    }
    return;
  }

  // --- Panels (Contents / Text / More) ---
  const int count = overlay == Overlay::Contents ? epub->getTocItemsCount()
                    : overlay == Overlay::Text   ? kTextRowCount
                                                 : static_cast<int>(moreItems.size());
  const int pageRows = std::max(1, toolbarUi->visibleRows());

  // Activate the highlighted row: change a value / jump to a chapter / run an
  // action. Shared by the Confirm button and a row tap.
  const auto activateRow = [this, count, &fastRedraw] {
    if (panelIndex < 0 || panelIndex >= count) return;
    if (overlay == Overlay::Text) {
      if (panelIndex == 0) {
        // Full font picker (built-in + SD fonts, live preview) -- the same
        // screen Settings uses; a popup cannot scroll a long font list.
        overlay = Overlay::None;
        overlayPopup.dismiss();
        discardOverlayPage();
        {
          RenderLock lock;  // the picker paints the framebuffer next
          settleOverlayRefresh();
        }
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 applyReaderTextSettings();
                                 overlay = Overlay::Text;  // back to the Text panel
                                 panelIndex = 0;
                                 if (toolbarUi) toolbarUi->begin();  // the picker drew its own FUI screen
                                 requestUpdate();                    // re-render page + Text panel
                               });
      } else if (panelIndex == 4) {
        // Focus Reading is a genuine on/off: a tap toggles and applies live.
        SETTINGS.focusReadingEnabled = SETTINGS.focusReadingEnabled ? 0 : 1;
        applyTextSettingLive();
      } else {
        // Enum rows open the Settings-style option picker.
        showTextRowPopup(panelIndex);
      }
    } else if (overlay == Overlay::Contents) {
      const auto item = epub->getTocItem(panelIndex);
      if (item.spineIndex != -1) {
        RenderLock lock;
        clearDeferredReposition();
        currentSpineIndex = item.spineIndex;
        pendingAnchor = item.anchor;
        nextPageNumber = 0;
        section.reset();
      }
      overlay = Overlay::None;
      discardOverlayPage();
      requestUpdate();
    } else if (overlay == Overlay::More) {
      activateMoreRow(panelIndex);
    }
  };

  // Steps up to the toolbar -- the Back button and a tap on the page above
  // the sheet.
  const auto dismissPanel = [this, &fastRedraw] {
    overlay = Overlay::Toolbar;
    // Restore the snapshotted page under the toolbar instead of re-rendering
    // it (2+ refreshes -> one FAST). Re-store right away so another panel
    // round-trip can restore again.
    if (overlayPageStored) {
      {
        RenderLock lock;  // the render task shares the framebuffer
        settleOverlayRefresh();
        // No baseline resync: the glass shows the panel, and erasing it needs
        // the differential to keep diffing against the last pushed frame.
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
      }
      fastRedraw();  // takes its own RenderLock
      return;
    }
    requestUpdate();
  };

  // Pages the list by one screen of rows through the nav (measured page size,
  // no-op at the ends). A shown cursor rides along so the buttons continue
  // from what is visible; on touch boards only the viewport moves.
  const auto pageList = [this, count, pageRows, &fastRedraw](int direction) {
    if (count <= 0) return;
    const bool moved = toolbarUi->nav().scrollBy(direction * pageRows, count);
    if (panelCursorShown) {
      panelIndex = std::clamp(panelIndex + direction * pageRows, 0, count - 1);
      fastRedraw();
      return;
    }
    if (moved) fastRedraw();
  };

  switch (routed.event) {
    case ReaderToolbarUi::Event::Dismiss:
      dismissPanel();
      return;
    case ReaderToolbarUi::Event::Tool: {
      // Sheet-bottom tool switcher: hop straight to another panel.
      const Overlay target = toolOverlay(routed.value);
      if (target != overlay) {
        focusedTool = routed.value;
        openOverlay(target);
      }
      return;
    }
    case ReaderToolbarUi::Event::Row:
      // A tap on the right-edge strip pages the sheet instead (upper half =
      // previous page, lower half = next): swipes are unreliable on etched
      // glass, and a long contents list needs a fast way through.
      if (routed.x >= renderer.getScreenWidth() - 44) {
        pageList(routed.y >= renderer.getScreenHeight() - (renderer.getScreenHeight() * 62) / 200 ? 1 : -1);
        return;
      }
      panelIndex = routed.value;
      panelCursorShown = false;
      activateRow();
      return;
    default:
      break;
  }
  // Swipe up/down pages the list. Checked before the routed-frame return:
  // FUI routes every touch frame over the sheet, so a swipe's frames count as
  // routed (without dispatching -- too much travel for a tap) and the gesture
  // would otherwise never be seen.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    pageList(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return;
  }
  if (routed.routed) return;  // consumed by the chrome (title band, dead space)

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    dismissPanel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow();
    return;
  }

  // Up/Down (side) and Left/Right (front) move the cursor: a tap steps one
  // row, holding past PANEL_HOLD_MS jumps PANEL_HOLD_STEP rows in one go, which
  // is how you cross a hundreds-of-chapters contents list without a press per
  // row. The jump fires once on the hold and swallows the release that ends it,
  // so it never doubles up with the tap step.
  if (count > 0) {
    const bool up = mappedInput.isPressed(MappedInputManager::Button::Up) ||
                    mappedInput.isPressed(MappedInputManager::Button::Left);
    const bool down = mappedInput.isPressed(MappedInputManager::Button::Down) ||
                      mappedInput.isPressed(MappedInputManager::Button::Right);
    if (!panelHoldJumped && (up || down) && mappedInput.getHeldTime() >= PANEL_HOLD_MS) {
      const int step = down ? PANEL_HOLD_STEP : -PANEL_HOLD_STEP;
      panelIndex = std::clamp(panelIndex + step, 0, count - 1);
      panelHoldJumped = true;
      panelCursorShown = true;
      fastRedraw();
      return;
    }

    const bool releasedUp = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool releasedDown = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
    if (releasedUp || releasedDown) {
      if (!panelHoldJumped) {
        panelIndex = releasedUp ? ButtonNavigator::previousIndex(panelIndex, count)
                                : ButtonNavigator::nextIndex(panelIndex, count);
        panelCursorShown = true;
        fastRedraw();
      }
      panelHoldJumped = false;
    }
  }
}

// First paint of the option picker over the panel (and highlight repaints).
// The dialog draws over the current framebuffer without clearing; erasing it
// on dismissal is the popup gate's restore in handleOverlayInput().
void EpubReaderActivity::paintOverlayPopup() {
  RenderLock lock;
  settleOverlayRefresh();
  overlayPopup.render(renderer);
  pushOverlayRefresh();
}

void EpubReaderActivity::stepReaderFontSize(const bool larger) {
  if (sdFontSystem.changeReaderFontSize(larger)) applyTextSettingLive();
}

void EpubReaderActivity::applyReaderTextSettings() {
  {
    RenderLock lock;
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    // Release the old parser and page table before settings serialization needs heap.
    section.reset();  // force re-pagination with the new settings
  }
  SETTINGS.saveToFile();
  // (Re)load or unload the selected SD-card font for the current family/size.
  // The reader otherwise only loads SD fonts on book open, so without this an
  // in-reader font change wouldn't take effect until re-opening the book.
  // Locked because the render task can load or unload SD font variants too.
  RenderLock lock;
  sdFontSystem.ensureLoaded(renderer);
}

// The More panel carries everything the classic list menu offers except the
// two entries that have their own tool (chapters -> Contents, text -> Text).
void EpubReaderActivity::buildMoreActions() {
  loadRetainedPageMetadata();
  using MA = EpubReaderMenuActivity::MenuAction;
  EpubReaderMenuActivity::buildMenuItems(moreItems, !currentPageFootnotes.empty(), !cachedBookmarks.empty(),
                                         CLIPPINGS.hasClippings());
  moreItems.erase(std::remove_if(moreItems.begin(), moreItems.end(),
                                 [](const auto& item) {
                                   return item.action == MA::SELECT_CHAPTER || item.action == MA::TEXT_SETTINGS;
                                 }),
                  moreItems.end());
}

std::string EpubReaderActivity::moreRowName(int row) const {
  return row >= 0 && row < static_cast<int>(moreItems.size()) ? I18N.get(moreItems[row].labelId) : "";
}

std::string EpubReaderActivity::moreRowValue(int row) const {
  using MA = EpubReaderMenuActivity::MenuAction;
  static constexpr StrId kOrient[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED,
                                      StrId::STR_LANDSCAPE_CCW};
  static_assert(std::size(kOrient) == CrossPointSettings::ORIENTATION_COUNT, "orientation labels");
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return "";
  switch (moreItems[row].action) {
    case MA::ROTATE_SCREEN:
      return I18N.get(kOrient[SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT]);
    case MA::AUTO_PAGE_TURN:
      if (automaticPageTurnActive && autoTurnOption == CUSTOM_AUTO_TURN) {
        char seconds[24];
        snprintf(seconds, sizeof(seconds), tr(STR_SECONDS_VALUE_FORMAT), SETTINGS.autoPageTurnSeconds);
        return seconds;
      }
      return (autoTurnOption == 0 || autoTurnOption >= static_cast<int>(std::size(PAGE_TURN_RATES)))
                 ? std::string(tr(STR_STATE_OFF))
                 : std::to_string(PAGE_TURN_RATES[autoTurnOption]);
    default:
      return "";
  }
}

void EpubReaderActivity::activateMoreRow(int row) {
  using MA = EpubReaderMenuActivity::MenuAction;
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return;
  {
    RenderLock lock;  // several actions launch screens that paint the framebuffer
    settleOverlayRefresh();
  }
  const auto action = moreItems[row].action;
  // In-place toggles keep the panel open and re-render the page beneath it.
  switch (action) {
    case MA::ROTATE_SCREEN: {
      static constexpr StrId kOrientIds[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                             StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
      static_assert(std::size(kOrientIds) == CrossPointSettings::ORIENTATION_COUNT, "orientation options");
      overlayPopup.show(StrId::STR_ORIENTATION, kOrientIds, static_cast<int>(std::size(kOrientIds)),
                        SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT, [this](int idx) {
                          if (idx == SETTINGS.orientation) return;
                          applyOrientation(static_cast<uint8_t>(idx));
                          // The stored page is laid out for the old orientation.
                          discardOverlayPage();
                          requestUpdate();
                        });
      paintOverlayPopup();
      return;
    }
    case MA::AUTO_PAGE_TURN: {
      std::vector<std::string> labels;
      labels.reserve(std::size(PAGE_TURN_RATES) + 1);
      labels.emplace_back(tr(STR_STATE_OFF));
      for (size_t i = 1; i < std::size(PAGE_TURN_RATES); ++i) labels.push_back(std::to_string(PAGE_TURN_RATES[i]));
      char seconds[24], custom[64];
      snprintf(seconds, sizeof(seconds), tr(STR_SECONDS_VALUE_FORMAT), SETTINGS.autoPageTurnSeconds);
      snprintf(custom, sizeof(custom), "%s (%s)", tr(STR_CUSTOM), seconds);
      labels.emplace_back(custom);
      overlayPopup.show(StrId::STR_AUTO_TURN_PAGES_PER_MIN, labels, autoTurnOption, [this](int idx) {
        if (idx == CUSTOM_AUTO_TURN) {
          autoTurnIntervalRequested = true;
          return;
        }
        autoTurnOption = idx;
        toggleAutoPageTurn(static_cast<uint8_t>(idx));
      });
      paintOverlayPopup();
      return;
    }
    case MA::BLUETOOTH:
      EpubReaderMenuActivity::toggleBluetooth();
      {
        RenderLock lock;
        renderOverlay();
        pushOverlayRefresh();
      }
      return;
    case MA::NIGHT_MODE:
      SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      discardOverlayPage();
      requestUpdate();
      return;
    case MA::FRONTLIGHT: {
      const bool lightOn = !Frontlight.isOn();
      Frontlight.setOn(lightOn);
      SETTINGS.frontlightOn = lightOn ? 1 : 0;
      SETTINGS.saveToFile();
      // Rebuild the stored reading page so the status icon also changes when
      // the toolbar closes instead of restoring its previous light state.
      discardOverlayPage();
      requestUpdate();
      return;
    }
    default:
      break;
  }
  // Leaf actions open their own screen / perform the action; close the overlay first.
  overlay = Overlay::None;
  if (action == MA::GO_TO_PERCENT && overlayPageStored) {
    // The percent dialog is a popup over the current frame: wipe the toolbar
    // chrome back to the clean page first so the dialog draws over the page,
    // not the sheet. No refresh push — the dialog's first frame carries it.
    RenderLock lock;
    settleOverlayRefresh();
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = false;
  } else {
    discardOverlayPage();
  }
  if (action == MA::TOGGLE_BOOKMARK) {
    // No child activity here to trigger the re-render the list menu relies on:
    // show the same confirmation popup the long-press path does.
    addBookmark();
    showBookmarkMessage = true;
    bookmarkMessageTime = millis();
    requestUpdate();
    return;
  }
  onReaderMenuConfirm(action);
  // Child screens re-render the reader on Pop; GO_HOME/DELETE_CACHE leave the
  // reader entirely. In-place actions would otherwise leave the closed panel
  // on screen.
  if (action != MA::GO_HOME && action != MA::DELETE_CACHE) requestUpdate();
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  pauseReadingSession();
  if (!epub) return;

  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';
  int targetSpineIndex = sameFile ? currentSpineIndex : epub->resolveHrefToSpineIndex(hrefStr);

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    return;
  }

  if (savePosition) {
    // Full: drop the oldest entry so Back always returns from the newest jump.
    if (footnoteDepth == MAX_FOOTNOTE_DEPTH) {
      std::copy(savedPositions + 1, savedPositions + MAX_FOOTNOTE_DEPTH, savedPositions);
      footnoteDepth--;
    }
    // A child screen (menu, footnote list) may have released the section;
    // nextPageNumber then holds the page it was on.
    const int page = section ? section->currentPage : nextPageNumber;
    savedPositions[footnoteDepth] = {currentSpineIndex, page};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, page);
  }

  {
    RenderLock lock;
    clearDeferredReposition();
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::saveLinkStack() const {
  // [depth] then depth x (spine u16 LE, page u16 LE)
  uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
  size_t size = 0;
  data[size++] = static_cast<uint8_t>(footnoteDepth);
  for (int i = 0; i < footnoteDepth; i++) {
    const SavedPosition& pos = savedPositions[i];
    data[size++] = pos.spineIndex & 0xFF;
    data[size++] = (pos.spineIndex >> 8) & 0xFF;
    data[size++] = pos.pageNumber & 0xFF;
    data[size++] = (pos.pageNumber >> 8) & 0xFF;
  }
  HalFile f;
  if (!Storage.openFileForWrite("ERS", epub->getCachePath() + "/links.bin", f)) return;
  if (f.write(data, size) != size) LOG_ERR("ERS", "Failed to write link stack");
}

void EpubReaderActivity::loadLinkStack() {
  const std::string path = epub->getCachePath() + "/links.bin";
  if (!Storage.exists(path.c_str())) return;
  {
    HalFile f;
    uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
    if (Storage.openFileForRead("ERS", path, f)) {
      const int size = f.read(data, sizeof(data));
      const int depth = size > 0 ? data[0] : 0;
      if (depth >= 1 && depth <= MAX_FOOTNOTE_DEPTH && size == 1 + depth * 4) {
        const int spineCount = epub->getSpineItemsCount();
        bool valid = true;
        for (int i = 0; i < depth; i++) {
          const uint8_t* p = data + 1 + i * 4;
          savedPositions[i] = {p[0] | (p[1] << 8), p[2] | (p[3] << 8)};
          valid = valid && savedPositions[i].spineIndex < spineCount;
        }
        if (valid) {
          footnoteDepth = depth;
          LOG_DBG("ERS", "Loaded link stack, depth %d", depth);
        }
      }
    }
  }
  // Consumed once: a later exit rewrites it, and an unclean shutdown must not
  // resurrect a stale stack.
  Storage.remove(path.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  pauseReadingSession();
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock;
    clearDeferredReposition();
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::loadCachedBookmarks() {
  cachedBookmarks.clear();
  if (!epub) {
    currentPageBookmarked = false;
    return;
  }

  BookmarkFile::load(epub->getPath(), cachedBookmarks);
  updateBookmarkFlag();
}

void EpubReaderActivity::addBookmark() {
  if (!section || !epub) return;
  LOG_DBG("ERS", "Toggle bookmark at spine %d, page %d", currentSpineIndex, section ? section->currentPage : -1);
  int currentPage;
  int pageCount;
  {
    RenderLock lock;
    pageCount = section->estimatedTotalPages();
    currentPage = section->currentPage;
  }

  SavedProgressPosition progress = ProgressMapper::toSavedProgress(epub, getCurrentPosition());
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, currentPage, pageCount);

  const size_t bookmarkCountBeforeToggle = cachedBookmarks.size();
  cachedBookmarks.erase(std::remove_if(cachedBookmarks.begin(), cachedBookmarks.end(),
                                       [&](const BookmarkEntry& b) {
                                         return bookmarkMatchesProgress(b, currentSpineIndex, currentPage, pageCount,
                                                                        pageRange);
                                       }),
                        cachedBookmarks.end());
  if (cachedBookmarks.size() != bookmarkCountBeforeToggle) {
    bookmarkRemoved = true;
    currentPageBookmarked = false;
  } else {
    std::string pageText;
    if (currentPage >= 0 && currentPage < pageCount) {
      pageText = section->getTextFromSectionFile();
    }
    BookmarkEntry entry;
    entry.percentage = progress.percentage;
    entry.xpath = progress.xpath;
    entry.summary = BookmarkUtil::sanitizeBookmarkSummary(pageText);
    entry.computedSpineIndex = currentSpineIndex;
    entry.computedChapterPageCount = pageCount;
    entry.computedChapterProgress = currentPage;
    const std::optional<uint32_t> offset =
        currentPageVisibleOffset.has_value() ? currentPageVisibleOffset
        : (currentPage >= 0 && currentPage < section->pageCount)
            ? section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))
            : std::nullopt;
    if (offset.has_value()) {
      entry.visibleTextOffset = *offset;
      entry.hasVisibleTextOffset = true;
    }
    cachedBookmarks.insert(cachedBookmarks.begin(), entry);
    bookmarkRemoved = false;
    currentPageBookmarked = true;
  }

  if (!BookmarkFile::save(epub->getPath(), cachedBookmarks)) {
    LOG_ERR("ERS", "Failed to save bookmarks");
  }
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || cachedBookmarks.empty()) {
    currentPageBookmarked = false;
    return;
  }
  const int pageCount = section->estimatedTotalPages();
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, section->currentPage, pageCount);
  currentPageBookmarked = std::any_of(cachedBookmarks.begin(), cachedBookmarks.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, currentSpineIndex, section->currentPage, pageCount, pageRange);
  });
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  // With the section released (reader left from the chapter list), the saved position stands in.
  const ChapterPosition position = chapterPosition();
  if (section || position.hasTotal()) {
    info.currentPage = position.displayPage();
    info.totalPages = position.totalPages;
    if (epub && epub->getBookSize() > 0 && position.hasTotal()) {
      info.progressPercent = bookPercentFor(position);
      // 100 only on the last page of a fully laid-out chapter: rounding must not
      // report a long book finished early. Without the section that is unknown.
      const bool lastPage = section && currentSpineIndex == epub->getSpineItemsCount() - 1 && !section->isBuilding() &&
                            !section->isPartial() && position.displayPage() >= position.totalPages;
      const float chapterEnd = static_cast<float>(position.displayPage()) / static_cast<float>(position.totalPages);
      const int endPct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterEnd) * 100.0f);
      info.endProgressPercent = lastPage ? 100 : std::clamp(endPct, 0, 99);
    }
  }
  if (isAtEndOfBook()) info.endProgressPercent = 100;
  return info;
}

int EpubReaderActivity::getProgressBasisPoints() const {
  if (isAtEndOfBook()) return 10000;
  if (!epub || !section || epub->getBookSize() == 0) return getProgressPercent() * 100;
  const int totalPages = section->estimatedTotalPages();
  if (totalPages <= 0) return getProgressPercent() * 100;
  const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(totalPages);
  const int basisPoints =
      static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 10000.0f + 0.5f);
  return std::clamp(basisPoints, 0, 10000);
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  localPos.hasResolvedSpineIndex = true;
  localPos.hasMappedPage = true;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    if (const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))) {
      localPos.visibleTextOffset = *offset;
      localPos.hasVisibleTextOffset = true;
    }
  }
  if (paragraphIndex.has_value()) {
    localPos.paragraphIndex = *paragraphIndex;
    localPos.hasParagraphIndex = true;
  }
  return localPos;
}

void EpubReaderActivity::loadReferencePages() {
  if (!SETTINGS.stablePageNumbers || referenceSpine == currentSpineIndex || !epub) return;
  referenceSpine = currentSpineIndex;
  referencePages = {};
  constexpr char manifest[] = "META-INF/x-locations.json";
  if (!referenceManifestChecked) {
    referenceManifestChecked = true;
    if (!epub->getItemSize(manifest, &referenceManifestBytes)) referenceManifestBytes = 0;
  }
  if (!referenceManifestBytes || referenceManifestBytes > 64 * 1024) return;
  // Fixed SAX state exceeds the C3 stack budget. Allocate only at chapter entry
  // while this optional feature is enabled, and release before rendering.
  auto stream = makeUniqueNoThrow<ReferencePageStream>(static_cast<uint16_t>(currentSpineIndex));
  if (!stream) {
    LOG_ERR("REFPAGE", "OOM: parser (%u bytes)", static_cast<unsigned>(sizeof(ReferencePageStream)));
    return;
  }
  if (!epub->readItemContentsToStream(manifest, *stream, 1024) || !stream->parser.finish(referencePages)) {
    referencePages = {};
    LOG_ERR("REFPAGE", "Invalid or unsupported reference numbering for chapter %d", currentSpineIndex);
    return;
  }
  LOG_DBG("REFPAGE", "Loaded chapter %d: %lu reference pages", currentSpineIndex,
          static_cast<unsigned long>(referencePages.pages));
}
