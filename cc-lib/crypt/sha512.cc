// Based on public domain C code by Naoki Shibata.

#include "crypt/sha512.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

static inline uint64_t rotr(uint64_t x, int n) {
  return (x >> n) | (x << (64 - n));
}

static inline uint64_t step1(uint64_t e, uint64_t f, uint64_t g) {
  return (rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41)) + ((e & f) ^ ((~ e) & g));
}

static inline uint64_t step2(uint64_t a, uint64_t b, uint64_t c) {
  return (rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39)) +
    ((a & b) ^ (a & c) ^ (b & c));
}

static inline void update_w(uint64_t *w, int i, const uint8_t *buffer) {
  for (int j = 0;j < 16;j++) {
    if (i < 16) {
      w[j] =
        ((uint64_t)buffer[0] << (8*7)) |
        ((uint64_t)buffer[1] << (8*6)) |
        ((uint64_t)buffer[2] << (8*5)) |
        ((uint64_t)buffer[3] << (8*4)) |
        ((uint64_t)buffer[4] << (8*3)) |
        ((uint64_t)buffer[5] << (8*2)) |
        ((uint64_t)buffer[6] << (8*1)) |
        ((uint64_t)buffer[7] << (8*0));
      buffer += 8;
    } else {
      uint64_t a = w[(j + 1) & 15];
      uint64_t b = w[(j + 14) & 15];
      uint64_t s0 = (rotr(a,  1) ^ rotr(a,  8) ^ (a >>  7));
      uint64_t s1 = (rotr(b, 19) ^ rotr(b, 61) ^ (b >>  6));
      w[j] += w[(j + 9) & 15] + s0 + s1;
    }
  }
}

static void sha512_block(SHA512::Ctx *sha) {
  uint64_t *state = sha->state;

  static constexpr uint64_t k[] = {
      0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
      0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
      0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
      0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
      0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
      0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
      0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
      0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
      0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
      0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
      0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
      0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
      0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
      0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
      0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
      0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
      0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
      0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
      0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
      0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
      0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
      0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
      0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
      0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
      0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
      0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
      0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
  };

  uint64_t a = state[0];
  uint64_t b = state[1];
  uint64_t c = state[2];
  uint64_t d = state[3];
  uint64_t e = state[4];
  uint64_t f = state[5];
  uint64_t g = state[6];
  uint64_t h = state[7];

  uint64_t w[16];

  for (int i = 0; i < 80; i += 16) {
    update_w(w, i, sha->buffer);

    for (int j = 0; j < 16; j += 4) {
      uint64_t temp = h + step1(e, f, g) + k[i + j + 0] + w[j + 0];
      h = temp + d;
      d = temp + step2(a, b, c);
      temp = g + step1(h, e, f) + k[i + j + 1] + w[j + 1];
      g = temp + c;
      c = temp + step2(d, a, b);
      temp = f + step1(g, h, e) + k[i + j + 2] + w[j + 2];
      f = temp + b;
      b = temp + step2(c, d, a);
      temp = e + step1(f, g, h) + k[i + j + 3] + w[j + 3];
      e = temp + a;
      a = temp + step2(b, c, d);
    }
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

void SHA512::Init(SHA512::Ctx *sha) {
  sha->state[0] = uint64_t{0x6a09e667f3bcc908};
  sha->state[1] = uint64_t{0xbb67ae8584caa73b};
  sha->state[2] = uint64_t{0x3c6ef372fe94f82b};
  sha->state[3] = uint64_t{0xa54ff53a5f1d36f1};
  sha->state[4] = uint64_t{0x510e527fade682d1};
  sha->state[5] = uint64_t{0x9b05688c2b3e6c1f};
  sha->state[6] = uint64_t{0x1f83d9abfb41bd6b};
  sha->state[7] = uint64_t{0x5be0cd19137e2179};
  sha->n_bits = 0;
  sha->buffer_counter = 0;
}

static inline void sha512_append_byte(SHA512::Ctx *sha, uint8_t byte) {
  sha->buffer[sha->buffer_counter++] = byte;
  sha->n_bits += 8;

  if (sha->buffer_counter == 128) {
    sha->buffer_counter = 0;
    sha512_block(sha);
  }
}

// PERF: These should probably try to operate in 128-bit chunks.
void SHA512::Update(SHA512::Ctx *sha, const uint8_t *bytes, size_t n_bytes) {
  for (size_t i = 0; i < n_bytes; i++) {
    sha512_append_byte(sha, bytes[i]);
  }
}

void SHA512::UpdateString(Ctx *c, std::string_view s) {
  for (size_t i = 0; i < s.size(); i++) {
    sha512_append_byte(c, s[i]);
  }
}

void SHA512::UpdateSpan(Ctx *c, std::span<const uint8_t> s) {
  for (size_t i = 0; i < s.size(); i++) {
    sha512_append_byte(c, s[i]);
  }
}

static void sha512_finalize(SHA512::Ctx *sha) {
  uint64_t n_bits = sha->n_bits;

  sha512_append_byte(sha, 0x80);

  while (sha->buffer_counter != 128 - 16) {
    sha512_append_byte(sha, 0);
  }

  for (int i = 15; i >= 0; i--) {
    uint8_t byte = (n_bits >> 8 * i) & 0xff;
    sha512_append_byte(sha, byte);
  }
}

void SHA512::Finalize(SHA512::Ctx *sha, uint8_t *ptr) {
  sha512_finalize(sha);

  for (int i = 0; i < 8; i++) {
    for (int j = 7; j >= 0; j--) {
      *ptr++ = (sha->state[i] >> j * 8) & 0xff;
    }
  }
}

std::array<uint8_t, SHA512::DIGEST_LENGTH>
SHA512::FinalArray(SHA512::Ctx *sha) {
  std::array<uint8_t, SHA512::DIGEST_LENGTH> ret;
  Finalize(sha, ret.data());
  return ret;
}

std::vector<uint8_t>
SHA512::FinalVector(SHA512::Ctx *sha) {
  std::vector<uint8_t> ret(SHA512::DIGEST_LENGTH);
  Finalize(sha, ret.data());
  return ret;
}

std::vector<uint8_t> SHA512::HashString(std::string_view s) {
  SHA512::Ctx c;
  SHA512::Init(&c);
  SHA512::UpdateSpan(&c, std::span<const uint8_t>(
                         (const uint8_t*)s.data(), s.size()));
  return SHA512::FinalVector(&c);
}

std::vector<uint8_t> SHA512::HashSpan(std::span<const uint8_t> s) {
  SHA512::Ctx c;
  SHA512::Init(&c);
  SHA512::UpdateSpan(&c, s);
  return SHA512::FinalVector(&c);
}

std::vector<uint8_t> SHA512::HashPtr(const void *ptr, size_t len) {
  return HashSpan(std::span<const uint8_t>((const uint8_t*)ptr, len));
}

std::string SHA512::Ascii(const std::vector<uint8_t> &s) {
  static constexpr char hd[] = "0123456789abcdef";
  std::string ret;
  size_t sz = s.size();
  ret.reserve(sz * 2);

  for (size_t i = 0; i < sz; i++) {
    ret.push_back(hd[(s[i] >> 4) & 0xF]);
    ret.push_back(hd[ s[i]       & 0xF]);
  }

  return ret;
}

static inline bool IsHexChar(unsigned char c) {
  if (c >= '0' && c <= '9') return true;
  if (c >= 'a' && c <= 'f') return true;
  if (c >= 'A' && c <= 'F') return true;
  return false;
}

bool SHA512::UnAscii(const std::string &s, std::vector<uint8_t> *out) {
  size_t sz = s.length();
  if (sz != 128) return false;

  out->clear();
  out->reserve(64);

  for (size_t i = 0; i < 64; i++) {
    unsigned char hi = s[i * 2];
    unsigned char lo = s[i * 2 + 1];
    if (!IsHexChar(hi) || !IsHexChar(lo)) return false;
    out->push_back((((hi | 4400) % 55) << 4) |
                   ((lo | 4400) % 55));
  }

  return true;
}
