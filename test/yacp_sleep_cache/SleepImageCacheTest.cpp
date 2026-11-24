#include "SleepImageCache.h"
#include <HalStorage.h>
#include <array>
#include <iostream>

int main() {
  using Cache = SleepImageCache;
  Cache::Spec spec{8, 64, 64, 1, 2, 0, 3, 0, 0};
  files["/sleep.bmp"] = std::vector<uint8_t>(901, 75);
  std::array<std::array<uint8_t, 64>, 3> planes{};
  for (unsigned p = 0; p < 3; ++p)
    for (unsigned i = 0; i < 64; ++i) planes[p][i] = static_cast<uint8_t>(i * 3 + p * 11);
  Cache cache;
  assert(cache.prepare("/sleep.bmp", spec));
  const std::string path = cache.path();
  assert(!cache.validate());
  for (const auto& plane : planes) assert(cache.appendPlane(plane.data(), plane.size()));
  assert(cache.validate());
  const auto complete = files[path];
  assert(complete.size() == 48 + 3 * 64);
  for (size_t cut = 0; cut < complete.size(); ++cut) {
    files[path] = {complete.begin(), complete.begin() + cut};
    assert(!cache.validate());
  }
  for (size_t byte = 0; byte < complete.size(); ++byte) {
    files[path] = complete;
    files[path][byte] ^= 1;
    assert(!cache.validate());
  }
  files[path] = complete;
  Cache reopened;
  assert(reopened.prepare("/sleep.bmp", spec));
  const auto writesBefore = writes;
  assert(reopened.validate());
  std::array<uint8_t, 64> frame{};
  for (unsigned p = 0; p < 3; ++p) {
    assert(reopened.readPlane(p, frame.data(), frame.size()));
    assert(frame == planes[p]);
  }
  assert(writes == writesBefore && !reopened.readPlane(3, frame.data(), frame.size()));
  // Production can borrow its framebuffer as fingerprint scratch before it is painted.
  // This changes I/O chunking, never the cache bytes or validation semantics.
  std::array<uint8_t, 1024> scratch{};
  Cache borrowed;
  reads = 0;
  assert(borrowed.prepare("/sleep.bmp", spec, scratch.data(), scratch.size()));
  assert(reads == 1);
  reads = 0;
  assert(borrowed.validate(scratch.data(), scratch.size()));
  assert(reads == 4);
  for (unsigned p = 0; p < 3; ++p) {
    assert(borrowed.readPlane(p, frame.data(), frame.size()));
    assert(frame == planes[p]);
  }
  // Every interrupted write is rejected, including an incomplete commit header.
  for (int budget = 0; budget < 288; ++budget) {
    Cache writer;
    assert(writer.prepare("/sleep.bmp", spec));
    writeBudget = budget;
    for (const auto& plane : planes) writer.appendPlane(plane.data(), plane.size());
    writeBudget = -1;
    assert(!writer.validate());
    assert(openFiles == 0);
  }
  files[path] = complete;
  files["/sleep.bmp"][500] ^= 1;  // Same-size source change must invalidate.
  Cache changed;
  assert(changed.prepare("/sleep.bmp", spec) && !changed.validate());
  files["/sleep.bmp"][500] ^= 1;
  for (unsigned field = 0; field < 9; ++field) {
    auto other = spec;
    switch (field) {
      case 0: ++other.width; break;
      case 1: ++other.height; break;
      case 2: ++other.bufferBytes; break;
      case 3: ++other.controller; break;
      case 4: ++other.mode; break;
      case 5: ++other.orientation; break;
      case 6: ++other.filter; break;
      case 7: ++other.coverMode; break;
      case 8: ++other.fadingFix; break;
    }
    Cache different;
    assert(different.prepare("/sleep.bmp", other) && !different.validate());
  }
  failRead = true;
  assert(!cache.validate() && !cache.readPlane(0, frame.data(), frame.size()));
  assert(!changed.prepare("/sleep.bmp", spec));
  failRead = false;
  failClose = true;
  assert(!changed.prepare("/sleep.bmp", spec));
  failClose = false;
  failOpen = true;
  assert(!changed.prepare("/sleep.bmp", spec));
  failOpen = false;
  assert(changed.prepare("/sleep.bmp", spec));
  failClose = true;
  assert(!changed.appendPlane(planes[0].data(), 64));
  failClose = false;
  assert(!changed.validate());
  assert(openFiles == 0);
  std::cout << "Sleep image cache: 240 truncations, 240 corruptions, 288 interrupted writes, nine key changes and storage faults passed\n";
}
