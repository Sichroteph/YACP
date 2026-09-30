#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryBudget.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "DictionaryDefinitionActivity.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1800;
constexpr size_t PREWARM_TEXT_CAPACITY = 2048;
constexpr uint32_t WORD_LIST_HEADROOM_BYTES = 12U * 1024U;

bool isSelectableToken(const char* text) {
  if (!text) return false;
  for (const uint8_t* current = reinterpret_cast<const uint8_t*>(text); *current != 0; current++) {
    if (*current < 0x80) {
      if (std::isalnum(*current)) return true;
    } else if (*current == 0xE2 && (current[1] == 0x80 || current[1] == 0x81)) {
      if (current[2] == 0) break;
      current += 2;
    } else {
      return true;
    }
  }
  return false;
}

}  // namespace

DictionaryWordSelectActivity::DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           std::unique_ptr<Page> page, const int fontId,
                                                           const int marginLeft, const int marginTop)
    : Activity("DictionaryWordSelect", renderer, mappedInput),
      page_(std::move(page)),
      fontId_(fontId),
      marginLeft_(marginLeft),
      marginTop_(marginTop) {}

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  if (!page_) {
    LOG_ERR("DICT", "Word selector has no page");
    finish();
    return;
  }
  lineHeight_ = renderer.getLineHeight(fontId_);

  // A 4 KB region snapshot is allocated once for this activity. Stack storage
  // would exceed the reader task budget, and static storage would penalize
  // every reading session even when Dictionary is never opened.
  snapshot_ = makeUniqueNoThrow<uint8_t[]>(SNAPSHOT_CAPACITY);
  if (!snapshot_) LOG_ERR("DICT", "OOM: %u-byte word highlight snapshot", static_cast<unsigned>(SNAPSHOT_CAPACITY));

  if (!extractWords()) {
    popup_ = Popup::NotFound;
    popupTime_ = millis();
    finishAfterPopup_ = true;
  } else {
    const int initial = closestInRow(rowCount_ / 2, renderer.getScreenWidth() / 2);
    if (initial >= 0) selected_ = initial;
  }
  requestUpdate();
}

void DictionaryWordSelectActivity::onExit() {
  snapshot_.reset();
  words_.reset();
  wordCount_ = 0;
  page_.reset();
  Activity::onExit();
}

bool DictionaryWordSelectActivity::extractWords() {
  size_t selectableCount = 0;
  for (const auto& element : page_->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    if (!line.getBlock() || !line.getBlock()->valid()) continue;
    for (uint16_t i = 0; i < line.getBlock()->wordCount(); i++) {
      if (isSelectableToken(line.getBlock()->wordText(i))) selectableCount++;
    }
  }
  const size_t availableCount = selectableCount;
  selectableCount = std::min(selectableCount, MAX_SELECTABLE_WORDS);
  if (selectableCount == 0) return false;

  const uint32_t reserveBytes = static_cast<uint32_t>(selectableCount * sizeof(WordBox));
  const auto memory = MemoryBudget::snapshot();
  if (memory.maxAllocHeap < reserveBytes + WORD_LIST_HEADROOM_BYTES) {
    LOG_ERR("DICT", "Low heap for %u dictionary words (%u max alloc, need %u)", static_cast<unsigned>(selectableCount),
            memory.maxAllocHeap, reserveBytes + WORD_LIST_HEADROOM_BYTES);
    return false;
  }
  words_ = makeUniqueNoThrow<WordBox[]>(selectableCount);
  if (!words_) {
    LOG_ERR("DICT", "OOM: %u-byte dictionary word list", reserveBytes);
    return false;
  }

  // This transient 2 KB aggregation buffer batches SD-font preparation. It is
  // too large for the task stack and is freed before the activity starts its
  // input loop. Lookup with built-in fonts does not require it.
  auto pageText = makeUniqueNoThrow<char[]>(PREWARM_TEXT_CAPACITY);
  if (!pageText) LOG_ERR("DICT", "OOM: %u-byte word prewarm buffer", static_cast<unsigned>(PREWARM_TEXT_CAPACITY));
  char* const pageTextData = pageText.get();
  size_t pageTextLength = 0;
  uint8_t styleMask = 0;
  rowCount_ = 0;

  for (const auto& element : page_->elements) {
    if (wordCount_ >= selectableCount) break;
    if (element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block || !block->valid()) continue;

    bool rowHasWords = false;
    const bool rtl = block->getBlockStyle().isRtl;
    const int count = block->wordCount();
    for (int step = 0; step < count && wordCount_ < selectableCount; step++) {
      const uint16_t i = static_cast<uint16_t>(rtl ? count - step - 1 : step);
      const char* text = block->wordText(i);
      if (!isSelectableToken(text)) continue;

      WordBox box;
      box.x = static_cast<int16_t>(line.xPos + block->wordXpos(i) + marginLeft_);
      box.y = static_cast<int16_t>(line.yPos + marginTop_);
      box.row = rowCount_;
      box.text = text;
      box.style = block->wordStyle(i);
      words_[wordCount_++] = box;
      rowHasWords = true;

      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(box.style) & 0x03));
      if (pageTextData) {
        const size_t textLength = strlen(text);
        if (pageTextLength + textLength + 2 < PREWARM_TEXT_CAPACITY) {
          memcpy(pageTextData + pageTextLength, text, textLength);
          pageTextLength += textLength;
          pageTextData[pageTextLength++] = ' ';
        }
      }
    }
    if (rowHasWords) rowCount_++;
  }

  if (pageTextData && pageTextLength > 0) {
    pageTextData[pageTextLength] = '\0';
    renderer.ensureSdCardFontReady(fontId_, pageTextData, styleMask == 0 ? 0x01 : styleMask);
  }
  for (size_t i = 0; i < wordCount_; i++) {
    auto& word = words_[i];
    const int width = renderer.getTextAdvanceX(fontId_, word.text, word.style);
    word.width = static_cast<int16_t>(std::clamp(width, 1, static_cast<int>(INT16_MAX)));
  }
  LOG_INF("DICT", "Word selector retained %u of %u selectable words (%u-byte list, free=%u, maxAlloc=%u)",
          static_cast<unsigned>(wordCount_), static_cast<unsigned>(availableCount), reserveBytes, memory.freeHeap,
          memory.maxAllocHeap);
  return wordCount_ > 0;
}

int DictionaryWordSelectActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(wordCount_); i++) {
    if (words_[i].row != row) continue;
    const int distance = std::abs(words_[i].x + words_[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

void DictionaryWordSelectActivity::moveVertical(const int direction) {
  if (wordCount_ == 0) return;
  const WordBox& current = words_[selected_];
  const int targetRow = static_cast<int>(current.row) + direction;
  if (targetRow < 0 || targetRow >= rowCount_) return;
  const int target = closestInRow(static_cast<uint16_t>(targetRow), current.x + current.width / 2);
  if (target >= 0 && target != selected_) {
    selected_ = target;
    requestUpdate();
  }
}

void DictionaryWordSelectActivity::performLookup() {
  if (wordCount_ == 0) return;
  popup_ = Popup::Busy;
  snapshotIndex_ = -1;
  requestUpdateAndWait();

  if (!discoveryAttempted_) {
    discoveryAttempted_ = true;
    dictionaryAvailable_ = dictionary_.discover();
  }
  if (!dictionaryAvailable_) {
    popup_ = Popup::Unavailable;
    popupTime_ = millis();
    requestUpdate();
    return;
  }

  Dictionary::Hit hit;
  const Dictionary::LookupResult lookup = dictionary_.locate(words_[selected_].text, hit);
  if (lookup == Dictionary::LookupResult::NotFound) {
    popup_ = Popup::NotFound;
    popupTime_ = millis();
    requestUpdate();
    return;
  }
  if (lookup != Dictionary::LookupResult::Found) {
    popup_ = lookup == Dictionary::LookupResult::Unavailable ? Popup::Unavailable : Popup::Error;
    popupTime_ = millis();
    requestUpdate();
    return;
  }

  // The definition buffer is a bounded, fallible 8 KB activity allocation.
  // Stack and static storage would respectively overflow the task budget or
  // impose a permanent RAM cost on readers that never use Dictionary.
  auto definition = makeUniqueNoThrow<char[]>(DictionaryDefinitionActivity::DEFINITION_CAPACITY);
  if (!definition) {
    LOG_ERR("DICT", "OOM: %u-byte definition buffer",
            static_cast<unsigned>(DictionaryDefinitionActivity::DEFINITION_CAPACITY));
    popup_ = Popup::Error;
    popupTime_ = millis();
    requestUpdate();
    return;
  }

  size_t definitionLength = 0;
  bool truncated = false;
  if (!dictionary_.readDefinition(hit, definition.get(), DictionaryDefinitionActivity::DEFINITION_CAPACITY,
                                  definitionLength, truncated)) {
    popup_ = Popup::Error;
    popupTime_ = millis();
    requestUpdate();
    return;
  }

  auto activity = makeUniqueNoThrow<DictionaryDefinitionActivity>(
      renderer, mappedInput, std::move(definition), definitionLength, hit.headword, hit.format, fontId_, truncated);
  if (!activity) {
    LOG_ERR("DICT", "OOM: DictionaryDefinitionActivity");
    popup_ = Popup::Error;
    popupTime_ = millis();
    requestUpdate();
    return;
  }

  popup_ = Popup::None;
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    snapshotIndex_ = -1;
    requestUpdate();
  });
}

void DictionaryWordSelectActivity::loop() {
  using Button = MappedInputManager::Button;
  if (popup_ == Popup::NotFound || popup_ == Popup::Unavailable || popup_ == Popup::Error) {
    if (mappedInput.wasReleased(Button::Back)) {
      finish();
      return;
    }
    if (millis() - popupTime_ >= POPUP_DURATION_MS) {
      if (finishAfterPopup_) {
        finish();
      } else {
        popup_ = Popup::None;
        requestUpdate();
      }
    }
    return;
  }

  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm) && wordCount_ > 0) {
    performLookup();
    return;
  }

  if (mappedInput.wasReleased(Button::Left) && selected_ > 0) {
    selected_--;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Right) && selected_ + 1 < static_cast<int>(wordCount_)) {
    selected_++;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up)) {
    moveVertical(-1);
  } else if (mappedInput.wasReleased(Button::Down)) {
    moveVertical(1);
  }
}

void DictionaryWordSelectActivity::drawPage() const {
  renderer.clearScreen(ReaderUtils::readerBackgroundColor());
  page_->render(renderer, fontId_, marginLeft_, marginTop_, ReaderUtils::readerForegroundBlack());
}

bool DictionaryWordSelectActivity::saveAndDrawHighlight() {
  if (wordCount_ == 0) return false;
  const WordBox& word = words_[selected_];
  int x = word.x - 2;
  int y = word.y - 2;
  int width = word.width + 4;
  int height = lineHeight_ + 4;
  if (x >= renderer.getScreenWidth() || y >= renderer.getScreenHeight()) return false;
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  width = std::min(width, renderer.getScreenWidth() - x);
  height = std::min(height, renderer.getScreenHeight() - y);
  if (width <= 0 || height <= 0) return false;

  bool saved = false;
  const size_t needed = renderer.getRegionByteSize(x, y, width, height);
  if (snapshot_ && needed > 0 && needed <= SNAPSHOT_CAPACITY) {
    saved = renderer.copyRegionToBuffer(x, y, width, height, snapshot_.get(), SNAPSHOT_CAPACITY);
  }
  snapshotX_ = static_cast<int16_t>(x);
  snapshotY_ = static_cast<int16_t>(y);
  snapshotW_ = static_cast<int16_t>(width);
  snapshotH_ = static_cast<int16_t>(height);
  snapshotIndex_ = saved ? selected_ : -1;

  const bool foregroundBlack = ReaderUtils::readerForegroundBlack();
  renderer.fillRect(x, y, width, height, foregroundBlack);
  renderer.drawText(fontId_, word.x, word.y, word.text, !foregroundBlack, word.style);
  return saved;
}

void DictionaryWordSelectActivity::drawHints() const {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), wordCount_ == 0 ? "" : tr(STR_LOOKUP), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  if (popup_ == Popup::None && snapshotIndex_ >= 0 && wordCount_ > 0 && selected_ != snapshotIndex_) {
    if (renderer.copyBufferToRegion(snapshotX_, snapshotY_, snapshotW_, snapshotH_, snapshot_.get(),
                                    SNAPSHOT_CAPACITY)) {
      if (auto* cache = renderer.getFontCacheManager()) {
        auto scope = cache->createPrewarmScope();
        renderer.drawText(fontId_, words_[selected_].x, words_[selected_].y, words_[selected_].text, true,
                          words_[selected_].style);
        if (scope.endScanAndPrewarm() && saveAndDrawHighlight()) {
          drawHints();
          renderer.displayBuffer(HalDisplay::FAST_REFRESH);
          return;
        }
      } else if (saveAndDrawHighlight()) {
        drawHints();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        return;
      }
    }
  }

  const auto drawCompleteScreen = [this]() {
    drawPage();
    if (wordCount_ > 0) saveAndDrawHighlight();
    drawHints();

    if (popup_ != Popup::None) {
      snapshotIndex_ = -1;
      StrId message = StrId::STR_LOADING_POPUP;
      if (popup_ == Popup::NotFound) message = StrId::STR_DICT_NOT_FOUND;
      if (popup_ == Popup::Unavailable) message = StrId::STR_NO_FILES_FOUND;
      if (popup_ == Popup::Error) message = StrId::STR_DICT_ERROR;
      GUI.drawPopup(renderer, I18N.get(message));
      return;
    }
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  };

  if (auto* cache = renderer.getFontCacheManager()) {
    auto scope = cache->createPrewarmScope();
    page_->renderText(renderer, fontId_, marginLeft_, marginTop_, ReaderUtils::readerForegroundBlack());
    if (!scope.endScanAndPrewarm()) {
      LOG_ERR("DICT", "Failed to prewarm reader font for word selection");
    }
    drawCompleteScreen();
    return;
  }
  drawCompleteScreen();
}
