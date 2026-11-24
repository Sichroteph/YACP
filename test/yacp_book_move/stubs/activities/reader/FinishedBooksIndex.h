#pragma once
#include "HalStorage.h"
inline std::string finishedPath;
inline bool historyCollision = false;
struct FinishedBooksIndex {
  static bool canMigratePath(const std::string&, const std::string&) { return !historyCollision; }
  static bool migratePath(const std::string& from, const std::string& to) {
    if (finishedPath != from) return true;
    if (fail()) return false;
    finishedPath = to;
    return true;
  }
};
