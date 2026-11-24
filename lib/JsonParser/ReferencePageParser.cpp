#include "ReferencePageParser.h"
#include <algorithm>
#include <cmath>
#include <cstring>

bool ReferencePageInfo::resolve(float progress, uint32_t& page, uint32_t& pageCount) const {
  page = pageCount = 0;
  if (!count || !total || !unitsPerPage || !pages || start >= total || !std::isfinite(progress)) return false;
  const uint64_t completed = start + static_cast<uint64_t>(std::clamp(progress, 0.0f, 1.0f) * count);
  page = static_cast<uint32_t>(std::min<uint64_t>(completed / unitsPerPage + 1, pages));
  pageCount = pages;
  return true;
}

ReferencePageParser::ReferencePageParser(uint16_t index)
    : parser(JsonCallbacks{this, onKey, onString, onNumber, onBool, onNull,
                           onObjectStart, onObjectEnd, onArrayStart, onArrayEnd}), wanted(index) {}

bool ReferencePageParser::takeKey() {
  if (key == None) return false;
  auto& seen = depth == 1 ? rootSeen : entrySeen;
  const uint32_t bit = 1UL << key;
  if (seen & bit) invalid = true;
  seen |= bit;
  return true;
}

void ReferencePageParser::onKey(void* ctx, const char* text, size_t size) {
  auto& p = *static_cast<ReferencePageParser*>(ctx);
  p.key = None;
  const auto equal = [text, size](const char* value) { return std::strlen(value) == size && !std::memcmp(text, value, size); };
  if (p.depth == 1) {
    if (equal("format")) p.key = Format;
    else if (equal("version")) p.key = Version;
    else if (equal("totalLocations")) p.key = Locations;
    else if (equal("totalWords")) p.key = Words;
    else if (equal("totalCharacters")) p.key = Characters;
    else if (equal("wordsPerReferencePage")) p.key = WordsPerPage;
    else if (equal("charactersPerReferencePage")) p.key = CharactersPerPage;
    else if (equal("totalReferencePages")) p.key = Pages;
    else if (equal("spine")) p.key = Spine;
  } else if (p.inSpine && p.depth == 3) {
    if (equal("index")) p.key = Index;
    else if (equal("startLocation")) p.key = StartLocation;
    else if (equal("endLocation")) p.key = EndLocation;
    else if (equal("wordStart")) p.key = WordStart;
    else if (equal("wordCount")) p.key = WordCount;
    else if (equal("characterStart")) p.key = CharacterStart;
    else if (equal("characterCount")) p.key = CharacterCount;
  }
}

void ReferencePageParser::onString(void* ctx, const char* text, size_t size) {
  auto& p = *static_cast<ReferencePageParser*>(ctx);
  if (p.done || !p.depth || (p.inSpine && p.depth == 2)) p.invalid = true;
  if (p.takeKey()) {
    if (p.key != Format) p.invalid = true;
    else p.validFormat = (size == 11 && !std::memcmp(text, "x-locations", size)) ||
                         (size == 18 && !std::memcmp(text, "crossink-locations", size));
  }
  p.key = None;
}

void ReferencePageParser::onNumber(void* ctx, const char* text, size_t size) {
  auto& p = *static_cast<ReferencePageParser*>(ctx);
  if (p.done || !p.depth || (p.inSpine && p.depth == 2)) p.invalid = true;
  if (!p.takeKey()) return;
  uint32_t value = 0;
  if (!size) p.invalid = true;
  for (size_t i = 0; i < size; ++i) {
    if (text[i] < '0' || text[i] > '9' || value > (UINT32_MAX - (text[i] - '0')) / 10) {
      p.invalid = true;
      return;
    }
    value = value * 10 + text[i] - '0';
  }
  switch (p.key) {
    case Version: p.version = value; break;
    case Locations: p.locations = value; break;
    case Words: p.words = value; break;
    case Characters: p.characters = value; break;
    case WordsPerPage: p.wordsPerPage = value; break;
    case CharactersPerPage: p.charactersPerPage = value; break;
    case Pages: p.pages = value; break;
    case Index: p.current.index = value; break;
    case StartLocation: p.current.startLocation = value; break;
    case EndLocation: p.current.endLocation = value; break;
    case WordStart: p.current.wordStart = value; break;
    case WordCount: p.current.wordCount = value; break;
    case CharacterStart: p.current.characterStart = value; break;
    case CharacterCount: p.current.characterCount = value; break;
    default: p.invalid = true; break;
  }
  p.key = None;
}

void ReferencePageParser::onBool(void* ctx, bool) { onNull(ctx); }
void ReferencePageParser::onNull(void* ctx) {
  auto& p = *static_cast<ReferencePageParser*>(ctx);
  if (p.done || !p.depth) p.invalid = true;
  if (p.takeKey() || (p.inSpine && p.depth == 2)) p.invalid = true;
  p.key = None;
}
void ReferencePageParser::onObjectStart(void* ctx) {
  auto& p = *static_cast<ReferencePageParser*>(ctx);
  if (p.done || p.takeKey()) p.invalid = true;
  ++p.depth;
  if (p.inSpine && p.depth == 3) {
    p.current = {};
    p.current.index = p.ordinal++;
    p.entrySeen = 0;
  }
  p.key = None;
}
void ReferencePageParser::onObjectEnd(void* ctx) {
  auto& p = *static_cast<ReferencePageParser*>(ctx);
  if (p.inSpine && p.depth == 3 && p.current.index == p.wanted) {
    if (p.found) p.invalid = true;
    p.selected = p.current;
    p.found = true;
  }
  if (p.depth == 1) p.done = true;
  if (p.depth) --p.depth;
  p.key = None;
}
void ReferencePageParser::onArrayStart(void* ctx) {
  auto& p = *static_cast<ReferencePageParser*>(ctx);
  if (p.done || (p.key != None && p.key != Spine) || (p.inSpine && p.depth == 2)) p.invalid = true;
  if (p.key == Spine && p.depth == 1) {
    p.takeKey();
    p.inSpine = true;
  }
  ++p.depth;
  p.key = None;
}
void ReferencePageParser::onArrayEnd(void* ctx) {
  auto& p = *static_cast<ReferencePageParser*>(ctx);
  if (p.depth == 2) p.inSpine = false;
  if (p.depth) --p.depth;
  p.key = None;
}

bool ReferencePageParser::finish(ReferencePageInfo& out) const {
  out = {};
  if (invalid || parser.hasError() || !syntax.complete() || !done || depth || !validFormat || version != 1 || !locations || !found ||
      !selected.startLocation || selected.endLocation < selected.startLocation || selected.endLocation > locations) return false;
  const bool useCharacters = characters && charactersPerPage;
  out.start = useCharacters ? selected.characterStart : selected.wordStart;
  out.count = useCharacters ? selected.characterCount : selected.wordCount;
  out.total = useCharacters ? characters : words;
  out.unitsPerPage = useCharacters ? charactersPerPage : wordsPerPage ? wordsPerPage : 1500;
  const uint64_t calculatedPages = (static_cast<uint64_t>(out.total) + out.unitsPerPage - 1) / out.unitsPerPage;
  out.pages = pages ? pages : static_cast<uint32_t>(calculatedPages);
  return out.total && out.count && out.start < out.total && out.count <= out.total - out.start && out.pages;
}
