#pragma once

// FreeInk SDK — EPUB package parsing for FreeInkBook.
//
// Streaming parsers for the small XML documents that describe a book:
// META-INF/container.xml, the OPF package (metadata/manifest/spine), and the
// table of contents (EPUB 3 nav document or EPUB 2 NCX). Variable-size
// results use a two-pass parse — count, allocate exact arrays from the book
// arena, fill — so nothing is over-reserved and nothing reallocates. The
// documents are tiny, so the second inflate is cheap.

#include <stddef.h>

#include "BookArena.h"
#include "BookStorage.h"
#include "BookTypes.h"
#include "epub/ZipCatalog.h"

namespace freeink {
namespace book {

struct PackageResult {
  BookMetadata metadata;
  ManifestItem* manifest = nullptr;
  size_t manifestCount = 0;
  uint16_t* spine = nullptr;  // indices into manifest, reading order
  size_t spineCount = 0;
  int navIndex = -1;  // manifest index of the EPUB 3 nav document, or -1
  int ncxIndex = -1;  // manifest index of the EPUB 2 NCX, or -1
};

// Reads META-INF/container.xml and returns the OPF path (arena-owned).
BookStatus parseContainer(BookSource& source, const ZipEntry& entry, Arena& bookArena,
                          Arena& scratch, const char** opfPathOut);

// Reads the OPF package. `opfDir` is the directory of the OPF inside the
// container ("" for root); manifest hrefs are resolved against it.
BookStatus parsePackage(BookSource& source, const ZipEntry& entry, const char* opfDir,
                        Arena& bookArena, Arena& scratch, PackageResult* out);

// Reads a table of contents into arena-owned entries. `docDir` is the
// directory of the TOC document; hrefs are resolved against it.
BookStatus parseNavToc(BookSource& source, const ZipEntry& entry, const char* docDir,
                       Arena& bookArena, Arena& scratch, TocEntry** entriesOut,
                       size_t* countOut);
BookStatus parseNcxToc(BookSource& source, const ZipEntry& entry, const char* docDir,
                       Arena& bookArena, Arena& scratch, TocEntry** entriesOut,
                       size_t* countOut);

// Path helpers shared by the parsers (exposed for tests).
//
// resolveHref percent-decodes `href`, resolves it against `baseDir` handling
// "." and "..", strips any #fragment (returned separately, arena-owned), and
// returns the container-absolute path (arena-owned) — or nullptr on overflow
// or arena exhaustion.
const char* resolveHref(Arena& arena, const char* baseDir, const char* href,
                        const char** fragmentOut);

// Writes the directory part of `path` (no trailing slash, "" if none) into
// `out`. Returns false if it does not fit.
bool dirName(const char* path, char* out, size_t outCap);

}  // namespace book
}  // namespace freeink
