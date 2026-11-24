#pragma once
#include <Arduino.h>
#include <cassert>
#include <mbedtls/base64.h>
namespace base64 {
inline String encode(const uint8_t* data, size_t length) {
  size_t count = 0;
  const int status = mbedtls_base64_encode(nullptr, 0, &count, data, length);
  assert(status == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || status == 0);
  String out(count, '\0');
  assert(mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out.data()), out.size(), &count, data, length) == 0);
  out.resize(count);
  return out;
}
}
