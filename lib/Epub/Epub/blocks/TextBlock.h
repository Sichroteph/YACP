#pragma once
#include <EpdFontFamily.h>
#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include "Block.h"
#include "BlockStyle.h"

// Represents one rendered line. Per-word data is packed into one heap arena
// instead of separate vectors/strings, which reduces heap fragmentation when
// pages are loaded and discarded repeatedly on the ESP32-C3.
class TextBlock final : public Block {
 private:
  BlockStyle blockStyle;
  uint16_t numWords = 0;
  uint16_t textBytes = 0;  // Total size of the text region, including NULs.
  bool bionicPresent = false;
  bool guideDotsPresent = false;
  bool wordFlagsPresent = false;
  bool isValid = true;
  std::unique_ptr<uint8_t[]> arena;

  const uint16_t* textOffArr = nullptr;
  const int16_t* xposArr = nullptr;
  const uint16_t* bionicSuffixXArr = nullptr;    // null when !bionicPresent
  const uint16_t* guideDotXOffsetArr = nullptr;  // null when !guideDotsPresent
  const uint8_t* stylesArr = nullptr;
  const uint8_t* bionicBoundaryArr = nullptr;  // null when !bionicPresent
  const uint8_t* wordFlagsArr = nullptr;       // null when !wordFlagsPresent
  const char* textArr = nullptr;

  TextBlock() = default;  // deserialize() fills the fields directly.
  static size_t arenaSize(uint16_t wordCount, bool hasBionic, bool hasGuideDots, bool hasWordFlags, uint16_t textBytes);
  void bindArenaPointers();

 public:
  static constexpr uint8_t WORD_FLAG_BACKGROUND_BLACK = 0x01;
  static constexpr uint8_t WORD_FLAG_INSERTED_HYPHEN = 0x02;

  explicit TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& bionicBoundary,
                     const std::vector<uint16_t>& bionicSuffixX, const std::vector<uint16_t>& guideDotXOffset,
                     const std::vector<uint8_t>& wordFlags, const BlockStyle& blockStyle = BlockStyle());
  ~TextBlock() override = default;
  TextBlock(const TextBlock&) = delete;
  TextBlock& operator=(const TextBlock&) = delete;

  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  bool isEmpty() override { return numWords == 0; }
  bool valid() const { return isValid; }
  uint16_t wordCount() const { return numWords; }
  const char* wordText(const uint16_t i) const { return textArr + textOffArr[i]; }
  uint16_t wordTextLen(const uint16_t i) const {
    const uint16_t end = (i + 1 < numWords) ? textOffArr[i + 1] : textBytes;
    return end - textOffArr[i] - 1;
  }
  int16_t wordXpos(const uint16_t i) const { return xposArr[i]; }
  EpdFontFamily::Style wordStyle(const uint16_t i) const { return static_cast<EpdFontFamily::Style>(stylesArr[i]); }
  uint8_t bionicBoundary(const uint16_t i) const { return bionicPresent ? bionicBoundaryArr[i] : 0; }
  uint16_t bionicSuffixX(const uint16_t i) const { return bionicPresent ? bionicSuffixXArr[i] : 0; }
  uint16_t guideDotXOffset(const uint16_t i) const { return guideDotsPresent ? guideDotXOffsetArr[i] : 0; }
  uint8_t wordFlags(const uint16_t i) const { return wordFlagsPresent ? wordFlagsArr[i] : 0; }
  bool wordEndsWithInsertedHyphen(const uint16_t i) const { return (wordFlags(i) & WORD_FLAG_INSERTED_HYPHEN) != 0; }

  void render(const GfxRenderer& renderer, int fontId, int x, int y, bool foregroundBlack = true) const;
  BlockType getType() override { return TEXT_BLOCK; }
  bool serialize(HalFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(HalFile& file);
};
