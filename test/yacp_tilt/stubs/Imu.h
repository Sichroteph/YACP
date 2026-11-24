#pragma once

class Imu {
 public:
  struct Sample { float ax, ay, az, gx, gy, gz; };
  static inline bool available = true, sleepOk = true, wakeOk = true, readOk = true;
  static inline unsigned begins = 0, sleeps = 0, wakes = 0, reads = 0;
  static inline Sample sample{};
  bool begin() { ++begins; return available; }
  bool hasGyroscope() const { return available; }
  bool sleep() { ++sleeps; return sleepOk; }
  bool wake() { ++wakes; return wakeOk; }
  bool read(Sample& out) { ++reads; out = sample; return readOk; }
  static void reset() {
    available = sleepOk = wakeOk = readOk = true;
    begins = sleeps = wakes = reads = 0;
    sample = {};
  }
};
