
#include "zip.h"

#include <cstdio>

#include "ansi.h"
#include "arcfour.h"
#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"

static std::string DumpString(const std::string &s) {
  std::string out = StringPrintf("%d bytes:\n", (int)s.size());

  for (int p = 0; p < (int)s.size(); p += 16) {
    // Print line, first hex, then ascii
    for (int i = 0; i < 16; i++) {
      int idx = p * 16 + i;
      if (idx < (int)s.size()) {
        char c = s[idx];
        StringAppendF(&out, "%02x ", c);
      } else {
        StringAppendF(&out, " . ");
      }
    }

    StringAppendF(&out, "| ");

    for (int i = 0; i < 16; i++) {
      int idx = p * 16 + i;
      if (idx < (int)s.size()) {
        char c = s[idx];
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


static void TestOneRoundTrip(const std::string &s) {
  std::string enc = ZIP::ZipString(s, 7);
  std::string dec = ZIP::UnzipString(enc);
  CHECK(dec == s) << "Round trip failed for: " << s << "\n"
    "Which encoded to:\n" << DumpString(enc) <<
    "And then decoded to:\n" << DumpString(dec);
}

static void TestRoundTrip() {
  // TODO: Generate random test vectors (making sure to have some that
  // are compressible) and test that s == decompress(compress(s)).

  TestOneRoundTrip("hi");

  TestOneRoundTrip(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "123456789 3.141592653589\n"
      "aaaaaaaaaaaaaa"
      "a aardvark. a albatros. a appalacian.\n"
      "0000000001111111112222222222333333333\n"
      "2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384\n");
}


int main(int argc, char **argv) {
  ANSI::Init();

  TestRoundTrip();

  printf("OK\n");
  return 0;
}
