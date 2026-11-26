#include "RecentBooksStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

void RecentBooksStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : recentBooks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }
}

bool RecentBooksStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'books' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  recentBooks.clear();
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  recentBooks.reserve(std::min(arr.size(), static_cast<size_t>(MAX_RECENT_BOOKS)));
  for (JsonObjectConst obj : arr) {
    if (getCount() >= MAX_RECENT_BOOKS) break;
    RecentBook book;
    book.path = obj["path"] | "";
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.coverBmpPath = obj["coverBmpPath"] | "";
    recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", getCount());
  return true;
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath, const bool resuming) {
  if (resuming && !recentBooks.empty()) {
    const auto& first = recentBooks.front();
    if (first.path == path && first.title == title && first.author == author && first.coverBmpPath == coverBmpPath) {
      return;
    }
  }

  // Drop stale entries first so a new add can't evict a valid book in their stead.
  pruneMissing();

  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    // Move the existing entry to the front instead of copying it. The reader calls this after its first
    // render, so fresh string buffers would land among reader allocations and outlive them, splitting the
    // largest free block once the reader exits.
    std::rotate(recentBooks.begin(), it, it + 1);
    RecentBook& book = recentBooks.front();
    if (book.title != title) book.title = title;
    if (book.author != author) book.author = author;
    if (book.coverBmpPath != coverBmpPath) book.coverBmpPath = coverBmpPath;
  } else {
    recentBooks.insert(recentBooks.begin(), {path, title, author, coverBmpPath});
    if (recentBooks.size() > MAX_RECENT_BOOKS) {
      recentBooks.resize(MAX_RECENT_BOOKS);
    }
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  const size_t index = static_cast<size_t>(it - recentBooks.begin());
  RecentBook removed = std::move(*it);
  recentBooks.erase(it);
  if (!saveToFile()) {
    recentBooks.insert(recentBooks.begin() + index, std::move(removed));
    LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
    return false;
  }
  return true;
}

bool RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });
  if (it == recentBooks.end()) {
    return true;
  }
  const auto previousCover = it->coverBmpPath;
  it->path = newPath;
  if (!oldCachePath.empty() && it->coverBmpPath.size() > oldCachePath.size() &&
      it->coverBmpPath.rfind(oldCachePath, 0) == 0 && it->coverBmpPath[oldCachePath.size()] == '/') {
    it->coverBmpPath = newCachePath + it->coverBmpPath.substr(oldCachePath.size());
  }
  if (saveToFile()) return true;
  it->path = oldPath;
  it->coverBmpPath = previousCover;
  LOG_ERR("RBS", "Failed to persist renamed recent-book path");
  return false;
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

bool RecentBooksStore::pruneMissing() {
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &isMissing), recentBooks.end());
  return recentBooks.size() != before;
}
