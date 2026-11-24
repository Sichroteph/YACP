#include "util/BootShortcut.h"
using boot_shortcut::Action;
using boot_shortcut::forBoot;
constexpr uint8_t BACK = 1u << 0;
constexpr uint8_t UP = 1u << 4;
constexpr uint8_t DOWN = 1u << 5;
// The error screen uses a software reset, without a PowerButton wake reason.
static_assert(forBoot(UP, UP, false, false) == Action::FirmwareUpdate);
static_assert(forBoot(UP, UP, true, false) == Action::FirmwareUpdate);
// Power alone keeps the retry path; unstable samples never select recovery.
static_assert(forBoot(0, 0, false, false) == Action::None);
static_assert(forBoot(UP, 0, false, false) == Action::None);
static_assert(forBoot(0, UP, false, false) == Action::None);
// A reader's silent restart and normal buttons cannot enter recovery.
static_assert(forBoot(UP, UP, false, true) == Action::None);
static_assert(forBoot(UP, UP, true, true) == Action::None);
static_assert(forBoot(BACK, BACK, false, false) == Action::None);
static_assert(forBoot(DOWN, DOWN, false, false) == Action::None);
static_assert(forBoot(BACK, BACK, true, false) == Action::SafeHome);
static_assert(forBoot(DOWN, DOWN, true, false) == Action::JoinNetwork);
int main() {}
