#pragma once
#include "HalStorage.h"
struct FakeRecents {
  std::string path;
  bool updatePath(const std::string& from, const std::string& to, const std::string&, const std::string&) {
    if (path != from) return true;
    if (fail()) { files["/.crosspoint/recent.json"] = {'{'}; return false; }
    path = to;
    const auto json = "recent:" + to;
    files["/.crosspoint/recent.json"] = {json.begin(), json.end()};
    return true;
  }
};
inline FakeRecents RECENT_BOOKS;
