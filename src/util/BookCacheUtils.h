#pragma once

#include <string>

// Clears recognized generated reading-cache files. User state and unknown
// entries remain in place, including when a removal fails or is interrupted.
void clearBookCache(const std::string& path);
bool clearBookCachePreservingUserState(const std::string& path);
bool clearBookCacheDirectoryPreservingUserState(const std::string& cachePath);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);

// Delete a local book and its local clipping state after the file is removed.
bool removeBookFile(const std::string& path);
