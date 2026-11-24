#pragma once

#include <HalStorage.h>
#include <type_traits>

namespace statsio {
template <typename T>
bool readPod(HalFile& file, T& value) {
  static_assert(std::is_integral_v<T>);
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(value)) == static_cast<int>(sizeof(value));
}

template <typename T>
bool writePod(HalFile& file, const T& value) {
  static_assert(std::is_integral_v<T>);
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
}
}  // namespace statsio
