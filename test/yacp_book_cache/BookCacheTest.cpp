#include "BookCacheUtils.h"
#include "BookCachePolicy.h"
#include <HalStorage.h>
#include <algorithm>
#include <functional>

int main() {
  const std::string root = "/.crosspoint/epub_1";
  cacheDirectories = {"/.crosspoint", root, root + "/sections", root + "/html", root + "/unknown"};
  const std::vector<std::string> generated = {"book.bin", "spine.bin.tmp", "toc.bin.tmp", ".items.bin",
      "css_rules.cache", "css_rules.cache.tmp", "css_rules.cache.bak", "cover_original.bmp",
      "cover_original_crop.bmp", "cover_legacy_v2.bmp", "cover_legacy_v2_crop.bmp", "thumb_123.bmp",
      "img_0_1.PNG", "img_0_1.pxc", "img_22_31.jpeg", "sections/0.bin", "sections/12.bin.part",
      "html/0.html", "html/.tmp_12.html"};
  const std::vector<std::string> preserved = {"progress.bin", "progress.bin.bak", "progress.bin.tmp",
      "stats.bin", "stats_v4.bin", "stats_v5.bin", "stats_v6.bin", "stats_v6.bin.bak", "stats_v6.bin.tmp",
      "stats_v99.bin", "reader_settings.bin", "reader_profile.0", "bookmarks.bin", "clippings.bin",
      "unknown/data.bin", "sections/user.bin", "sections/notes.txt", "html/my-notes.html", "img_0_note.jpg",
      "img_0_1.doc", "thumb_custom.bmp", "cover_custom.bmp", "spine.bin", "custom.json"};
  for (const auto& name : generated) cacheFiles[root + '/' + name] = {1, 2, 3};
  for (const auto& name : preserved) cacheFiles[root + '/' + name] = {6, 5, 4};
  const auto original = cacheFiles;
  const auto directories = cacheDirectories;
  const auto assertPreserved = [&] {
    for (const auto& name : preserved) assert(cacheFiles.at(root + '/' + name) == original.at(root + '/' + name));
    assert(cacheDirectories == directories);
    for (const auto& entry : cacheOpenFiles) assert(entry.second == 0);
  };
  assert(clearBookCacheDirectoryPreservingUserState(root));
  assert(cacheRemovals.size() == generated.size() && cacheRemovals.front() == root + "/book.bin");
  assertPreserved();
  for (const auto& name : generated) assert(!cacheFiles.count(root + '/' + name));
  const auto removed = cacheRemovals.size();
  assert(clearBookCacheDirectoryPreservingUserState(root) && cacheRemovals.size() == removed);
  for (size_t failure = 1; failure <= generated.size(); ++failure) {
    cacheFiles = original;
    cacheRemovals.clear();
    cacheFailRemoval = failure;
    assert(!clearBookCacheDirectoryPreservingUserState(root));
    assert(cacheRemovals.size() == failure);  // Stop at the first error.
    assertPreserved();
    cacheFailRemoval = SIZE_MAX;
    assert(clearBookCacheDirectoryPreservingUserState(root));
    assertPreserved();
    for (const auto& name : generated) assert(!cacheFiles.count(root + '/' + name));
  }
  cacheFiles = original;
  cacheFailOpen = root + "/sections";
  cacheRemovals.clear();
  assert(!clearBookCacheDirectoryPreservingUserState(root));
  assert(cacheRemovals.size() == 1 && cacheRemovals.front() == root + "/book.bin");
  assertPreserved();
  cacheFailOpen.clear();
  for (const char* path : {"", "/", "/.crosspoint", "/.crosspoint/epub_", "/.crosspoint/epub_1/..",
                          "/.crosspoint/epub_abc", "/.crosspoint/epub_1/sections", "/elsewhere/epub_1"}) {
    const auto before = cacheFiles;
    assert(!clearBookCacheDirectoryPreservingUserState(path));
    assert(cacheFiles == before);
  }
  assert(clearBookCacheDirectoryPreservingUserState("/.crosspoint/epub_999"));
  for (const char* suffix : {"txt", "md", "xtc"}) {
    const auto path = std::string("/book.") + suffix;
    const auto cache = std::string("/.crosspoint/") + (std::string(suffix) == "xtc" ? "xtc_" : "txt_") +
                       std::to_string(std::hash<std::string>{}(path));
    cacheDirectories.insert(cache);
    cacheFiles[cache + "/progress.bin"] = {4, 3, 2, 1};
    cacheFiles[cache + "/stats_v6.bin"] = {1, 2, 3};
    cacheFiles[cache + "/cover.bmp"] = {0};
    cacheFiles[cache + "/thumb_100.bmp"] = {0};
    if (std::string(suffix) != "xtc") cacheFiles[cache + "/index.bin"] = {0};
    assert(clearBookCachePreservingUserState(path));
    assert(cacheFiles.count(cache + "/progress.bin") && cacheFiles.count(cache + "/stats_v6.bin"));
    assert(!cacheFiles.count(cache + "/cover.bmp") && !cacheFiles.count(cache + "/thumb_100.bmp"));
    assert(!cacheFiles.count(cache + "/index.bin"));
  }
  puts("PASS: selective cache invalidation, 24 preserved entries, 19 interrupted removals/retries, paths and formats");
}
