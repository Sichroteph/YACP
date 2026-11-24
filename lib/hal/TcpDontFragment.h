#pragma once

#include <cstddef>
#include <cstdint>

namespace hal_network {

// Adapted from YACP's station-output hook. Bytes are read explicitly so an
// unaligned pbuf payload is safe on C3. Only a complete, unfragmented TCP IPv4
// header is changed; the payload and other protocols remain untouched.
inline bool markTcpDontFragment(uint8_t* packet, const size_t contiguousBytes, const size_t packetBytes) {
  if (!packet || contiguousBytes < 20 || packetBytes < contiguousBytes) return false;
  const size_t headerBytes = static_cast<size_t>(packet[0] & 0x0fU) * 4;
  const size_t totalBytes = (static_cast<size_t>(packet[2]) << 8) | packet[3];
  if ((packet[0] >> 4) != 4 || packet[9] != 6 || headerBytes < 20 || headerBytes > contiguousBytes ||
      totalBytes < headerBytes || totalBytes > packetBytes) return false;
  const uint16_t flags = (static_cast<uint16_t>(packet[6]) << 8) | packet[7];
  if ((flags & 0x3fffU) != 0 || (flags & 0x4000U) != 0) return false;
  packet[6] |= 0x40;
  packet[10] = packet[11] = 0;
  uint32_t sum = 0;
  for (size_t i = 0; i < headerBytes; i += 2) {
    sum += (static_cast<uint16_t>(packet[i]) << 8) | packet[i + 1];
  }
  while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
  const uint16_t checksum = static_cast<uint16_t>(~sum);
  packet[10] = static_cast<uint8_t>(checksum >> 8);
  packet[11] = static_cast<uint8_t>(checksum);
  return true;
}

}  // namespace hal_network
