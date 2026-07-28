#pragma once

// FreeInk SDK — HTML named-entity filter for FreeInkBook chapter streams.
//
// EPUB chapters routinely use XHTML named entities (&nbsp; &mdash; &hellip;)
// whose definitions live in a DTD the engine deliberately never fetches. A
// strict XML parser rejects them, so chapter bytes pass through this filter
// before expat: known names become their UTF-8 characters, the five XML
// built-ins and numeric references pass through untouched, unknown names
// become U+FFFD, and bare ampersands are repaired to &amp; instead of killing
// the parse. Entities split across read boundaries are handled by a small
// carry buffer; the filter allocates nothing.
//
// Package documents (OPF/NCX/nav) are parsed strictly, without this filter.

#include <stdint.h>

#include "epub/ZipCatalog.h"

namespace freeink {
namespace book {

class EntityFilter {
 public:
  void reset() {
    entityLen_ = 0;
    inEntity_ = false;
    pendingLen_ = pendingPos_ = 0;
    rawPos_ = rawLen_ = 0;
    sourceDone_ = false;
  }

  // Pulls from `reader` and produces filtered bytes. Returns bytes produced,
  // 0 at end of stream, or a negative value on read error.
  int32_t read(ZipEntryReader& reader, char* dst, uint32_t cap);

 private:
  void resolveEntity();          // entity buffer → pending output
  void emitPending(const char* bytes, uint32_t len);

  static constexpr uint32_t kMaxEntity = 12;   // longest name + '&' + ';'
  static constexpr uint32_t kRawSize = 512;

  char entity_[kMaxEntity + 1];
  uint8_t entityLen_ = 0;
  bool inEntity_ = false;

  // Expanded output that did not fit the caller's buffer yet.
  char pending_[32];
  uint8_t pendingLen_ = 0;
  uint8_t pendingPos_ = 0;

  char raw_[kRawSize];
  uint32_t rawPos_ = 0;
  uint32_t rawLen_ = 0;
  bool sourceDone_ = false;
};

}  // namespace book
}  // namespace freeink
