#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

class HalNearby {
 public:
  HalNearby();
  ~HalNearby();
  bool begin(uint8_t channel);
  void end();
  bool send(const uint8_t* destination, const uint8_t* data, size_t size);
  bool poll(uint8_t* source, uint8_t* data, size_t capacity, size_t& size);
  bool overflowed() const;
  static bool factoryMac(uint8_t* output);

 private:
  class Impl;
  std::unique_ptr<Impl> impl;
};
