#include "util/BookPathMove.h"
#include "util/BookGallery.h"
#include "util/BookmarkUtil.h"
#include "ClippingStore.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "activities/reader/FinishedBooksIndex.h"
#include <cstdio>
#include <functional>

// Only gallery naming is stubbed; its own format is covered separately.
bool BookGallery::folderForBook(std::string_view path, char (&out)[FOLDER_CAPACITY]) {
  snprintf(out, sizeof(out), "/.book-galleries/%zu", std::hash<std::string_view>{}(path));
  return true;
}
const std::string source = "/Books/Example.epub", target = "/read/Example.epub";
std::string cache(const std::string& p) { return "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(p)); }
std::string profile(const std::string& p) { return "/.crosspoint/book_profiles/epub_" + std::to_string(std::hash<std::string>{}(p)); }
std::string gallery(const std::string& p) { char value[BookGallery::FOLDER_CAPACITY]; BookGallery::folderForBook(p, value); return value; }
void put(const std::string& path, const std::string& content) { files[path] = {content.begin(), content.end()}; }
std::string text(const std::string& path) { const auto& bytes = files.at(path); return {bytes.begin(), bytes.end()}; }
void seed() {
  for (const auto& h : handles) assert(h.second == 0);
  files.clear(); directories.clear(); handles.clear(); mutation = failAt = 0; tornWrites = historyCollision = false;
  assert(BookPathMove::recover());
  directories = {"/.crosspoint", "/Books", "/read", cache(source), profile(source), gallery(source)};
  put(source, "EPUB payload");
  put(cache(source) + "/progress.bin", "exact progress");
  put(cache(source) + "/stats.bin", "reading history");
  put(cache(source) + "/unknown.bin", "keep unknown data");
  put(profile(source) + "/a.bin", "book preferences");
  put(gallery(source) + "/image.bmp", "sleep image");
  put(BookmarkUtil::getBookmarkPath(source), "bookmarks");
  for (const char* suffix : ClippingStore::FILE_SUFFIXES) {
    put(ClippingStore::filePathForBook(source, "epub") + suffix, std::string("clipping data") + suffix);
  }
  put("/.crosspoint/recent.json", "recent:" + source);
  put("/.crosspoint/state.json", "state:" + source);
  finishedPath = RECENT_BOOKS.path = APP_STATE.openEpubPath = source;
}
void checkContent(const std::string& path) {
  assert(text(path) == "EPUB payload");
  assert(text(cache(path) + "/progress.bin") == "exact progress");
  assert(text(cache(path) + "/stats.bin") == "reading history");
  assert(text(cache(path) + "/unknown.bin") == "keep unknown data");
  assert(text(profile(path) + "/a.bin") == "book preferences");
  assert(text(gallery(path) + "/image.bmp") == "sleep image");
  assert(text(BookmarkUtil::getBookmarkPath(path)) == "bookmarks");
  for (const char* suffix : ClippingStore::FILE_SUFFIXES) {
    assert(text(ClippingStore::filePathForBook(path, "epub") + suffix) == std::string("clipping data") + suffix);
    const auto& otherPath = path == source ? target : source;
    assert(!files.count(ClippingStore::filePathForBook(otherPath, "epub") + suffix));
  }
}
bool reboot() {
  if (!BookPathMove::prepareRecovery()) return false;
  RECENT_BOOKS.path = text("/.crosspoint/recent.json").substr(7);
  APP_STATE.openEpubPath = text("/.crosspoint/state.json").substr(6);
  return BookPathMove::recover();
}
int main() {
  seed(); assert(BookPathMove::move(source, target)); checkContent(target);
  assert(finishedPath == target && RECENT_BOOKS.path == target && APP_STATE.openEpubPath == target);
  const unsigned operations = mutation;
  unsigned cases = 1;
  for (unsigned n = 1; n <= operations; ++n) {
    for (bool torn : {false, true}) {
      seed(); failAt = n; tornWrites = torn;
      assert(!BookPathMove::move(source, target));
      failAt = 0;
      if (reboot()) {
        if (files.count(target)) { checkContent(target); assert(finishedPath == target && RECENT_BOOKS.path == target && APP_STATE.openEpubPath == target); }
        else checkContent(source);
        assert(!BookPathMove::blocked());
      } else {
        // A torn intent has not authorized any rename. Keep it, and all data.
        assert(BookPathMove::blocked());
        checkContent(source);
      }
      ++cases;
    }
  }
  for (unsigned component = 0; component < 5; ++component) {
    seed();
    const std::string collision[] = {target, cache(target), BookmarkUtil::getBookmarkPath(target), profile(target), gallery(target)};
    put(collision[component], "unrelated target data");
    const auto originals = files;
    assert(!BookPathMove::move(source, target)); assert(files == originals); assert(mutation == 0); ++cases;
  }
  for (const char* suffix : ClippingStore::FILE_SUFFIXES) {
    seed();
    put(ClippingStore::filePathForBook(target, "epub") + suffix, "unrelated clippings");
    const auto originals = files;
    assert(!BookPathMove::move(source, target));
    assert(files == originals);
    assert(mutation == 0);
    ++cases;
  }
  for (const std::string& invalid : std::vector<std::string>{"/", "/read/../Example.epub", "/.hidden/Example.epub", "/read//Example.epub", "/books/example.epub", "/read/Example.xtc", std::string("/read/") + std::string(512, 'a')}) {
    seed(); const auto originals = files; assert(!BookPathMove::move(source, invalid)); assert(files == originals); ++cases;
  }
  seed(); historyCollision = true; assert(!BookPathMove::move(source, target)); checkContent(source); assert(mutation == 0); ++cases;
  // Interrupt every metadata write, then interrupt its recovery as well.
  for (unsigned n = 1; n <= operations; ++n) {
    seed(); failAt = n; BookPathMove::move(source, target); failAt = 0;
    if (!files.count("/.crosspoint/book-move.bin")) continue;
    if (!BookPathMove::prepareRecovery()) continue;
    mutation = 0; failAt = 1; reboot(); failAt = 0;
    assert(reboot()); checkContent(target); ++cases;
  }
  // The unified TXT reader stores reflowable data under epub_. Protected
  // book sidecars join the same journal so a restart cannot lose access.
  seed();
  const std::string txt = "/Books/Example.txt", movedTxt = "/read/Example.txt";
  put(txt, "text payload");
  put(txt + ".key", "key payload");
  put(txt + ".rights", "rights payload");
  directories.insert(cache(txt));
  put(cache(txt) + "/progress.bin", "converted position");
  assert(BookPathMove::move(txt, movedTxt));
  assert(text(cache(movedTxt) + "/progress.bin") == "converted position");
  assert(text(movedTxt + ".key") == "key payload");
  assert(text(movedTxt + ".rights") == "rights payload");
  ++cases;

  // Corrupt intent cannot initiate any recovery operation.
  seed(); failAt = operations; assert(!BookPathMove::move(source, target)); failAt = 0;
  files["/.crosspoint/book-move.bin"][31] ^= 1;
  const auto before = files; assert(!reboot()); assert(files == before); ++cases;
  printf("PASS: %u move/recovery/collision/fault cases, %u mutation points\n", cases, operations);
}
