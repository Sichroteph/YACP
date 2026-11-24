#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#define private public
#include "FreeInkDisplay.h"
#include "src/driver/Uc8279X4Driver.h"
#undef private

using namespace freeink;

static size_t count(const EpdBus& bus, uint8_t command) {
  return std::count_if(bus.writes.begin(), bus.writes.end(),
                       [command](const auto& write) { return write.command == command; });
}

static size_t index(const EpdBus& bus, uint8_t command) {
  const auto found = std::find_if(bus.writes.begin(), bus.writes.end(),
                                  [command](const auto& write) { return write.command == command; });
  assert(found != bus.writes.end());
  return found - bus.writes.begin();
}

static void checkPlane(const EpdBus& bus, uint8_t command, const std::vector<uint8_t>& image) {
  const auto& bytes = bus.writes[index(bus, command)].bytes;
  assert(bytes.size() == 60000);
  assert(std::all_of(bytes.begin(), bytes.begin() + 12000, [](uint8_t byte) { return byte == 0xFF; }));
  assert(std::equal(image.begin(), image.end(), bytes.begin() + 12000));
}

int main() {
  BoardConfig::ACTIVE.board = BoardConfig::Board::XteinkX4Pro;
  BoardConfig::ACTIVE.displayController = BoardConfig::DisplayController::UC8279;
  BoardConfig::ACTIVE.displayControllerVariant = 0x68;
  Uc8279X4Driver driver;
  EpdBus bus;
  std::vector<uint8_t> first(48000, 0x33), next(48000, 0xA6);

  // No speculative waveform against an unknown physical baseline.
  assert(!driver.displayMonochromeReinforcement(bus, first.data(), true));
  assert(bus.writes.empty());
  driver.display(bus, first.data(), nullptr, RefreshMode::Full, true);
  assert(driver._oldPlaneValid && !driver._needFullClear && !driver._grayRefreshedOnce);
  bus.clear();

  // A first monochrome transition works without ever rendering grayscale.
  assert(driver.displayMonochromeReinforcement(bus, next.data(), true));
  assert(!driver._grayRefreshedOnce && driver._grayBase == nullptr);
  assert(count(bus, 0x12) == 1);
  assert(index(bus, 0x04) < index(bus, 0x00));  // PON before custom PSR.
  assert(index(bus, 0x13) < index(bus, 0x12));
  assert(index(bus, 0x12) < index(bus, 0x10));  // OLD synchronized after DRF.
  assert(index(bus, 0x10) < index(bus, 0x02));
  assert(!driver._isScreenOn);
  checkPlane(bus, 0x13, next);
  checkPlane(bus, 0x10, next);
  for (uint8_t command = 0x20; command <= 0x24; ++command) {
    assert(count(bus, command) == 1);
    assert(bus.writes[index(bus, command)].bytes.size() == 42);
  }
  const auto& window = bus.writes[index(bus, 0x90)].bytes;
  assert(window[4] == 0 && window[5] == 120 && window[6] == 2 && window[7] == 87);

  // Repeated pages remain one pass; leaving power on preserves that contract.
  bus.clear();
  assert(driver.displayMonochromeReinforcement(bus, next.data(), false));
  assert(count(bus, 0x12) == 1 && count(bus, 0x02) == 0 && driver._isScreenOn);
  for (auto board : {BoardConfig::Board::XteinkX4Pro, BoardConfig::Board::XteinkX4}) {
    BoardConfig::ACTIVE.board = board;
    for (uint8_t variant : {0x00, 0x01, 0x02, 0x03, 0x67, 0x69, 0xFF}) {
      bus.clear();
      BoardConfig::ACTIVE.displayControllerVariant = variant;
      assert(!driver.displayMonochromeReinforcement(bus, first.data(), false));
      assert(bus.writes.empty());
    }
  }
  BoardConfig::ACTIVE.displayControllerVariant = 0x68;
  BoardConfig::ACTIVE.board = BoardConfig::Board::XteinkX4;
  bus.clear();
  assert(driver.displayMonochromeReinforcement(bus, first.data(), true));
  assert(count(bus, 0x12) == 1 && count(bus, 0x20) == 1 && !driver._isScreenOn);
  checkPlane(bus, 0x13, first);
  checkPlane(bus, 0x10, first);
  assert(driver._grayBase == nullptr);
  for (auto controller : {BoardConfig::DisplayController::SSD1677, BoardConfig::DisplayController::UC8179}) {
    BoardConfig::ACTIVE.displayController = controller;
    bus.clear();
    assert(!driver.displayMonochromeReinforcement(bus, first.data(), false) && bus.writes.empty());
  }
  BoardConfig::ACTIVE.displayController = BoardConfig::DisplayController::UC8279;
  for (auto board : {BoardConfig::Board::XteinkX4Classic, BoardConfig::Board::XteinkX3,
                     BoardConfig::Board::XteinkX3Uc8279, BoardConfig::Board::Sticky}) {
    BoardConfig::ACTIVE.board = board;
    bus.clear();
    assert(!driver.displayMonochromeReinforcement(bus, first.data(), false) && bus.writes.empty());
  }
  BoardConfig::ACTIVE.board = BoardConfig::Board::XteinkX4Pro;

  // The facade supplies a real full clean after AA, invalidation or inversion.
  FreeInkDisplay display(1, 2, 3, 4, 5, 6);
  display._driver = &driver;
  display.begin();
  std::memcpy(display.getFrameBuffer(), first.data(), first.size());
  driver._redriveAfterGray = true;
  display._bus.clear();
  assert(!display.displayMonochromeReinforcement(true));
  assert(count(display._bus, 0x12) == 1 && count(display._bus, 0x20) == 0);
  assert(driver._oldPlaneValid && !driver._redriveAfterGray);
  display._bus.clear();
  assert(display.displayMonochromeReinforcement(true));
  assert(count(display._bus, 0x12) == 1 && count(display._bus, 0x20) == 1);
  driver.requestResync(1);
  display._bus.clear();
  assert(!display.displayMonochromeReinforcement(true));
  assert(count(display._bus, 0x20) == 0);
  display.setInverted(true);
  display._bus.clear();
  assert(!display.displayMonochromeReinforcement(true));
  assert(count(display._bus, 0x20) == 0);
  std::puts("UC8279 0x68: one mono pass, old/new bytes, power ordering, variant gates and clean fallbacks passed");
}
