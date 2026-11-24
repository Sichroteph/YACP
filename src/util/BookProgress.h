#pragma once

#include <string>

// Saved reading percentage, or -1 when unavailable. Never builds reading caches.
// YACP Home includes the displayed EPUB page, as its reader does. Other
// callers retain CrossPoint's percentage of pages preceding that page.
int loadBookProgress(const std::string& path, bool includeDisplayedPage = false);
