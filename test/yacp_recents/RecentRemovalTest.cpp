#include "../../src/RecentBooksStore.h"
#include <cassert>
#include <cstdio>

int main() {
  JsonDocument doc;
  auto books = doc["books"].to<JsonArray>();
  for (int i = 0; i < 18; ++i) {
    auto book = books.add<JsonObject>();
    book["path"] = "/book-" + std::to_string(i) + ".epub";
    book["title"] = "Title " + std::to_string(i);
    book["author"] = "Author " + std::to_string(i);
    book["coverBmpPath"] = "/cover-" + std::to_string(i) + ".bmp";
  }
  std::string expected;
  serializeJson(doc, expected);
  for (int selected = 0; selected < 18; ++selected) {
    assert(RECENT_BOOKS.fromJson(doc.as<JsonVariantConst>()));
    const std::string path = "/book-" + std::to_string(selected) + ".epub";
    persistOk = false;
    assert(!RECENT_BOOKS.removeByPath(path));
    JsonDocument failed;
    RECENT_BOOKS.toJson(failed);
    std::string actual;
    serializeJson(failed, actual);
    assert(actual == expected);
    persistOk = true;
    assert(RECENT_BOOKS.removeByPath(path));
    assert(RECENT_BOOKS.getCount() == 17);
    assert(!RECENT_BOOKS.removeByPath(path));
    size_t index = 0;
    for (const auto& book : RECENT_BOOKS.getBooks()) {
      if (static_cast<int>(index) == selected) ++index;
      assert(book.path == std::string(doc["books"][index]["path"].as<const char*>()));
      assert(book.title == std::string(doc["books"][index]["title"].as<const char*>()));
      assert(book.author == std::string(doc["books"][index]["author"].as<const char*>()));
      assert(book.coverBmpPath == std::string(doc["books"][index]["coverBmpPath"].as<const char*>()));
      ++index;
    }
  }
  puts("PASS: removal, missing entry and failed-save rollback at all 18 positions");
  for (int selected = 0; selected < 18; ++selected) {
    const std::string source = "/book-" + std::to_string(selected) + ".epub";
    const std::string target = "/read/book-" + std::to_string(selected) + ".epub";
    books[selected]["coverBmpPath"] = "/.crosspoint/epub_123/thumb.bmp";
    assert(RECENT_BOOKS.fromJson(doc.as<JsonVariantConst>()));
    persistOk = false;
    assert(!RECENT_BOOKS.updatePath(source, target, "/.crosspoint/epub_123", "/.crosspoint/epub_456"));
    assert(RECENT_BOOKS.getBooks()[selected].path == source);
    assert(RECENT_BOOKS.getBooks()[selected].coverBmpPath == "/.crosspoint/epub_123/thumb.bmp");
    persistOk = true;
    assert(RECENT_BOOKS.updatePath(source, target, "/.crosspoint/epub_123", "/.crosspoint/epub_456"));
    assert(RECENT_BOOKS.getBooks()[selected].path == target);
    assert(RECENT_BOOKS.getBooks()[selected].coverBmpPath == "/.crosspoint/epub_456/thumb.bmp");
    assert(RECENT_BOOKS.updatePath(source, target, "/.crosspoint/epub_123", "/.crosspoint/epub_456"));
  }
  books[0]["coverBmpPath"] = "/.crosspoint/epub_1234/thumb.bmp";
  assert(RECENT_BOOKS.fromJson(doc.as<JsonVariantConst>()));
  assert(RECENT_BOOKS.updatePath("/book-0.epub", "/read/book-0.epub", "/.crosspoint/epub_123", "/.crosspoint/epub_456"));
  assert(RECENT_BOOKS.getBooks()[0].coverBmpPath == "/.crosspoint/epub_1234/thumb.bmp");
  puts("PASS: rename, failed-save rollback and retry at all 18 positions; cover prefix boundary");
}
