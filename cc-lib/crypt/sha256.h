/*
  The following code is based on OpenSSL, licensed under the Apache 2.0
  license; see sha256.cc.
*/

#ifndef _CC_LIB_CRYPT_SHA256_H
#define _CC_LIB_CRYPT_SHA256_H

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SHA256 {
  // Size of output digest in bytes.
  static constexpr int DIGEST_LENGTH = 32;

  // SHA-256 treats input data as a contiguous array of 32 bit wide
  // big-endian values.
  static constexpr int SHA_LBLOCK = 16;
  static constexpr int SHA_CBLOCK = 4 * SHA_LBLOCK;

  struct Ctx {
    uint32_t h[8];
    uint32_t Nl, Nh;
    uint32_t data[SHA_LBLOCK];
    unsigned int num;
  };

  static void Init(Ctx *c);
  static void Update(Ctx *c, const uint8_t *data, size_t len);
  static void UpdateString(Ctx *c, std::string_view s);
  static void UpdateSpan(Ctx *c, std::span<const uint8_t> s);
  // out should point to a 32-byte buffer.
  static void Finalize(Ctx *c, unsigned char *out);
  // Finalize but return a new vector of 32 bytes.
  static std::vector<uint8_t> FinalVector(Ctx *c);
  // Or an array by value.
  static std::array<uint8_t, 32> FinalArray(Ctx *c);

  // Convert from 32-byte digest to lowercase hex string.
  static std::string Ascii(const std::vector<uint8_t> &v);
  // Convert from mixed-case ascii to SHA256 digest. true on success.
  static bool UnAscii(const std::string &s, std::vector<uint8_t> *out);

  // Convenience methods.
  static std::vector<uint8_t> HashString(const std::string &s);
  static std::vector<uint8_t> HashVector(const std::vector<uint8_t> &v);
  static std::vector<uint8_t> HashSpan(std::span<const uint8_t> s);
  static std::vector<uint8_t> HashPtr(const void *ptr, size_t len);
  static std::vector<uint8_t> HashStringView(std::string_view s);
};

#endif
