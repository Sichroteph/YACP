#pragma once

#include <Dictionary.h>
#include <Epub/Page.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "activities/Activity.h"

class DictionaryWordSelectActivity final : public Activity {
 public:
  DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                               int fontId, int marginLeft, int marginTop);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowReaderIdlePowerSaving() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  struct WordBox {
    int16_t x = 0;
    int16_t y = 0;
    int16_t width = 0;
    uint16_t row = 0;
    const char* text = nullptr;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  };

  enum class Popup : uint8_t { None, Busy, NotFound, Unavailable, Error };

  static constexpr size_t MAX_SELECTABLE_WORDS = 240;
  static constexpr size_t SNAPSHOT_CAPACITY = 4096;

  bool extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  void moveVertical(int direction);
  void performLookup();
  bool saveAndDrawHighlight();
  void drawPage() const;
  void drawHints() const;

  std::unique_ptr<Page> page_;
  int fontId_ = 0;
  int marginLeft_ = 0;
  int marginTop_ = 0;
  int lineHeight_ = 0;

  std::unique_ptr<WordBox[]> words_;
  size_t wordCount_ = 0;
  int selected_ = 0;
  uint16_t rowCount_ = 0;

  Dictionary dictionary_;
  bool discoveryAttempted_ = false;
  bool dictionaryAvailable_ = false;

  Popup popup_ = Popup::None;
  unsigned long popupTime_ = 0;
  bool finishAfterPopup_ = false;

  std::unique_ptr<uint8_t[]> snapshot_;
  int16_t snapshotX_ = 0;
  int16_t snapshotY_ = 0;
  int16_t snapshotW_ = 0;
  int16_t snapshotH_ = 0;
  int snapshotIndex_ = -1;
};
