/*
  Based on code by Steve Reid and James H. Brown in the 1990s.
  Public domain.
*/

#include "crypt/sha1.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

static inline uint32_t rol(uint32_t w, int bits) {
  return std::rotl<uint32_t>(w, bits);
}

static inline uint32_t bswap_32(uint32_t val) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap32(val);
#else
  return (val << 24) | ((val << 8) & 0x00ff0000) |
    ((val >> 8) & 0x0000ff00) | (val >> 24);
#endif
}

static inline uint64_t bswap_64(uint64_t val) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap64(val);
#else
  val = (val & 0x00000000FFFFFFFF) << 32 | (val & 0xFFFFFFFF00000000) >> 32;
  val = (val & 0x0000FFFF0000FFFF) << 16 | (val & 0xFFFF0000FFFF0000) >> 16;
  val = (val & 0x00FF00FF00FF00FF) << 8  | (val & 0xFF00FF00FF00FF00) >> 8;
  return val;
#endif
}

/* blk0() and blk() perform the initial expand. */
#define blk0(i) w[i]
#define blk(i) (w[i&15] = rol(w[(i+13)&15]^w[(i+8)&15] ^ w[(i+2)&15]^w[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

/* Hash a single 512-bit block. This is the core of the algorithm. */
static void SHA1Transform(uint32_t state[5], const uint8_t buffer[64]) {
  uint32_t a, b, c, d, e;

  // This local array will hold the 16 words of the message schedule.
  uint32_t w[16];

  // Convert into 16 big-endian uint32_t words. memcpy avoids
  // strict-aliasing issues.
  for (int i = 0; i < 16; ++i) {
    memcpy(&w[i], buffer + i * 4, 4);
    // The SHA-1 standard expects the data in big-endian format.
    // If our system is little-endian, we must byte-swap.
    if constexpr (std::endian::native == std::endian::little) {
      w[i] = bswap_32(w[i]);
    }
  }

  /* Copy context->state[] to working vars */
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];

  /* 4 rounds of 20 operations each. Loop unrolled. */
  R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
  R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
  R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
  R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
  R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
  R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
  R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
  R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
  R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
  R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
  R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
  R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
  R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
  R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
  R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
  R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
  R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
  R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
  R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
  R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

  /* Add the working vars back into context.state[] */
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;

  /* Wipe variables */
  a = b = c = d = e = 0;
  memset((void *volatile)w, 0, sizeof(w));
}

/* SHA1Init - Initialize new context */
void SHA1::Init(SHA1::Ctx *context) {
  context->state[0] = 0x67452301;
  context->state[1] = 0xEFCDAB89;
  context->state[2] = 0x98BADCFE;
  context->state[3] = 0x10325476;
  context->state[4] = 0xC3D2E1F0;
  context->count[0] = 0;
  context->count[1] = 0;
}

void SHA1::Update(Ctx *context, const uint8_t *data, size_t len) {
  size_t j = (context->count[0] >> 3) & 63;
  if ((context->count[0] += len << 3) < (len << 3))
    context->count[1]++;
  context->count[1] += (len >> 29);

  size_t i = 0;
  if ((j + len) > 63) {
    memcpy(&context->buffer[j], data, (i = 64-j));
    SHA1Transform(context->state, context->buffer);
    for ( ; i + 63 < len; i += 64) {
      SHA1Transform(context->state, data + i);
    }
    j = 0;
  }
  memcpy(&context->buffer[j], &data[i], len - i);
}

void SHA1::Finalize(Ctx *context, uint8_t *digest) {

  // First, prepare the 64-bit message length in big-endian format.
  // The length must be calculated *before* we add the final padding.
  uint8_t finalcount[8];
  uint64_t bit_count =
    (static_cast<uint64_t>(context->count[1]) << 32) | context->count[0];

  uint64_t bit_count_be = bit_count;
  if constexpr (std::endian::native == std::endian::little) {
    bit_count_be = bswap_64(bit_count);
  }
  memcpy(finalcount, &bit_count_be, 8);

  // Now, apply the padding. Append the '1' bit (0x80).
  Update(context, (const uint8_t *)"\x80", 1);

  // Pad with zeros until we have space for the 8-byte length.
  // The last block must be filled to 56 bytes (64 - 8).
  while ((context->count[0] >> 3) % 64 != (64 - 8)) {
    Update(context, (const uint8_t *)"\0", 1);
  }

  // Append the original message length. This will trigger the final transform.
  Update(context, finalcount, 8);

  // Convert the final state (5 uint32_t words) to a 20-byte big-endian digest.
  for (size_t i = 0; i < DIGEST_LENGTH / 4; i++) {
    uint32_t word = context->state[i];
    if constexpr (std::endian::native == std::endian::little) {
      word = bswap_32(word);
    }
    memcpy(digest + i * 4, &word, 4);
  }

  memset((void* volatile)context->buffer, 0, 64);
  memset((void* volatile)context->state, 0, 20);
  memset((void* volatile)context->count, 0, 8);
  memset((void* volatile)finalcount, 0, 8);
}

std::vector<uint8_t> SHA1::FinalVector(SHA1::Ctx *ctx) {
  std::vector<uint8_t> result(DIGEST_LENGTH);
  Finalize(ctx, result.data());
  return result;
}

std::array<uint8_t, SHA1::DIGEST_LENGTH> SHA1::FinalArray(SHA1::Ctx *ctx) {
  std::array<uint8_t, DIGEST_LENGTH> result;
  Finalize(ctx, result.data());
  return result;
}


std::array<uint8_t, 20>
SHA1::HMAC(std::span<const uint8_t> key,
           std::span<const uint8_t> message) {
  static constexpr size_t BLOCK_SIZE = 64;
  std::array<uint8_t, BLOCK_SIZE> processed_key{};

  if (key.size() > BLOCK_SIZE) {
    // If key is longer than block size, hash it.
    Ctx ctx;
    Init(&ctx);
    Update(&ctx, key.data(), key.size());
    Finalize(&ctx, processed_key.data());
  } else {
    // Otherwise, copy into the zero-initialized buffer.
    memcpy(processed_key.data(), key.data(), key.size());
  }

  std::array<uint8_t, BLOCK_SIZE> inner_key_pad;
  std::array<uint8_t, BLOCK_SIZE> outer_key_pad;

  for (size_t i = 0; i < BLOCK_SIZE; i++) {
    inner_key_pad[i] = processed_key[i] ^ 0x36;
    outer_key_pad[i] = processed_key[i] ^ 0x5c;
  }

  Ctx ctx;

  // Inner hash of: (K ^ ipad) || message
  Init(&ctx);
  Update(&ctx, inner_key_pad.data(), inner_key_pad.size());
  Update(&ctx, message.data(), message.size());
  std::array<uint8_t, DIGEST_LENGTH> inner_hash = FinalArray(&ctx);

  // Outer hash of: (K ^ opad) || inner_hash
  Init(&ctx);
  Update(&ctx, outer_key_pad.data(), outer_key_pad.size());
  Update(&ctx, inner_hash.data(), inner_hash.size());
  return FinalArray(&ctx);
}
