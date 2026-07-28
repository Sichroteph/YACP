// FreeInkBook — HTML named-entity to UTF-8 stream filter.

#include "text/EntityFilter.h"

#include <string.h>

namespace freeink {
namespace book {

namespace {

struct NamedEntity {
  const char* name;
  const char* utf8;
};

// The names that actually occur in books; anything rarer degrades to U+FFFD
// rather than a parse failure. Kept sorted only for readability — lookup is
// linear over a table this size.
constexpr NamedEntity kEntities[] = {
    {"aelig", "æ"},  {"agrave", "à"}, {"auml", "ä"},   {"bull", "•"},
    {"ccedil", "ç"}, {"cent", "¢"},   {"copy", "©"},   {"dagger", "†"},
    {"Dagger", "‡"}, {"deg", "°"},    {"divide", "÷"}, {"eacute", "é"},
    {"egrave", "è"}, {"euro", "€"},   {"frac12", "½"}, {"frac14", "¼"},
    {"hellip", "…"}, {"laquo", "«"},  {"ldquo", "“"},  {"lsquo", "‘"},
    {"mdash", "—"},  {"middot", "·"}, {"minus", "−"},  {"nbsp", "\xC2\xA0"},
    {"ndash", "–"},  {"ntilde", "ñ"}, {"oelig", "œ"},  {"ouml", "ö"},
    {"para", "¶"},   {"permil", "‰"}, {"plusmn", "±"}, {"pound", "£"},
    {"prime", "′"},  {"Prime", "″"},  {"raquo", "»"},  {"rdquo", "”"},
    {"reg", "®"},    {"rsquo", "’"},  {"sect", "§"},   {"shy", "\xC2\xAD"},
    {"sup2", "²"},   {"sup3", "³"},   {"szlig", "ß"},  {"times", "×"},
    {"trade", "™"},  {"uuml", "ü"},   {"yen", "¥"},
};

// XML's own entities and numeric references pass through for expat itself.
bool passesThrough(const char* name, uint8_t len) {
  if (len > 0 && name[0] == '#') return true;
  return (len == 3 && memcmp(name, "amp", 3) == 0) || (len == 2 && memcmp(name, "lt", 2) == 0) ||
         (len == 2 && memcmp(name, "gt", 2) == 0) || (len == 4 && memcmp(name, "quot", 4) == 0) ||
         (len == 4 && memcmp(name, "apos", 4) == 0);
}

bool isEntityChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '#' || (c == 'x');
}

}  // namespace

void EntityFilter::emitPending(const char* bytes, uint32_t len) {
  // Callers only queue expansions bounded by kMaxEntity + "&amp;"; the
  // pending buffer is sized so this never overflows between drains.
  if (pendingLen_ + len > sizeof(pending_)) len = sizeof(pending_) - pendingLen_;
  memcpy(pending_ + pendingLen_, bytes, len);
  pendingLen_ += static_cast<uint8_t>(len);
}

void EntityFilter::resolveEntity() {
  // entity_ holds the name without '&' or ';'.
  entity_[entityLen_] = '\0';
  if (passesThrough(entity_, entityLen_)) {
    emitPending("&", 1);
    emitPending(entity_, entityLen_);
    emitPending(";", 1);
  } else {
    for (const NamedEntity& e : kEntities) {
      if (strcmp(e.name, entity_) == 0) {
        emitPending(e.utf8, static_cast<uint32_t>(strlen(e.utf8)));
        entityLen_ = 0;
        inEntity_ = false;
        return;
      }
    }
    emitPending("�", 3);  // unknown name — degrade, don't fail the parse
  }
  entityLen_ = 0;
  inEntity_ = false;
}

int32_t EntityFilter::read(ZipEntryReader& reader, char* dst, uint32_t cap) {
  uint32_t produced = 0;
  for (;;) {
    // Drain expanded output first.
    while (pendingPos_ < pendingLen_ && produced < cap) {
      dst[produced++] = pending_[pendingPos_++];
    }
    if (pendingPos_ >= pendingLen_) pendingPos_ = pendingLen_ = 0;
    if (produced == cap) return static_cast<int32_t>(produced);

    if (rawPos_ >= rawLen_) {
      if (sourceDone_) {
        if (inEntity_) {
          // Truncated entity at end of stream: repair the '&' and flush the
          // rest literally.
          emitPending("&amp;", 5);
          emitPending(entity_, entityLen_);
          entityLen_ = 0;
          inEntity_ = false;
          continue;
        }
        return static_cast<int32_t>(produced);
      }
      const int32_t n = reader.read(raw_, kRawSize);
      if (n < 0) return -1;
      if (n == 0) {
        sourceDone_ = true;
        continue;
      }
      rawPos_ = 0;
      rawLen_ = static_cast<uint32_t>(n);
    }

    while (rawPos_ < rawLen_ && produced < cap && pendingLen_ == 0) {
      const char c = raw_[rawPos_];
      if (!inEntity_) {
        if (c == '&') {
          inEntity_ = true;
          entityLen_ = 0;
        } else {
          dst[produced++] = c;
        }
        ++rawPos_;
        continue;
      }
      if (c == ';') {
        ++rawPos_;
        resolveEntity();
        break;  // drain pending
      }
      if (!isEntityChar(c) || entityLen_ >= kMaxEntity - 2) {
        // Not an entity after all (bare '&', or something too long): repair
        // the ampersand so expat survives, replay what we swallowed.
        emitPending("&amp;", 5);
        emitPending(entity_, entityLen_);
        entityLen_ = 0;
        inEntity_ = false;
        break;  // drain pending; current byte reprocessed next pass
      }
      entity_[entityLen_++] = c;
      ++rawPos_;
    }
  }
}

}  // namespace book
}  // namespace freeink
