#include "HalTiltSensor.h"

#include <Logging.h>

HalTiltSensor halTiltSensor;  // Singleton instance

bool HalTiltSensor::readGyro(float& gx, float& gy, float& gz) const {
  Imu::Sample sample;
  if (!_sdkImu.read(sample)) return false;
  gx = sample.gx;
  gy = sample.gy;
  gz = sample.gz;
  return true;
}

void HalTiltSensor::begin() {
  if (!_sdkImu.begin()) {
    LOG_ERR("GYR", "SDK IMU not found");
    return;
  }
  // Accelerometer-only parts cannot drive the tilt gesture.
  _available = _sdkImu.hasGyroscope();
  if (!_available) {
    if (!_sdkImu.sleep()) LOG_ERR("GYR", "IMU standby failed");
    LOG_INF("GYR", "IMU has no gyroscope");
    return;
  }
  _initMs = millis();
  _lastPollMs = _initMs;
  // begin() starts sampling. Stand by until the reader requests tilt input.
  _isAwake = true;
  deepSleep();
  LOG_INF("GYR", "SDK IMU initialized");
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

  _lastPowerAttemptMs = millis();
  if (!_sdkImu.wake()) {
    _powerStateUncertain = true;  // The SDK can have written only part of the register sequence.
    LOG_ERR("GYR", "IMU wake failed");
    return false;
  }

  _lastPollMs = millis();
  _lastTiltMs = millis();
  _wakeMs = millis();
  _isAwake = true;
  _powerStateUncertain = false;
  clearPendingEvents();
  _inTilt = false;
  return true;
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

  _lastPowerAttemptMs = millis();
  if (!_sdkImu.sleep()) {
    _powerStateUncertain = true;
    LOG_ERR("GYR", "IMU sleep failed");
    return false;
  }

  clearPendingEvents();
  _inTilt = false;
  _isAwake = false;
  _powerStateUncertain = false;
  return true;
}

void HalTiltSensor::update(const uint8_t mode, const uint8_t orientation, const bool inReader) {
  if (!_available) {
    return;
  }

  const unsigned long now = millis();
  const bool shouldBeAwake = mode >= CrossPointTiltPageTurn::TILT_NORMAL &&
                            mode <= CrossPointTiltPageTurn::TILT_FORWARD_BACK_INVERTED && inReader;
  if (!shouldBeAwake) {
    clearPendingEvents();
    _inTilt = false;
  }
  // A failed SDK sequence leaves the hardware state unknown. Retry at most
  // once per second, and never poll a sensor whose wake was not confirmed.
  if (_powerStateUncertain && (now - _lastPowerAttemptMs) < POWER_RETRY_MS) return;
  if (shouldBeAwake != _isAwake || _powerStateUncertain) {
    if (shouldBeAwake) wake();
    else deepSleep();
    return;
  }
  if (!shouldBeAwake) return;
  // Stabilization: discard readings during gyro startup transient
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  float gx, gy, gz;
  if (!readGyro(gx, gy, gz)) {
    return;
  }

  // Map the gyro axis to left/right tilt based on reader orientation.
  // On the X3 PCB: X axis = left/right in portrait, Y axis = left/right in landscape.
  float tiltAxis;
  const bool forwardBack = mode == CrossPointTiltPageTurn::TILT_FORWARD_BACK ||
                           mode == CrossPointTiltPageTurn::TILT_FORWARD_BACK_INVERTED;
  switch (orientation) {
    case CrossPointOrientation::PORTRAIT:
      tiltAxis = forwardBack ? gy : gx;
      break;
    case CrossPointOrientation::INVERTED:
      tiltAxis = forwardBack ? -gy : -gx;
      break;
    case CrossPointOrientation::LANDSCAPE_CW:
      tiltAxis = forwardBack ? gx : -gy;
      break;
    case CrossPointOrientation::LANDSCAPE_CCW:
      tiltAxis = forwardBack ? -gx : gy;
      break;
    default:
      tiltAxis = gx;
      break;
  }
  if (mode == CrossPointTiltPageTurn::TILT_INVERTED ||
      mode == CrossPointTiltPageTurn::TILT_FORWARD_BACK_INVERTED) tiltAxis = -tiltAxis;

  if (_inTilt) {
    // Wait for device to return to neutral before allowing next trigger
    if (fabsf(tiltAxis) < NEUTRAL_RATE_DPS) {
      _inTilt = false;
    }
  } else {
    // Check for new tilt gesture (with cooldown)
    if ((now - _lastTiltMs) >= COOLDOWN_MS) {
      if (tiltAxis > RATE_THRESHOLD_DPS) {
        _tiltForwardEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
      } else if (tiltAxis < -RATE_THRESHOLD_DPS) {
        _tiltBackEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
      }
    }
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool val = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool val = _tiltBackEvent;
  _tiltBackEvent = false;
  return val;
}

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}
