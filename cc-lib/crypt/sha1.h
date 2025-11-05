
#ifndef _CC_LIB_CRYPT_SHA1_H
#define _CC_LIB_CRYPT_SHA1_H

#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <span>

// Note that SHA-1 is considered insecure for signature applications.
// Full collisions are known and prefix attacks are practical.
//
// HMAC (as used in TLS 1.2 for example) may still be OK.

struct SHA1 {
  // Size of output digest in bytes.
  static constexpr int DIGEST_LENGTH = 20;

  // SHA-256 treats input data as a contiguous array of 32 bit wide
  // big-endian values.
  static constexpr int SHA_LBLOCK = 16;
  static constexpr int SHA_CBLOCK = 4 * SHA_LBLOCK;

  struct Ctx {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
  };

  static void Init(Ctx *context);
  static void Update(Ctx *context, const uint8_t *data, size_t len);
  static void Finalize(Ctx *context, uint8_t *digest);
  static std::vector<uint8_t> FinalVector(Ctx *c);
  static std::array<uint8_t, DIGEST_LENGTH> FinalArray(Ctx *c);

  // Keyed hash function.
  static std::array<uint8_t, DIGEST_LENGTH>
  HMAC(std::span<const uint8_t> key,
       std::span<const uint8_t> message);

 private:
  SHA1() = delete;
};



#endif
