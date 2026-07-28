/* Minimal expat configuration for FreeInkBook.
 *
 * Expat parses local book files only, so the hash-salt entropy that protects
 * network-facing parsers from collision attacks is not needed; the weak
 * fallback keeps the dependency freestanding (no /dev/urandom, getrandom, or
 * time syscalls required on embedded targets).
 */
#pragma once

#define XML_GE 1
#define XML_CONTEXT_BYTES 1024
#define XML_POOR_ENTROPY 1
#define HAVE_MEMMOVE 1

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define BYTEORDER 4321
#else
#define BYTEORDER 1234
#endif
