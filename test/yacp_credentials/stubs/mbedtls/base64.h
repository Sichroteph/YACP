#pragma once
#include <cstddef>
// Host ABI declarations match the SDK header. The implementation is the
// installed libmbedcrypto.so.7, not a substitute decoder in this harness.
#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL -0x002A
extern "C" int mbedtls_base64_decode(unsigned char*, size_t, size_t*, const unsigned char*, size_t);
extern "C" int mbedtls_base64_encode(unsigned char*, size_t, size_t*, const unsigned char*, size_t);
