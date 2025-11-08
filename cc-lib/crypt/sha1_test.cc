
/*
  Based on code by Steve Reid and James H. Brown in the 1990s.
  Public domain.
*/

#include "sha1.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "base/print.h"
#include "base/logging.h"

#define CHECK_SPAN_EQ(a, b) do {                \
    auto aa = std::span<const uint8_t>(a);      \
    auto bb = std::span<const uint8_t>(b);      \
    CHECK(aa.size() == bb.size());              \
    for (size_t i = 0; i < aa.size(); i++) {    \
      CHECK(aa[i] == bb[i]);                    \
    }                                           \
  } while (0)

static std::vector<uint8_t> StringVec(std::string_view v) {
  std::vector<uint8_t> ret(v.size());
  for (size_t i = 0; i < v.size(); i++) {
    ret[i] = v[i];
  }
  return ret;
}

// Test Vectors (from FIPS PUB 180-1)
static void ReidTests() {
  auto Test = [](std::span<const uint8_t> input,
                 const std::array<uint8_t, SHA1::DIGEST_LENGTH> &expected) {
      SHA1::Ctx context;
      SHA1::Init(&context);
      SHA1::Update(&context, input.data(), input.size());
      const std::array<uint8_t, SHA1::DIGEST_LENGTH> actual =
        SHA1::FinalArray(&context);

      CHECK_SPAN_EQ(expected, actual);
    };

  Test(StringVec("abc"),
       {0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E,
        0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D});

  Test(StringVec(
           "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
       {0x84, 0x98, 0x3E, 0x44, 0x1C, 0x3B, 0xD2, 0x6E, 0xBA, 0xAE,
        0x4A, 0xA1, 0xF9, 0x51, 0x29, 0xE5, 0xE5, 0x46, 0x70, 0xF1});

  {
    // 1 million 'a' characters.
    SHA1::Ctx context;
    uint8_t digest[20];
    SHA1::Init(&context);
    for (int k = 0; k < 1000000; k++) {
      SHA1::Update(&context, (uint8_t*)"a", 1);
    }
    SHA1::Finalize(&context, digest);

    std::array<uint8_t, SHA1::DIGEST_LENGTH> expected =
      {0x34, 0xAA, 0x97, 0x3C, 0xD4, 0xC4, 0xDA, 0xA4, 0xF6, 0x1E,
       0xEB, 0x2B, 0xDB, 0xAD, 0x27, 0x31, 0x65, 0x34, 0x01, 0x6F};
    CHECK_SPAN_EQ(digest, expected);
  }
}


static void TestHMAC() {
  {
    std::vector<uint8_t> key(20, 0x0b);
    std::vector<uint8_t> message = StringVec("Hi There");
    std::vector<uint8_t> expected = {
      0xb6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64, 0xe2, 0x8b,
      0xc0, 0xb6, 0xfb, 0x37, 0x8c, 0x8e, 0xf1, 0x46, 0xbe, 0x00,
    };

    std::array<uint8_t, SHA1::DIGEST_LENGTH> actual =
      SHA1::HMAC(key, message);

    CHECK_SPAN_EQ(actual, expected);
  }

  {
    std::vector<uint8_t> key = StringVec("Jefe");
    std::vector<uint8_t> message = StringVec(
        "what do ya want for nothing?");

    std::vector<uint8_t> expected = {
      0xef, 0xfc, 0xdf, 0x6a, 0xe5, 0xeb, 0x2f, 0xa2, 0xd2, 0x74,
      0x16, 0xd5, 0xf1, 0x84, 0xdf, 0x9c, 0x25, 0x9a, 0x7c, 0x79,
    };

    std::array<uint8_t, SHA1::DIGEST_LENGTH> actual =
      SHA1::HMAC(key, message);

    CHECK_SPAN_EQ(actual, expected);
  }

  {
    std::vector<uint8_t> key(20, 0xaa);
    std::vector<uint8_t> message(50, 0xdd);

    std::vector<uint8_t> expected = {
      0x12, 0x5d, 0x73, 0x42, 0xb9, 0xac, 0x11, 0xcd, 0x91, 0xa3,
      0x9a, 0xf4, 0x8a, 0xa1, 0x7b, 0x4f, 0x63, 0xf1, 0x75, 0xd3,
    };

    std::array<uint8_t, SHA1::DIGEST_LENGTH> actual =
      SHA1::HMAC(key, message);

    CHECK_SPAN_EQ(actual, expected);
  }
}

int main(int argc, char** argv) {

  ReidTests();
  TestHMAC();

  Print("OK\n");
  return 0;
}
