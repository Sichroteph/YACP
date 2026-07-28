#pragma once

// FreeInk SDK — FreeInkBook umbrella header.
//
// FreeInkBook turns an EPUB container on external storage into paginated
// pages for e-paper panels. This is the Phase 1 surface: opening a book,
// reading its metadata, spine, and table of contents, and streaming raw item
// content. Layout, pagination, and rendering arrive in later phases (see
// docs/freeink-book-design.md).
//
// Memory model: everything the open book retains (ZIP catalog, metadata,
// manifest, spine, TOC) lives in the caller-supplied book arena and stays
// valid until that arena resets. Transient parse state uses the scratch
// arena and is released before each call returns. The engine itself never
// calls malloc; the vendored XML parser's bounded internal heap use is
// confined to open/build time.
//
// Freestanding C++17 — no Arduino or ESP-IDF dependency; the full pipeline
// runs in host-side unit tests.

#include <stddef.h>

#include "BookArena.h"
#include "BookStorage.h"
#include "BookTypes.h"
#include "epub/ZipCatalog.h"

namespace freeink {
namespace book {

class Book {
 public:
  // Parses the container structure (ZIP directory, OPF package, TOC). The
  // source must outlive the Book; the book arena must stay live and unreset
  // for as long as the returned data is used.
  BookStatus open(BookSource& source, Arena& bookArena, Arena& scratch);

  const BookMetadata& metadata() const { return metadata_; }

  size_t manifestCount() const { return manifestCount_; }
  const ManifestItem* manifestItem(size_t index) const {
    return index < manifestCount_ ? &manifest_[index] : nullptr;
  }

  // Spine = reading order. spineItem(i) resolves to the manifest entry.
  size_t spineCount() const { return spineCount_; }
  const ManifestItem* spineItem(size_t index) const {
    return index < spineCount_ ? &manifest_[spine_[index]] : nullptr;
  }

  size_t tocCount() const { return tocCount_; }
  const TocEntry* tocEntry(size_t index) const {
    return index < tocCount_ ? &toc_[index] : nullptr;
  }

  // Opens a manifest item for streaming. Reader buffers come from `scratch`;
  // take a mark first and release it after the last read().
  BookStatus openItem(const ManifestItem& item, ZipEntryReader* reader, Arena& scratch) const;

  const ZipCatalog& zip() const { return zip_; }

 private:
  ZipCatalog zip_;
  BookSource* source_ = nullptr;
  BookMetadata metadata_;
  ManifestItem* manifest_ = nullptr;
  size_t manifestCount_ = 0;
  uint16_t* spine_ = nullptr;
  size_t spineCount_ = 0;
  TocEntry* toc_ = nullptr;
  size_t tocCount_ = 0;
};

}  // namespace book
}  // namespace freeink
