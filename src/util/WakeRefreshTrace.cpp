#include "WakeRefreshTrace.h"

#ifdef YACP_X4PRO_WAKE_TRACE
#include <Arduino.h>
#include <HalStorage.h>

#include <atomic>
#include <cstdio>

namespace {
struct Entry {
  std::atomic<const char*> name{nullptr};
  unsigned long ms;
  int a, b, c;
};
constexpr unsigned CAPACITY = 128;
Entry entries[CAPACITY];
std::atomic<unsigned> next{0};
}

namespace wake_refresh_trace {
void event(const char* name, int a, int b, int c) {
  const auto index = next.fetch_add(1, std::memory_order_relaxed);
  if (index >= CAPACITY) return;
  auto& entry = entries[index];
  entry.ms = millis();
  entry.a = a;
  entry.b = b;
  entry.c = c;
  entry.name.store(name, std::memory_order_release);
}

void begin(const char* phase) {
  for (auto& entry : entries) entry.name.store(nullptr, std::memory_order_relaxed);
  next.store(0, std::memory_order_relaxed);
  event(phase);
}

void save(const char* path) {
  HalFile file;
  if (!Storage.openFileForWrite("WAKETRACE", path, file)) return;
  const auto count = next.load(std::memory_order_acquire);
  for (unsigned index = 0; index < count && index < CAPACITY; ++index) {
    const auto& entry = entries[index];
    const auto* name = entry.name.load(std::memory_order_acquire);
    if (!name) continue;
    char line[112];
    const int length = snprintf(line, sizeof(line), "%u %lu %s %d %d %d\n",
                                index, entry.ms, name, entry.a, entry.b, entry.c);
    if (length > 0 && length < static_cast<int>(sizeof(line))) {
      file.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(length));
    }
  }
  if (count > CAPACITY) {
    constexpr unsigned char overflow[] = "trace_overflow\n";
    file.write(overflow, sizeof(overflow) - 1);
  }
  file.close();
}
}

extern "C" void yacpWakeTrace(const char* name, int a, int b, int c) {
  wake_refresh_trace::event(name, a, b, c);
}
#endif
