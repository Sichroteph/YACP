#include "NearbyStatsProtocol.h"

#include <cstring>

namespace nearby_stats {
namespace {
constexpr Mac BROADCAST = {255, 255, 255, 255, 255, 255};
bool validName(const uint8_t* data, size_t size) {
  if (!data || size < 2 || size > NAME_SIZE) return false;
  // Preserve the bounded name bytes used by YACP; exclude control characters.
  for (size_t i = 0; i < size; ++i) if (data[i] < 32 || data[i] == 127) return false;
  return true;
}
}  // namespace

bool validStats(const uint8_t* data, const size_t size) {
  return data && ((size == 13 && data[0] == 1) || (size == 17 && data[0] == 2) ||
                  (size == STATS_SIZE && data[0] == 3));
}

bool validMac(const Mac& mac) { return mac != Mac{} && (mac[0] & 1u) == 0; }

Decode decode(const uint8_t* data, const size_t size, Packet& output) {
  output = {};
  if (!data || size < HEADER_SIZE || size > HEADER_SIZE + STATS_SIZE ||
      memcmp(data, "CISS", 4) || data[4] != 1 || data[7] != 0 || size != HEADER_SIZE + data[6] ||
      data[5] < 1 || data[5] > 4) return Decode::Invalid;
  output.type = static_cast<Type>(data[5]);
  memcpy(output.device.data(), data + 8, 6);
  output.payload = data + HEADER_SIZE;
  output.size = data[6];
  if (!validMac(output.device)) return Decode::Invalid;
  if (output.type == Type::Stats) return validStats(output.payload, output.size) ? Decode::Valid : Decode::UnsupportedStats;
  if (output.type == Type::Name) return validName(output.payload, output.size) ? Decode::Valid : Decode::Invalid;
  return output.size == 0 ? Decode::Valid : Decode::Invalid;
}

size_t encode(uint8_t* output, size_t capacity, Type type, const Mac& device,
              const uint8_t* payload, size_t size) {
  if (!output || capacity < HEADER_SIZE + size || !validMac(device) ||
      (type == Type::Stats ? !validStats(payload, size) :
       type == Type::Name ? !validName(payload, size) :
       (type != Type::Hello && type != Type::Ack) || size != 0)) return 0;
  memcpy(output, "CISS", 4);
  output[4] = 1;
  output[5] = static_cast<uint8_t>(type);
  output[6] = static_cast<uint8_t>(size);
  output[7] = 0;
  memcpy(output + 8, device.data(), 6);
  if (size) memcpy(output + HEADER_SIZE, payload, size);
  return HEADER_SIZE + size;
}

Session::Session(const Mac& local, const char* name, Hooks hooks) : hooks_(hooks), local_(local) {
  const size_t size = name ? strlen(name) : 0;
  if (validName(reinterpret_cast<const uint8_t*>(name), size)) memcpy(name_.data(), name, size);
}

void Session::fail(Error error) { error_ = error; state_ = State::Error; }

bool Session::prepare(uint32_t now) {
  bound_ = saved_ = sent_ = acknowledged_ = false;
  peerDevice_ = peerSource_ = {};
  peerName_ = {};
  error_ = Error::None;
  began_ = helloAt_ = statsAt_ = now;
  localSize_ = 0;
  if (!validMac(local_) || !hooks_.readLocal(hooks_.context, localStats_.data(), localSize_) ||
      !validStats(localStats_.data(), localSize_)) {
    fail(Error::LocalStats);
    return false;
  }
  state_ = State::Discovering;
  return true;
}

bool Session::send(Type type, const Mac& destination, const uint8_t* data, size_t size) {
  uint8_t packet[HEADER_SIZE + STATS_SIZE];  // 173 bounded stack bytes, maintenance screen only.
  const size_t length = encode(packet, sizeof(packet), type, local_, data, size);
  return length && hooks_.send(hooks_.context, destination, packet, length);
}

void Session::start(uint32_t now) {
  if (prepare(now)) send(Type::Hello, BROADCAST);
}

void Session::sendStats(uint32_t now) {
  statsAt_ = now;
  if (name_[0]) send(Type::Name, peerSource_, reinterpret_cast<const uint8_t*>(name_.data()), strlen(name_.data()));
  // A failed retry must not invalidate an earlier successfully queued snapshot.
  sent_ = send(Type::Stats, peerSource_, localStats_.data(), localSize_) || sent_;
}

void Session::receive(const Mac& source, const uint8_t* data, size_t size, uint32_t now) {
  if (state_ == State::Error || !validMac(source)) return;
  Packet packet;
  const Decode decoded = decode(data, size, packet);
  if (decoded == Decode::Invalid || packet.device == local_) return;
  if (bound_ && (packet.device != peerDevice_ || source != peerSource_)) return;
  if (packet.type == Type::Ack && (!bound_ || !sent_)) return;
  if (state_ == State::Ready || (state_ == State::Synced && packet.type == Type::Hello)) {
    if (!prepare(now)) return;
  }
  if (!bound_) {
    peerDevice_ = packet.device;
    peerSource_ = source;
    bound_ = true;
    state_ = State::Syncing;
  }
  if (decoded == Decode::UnsupportedStats) { fail(Error::Version); return; }
  switch (packet.type) {
    case Type::Hello:
      sendStats(now);
      break;
    case Type::Name:
      peerName_ = {};
      memcpy(peerName_.data(), packet.payload, packet.size);
      break;
    case Type::Stats:
      if (!saved_) {
        if (!hooks_.savePeer(hooks_.context, peerDevice_, packet.payload, packet.size)) {
          fail(Error::Save);
          return;
        }
        saved_ = true;
      }
      send(Type::Ack, peerSource_);
      if (!sent_ || !acknowledged_) sendStats(now);
      break;
    case Type::Ack:
      acknowledged_ = true;
      break;
  }
  if (saved_ && acknowledged_) state_ = State::Synced;
}

void Session::tick(uint32_t now) {
  if (state_ != State::Discovering && state_ != State::Syncing) return;
  if (now - began_ >= TIMEOUT_MS) { fail(Error::Timeout); return; }
  if (!bound_ && now - helloAt_ >= RETRY_MS) {
    helloAt_ = now;
    send(Type::Hello, BROADCAST);
  } else if (bound_ && !acknowledged_ && now - statsAt_ >= RETRY_MS) {
    sendStats(now);
  }
}
}  // namespace nearby_stats
