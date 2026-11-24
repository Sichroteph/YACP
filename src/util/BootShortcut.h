#pragma once

#include <cstdint>

// Physical C3 ladder indices, before settings or orientation remapping.
namespace boot_shortcut {
enum class Action : uint8_t { None, SafeHome, FirmwareUpdate, JoinNetwork };

constexpr Action fromSamples(const uint8_t first, const uint8_t second) {
  const uint8_t stable = first & second;
  if (stable & (1u << 0)) return Action::SafeHome;
  if (stable & (1u << 4)) return Action::FirmwareUpdate;
  if (stable & (1u << 5)) return Action::JoinNetwork;
  return Action::None;
}
// Firmware recovery must remain reachable after an error-screen software reset.
// Other shortcuts keep their normal power-wake policy; silent reader restarts
// must never interpret a still-held reading button as a boot command.
constexpr Action forBoot(const uint8_t first, const uint8_t second, const bool powerWake, const bool silent) {
  if (silent) return Action::None;
  const auto action = fromSamples(first, second);
  return powerWake || action == Action::FirmwareUpdate ? action : Action::None;
}
}  // namespace boot_shortcut
