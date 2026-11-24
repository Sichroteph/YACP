#include "NearbyStatsProtocol.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

using namespace nearby_stats;
struct Datagram { Mac destination; std::vector<uint8_t> data; };
struct Device {
  Mac mac;
  std::deque<Datagram> output;
  std::vector<uint8_t> saved;
  unsigned writes = 0;
  bool readFails = false, writeFails = false;
  Session session;
  explicit Device(uint8_t id) : mac{2,0,0,0,0,id}, session(mac, "Reader", {this, read, send, save}) {}
  static bool read(void* ctx, uint8_t* data, size_t& size) {
    auto& self = *static_cast<Device*>(ctx);
    if (self.readFails) return false;
    size = STATS_SIZE;
    memset(data, 0, size);
    data[0] = 3;
    data[5] = self.mac[5];
    return true;
  }
  static bool send(void* ctx, const Mac& destination, const uint8_t* data, size_t size) {
    static_cast<Device*>(ctx)->output.push_back({destination, {data, data + size}});
    return true;
  }
  static bool save(void* ctx, const Mac& peer, const uint8_t* data, size_t size) {
    auto& self = *static_cast<Device*>(ctx);
    assert(peer != self.mac);
    if (self.writeFails) return false;
    ++self.writes;
    self.saved.assign(data, data + size);
    return true;
  }
};

std::vector<uint8_t> legacy(Type type, const Mac& device, std::vector<uint8_t> data = {}) {
  std::vector<uint8_t> bytes = {'C','I','S','S',1,static_cast<uint8_t>(type),static_cast<uint8_t>(data.size()),0};
  bytes.insert(bytes.end(),device.begin(),device.end());
  bytes.insert(bytes.end(),data.begin(),data.end());
  return bytes;
}

void deliver(Device& from, Device& to, uint32_t now, int& drop) {
  // Drain a snapshot, so replies and duplicate replies cannot create an unbounded loop.
  const size_t count = from.output.size();
  for (size_t i = 0; i < count; ++i) {
    auto packet = from.output.front(); from.output.pop_front();
    if (drop-- == 0) continue;
    assert(packet.destination == to.mac || packet.destination == Mac({255,255,255,255,255,255}));
    to.session.receive(from.mac,packet.data.data(),packet.data.size(),now);
    to.session.receive(from.mac,packet.data.data(),packet.data.size(),now);  // Radio duplicates.
  }
}

int main() {
  const Mac peer{2,3,4,5,6,7};
  Packet packet;
  uint8_t encoded[HEADER_SIZE + STATS_SIZE];
  for (const auto pair : {std::pair<int,int>{1,13},{2,17},{3,159}}) {
    std::vector<uint8_t> stats(pair.second,0x42); stats[0] = pair.first;
    const auto reference = legacy(Type::Stats,peer,stats);
    assert(encode(encoded,sizeof(encoded),Type::Stats,peer,stats.data(),stats.size()) == reference.size());
    assert(memcmp(encoded,reference.data(),reference.size()) == 0);
    assert(decode(reference.data(),reference.size(),packet) == Decode::Valid);
    for (size_t length = 0; length < reference.size(); ++length)
      assert(decode(reference.data(),length,packet) == Decode::Invalid);
  }
  for (int dropped = -1; dropped < 12; ++dropped) {
    Device a(1), b(2);
    int drop = dropped;
    a.session.start(0);
    for (uint32_t now = 0; now < 6000; now += 50) {
      deliver(a,b,now,drop); deliver(b,a,now,drop);
      a.session.tick(now); b.session.tick(now);
    }
    assert(a.session.state() == State::Synced && b.session.state() == State::Synced);
    assert(a.writes == 1 && b.writes == 1);
    assert(a.saved[5] == 2 && b.saved[5] == 1);
    assert(strcmp(a.session.peerName(),"Reader") == 0);
  }
  Device idle(1);
  const auto ack = legacy(Type::Ack,peer);
  idle.session.receive(peer,ack.data(),ack.size(),0);
  assert(idle.session.state() == State::Ready && idle.writes == 0);
  idle.session.start(UINT32_MAX - 100);
  idle.session.tick(12000 - 101);
  assert(idle.session.state() == State::Error && idle.session.error() == Error::Timeout);
  Device locked(1);
  auto hello = legacy(Type::Hello,peer);
  locked.session.receive(peer,hello.data(),hello.size(),10);
  const Mac intruder{4,3,4,5,6,7};
  auto foreign = legacy(Type::Stats,intruder,std::vector<uint8_t>(159,3));
  locked.session.receive(intruder,foreign.data(),foreign.size(),20);
  assert(locked.writes == 0 && locked.session.peerDevice() == peer);
  auto wrongVersion = legacy(Type::Stats,peer,std::vector<uint8_t>(159,4));
  locked.session.receive(peer,wrongVersion.data(),wrongVersion.size(),30);
  assert(locked.session.error() == Error::Version && locked.writes == 0);
  Device unavailable(1); unavailable.readFails = true; unavailable.session.start(0);
  assert(unavailable.session.error() == Error::LocalStats && unavailable.output.empty());
  Device diskFailure(1); diskFailure.writeFails = true;
  auto valid = legacy(Type::Stats,peer,std::vector<uint8_t>(159,3));
  diskFailure.session.receive(peer,valid.data(),valid.size(),0);
  assert(diskFailure.session.error() == Error::Save && diskFailure.output.empty());
  std::puts("PASS: legacy bytes, packet bounds, lost/duplicate packets, peer lock, wrap, failures");
}
