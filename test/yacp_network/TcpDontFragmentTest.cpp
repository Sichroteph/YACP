#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "TcpDontFragment.h"

static std::array<uint8_t, 61> makePacket(const unsigned headerBytes = 20) {
  std::array<uint8_t, 61> bytes{};
  // Deliberately place the IPv4 header at an unaligned address.
  auto* p = bytes.data() + 1;
  p[0] = 0x40 | (headerBytes / 4);
  p[3] = 60;
  p[4] = 0x12; p[5] = 0x34;
  p[8] = 64; p[9] = 6;
  p[12] = 192; p[13] = 168; p[14] = 1; p[15] = 2;
  p[16] = 192; p[17] = 168; p[18] = 1; p[19] = 3;
  for (unsigned i = 20; i < 60; ++i) p[i] = static_cast<uint8_t>(i);
  return bytes;
}

static uint16_t checksumSum(const uint8_t* p, unsigned count) {
  uint32_t sum = 0;
  for (unsigned i = 0; i < count; i += 2) sum += (p[i] << 8) | p[i + 1];
  sum = (sum & 0xffffU) + (sum >> 16);
  sum = (sum & 0xffffU) + (sum >> 16);
  return static_cast<uint16_t>(sum);
}

int main() {
  for (const unsigned header : {20U, 24U, 60U}) {
    auto bytes = makePacket(header);
    const auto original = bytes;
    auto* p = bytes.data() + 1;
    assert(hal_network::markTcpDontFragment(p, header, 60));
    assert(p[6] == 0x40 && checksumSum(p, header) == 0xffff);
    for (unsigned i = 0; i < 60; ++i) {
      if (i != 6 && i != 10 && i != 11) assert(p[i] == original[i + 1]);
    }
    const auto once = bytes;
    assert(!hal_network::markTcpDontFragment(p, header, 60) && bytes == once);
  }
  for (const uint16_t fragment : {uint16_t{0x2000}, uint16_t{1}, uint16_t{0x2010}}) {
    auto bytes = makePacket();
    bytes[7] = fragment >> 8; bytes[8] = fragment;
    const auto original = bytes;
    assert(!hal_network::markTcpDontFragment(bytes.data() + 1, 60, 60) && bytes == original);
  }
  for (const uint8_t protocol : {uint8_t{1}, uint8_t{17}, uint8_t{58}}) {
    auto bytes = makePacket(); bytes[10] = protocol;
    const auto original = bytes;
    assert(!hal_network::markTcpDontFragment(bytes.data() + 1, 60, 60) && bytes == original);
  }
  auto bytes = makePacket();
  assert(!hal_network::markTcpDontFragment(nullptr, 60, 60));
  assert(!hal_network::markTcpDontFragment(bytes.data() + 1, 19, 60));
  assert(!hal_network::markTcpDontFragment(bytes.data() + 1, 60, 59));
  bytes[1] = 0x65;  // IPv6
  assert(!hal_network::markTcpDontFragment(bytes.data() + 1, 60, 60));
  bytes[1] = 0x44;  // Invalid IPv4 header length
  assert(!hal_network::markTcpDontFragment(bytes.data() + 1, 60, 60));
  bytes[1] = 0x4f;  // Header split across pbufs must not be modified
  assert(!hal_network::markTcpDontFragment(bytes.data() + 1, 20, 60));
  bytes[4] = 19;
  assert(!hal_network::markTcpDontFragment(bytes.data() + 1, 60, 60));
  std::puts("TCP DF: checksum, options, idempotence, fragments, other protocols and malformed packets passed");
}
