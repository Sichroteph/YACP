#pragma once
#include <cstdint>
#include "StreamingJsonParser.h"
#include "JsonSyntaxGuard.h"

struct ReferencePageInfo {
  uint32_t start = 0, count = 0, total = 0, unitsPerPage = 0, pages = 0;
  bool resolve(float chapterProgress, uint32_t& page, uint32_t& pageCount) const;
};

// Fixed parser state; unrelated chapter records are discarded while streaming.
class ReferencePageParser {
 public:
  explicit ReferencePageParser(uint16_t spineIndex);
  void feed(const char* data, size_t length) { syntax.feed(data, length); parser.feed(data, length); }
  bool finish(ReferencePageInfo& out) const;

 private:
  enum Key : uint8_t { None, Format, Version, Locations, Words, Characters, WordsPerPage,
                      CharactersPerPage, Pages, Spine, Index, StartLocation, EndLocation,
                      WordStart, WordCount, CharacterStart, CharacterCount };
  struct Entry {
    uint32_t index = 0, startLocation = 0, endLocation = 0;
    uint32_t wordStart = 0, wordCount = 0, characterStart = 0, characterCount = 0;
  } current, selected;
  StreamingJsonParser parser;
  JsonSyntaxGuard syntax;
  uint16_t wanted;
  uint32_t ordinal = 0, rootSeen = 0, entrySeen = 0;
  uint32_t version = 0, locations = 0, words = 0, characters = 0;
  uint32_t wordsPerPage = 0, charactersPerPage = 0, pages = 0;
  Key key = None;
  uint8_t depth = 0;
  bool inSpine = false, found = false, validFormat = false, done = false, invalid = false;
  bool takeKey();
  static void onKey(void*, const char*, size_t);
  static void onString(void*, const char*, size_t);
  static void onNumber(void*, const char*, size_t);
  static void onBool(void*, bool);
  static void onNull(void*);
  static void onObjectStart(void*);
  static void onObjectEnd(void*);
  static void onArrayStart(void*);
  static void onArrayEnd(void*);
};
