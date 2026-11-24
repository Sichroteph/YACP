#include "HalNetwork.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_netif_net_stack.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>

#include "TcpDontFragment.h"

HalNetwork halNetwork;

namespace {
// Installed and used on the TCP/IP thread. No packet-time allocation.
netif_output_fn originalStationOutput = nullptr;

err_t outputWithTcpDontFragment(struct netif* interface, struct pbuf* packet, const ip4_addr_t* address) {
  if (packet) {
    hal_network::markTcpDontFragment(static_cast<uint8_t*>(packet->payload), packet->len, packet->tot_len);
  }
  if (!originalStationOutput) {
    LOG_ERR("NET", "Missing station output callback");
    return ERR_IF;
  }
  return originalStationOutput(interface, packet, address);
}

struct HookInstallResult {
  bool installed = false;
};

void installStationOutputHook(void* context) {
  auto* result = static_cast<HookInstallResult*>(context);
  auto* station = static_cast<struct netif*>(esp_netif_get_netif_impl(WiFi.STA.netif()));
  if (!station || !station->output) return;
  if (station->output != outputWithTcpDontFragment) {
    originalStationOutput = station->output;
    station->output = outputWithTcpDontFragment;
  }
  result->installed = true;
}
}  // namespace

bool HalNetwork::enableTcpDontFragmentOnStation() {
  HookInstallResult result;
  const err_t status = tcpip_callback_wait(installStationOutputHook, &result);
  if (status != ERR_OK || !result.installed) {
    LOG_ERR("NET", "TCP IPv4 Don't Fragment hook failed (callback=%d installed=%d)", status, result.installed);
    return false;
  }
  LOG_INF("NET", "TCP IPv4 Don't Fragment enabled on station output");
  return true;
}
