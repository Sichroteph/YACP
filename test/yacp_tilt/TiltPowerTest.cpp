#include <cassert>
#include <cstdio>
#include "HalTiltSensor.h"

unsigned long testMillis = 0;

static void poll(HalTiltSensor& sensor, const uint8_t mode, const bool reader, const unsigned advance = 50) {
  testMillis += advance;
  sensor.update(mode, 0, reader);
}

int main() {
  Imu::reset();
  HalTiltSensor sensor;
  sensor.begin();
  assert(Imu::begins == 1 && Imu::sleeps == 1 && Imu::wakes == 0);
  for (unsigned i = 0; i < 500; ++i) poll(sensor, 1, false);
  assert(Imu::wakes == 0 && Imu::reads == 0 && Imu::sleeps == 1);
  poll(sensor, 1, true);
  assert(Imu::wakes == 1 && Imu::reads == 0);
  Imu::sample.gx = 300;
  poll(sensor, 1, true, 200);
  assert(Imu::reads == 0);
  poll(sensor, 1, true, 400);
  assert(sensor.wasTiltedForward() && !sensor.wasTiltedForward());
  assert(!sensor.wasTiltedBack());
  // A held gesture cannot turn pages repeatedly.
  poll(sensor, 1, true, 1000);
  assert(!sensor.wasTiltedForward());
  Imu::sample.gx = 0;
  poll(sensor, 1, true);
  Imu::sample.gx = 300;
  poll(sensor, 2, true, 600);
  assert(sensor.wasTiltedBack());
  // Leaving the reader clears activity and gestures, with one standby call.
  poll(sensor, 1, false);
  assert(!sensor.hadActivity() && !sensor.wasTiltedBack());
  const auto readCount = Imu::reads;
  for (unsigned i = 0; i < 500; ++i) poll(sensor, 1, false);
  assert(Imu::wakes == 1 && Imu::sleeps == 2 && Imu::reads == readCount);
  poll(sensor, 1, true);
  poll(sensor, 0, true);
  assert(Imu::sleeps == 3 && Imu::wakes == 2);

  // Standby failure during initialization is retried, not treated as sleep.
  Imu::reset();
  HalTiltSensor initFailure;
  Imu::sleepOk = false;
  initFailure.begin();
  for (unsigned i = 0; i < 19; ++i) poll(initFailure, 0, false);
  assert(Imu::sleeps == 1 && Imu::reads == 0);
  Imu::sleepOk = true;
  poll(initFailure, 0, false);
  assert(Imu::sleeps == 2);
  for (unsigned i = 0; i < 100; ++i) poll(initFailure, 0, false);
  assert(Imu::sleeps == 2);

  // A partially failed wake must also be put back to sleep on exit.
  Imu::reset();
  HalTiltSensor wakeFailure;
  wakeFailure.begin();
  Imu::wakeOk = false;
  poll(wakeFailure, 1, true);
  for (unsigned i = 0; i < 19; ++i) poll(wakeFailure, 1, true);
  assert(Imu::wakes == 1 && Imu::reads == 0);
  poll(wakeFailure, 1, false);
  assert(Imu::sleeps == 2 && Imu::reads == 0);
  Imu::wakeOk = true;
  poll(wakeFailure, 1, true);
  assert(Imu::wakes == 2);
  Imu::sleepOk = false;
  poll(wakeFailure, 1, false);
  for (unsigned i = 0; i < 19; ++i) poll(wakeFailure, 1, false);
  assert(Imu::sleeps == 3 && Imu::reads == 0);
  Imu::sleepOk = true;
  poll(wakeFailure, 1, false);
  assert(Imu::sleeps == 4);

  Imu::reset();
  Imu::available = false;
  HalTiltSensor absent;
  absent.begin();
  for (unsigned i = 0; i < 100; ++i) poll(absent, 1, true);
  assert(Imu::wakes == 0 && Imu::sleeps == 0 && Imu::reads == 0);
  // For each orientation, a positive physical gesture has the legacy YACP
  // sign. Modes 1/2 remain the unmodified CrossPoint left/right convention.
  for (uint8_t orientation = 0; orientation < 4; ++orientation) {
    for (uint8_t mode = 1; mode <= 4; ++mode) {
      Imu::reset();
      HalTiltSensor axis;
      axis.begin();
      axis.update(mode, orientation, true);
      if (mode <= 2) {
        if (orientation % 2 == 0) Imu::sample.gx = orientation == 0 ? 300 : -300;
        else Imu::sample.gy = orientation == 1 ? -300 : 300;
      } else {
        if (orientation % 2 == 0) Imu::sample.gy = orientation == 0 ? 300 : -300;
        else Imu::sample.gx = orientation == 1 ? 300 : -300;
      }
      testMillis += 600;
      axis.update(mode, orientation, true);
      assert(axis.wasTiltedForward() == (mode == 1 || mode == 3));
      assert(axis.wasTiltedBack() == (mode == 2 || mode == 4));
    }
  }
  std::puts("Tilt lifecycle, debounce, SDK failures and absent IMU passed");
}
