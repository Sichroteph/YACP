#include "TextBlock.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr uint16_t MAX_WORDS_PER_TEXT_BLOCK = 512;

uint16_t measureBackgroundWidth(const GfxRenderer& renderer, const int fontId, const char* word,
                                const EpdFontFamily::Style style) {
  if (word[0] == ' ' && word[1] == '\0') {
    return renderer.getSpaceWidth(fontId, style);
  }
  return static_cast<uint16_t>(std::max(0, renderer.getTextAdvanceX(fontId, word, style)));
}

bool isWhitespaceOnlyBackgroundToken(const char* word) {
  if (!word || *word == '\0') {
    return false;
  }

  for (size_t i = 0; word[i] != '\0';) {
    const auto c = static_cast<uint8_t>(word[i]);
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
      ++i;
      continue;
    }
    if (c == 0xC2 && word[i + 1] != '\0' && static_cast<uint8_t>(word[i + 1]) == 0xA0) {
      i += 2;
      continue;
    }
    if (c == 0xE2 && word[i + 1] != '\0' && word[i + 2] != '\0' && static_cast<uint8_t>(word[i + 1]) == 0x80 &&
        static_cast<uint8_t>(word[i + 2]) == 0xAF) {
      i += 3;
      continue;
    }
    return false;
  }

  return true;
}

bool hasSyntheticIndentPrefix(const char* word, const uint16_t len) {
  return len >= 3 && static_cast<uint8_t>(word[0]) == 0xE2 && static_cast<uint8_t>(word[1]) == 0x80 &&
         static_cast<uint8_t>(word[2]) == 0x83;
}

}  // namespace

size_t TextBlock::arenaSize(const uint16_t wordCount, const bool hasBionic, const bool hasGuideDots,
                            const bool hasWordFlags, const uint16_t textBytes) {
  // 16-bit arrays first so direct loads stay aligned on RISC-V, then byte arrays, then text.
  size_t size = static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t));
  if (hasBionic) {
    size += static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(uint8_t));
  }
  if (hasGuideDots) {
    size += static_cast<size_t>(wordCount) * sizeof(uint16_t);
  }
  if (hasWordFlags) {
    size += static_cast<size_t>(wordCount) * sizeof(uint8_t);
  }
  return size + textBytes;
}

void TextBlock::bindArenaPointers() {
  uint8_t* base = arena.get();
  const size_t wc = numWords;
  textOffArr = reinterpret_cast<const uint16_t*>(base);
  xposArr = reinterpret_cast<const int16_t*>(base + wc * 2);
  size_t off = wc * 4;
  if (bionicPresent) {
    bionicSuffixXArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * 2;
  }
  if (guideDotsPresent) {
    guideDotXOffsetArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * 2;
  }
  stylesArr = base + off;
  off += wc;
  if (bionicPresent) {
    bionicBoundaryArr = base + off;
    off += wc;
  }
  if (wordFlagsPresent) {
    wordFlagsArr = base + off;
    off += wc;
  }
  textArr = reinterpret_cast<const char*>(base + off);
}

TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& bionicBoundary,
                     const std::vector<uint16_t>& bionicSuffixX, const std::vector<uint16_t>& guideDotXOffset,
                     const std::vector<uint8_t>& wordFlags, const BlockStyle& blockStyle)
    : blockStyle(blockStyle) {
  const bool hasBionic = !bionicBoundary.empty();
  const bool hasGuideDots = !guideDotXOffset.empty();
  const bool hasWordFlags = !wordFlags.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() || words.size() > MAX_WORDS_PER_TEXT_BLOCK ||
      (hasBionic && (words.size() != bionicBoundary.size() || words.size() != bionicSuffixX.size())) ||
      (!hasBionic && !bionicSuffixX.empty()) || (hasGuideDots && words.size() != guideDotXOffset.size()) ||
      (hasWordFlags && words.size() != wordFlags.size())) {
    LOG_ERR("TXB",
            "Construction failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u, "
            "dotX=%u, flags=%u)",
            static_cast<uint32_t>(words.size()), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(bionicBoundary.size()),
            static_cast<uint32_t>(bionicSuffixX.size()), static_cast<uint32_t>(guideDotXOffset.size()),
            static_cast<uint32_t>(wordFlags.size()));
    isValid = false;
    return;
  }

  numWords = static_cast<uint16_t>(words.size());
  bionicPresent = hasBionic;
  guideDotsPresent = hasGuideDots;
  wordFlagsPresent = hasWordFlags;
  if (numWords == 0) {
    return;
  }

  size_t totalText = 0;
  for (const auto& word : words) {
    totalText += word.size() + 1;
  }
  if (totalText > UINT16_MAX) {
    LOG_ERR("TXB", "Construction failed: text size %u exceeds arena limit", static_cast<uint32_t>(totalText));
    numWords = 0;
    bionicPresent = false;
    guideDotsPresent = false;
    wordFlagsPresent = false;
    isValid = false;
    return;
  }
  textBytes = static_cast<uint16_t>(totalText);

  const size_t size = arenaSize(numWords, bionicPresent, guideDotsPresent, wordFlagsPresent, textBytes);
  arena = makeUniqueNoThrow<uint8_t[]>(size);
  if (!arena) {
    LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
    numWords = 0;
    textBytes = 0;
    bionicPresent = false;
    guideDotsPresent = false;
    wordFlagsPresent = false;
    isValid = false;
    return;
  }
  bindArenaPointers();

  auto* textOff = const_cast<uint16_t*>(textOffArr);
  auto* xpos = const_cast<int16_t*>(xposArr);
  auto* styles = const_cast<uint8_t*>(stylesArr);
  auto* text = const_cast<char*>(textArr);
  uint16_t off = 0;
  for (uint16_t i = 0; i < numWords; i++) {
    textOff[i] = off;
    xpos[i] = wordXpos[i];
    styles[i] = static_cast<uint8_t>(wordStyles[i]);
    memcpy(text + off, words[i].data(), words[i].size());
    off += static_cast<uint16_t>(words[i].size());
    text[off++] = '\0';
  }
  if (bionicPresent) {
    auto* suffixX = const_cast<uint16_t*>(bionicSuffixXArr);
    auto* boundary = const_cast<uint8_t*>(bionicBoundaryArr);
    for (uint16_t i = 0; i < numWords; i++) {
      suffixX[i] = bionicSuffixX[i];
      boundary[i] = bionicBoundary[i];
    }
  }
  if (guideDotsPresent) {
    auto* dotX = const_cast<uint16_t*>(guideDotXOffsetArr);
    for (uint16_t i = 0; i < numWords; i++) {
      dotX[i] = guideDotXOffset[i];
    }
  }
  if (wordFlagsPresent) {
    auto* flags = const_cast<uint8_t*>(wordFlagsArr);
    for (uint16_t i = 0; i < numWords; i++) {
      flags[i] = wordFlags[i];
    }
  }
}

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y,
                       const bool foregroundBlack) const {
  if (!isValid) {
    LOG_ERR("TXB", "Render skipped: invalid block");
    return;
  }

  const bool scanning = renderer.isFontCacheScanning();
  const int ascender = renderer.getFontAscenderSize(fontId);
  for (uint16_t i = 0; i < numWords; i++) {
    const char* word = wordText(i);
    const uint16_t wordLen = wordTextLen(i);
    const int wordX = wordXpos(i) + x;
    const EpdFontFamily::Style currentStyle = wordStyle(i);
    const uint8_t boundary = bionicBoundary(i);
    const auto baseDir =
        static_cast<BidiUtils::BidiBaseDir>(BidiUtils::detectParagraphLevel(word, blockStyle.isRtl ? 1 : 0));

    if ((wordFlags(i) & WORD_FLAG_BACKGROUND_BLACK) != 0 && isWhitespaceOnlyBackgroundToken(word)) {
      const uint16_t backgroundWidth = measureBackgroundWidth(renderer, fontId, word, currentStyle);
      if (backgroundWidth > 0) {
        renderer.fillRect(wordX, y, backgroundWidth, renderer.getFontAscenderSize(fontId), true);
      }
    }

    int wordY = y;
    if ((currentStyle & EpdFontFamily::SUP) != 0) {
      wordY -= ascender * 2 / 5;
    } else if ((currentStyle & EpdFontFamily::SUB) != 0) {
      wordY += ascender / 4;
    }

    if (boundary > 0) {
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      char boldBuf[40];
      const size_t boldLen =
          std::min<size_t>({static_cast<size_t>(boundary), static_cast<size_t>(wordLen), sizeof(boldBuf) - 1});
      memcpy(boldBuf, word, boldLen);
      boldBuf[boldLen] = '\0';
      renderer.drawText(fontId, wordX, wordY, boldBuf, foregroundBlack, boldStyle, baseDir);
      renderer.drawText(fontId, wordX + bionicSuffixX(i), wordY, word + boldLen, foregroundBlack, currentStyle,
                        baseDir);
    } else {
      renderer.drawText(fontId, wordX, wordY, word, foregroundBlack, currentStyle, baseDir);
    }

    const uint16_t dotOffset = guideDotXOffset(i);
    if (dotOffset > 0) {
      renderer.drawText(fontId, wordX + dotOffset, wordY, "\xc2\xb7", foregroundBlack, EpdFontFamily::REGULAR, baseDir);
    }

    if (!scanning && (currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      int startX = wordX;
      int underlineWidth = renderer.getTextWidth(fontId, word, currentStyle, baseDir);
      const int underlineY = wordY + ascender + 2;

      if (hasSyntheticIndentPrefix(word, wordLen)) {
        const char* visiblePtr = word + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        startX = wordX + prefixWidth;
        underlineWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle, baseDir);
      }

      if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
        underlineWidth = (underlineWidth + 1) / 2;
      }

      renderer.drawLine(startX, underlineY, startX + underlineWidth, underlineY, 3, foregroundBlack);
    }

    if ((currentStyle & EpdFontFamily::STRIKETHROUGH) != 0) {
      int startX = wordX;
      int strikeWidth = renderer.getTextWidth(fontId, word, currentStyle, baseDir);
      const int strikeY = y + renderer.getFontAscenderSize(fontId) / 2 + 6;

      if (hasSyntheticIndentPrefix(word, wordLen)) {
        const char* visiblePtr = word + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        startX = wordX + prefixWidth;
        strikeWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle, baseDir);
      }

      if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
        strikeWidth = (strikeWidth + 1) / 2;
      }

      renderer.drawLine(startX, strikeY, startX + strikeWidth, strikeY, 3, foregroundBlack);
    }
  }
}

bool TextBlock::serialize(HalFile& file) const {
  if (!isValid) {
    LOG_ERR("TXB", "Serialization failed: invalid block");
    return false;
  }

  if (!serialization::tryWritePod(file, numWords) ||
      !serialization::tryWritePod(file, static_cast<uint8_t>(bionicPresent ? 1 : 0)) ||
      !serialization::tryWritePod(file, static_cast<uint8_t>(guideDotsPresent ? 1 : 0)) ||
      !serialization::tryWritePod(file, static_cast<uint8_t>(wordFlagsPresent ? 1 : 0)) ||
      !serialization::tryWritePod(file, textBytes)) {
    LOG_ERR("TXB", "Serialization failed: could not write block header");
    return false;
  }
  if (numWords > 0) {
    const size_t size = arenaSize(numWords, bionicPresent, guideDotsPresent, wordFlagsPresent, textBytes);
    if (file.write(arena.get(), size) != static_cast<int>(size)) {
      LOG_ERR("TXB", "Serialization failed: arena write (%u bytes)", static_cast<uint32_t>(size));
      return false;
    }
  }

  return serialization::tryWritePod(file, blockStyle.alignment) &&
         serialization::tryWritePod(file, blockStyle.textAlignDefined) &&
         serialization::tryWritePod(file, blockStyle.marginTop) &&
         serialization::tryWritePod(file, blockStyle.marginBottom) &&
         serialization::tryWritePod(file, blockStyle.marginLeft) &&
         serialization::tryWritePod(file, blockStyle.marginRight) &&
         serialization::tryWritePod(file, blockStyle.paddingTop) &&
         serialization::tryWritePod(file, blockStyle.paddingBottom) &&
         serialization::tryWritePod(file, blockStyle.paddingLeft) &&
         serialization::tryWritePod(file, blockStyle.paddingRight) &&
         serialization::tryWritePod(file, blockStyle.textIndent) &&
         serialization::tryWritePod(file, blockStyle.textIndentDefined) &&
         serialization::tryWritePod(file, blockStyle.isRtl) &&
         serialization::tryWritePod(file, blockStyle.directionDefined);
}

std::unique_ptr<TextBlock> TextBlock::deserialize(HalFile& file) {
  uint16_t wc = 0;
  uint8_t hasBionic = 0;
  uint8_t hasGuideDots = 0;
  uint8_t hasWordFlags = 0;
  uint16_t textBytes = 0;
  if (!serialization::tryReadPod(file, wc) || !serialization::tryReadPod(file, hasBionic) ||
      !serialization::tryReadPod(file, hasGuideDots) || !serialization::tryReadPod(file, hasWordFlags) ||
      !serialization::tryReadPod(file, textBytes)) {
    LOG_ERR("TXB", "Deserialization failed: could not read block header");
    return nullptr;
  }

  if (wc > MAX_WORDS_PER_TEXT_BLOCK) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }
  if (hasBionic > 1 || hasGuideDots > 1 || hasWordFlags > 1) {
    LOG_ERR("TXB", "Deserialization failed: invalid metadata flags");
    return nullptr;
  }
  if ((wc == 0 && textBytes != 0) || (wc > 0 && textBytes < wc)) {
    LOG_ERR("TXB", "Deserialization failed: bad text size %u for %u words", textBytes, wc);
    return nullptr;
  }

  std::unique_ptr<TextBlock> block(new (std::nothrow) TextBlock());
  if (!block) {
    LOG_ERR("TXB", "Deserialization failed: could not allocate TextBlock");
    return nullptr;
  }
  block->numWords = wc;
  block->textBytes = textBytes;
  block->bionicPresent = hasBionic != 0;
  block->guideDotsPresent = hasGuideDots != 0;
  block->wordFlagsPresent = hasWordFlags != 0;

  if (wc > 0) {
    const size_t size =
        arenaSize(wc, block->bionicPresent, block->guideDotsPresent, block->wordFlagsPresent, textBytes);
    const int remaining = file.available();
    if (remaining < 0 || static_cast<size_t>(remaining) < size) {
      LOG_ERR("TXB", "Deserialization failed: truncated arena (%u bytes needed, %d available)",
              static_cast<uint32_t>(size), remaining);
      return nullptr;
    }
    block->arena = makeUniqueNoThrow<uint8_t[]>(size);
    if (!block->arena) {
      LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
      return nullptr;
    }
    if (file.read(block->arena.get(), size) != static_cast<int>(size)) {
      LOG_ERR("TXB", "Deserialization failed: arena read (%u bytes)", static_cast<uint32_t>(size));
      return nullptr;
    }
    block->bindArenaPointers();

    const uint16_t* textOff = block->textOffArr;
    const char* text = block->textArr;
    if (textOff[0] != 0 || text[textBytes - 1] != '\0') {
      LOG_ERR("TXB", "Deserialization failed: corrupt text layout");
      return nullptr;
    }
    for (uint16_t i = 1; i < wc; i++) {
      if (textOff[i] <= textOff[i - 1] || textOff[i] >= textBytes || text[textOff[i] - 1] != '\0') {
        LOG_ERR("TXB", "Deserialization failed: corrupt word offset %u", i);
        return nullptr;
      }
    }
  }

  BlockStyle& blockStyle = block->blockStyle;
  if (!serialization::tryReadPod(file, blockStyle.alignment) ||
      !serialization::tryReadPod(file, blockStyle.textAlignDefined) ||
      !serialization::tryReadPod(file, blockStyle.marginTop) ||
      !serialization::tryReadPod(file, blockStyle.marginBottom) ||
      !serialization::tryReadPod(file, blockStyle.marginLeft) ||
      !serialization::tryReadPod(file, blockStyle.marginRight) ||
      !serialization::tryReadPod(file, blockStyle.paddingTop) ||
      !serialization::tryReadPod(file, blockStyle.paddingBottom) ||
      !serialization::tryReadPod(file, blockStyle.paddingLeft) ||
      !serialization::tryReadPod(file, blockStyle.paddingRight) ||
      !serialization::tryReadPod(file, blockStyle.textIndent) ||
      !serialization::tryReadPod(file, blockStyle.textIndentDefined) ||
      !serialization::tryReadPod(file, blockStyle.isRtl) ||
      !serialization::tryReadPod(file, blockStyle.directionDefined)) {
    LOG_ERR("TXB", "Deserialization failed: truncated block style metadata");
    return nullptr;
  }

  return block;
}
