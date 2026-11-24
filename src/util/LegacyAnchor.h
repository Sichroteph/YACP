#pragma once

#include "LegacyMigrationIo.h"
#include <Epub.h>
#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Utf8.h>
#include <expat.h>

namespace legacy {
// One reusable bounded matcher (under 4 KiB), allocated on the migration heap.
// Source chapters stream from ZIP through Expat; no chapter-sized text buffer.
class Anchor final : public Print {
  static constexpr size_t LIMIT = 256;
  uint32_t needle[LIMIT]{};
  uint32_t positions[LIMIT]{};
  uint16_t prefix[LIMIT]{};
  uint16_t length = 0, matched = 0;
  uint32_t visible = 0, normalized = 0, foundOffset = 0;
  unsigned matches = 0, depth = 0, bodyDepth = 0, hiddenDepth = 0;
  XML_Parser parser = nullptr;
  bool valid = true, collecting = false;
  char cluster[64]{};
  unsigned clusterSize = 0;
  uint32_t clusterOffset = 0;

  static bool ignored(uint32_t c) {
    return c <= 32 || c == 0x85 || c == 0xa0 || c == 0xad || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200b) || c == 0x2028 || c == 0x2029 ||
           c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
  }
  void emit(uint32_t c, uint32_t offset) {
    if (ignored(c)) return;
    if (collecting) {
      if (length == LIMIT) { valid = false; return; }
      needle[length++] = c;
      return;
    }
    positions[normalized % LIMIT] = offset;
    ++normalized;
    while (matched && needle[matched] != c) matched = prefix[matched - 1];
    if (needle[matched] == c) ++matched;
    if (matched == length) {
      foundOffset = positions[(normalized - length) % LIMIT];
      ++matches;
      matched = prefix[matched - 1];
    }
  }
  void flushCluster() {
    if (!clusterSize) return;
    cluster[clusterSize] = 0;
    utf8ComposeNfcInPlace(cluster);
    const auto* p = reinterpret_cast<const unsigned char*>(cluster);
    while (*p) emit(utf8NextCodepoint(&p), clusterOffset);
    clusterSize = 0;
  }
  void text(const char* bytes, size_t size) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes);
    const auto* end = p + size;
    while (p < end) {
      const auto* start = p;
      const uint32_t c = utf8NextCodepoint(&p);
      if (p > end || c == REPLACEMENT_GLYPH) { valid = false; return; }
      const bool mark = (c >= 0x300 && c <= 0x36f) || (c >= 0x1ab0 && c <= 0x1aff) ||
                        (c >= 0x1dc0 && c <= 0x1dff);
      if (!mark) flushCluster();
      if (!clusterSize) clusterOffset = visible;
      if (clusterSize + (p - start) >= sizeof(cluster)) { valid = false; return; }
      memcpy(cluster + clusterSize, start, p - start);
      clusterSize += p - start;
      ++visible;
    }
  }
  static std::string_view local(const char* tag) {
    const char* colon = strrchr(tag, ':');
    return colon ? colon + 1 : tag;
  }
  static void start(void* user, const char* name, const char**) {
    auto& s = *static_cast<Anchor*>(user);
    ++s.depth;
    if (VisibleTextUtils::equalsTag(local(name), "body")) s.bodyDepth = s.depth;
    if (s.bodyDepth && !s.hiddenDepth && VisibleTextUtils::isNonVisibleElement(local(name))) s.hiddenDepth = s.depth;
  }
  static void end(void* user, const char*) {
    auto& s = *static_cast<Anchor*>(user);
    if (s.depth == s.hiddenDepth) s.hiddenDepth = 0;
    if (s.depth == s.bodyDepth) s.bodyDepth = 0;
    if (s.depth) --s.depth;
  }
  static void characters(void* user, const char* text, int count) {
    auto& s = *static_cast<Anchor*>(user);
    if (s.bodyDepth && !s.hiddenDepth) s.text(text, count);
  }
  static void entity(void* user, const char* text, int count) {
    if (count < 3 || text[0] != '&' || text[count - 1] != ';') return;
    const char* value = lookupHtmlEntity(text, count);
    characters(user, value ? value : text, value ? strlen(value) : count);
  }
 public:
  ~Anchor() override { if (parser) XML_ParserFree(parser); }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* bytes, size_t count) override {
    if (!valid || XML_Parse(parser, reinterpret_cast<const char*>(bytes), count, false) == XML_STATUS_ERROR) {
      valid = false; return 0;
    }
    return count;
  }
  bool find(Epub& epub, uint16_t spine, const char* line, uint32_t& offset) {
    if (parser) { XML_ParserFree(parser); parser = nullptr; }
    length = matched = 0; visible = normalized = matches = depth = bodyDepth = hiddenDepth = clusterSize = 0;
    valid = collecting = true;
    text(line, strlen(line)); flushCluster(); collecting = false;
    if (!valid || length < 8 || spine >= epub.getSpineItemsCount()) return false;
    prefix[0] = 0;
    for (uint16_t i = 1, j = 0; i < length; ++i) {
      while (j && needle[i] != needle[j]) j = prefix[j - 1];
      if (needle[i] == needle[j]) ++j;
      prefix[i] = j;
    }
    visible = 0;
    parser = XML_ParserCreate(nullptr);
    if (!parser) { LOG_ERR("MIG", "Cannot allocate anchor XML parser"); return false; }
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, start, end);
    XML_SetCharacterDataHandler(parser, characters);
    XML_SetDefaultHandlerExpand(parser, entity);
    auto chapter = epub.getSpineItem(spine);
    const bool streamed = epub.readItemContentsToStream(chapter.href, *this, 1024);
    const bool finished = XML_Parse(parser, nullptr, 0, true) != XML_STATUS_ERROR;
    flushCluster();
    XML_ParserFree(parser); parser = nullptr;
    if (!streamed || !finished || !valid || matches != 1) return false;
    offset = foundOffset;
    return true;
  }
};

inline bool readAt(HalFile& file, size_t position, uint8_t* bytes, size_t count) {
  return position <= file.size() && count <= file.size() - position && file.seek(position) &&
         file.read(bytes, count) == int(count);
}

inline bool cachedLine(const std::string& path, uint16_t page, char* line, size_t capacity) {
  auto file = Storage.open(path.c_str());
  uint8_t header[44];
  if (!file || !readAt(file, 0, header, sizeof(header))) return false;
  const bool modern = memcmp(header, "\xff" "CXS\x2c", 5) == 0;
  if (!modern && header[0] != 27) return false;
  const uint16_t count = u16(header + (modern ? 26 : 19));
  const uint32_t lut = u32(header + (modern ? 28 : 21));
  if (page >= count || lut < (modern ? 44u : 37u) || uint64_t(lut) + count * 4 > file.size()) return false;
  if (!readAt(file, lut + page * 4, header, 4)) return false;
  const uint32_t position = u32(header);
  uint32_t end = lut;
  if (page + 1 < count) { if (!readAt(file, lut + (page + 1) * 4, header, 4)) return false; end = u32(header); }
  if (position < (modern ? 44u : 37u) || position >= end || end > lut || !readAt(file, position, header, 14) || header[2] != 1) return false;
  const uint16_t words = u16(header + 7);
  if (!words || words > 512) return false;
  size_t used = 0;
  if (!modern) {
    size_t cursor = position + 9;
    for (unsigned i = 0; i < words; ++i) {
      if (!readAt(file, cursor, header, 4)) return false;
      const auto n = u32(header); cursor += 4;
      if (!n || n >= capacity - used || cursor + n > end || !file.seek(cursor) ||
          file.read(line + used, n) != int(n)) return false;
      used += n; cursor += n; line[used++] = ' ';
    }
  } else {
    const uint8_t bionic = header[9], guide = header[10], flags = header[11];
    if (bionic > 1 || guide > 1 || flags > 1) return false;
    const uint16_t textSize = u16(header + 12);
    const size_t arena = position + 14;
    const size_t text = arena + words * (5 + 3 * bionic + 2 * guide + flags);
    if (text + textSize > end) return false;
    for (unsigned i = 0; i < words; ++i) {
      if (!readAt(file, arena + 2 * i, header, 2)) return false;
      const uint16_t begin = u16(header);
      uint16_t finish = textSize;
      if (i + 1 < words) { if (!readAt(file, arena + 2 * (i + 1), header, 2)) return false; finish = u16(header); }
      const size_t n = finish - begin;
      if (finish <= begin || finish > textSize || n >= capacity - used ||
          !file.seek(text + begin) || file.read(line + used, n) != int(n) || line[used + n - 1]) return false;
      used += n - 1;
      if (flags) {
        if (!readAt(file, arena + words * (5 + 3 * bionic + 2 * guide) + i, header, 1)) return false;
        if (header[0] & 2) {
          if (used && line[used - 1] == '-') --used;
          else if (used >= 3 && memcmp(line + used - 3, "\xe2\x80\x90", 3) == 0) used -= 3;
        }
      }
      line[used++] = ' ';
    }
  }
  if (used >= capacity) return false;
  line[used] = 0;
  return file.close();
}
}
