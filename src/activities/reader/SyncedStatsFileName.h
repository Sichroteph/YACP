#pragma once

#include <cstddef>
#include <cstring>

namespace nearby_stats {
inline bool canonicalFileName(const char* name) {
  if (!name || strlen(name) != 23 || strncmp(name,"device_",7) || strcmp(name + 19,".bin")) return false;
  for (size_t i = 7; i < 19; ++i) {
    if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
  }
  return true;
}
}  // namespace nearby_stats
