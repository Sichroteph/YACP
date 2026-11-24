#include "BookCacheUtils.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <functional>

#include "BookCachePolicy.h"
#include "TaskWatchdog.h"

namespace {
bool clearGeneratedFiles(const std::string& path, book_cache::Kind kind, book_cache::Directory group) {
  if (!Storage.exists(path.c_str())) return true;
  auto directory = Storage.open(path.c_str());
  if (!directory || !directory.isDirectory()) {
    directory.close();
    LOG_ERR("BookCache", "Cannot inspect cache directory: %s", path.c_str());
    return false;
  }
  char name[96];
  // One reusable path allocation on an explicit maintenance action. Never
  // build a vector of files or instantiate a reader to identify its cache.
  std::string target;
  target.reserve(path.size() + sizeof(name));
  for (auto file = directory.openNextFile(); file; file = directory.openNextFile()) {
    resetTaskWatchdogIfSubscribed();
    const bool isDirectory = file.isDirectory();
    const size_t length = file.getName(name, sizeof(name));
    file.close();
    if (isDirectory || length == 0 || length >= sizeof(name) - 1 || !book_cache::generated(kind, group, name)) continue;
    target.assign(path);
    target += '/';
    target += name;
    if (!Storage.remove(target.c_str())) {
      directory.close();
      LOG_ERR("BookCache", "Cache removal failed: %s", target.c_str());
      return false;
    }
  }
  directory.close();
  return true;
}
}  // namespace

#include "ClippingStore.h"

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  return book_cache::kind(name) != book_cache::Kind::Unknown;
}

bool clearBookCacheDirectoryPreservingUserState(const std::string& cachePath) {
  constexpr std::string_view prefix = "/.crosspoint/";
  if (cachePath.compare(0, prefix.size(), prefix)) return false;
  const auto kind = book_cache::kind(std::string_view(cachePath).substr(prefix.size()));
  if (kind == book_cache::Kind::Unknown) return false;
  // Invalidate the index first. If later cleanup stops, the next open cannot
  // accept stale metadata after a replaced book; user-state files never move.
  const char* index = kind == book_cache::Kind::Epub ? "/book.bin" :
                      kind == book_cache::Kind::Txt ? "/index.bin" : nullptr;
  if (index) {
    const auto path = cachePath + index;
    if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
      LOG_ERR("BookCache", "Could not invalidate index: %s", path.c_str());
      return false;
    }
  }
  // Only fixed, known subdirectories are visited. Unknown files/directories,
  // including all statistics versions and progress backups, are untouched.
  if (kind == book_cache::Kind::Epub &&
      (!clearGeneratedFiles(cachePath + "/sections", kind, book_cache::Directory::Sections) ||
       !clearGeneratedFiles(cachePath + "/html", kind, book_cache::Directory::Html))) return false;
  return clearGeneratedFiles(cachePath, kind, book_cache::Directory::Root);
}

bool clearBookCachePreservingUserState(const std::string& path) {
  const char* prefix = FsHelpers::hasReflowableBookExtension(path) ? "epub_" :
                       FsHelpers::hasXtcExtension(path) ? "xtc_" : nullptr;
  if (!prefix) return false;
  return clearBookCacheDirectoryPreservingUserState(std::string("/.crosspoint/") + prefix +
                                                   std::to_string(std::hash<std::string>{}(path)));
}

void clearBookCache(const std::string& path) {
  if (!FsHelpers::hasEpubExtension(path) && !FsHelpers::hasXtcExtension(path) &&
      !FsHelpers::hasTxtExtension(path) && !FsHelpers::hasMarkdownExtension(path)) return;
  if (!clearBookCachePreservingUserState(path)) {
    LOG_ERR("BookCache", "Could not completely invalidate generated cache: %s", path.c_str());
  }
}

bool removeBookFile(const std::string& path) {
  if (!Storage.remove(path.c_str())) return false;
  clearBookCache(path);
  return ClippingStore::deleteForFilePath(path, "epub");
}
