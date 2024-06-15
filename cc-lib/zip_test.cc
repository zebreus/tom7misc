
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

static std::string DumpStringDiff(std::string_view s1,
                                  std::string_view s2) {
  std::string out = StringPrintf("%d bytes vs %d bytes\n",
                                 (int)s1.size(), (int)s2.size());
# define ANSI_LEFT  ANSI_FG(210, 128, 128)
# define ANSI_RIGHT ANSI_FG(210, 160, 112)

  bool equal_prefix = true;

  auto Equal16 = [s1, s2](int p) {
      for (int i = 0; i < 16; i++) {
        const int idx = p + i;
        if (idx >= s1.size() || idx >= s2.size()) return false;
        if (s1[idx] != s2[idx]) return false;
      }
      return true;
    };

  for (int p = 0; p < (int)std::max(s1.size(), s2.size()); p += 16) {
    if (equal_prefix && Equal16(p)) {
      continue;
    } else if (equal_prefix) {
      if (p > 0) {
        StringAppendF(&out,
            AGREY("...") " " ABLUE("%d equal bytes") " " AGREY("...") "\n",
            p);
      }
      equal_prefix = false;
    }

    // Print line, first hex, then ascii
    for (int i = 0; i < 16; i++) {
      const int idx = p + i;
      const int c1 = idx < s1.size() ? s1[idx] : -1;
      const int c2 = idx < s2.size() ? s2[idx] : -1;
      if (c1 == c2) {
        if (c1 >= 0) {
          StringAppendF(&out, AGREY("%02x=="), (uint8_t)c1);
        } else {
          StringAppendF(&out, AGREY("::::"));
        }
      } else {
        #define L
        StringAppendF(&out, ANSI_LEFT);
        if (c1 >= 0) {
          StringAppendF(&out, "%02x", (uint8_t)c1);
        } else {
          StringAppendF(&out, "__");
        }

        StringAppendF(&out, ANSI_RIGHT);
        if (c2 >= 0) {
          StringAppendF(&out, "%02x", (uint8_t)c2);
        } else {
          StringAppendF(&out, "__");
        }
      }
    }

    #if 0
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
    #endif

    StringAppendF(&out, ANSI_RESET "\n");
  }

  return out;
}


static std::string DumpVector(const std::vector<uint8_t> &v) {
  return DumpString(std::string_view{(const char *)v.data(), v.size()});
}

static std::string DumpVectorDiff(const std::vector<uint8_t> &v1,
                                  const std::vector<uint8_t> &v2) {
  return DumpStringDiff(std::string_view{(const char *)v1.data(), v1.size()},
                        std::string_view{(const char *)v2.data(), v2.size()});
}

static void TestOneRoundTrip(const std::string &s) {
  // printf("Input string:\n%s\n", DumpString(s).c_str());
  printf(AWHITE("Encode %d bytes:") "\n", (int)s.size());
  std::string enc = ZIP::ZipString(s, 7);
  printf(AWHITE("Decode %d bytes:") "\n", (int)enc.size());
  // printf("Encoded:\n%s\n", DumpString(enc).c_str());
  std::string dec = ZIP::UnzipString(enc);
  printf(AWHITE("Got %d bytes.") "\n", (int)dec.size());
  if (dec != s) {

    printf("%s\n", DumpStringDiff(s, dec).c_str());

    printf("Input " AWHITE("%d") " bytes, encoded " AYELLOW("%d")
           ", decoded " APURPLE("%d") "\n",
           (int)s.size(), (int)enc.size(), (int)dec.size());

    LOG(FATAL) << "Diffs";
    LOG(FATAL) << "Round trip failed for: " << s << "\n"
      "Which encoded to:\n" << DumpString(enc) <<
      "And then decoded to:\n" << DumpString(dec);
  }
}

static void TestOneRoundTrip(const std::vector<uint8_t> &v) {
  // printf("Test vec:\n%s\n", DumpVector(v).c_str());
  printf("Size %d\n", (int)v.size());
  std::vector<uint8_t> enc = ZIP::ZipVector(v, 7);
  std::vector<uint8_t> dec = ZIP::UnzipVector(enc);
  if (dec != v) {

    printf("%s\n", DumpVectorDiff(v, dec).c_str());
    LOG(FATAL) << "Diffs.\n";

    LOG(FATAL) << "Round trip failed for: " << DumpVector(v) << "\n"
      "Which encoded to:\n" << DumpVector(enc) <<
      "And then decoded to:\n" << DumpVector(dec);
  }
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

static void TestLong() {
  std::string s(33333, 'x');
  for (int i = 0; i < s.size(); i = i + (i >> 2) + 1) {
    s[i] = 'O';
  }
  TestOneRoundTrip(s);
}

int main(int argc, char **argv) {
  ANSI::Init();

  // TestRegression();
  TestLong();
  /*
  TestRoundTripFixed();
  TestRoundTripRandom();
  */

  printf("OK\n");
  return 0;
}
