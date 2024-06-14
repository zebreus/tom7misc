
#include "zip.h"

#include <cstdio>
#include <vector>
#include <string_view>
#include <cstdint>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "randutil.h"
#include "util.h"

static std::string DumpString(std::string_view s) {
  std::string out = StringPrintf("%d bytes:\n", (int)s.size());

  for (int p = 0; p < (int)s.size(); p += 16) {
    // Print line, first hex, then ascii
    for (int i = 0; i < 16; i++) {
      int idx = p + i;
      if (idx < (int)s.size()) {
        uint8_t c = s[idx];
        StringAppendF(&out, "%02x ", c);
      } else {
        StringAppendF(&out, " . ");
      }
    }

    StringAppendF(&out, "| ");

    for (int i = 0; i < 16; i++) {
      int idx = p + i;
      if (idx < (int)s.size()) {
        uint8_t c = s[idx];
        if (c >= 32 && c < 127) {
          StringAppendF(&out, "%c", c);
        } else {
          StringAppendF(&out, ".");
        }
      } else {
        StringAppendF(&out, " ");
      }
    }

    StringAppendF(&out, "\n");
  }

  return out;
}

static std::string DumpVector(const std::vector<uint8_t> &v) {
  return DumpString(std::string_view{(const char *)v.data(), v.size()});
}

static void TestOneRoundTrip(const std::string &s) {
  // printf("Input string:\n%s\n", DumpString(s).c_str());
  std::string enc = ZIP::ZipString(s, 7);
  // printf("Encoded:\n%s\n", DumpString(enc).c_str());
  std::string dec = ZIP::UnzipString(enc);
  CHECK(dec == s) << "Round trip failed for: " << s << "\n"
    "Which encoded to:\n" << DumpString(enc) <<
    "And then decoded to:\n" << DumpString(dec);
}

static void TestOneRoundTrip(const std::vector<uint8_t> &v) {
  // printf("Test vec:\n%s\n", DumpVector(v).c_str());
  printf("Size %d\n", (int)v.size());
  std::vector<uint8_t> enc = ZIP::ZipVector(v, 7);
  std::vector<uint8_t> dec = ZIP::UnzipVector(enc);
  CHECK(dec == v) << "Round trip failed for: " << DumpVector(v) << "\n"
    "Which encoded to:\n" << DumpVector(enc) <<
    "And then decoded to:\n" << DumpVector(dec);
}

static void TestRoundTripRandom() {
  ArcFour rc("zip_test");

  static constexpr int NUM_ROUNDS = 4096;
  for (int i = 0; i < NUM_ROUNDS; i++) {
    int size = 1 + RandTo(&rc, 131072 * 2);
    // int size = 128;
    std::vector<uint8_t> v;
    v.reserve(size);
    for (int x = 0; x < size; x++) v.push_back(rc.Byte());

    TestOneRoundTrip(v);
  }

}

static void TestRoundTripFixed() {
  // TODO: Generate random test vectors (making sure to have some that
  // are compressible) and test that s == decompress(compress(s)).

  // TestOneRoundTrip("");

  TestOneRoundTrip("hi");

  TestOneRoundTrip(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "123456789 3.141592653589\n"
      "aaaaaaaaaaaaaa"
      "a aardvark. a albatros. a appalacian.\n"
      "0000000001111111112222222222333333333\n"
      "2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384\n");
}

static void TestRegression() {
  std::string s(32769, 'x');
  TestOneRoundTrip(s);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestRegression();
  TestRoundTripFixed();
  TestRoundTripRandom();

  printf("OK\n");
  return 0;
}
