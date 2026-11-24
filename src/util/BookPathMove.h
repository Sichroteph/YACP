#pragma once

#include <string>

// Called only with the reader and its file handles closed. A durable intent
// precedes any rename; boot recovery finishes that intent before opening books.
namespace BookPathMove {
bool move(const std::string& source, const std::string& target);
// Before loading APP_STATE/RECENT_BOOKS: restore the checked pre-move snapshots.
bool hasPending();
bool prepareRecovery();
bool recover();
bool blocked();
}  // namespace BookPathMove
