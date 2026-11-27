#pragma once

#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace reading_data {
inline constexpr char fallbackRoot[] = "/.yacp-reading-v1";
inline bool isolated = false;

// HAL callers hold the storage lock. Two fixed buffers support rename without
// heap churn or large stack buffers on page turns in the isolated session.
inline const char* resolve(const char* path, unsigned slot = 0) {
  constexpr char original[] = "/.crosspoint";
  constexpr size_t length = sizeof(original) - 1;
  if (!isolated || !path || strncmp(path, original, length) || (path[length] && path[length] != '/')) return path;
#ifdef SIMULATOR
  static thread_local char paths[2][512];
#else
  static char paths[2][512];
#endif
  const int size = snprintf(paths[slot], sizeof(paths[slot]), "%s%s", fallbackRoot, path + length);
  if (size < 0 || static_cast<size_t>(size) >= sizeof(paths[slot])) {
    LOG_ERR("SD", "Isolated reading-data path is too long");
    return "";
  }
  return paths[slot];
}
}  // namespace reading_data
