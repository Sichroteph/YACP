#pragma once
#include <string>
#include <vector>
struct RecentBook {
  std::string path, title, author;
};
struct TestRecentBooks {
  std::vector<RecentBook> books;
  const std::vector<RecentBook>& getBooks() const { return books; }
};
inline TestRecentBooks RECENT_BOOKS;
