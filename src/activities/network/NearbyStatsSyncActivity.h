#pragma once

#include <HalNearby.h>
#include <optional>
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "network/NearbyStatsProtocol.h"

class NearbyStatsSyncActivity final : public Activity, private UiAppHost {
 public:
  NearbyStatsSyncActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("NearbyStatsSync",renderer,input), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  HalNearby radio;
  std::optional<nearby_stats::Session> session;
  std::array<uint8_t, nearby_stats::HEADER_SIZE + nearby_stats::STATS_SIZE> packet{};
  int touchCommand = -1;
  bool initialize();
  bool canSync() const;
  const char* statusText() const;
  static bool readLocal(void*, uint8_t*, size_t&);
  static bool send(void*, const nearby_stats::Mac&, const uint8_t*, size_t);
  static bool savePeer(void*, const nearby_stats::Mac&, const uint8_t*, size_t);
  static void navigationAction(const freeink::ui::ActionEvent&, void*);
  static void navigationScreen(UiScreen&, void*);
};
