/*

  The following code is based on OpenSSL, licensed under the Apache 2.0
  license; see sha256.cc.
*/

#ifndef _CC_LIB_CRYPT_SHA512_H
#define _CC_LIB_CRYPT_SHA512_H

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SHA512 {
  // Size of output digest in bytes.
  static constexpr int DIGEST_LENGTH = 64;

  struct Ctx {
    uint64_t state[8] = {};
    uint8_t buffer[128] = {};
    uint64_t n_bits = 0;
    uint8_t buffer_counter = 0;
  };

  static void Init(Ctx *c);
  static void Update(Ctx *c, const uint8_t *data, size_t len);
  static void UpdateString(Ctx *c, std::string_view s);
  static void UpdateSpan(Ctx *c, std::span<const uint8_t> s);
  // out should point to a 32-byte buffer.
  static void Finalize(Ctx *c, uint8_t *out);
  // Finalize but return a new vector of 32 bytes.
  static std::vector<uint8_t> FinalVector(Ctx *c);
  // Or an array by value.
  static std::array<uint8_t, DIGEST_LENGTH> FinalArray(Ctx *c);

  // Convert from 32-byte digest to lowercase hex string.
  static std::string Ascii(const std::vector<uint8_t> &v);
  // Convert from mixed-case ascii to SHA256 digest. true on success.
  static bool UnAscii(const std::string &s, std::vector<uint8_t> *out);

  // Convenience methods.
  static std::vector<uint8_t> HashString(std::string_view s);
  // Noting that vector has an implicit cast to span.
  static std::vector<uint8_t> HashSpan(std::span<const uint8_t> s);
  static std::vector<uint8_t> HashPtr(const void *ptr, size_t len);
};

#endif
