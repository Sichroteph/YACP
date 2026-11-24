#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Legacy YACP CISS v1. Independent from the SDK's CIFT file-transfer protocol.
namespace nearby_stats {
using Mac = std::array<uint8_t, 6>;
constexpr size_t HEADER_SIZE = 14;
constexpr size_t STATS_SIZE = 159;
constexpr size_t NAME_SIZE = 20;
constexpr uint32_t RETRY_MS = 750;
constexpr uint32_t TIMEOUT_MS = 12000;
enum class Type : uint8_t { Hello = 1, Stats = 2, Ack = 3, Name = 4 };
enum class Decode : uint8_t { Invalid, Valid, UnsupportedStats };
struct Packet {
  Type type = Type::Hello;
  Mac device{};
  const uint8_t* payload = nullptr;  // Borrowed until the next transport poll.
  size_t size = 0;
};
bool validStats(const uint8_t* data, size_t size);
bool validMac(const Mac& mac);
Decode decode(const uint8_t* data, size_t size, Packet& output);
size_t encode(uint8_t* output, size_t capacity, Type type, const Mac& device,
              const uint8_t* payload = nullptr, size_t size = 0);

enum class State : uint8_t { Ready, Discovering, Syncing, Synced, Error };
enum class Error : uint8_t { None, LocalStats, Transport, Version, Save, Timeout };

// The activity owns this bounded state only while the exchange screen is open.
// Callbacks run on its loop task. Radio callbacks never access storage or UI.
class Session {
 public:
  struct Hooks {
    void* context;
    bool (*readLocal)(void*, uint8_t*, size_t&);
    bool (*send)(void*, const Mac&, const uint8_t*, size_t);
    bool (*savePeer)(void*, const Mac&, const uint8_t*, size_t);
  };
  Session(const Mac& local, const char* name, Hooks hooks);
  void start(uint32_t now);
  void receive(const Mac& source, const uint8_t* data, size_t size, uint32_t now);
  void tick(uint32_t now);
  void fail(Error error);
  State state() const { return state_; }
  Error error() const { return error_; }
  const char* peerName() const { return peerName_.data(); }
  const Mac& peerDevice() const { return peerDevice_; }

 private:
  Hooks hooks_;
  Mac local_, peerDevice_{}, peerSource_{};
  std::array<char, NAME_SIZE + 1> name_{}, peerName_{};
  std::array<uint8_t, STATS_SIZE> localStats_{};
  size_t localSize_ = 0;
  State state_ = State::Ready;
  Error error_ = Error::None;
  bool bound_ = false, saved_ = false, sent_ = false, acknowledged_ = false;
  uint32_t began_ = 0, helloAt_ = 0, statsAt_ = 0;
  bool prepare(uint32_t now);
  bool send(Type type, const Mac& destination, const uint8_t* data = nullptr, size_t size = 0);
  void sendStats(uint32_t now);
};
}  // namespace nearby_stats
