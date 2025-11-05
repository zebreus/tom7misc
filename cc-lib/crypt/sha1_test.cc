
/*
  Based on code by Steve Reid and James H. Brown in the 1990s.
  Public domain.
*/

#include "sha1.h"

#include <cstdint>
#include <cstring>
#include <span>

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


/*
Test Vectors (from FIPS PUB 180-1)
"abc"
  A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
A million repetitions of "a"
  34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/


static const char *test_data[] = {
    "abc",
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
    "A million repetitions of 'a'"};
static const char *test_results[] = {
    "A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D",
    "84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1",
    "34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F"};
static void digest_to_hex(const uint8_t digest[SHA1::DIGEST_LENGTH],
                          char *output) {
    int i,j;
    char *c = output;
    for (i = 0; i < SHA1::DIGEST_LENGTH / 4; i++) {
        for (j = 0; j < 4; j++) {
            sprintf(c,"%02X", digest[i*4+j]);
            c += 2;
        }
        sprintf(c, " ");
        c += 1;
    }
    *(c - 1) = '\0';
}

static void ReidTests() {
  SHA1::Ctx context;
  uint8_t digest[20];
  char output[80];

  for (int k = 0; k < 2; k++) {
    SHA1::Init(&context);
    SHA1::Update(&context, (uint8_t*)test_data[k], strlen(test_data[k]));
    SHA1::Finalize(&context, digest);
    digest_to_hex(digest, output);
    if (strcmp(output, test_results[k])) {
      fprintf(stdout, "FAIL\n");
      fprintf(stderr,"* hash of \"%s\" incorrect:\n", test_data[k]);
      fprintf(stderr,"\t%s returned\n", output);
      fprintf(stderr,"\t%s is correct\n", test_results[k]);
      LOG(FATAL) << "Failed";
    }
  }

  /* million 'a' vector we feed separately */
  SHA1::Init(&context);
  for (int k = 0; k < 1000000; k++) {
    SHA1::Update(&context, (uint8_t*)"a", 1);
  }
  SHA1::Finalize(&context, digest);
  digest_to_hex(digest, output);
  if (strcmp(output, test_results[2])) {
    fprintf(stdout, "FAIL\n");
    fprintf(stderr,"* hash of \"%s\" incorrect:\n", test_data[2]);
    fprintf(stderr,"\t%s returned\n", output);
    fprintf(stderr,"\t%s is correct\n", test_results[2]);
    LOG(FATAL) << "Failed";
  }
}

static std::vector<uint8_t> StringVec(std::string_view v) {
  std::vector<uint8_t> ret(v.size());
  for (size_t i = 0; i < v.size(); i++) {
    ret[i] = v[i];
  }
  return ret;
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
