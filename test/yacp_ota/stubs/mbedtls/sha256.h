#pragma once
#include <openssl/sha.h>
using mbedtls_sha256_context = SHA256_CTX;
inline void mbedtls_sha256_init(SHA256_CTX*) {}
inline void mbedtls_sha256_free(SHA256_CTX*) {}
inline int mbedtls_sha256_starts(SHA256_CTX* c, int) { return SHA256_Init(c) == 1 ? 0 : -1; }
inline int mbedtls_sha256_update(SHA256_CTX* c, const unsigned char* p, size_t n) { return SHA256_Update(c,p,n) == 1 ? 0 : -1; }
inline int mbedtls_sha256_finish(SHA256_CTX* c, unsigned char* out) { return SHA256_Final(out,c) == 1 ? 0 : -1; }
