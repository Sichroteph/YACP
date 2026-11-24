#include "HalNearby.h"

#include <Logging.h>
#include <Memory.h>
#include <NearbyTransfer.h>
#include <esp_mac.h>
#include <cstring>

class HalNearby::Impl {
 public:
  freeink::nearby::EspNowTransport transport;
  freeink::nearby::EspNowTransport::Event event;
};

HalNearby::HalNearby() = default;
HalNearby::~HalNearby() = default;

bool HalNearby::begin(uint8_t channel) {
  end();
  // About 5.5 KB for the SDK's four receive slots and one dequeue scratch event.
  // Allocate once on screen entry, not on the loop stack or while reading a book.
  impl = makeUniqueNoThrow<Impl>();
  if (!impl || !impl->transport.begin(channel)) {
    LOG_ERR("NEARBY","Could not allocate or start nearby transport");
    end();
    return false;
  }
  return true;
}

void HalNearby::end() { impl.reset(); }

bool HalNearby::send(const uint8_t* destination, const uint8_t* data, size_t size) {
  const bool ok = impl && impl->transport.send(destination,data,size);
  if (!ok) LOG_ERR("NEARBY","Packet send failed");
  return ok;
}

bool HalNearby::poll(uint8_t* source, uint8_t* data, size_t capacity, size_t& size) {
  size = 0;
  if (!impl || !source || !data) return false;
  // Drain at most the SDK queue capacity per call, including irrelevant packets.
  for (unsigned i = 0; i < 4 && impl->transport.poll(impl->event); ++i) {
    if (impl->event.length > capacity) continue;
    memcpy(source,impl->event.sourceMac.data(),6);
    memcpy(data,impl->event.data.data(),impl->event.length);
    size = impl->event.length;
    return true;
  }
  return false;
}

bool HalNearby::overflowed() const { return impl && impl->transport.overflowed(); }
bool HalNearby::factoryMac(uint8_t* output) { return output && esp_efuse_mac_get_default(output) == 0; }
