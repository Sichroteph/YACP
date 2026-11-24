#pragma once
#include <string>
class Epub {
 public:
  Epub(const std::string&, const char*) {}
  bool load(bool, bool) { return false; }
  std::string getTitle() const { return {}; }
  std::string getAuthor() const { return {}; }
  std::string getThumbBmpPath() const { return {}; }
};
