#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <Wire.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;
  std::atomic<bool> displayBusyWait{false};

  // I2C fuel gauge configuration for X3 battery monitoring
  bool _batteryUseI2C = false;            // True if using I2C fuel gauge (X3), false for ADC (X4)
  mutable int _batteryCachedPercent = 0;  // Last read battery percentage * 10 (0-1000); callers divide by 10 (ADC/X4
                                          // path only — I2C/X3 path stores 0-100 directly)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds
  mutable bool _batteryHasValidSample = false;

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode

 public:
  static constexpr int LOW_POWER_FREQ = 10;                    // MHz
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms
  static constexpr unsigned long BATTERY_POLL_MS = 1500;       // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // The e-ink controller owns refresh timing while BUSY is asserted. These
  // hooks temporarily permit the low CPU frequency even though the render task
  // otherwise holds a normal-speed lock.
  void beginDisplayBusyWait();
  void endDisplayBusyWait();

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the currentLockMode
  void startDeepSleep(HalGPIO& gpio) const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;

  // Read the X3 fuel gauge's filtered signed current in mA.
  // Returns false on devices without a BQ27220 or when the I2C read fails.
  bool getBatteryAverageCurrent(int16_t& outCurrentMa) const;

  // Reuse the latest value without touching the ADC or I2C bus.
  bool getCachedBatteryPercentage(uint16_t& outPercentage) const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
