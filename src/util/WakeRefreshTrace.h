#pragma once

namespace wake_refresh_trace {
#ifdef YACP_X4PRO_WAKE_TRACE
void event(const char* name, int a = 0, int b = 0, int c = 0);
void begin(const char* phase);
void save(const char* path);
#else
inline void event(const char*, int = 0, int = 0, int = 0) {}
inline void begin(const char*) {}
inline void save(const char*) {}
#endif
}
