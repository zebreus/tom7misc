
#include "rle.h"

#include <cstdio>
#include <string_view>
#include <vector>
#include <string>
#include <cstdint>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "hexdump.h"
#include "randutil.h"
#include "status-bar.h"
#include "timer.h"

using uint8 = uint8_t;

static constexpr bool VERBOSE = false;

using namespace std;

static string ShowVector(const vector<uint8> &v) {
  string s = "{";
  for (int i = 0; i < (int)v.size(); i++) {
    AppendFormat(&s, "{}, ", v[i]);
  }
  return s + "}";
}

static void CheckSameVector(const vector<uint8> &a,
                            const vector<uint8> &b) {
  CHECK_EQ(a.size(), b.size()) << "\n"
                               << ShowVector(a) << "\n" << ShowVector(b);
  for (int i = 0; i < (int)a.size(); i++) {
    CHECK_EQ(a[i], b[i]) << "\n" << ShowVector(a) << "\n" << ShowVector(b);
  }
}

static void DecoderTests() {
  vector<uint8> empty = {};
  vector<uint8> d_empty = RLE::Decompress(empty);
  CHECK(d_empty.empty());

  vector<uint8> simple_run = {
    // Run of 4
    3,
    // Value = 42
    42,
  };
  CheckSameVector({42, 42, 42, 42}, RLE::Decompress(simple_run));

  vector<uint8> simple_singletons = {
    // Singletons
    0, 1, 0, 2, 0, 3, 0, 4,
  };
  CheckSameVector({1, 2, 3, 4}, RLE::Decompress(simple_singletons));

  vector<uint8> small = {
    // Run of 4x42
    3, 42,
    // Then a singleton zero
    0, 0,
    // Run of 2x99
    1, 99,
  };
  CheckSameVector({42, 42, 42, 42, 0, 99, 99},
                  RLE::Decompress(small));
}

static void EncoderTests() {
  // These don't strictly need to encode this way.
  CheckSameVector({3, 42}, RLE::Compress({42, 42, 42, 42}));
  CheckSameVector({3, 42, 0, 0, 1, 99},
                  RLE::Compress({42, 42, 42, 42, 0, 99, 99}));
  CheckSameVector({3, 42, 0, 0, 1, 99, 0, 8},
                  RLE::Compress({42, 42, 42, 42, 0, 99, 99, 8}));
}

static void TestOffByOne() {
  // Regression: This used to be unable to decode an anti-run at
  // the very end of the stream.
  vector<uint8> buggy_input = {129, 10, 20};

  vector<uint8> out;
  CHECK(RLE::DecompressEx(buggy_input, 128, &out));
  CheckSameVector({10, 20}, out);
}

static void TestRoundTrip(int line, std::string_view s) {
  std::vector<uint8_t> orig;
  for (size_t i = 0; i < s.size(); i++)
    orig.push_back(s[i]);
  std::vector<uint8_t> enc = RLE::Compress(orig);
  std::vector<uint8_t> dec;
  bool success = RLE::DecompressEx(enc, RLE::DEFAULT_CUTOFF, &dec);

  if (VERBOSE) {
    LOG(INFO) <<
      "On line " << line << "!\n"
      "Input: " << s << "\n"
      "Encoded to:\n" << HexDump::Color(enc) << "\n"
      "Decode: " << (success ? AGREEN("true") : ARED("false")) << "\n"
      "To:\n" << HexDump::Color(dec) << "\n";
  }

  CHECK(success && orig == dec) <<
    "Failed on line " << line << "!\n"
    "Input: " << s << "\n"
    "Encoded to:\n" << HexDump::Color(enc) << "\n"
    "Decode: " << (success ? AGREEN("true") : ARED("false")) << "\n"
    "To:\n" << HexDump::Color(dec) << "\n";
}

static void TestRoundTrips() {
  #define TEST_ROUND_TRIP(s_in) TestRoundTrip(__LINE__, s_in "");

  TEST_ROUND_TRIP("");
  TEST_ROUND_TRIP("a");
  TEST_ROUND_TRIP("ab");
  TEST_ROUND_TRIP("abc");
  TEST_ROUND_TRIP("abcabc");
  TEST_ROUND_TRIP("abcabcabcabc");
  TEST_ROUND_TRIP("abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc"
                  "abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc"
                  "abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc"
                  "abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc"
                  "abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc");
  TEST_ROUND_TRIP("baa");
  TEST_ROUND_TRIP("baaaaaaaaaaa");
  TEST_ROUND_TRIP("baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  TEST_ROUND_TRIP("baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  TEST_ROUND_TRIP("baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}

static void TestMany() {
  ArcFour rc{"rle_test"};
  int64_t compressed_bytes = 0, uncompressed_bytes = 0;
  #define NUM_TESTS 2000
  Timer timer;
  StatusBar status(1);
  for (int cutoff = 0; cutoff < 256; cutoff++) {
    if (cutoff % 10 == 0) {
      status.Progress(cutoff, 256, "benchmark / test");
    }

    const uint8 run_cutoff = cutoff;
    for (int test_num = 0; test_num < NUM_TESTS; test_num++) {
      int len = RandTo(&rc, 2048);
      CHECK_LT(len, 2048);
      vector<uint8> bytes;
      bytes.reserve(len);
      for (int j = 0; j < len; j++) {
        if (rc.Byte() < 10) {
          int runsize = rc.Byte() + rc.Byte();
          const uint8 target = rc.Byte();
          while (runsize--) {
            bytes.push_back(target);
            j++;
          }
        } else {
          bytes.push_back(rc.Byte());
        }
      }

      uncompressed_bytes += bytes.size();
      // fprintf(stderr, "Start: %s\n", ShowVector(bytes).c_str());
      vector<uint8> compressed = RLE::CompressEx(bytes, run_cutoff);
      // fprintf(stderr, "Compressed: %s\n", ShowVector(compressed).c_str());
      compressed_bytes += compressed.size();

      vector<uint8> uncompressed;
      CHECK(RLE::DecompressEx(compressed, run_cutoff, &uncompressed))
        << " test_num " << test_num;
      CHECK_EQ(uncompressed.size(), bytes.size());
      for (int i = 0; i < (int)uncompressed.size(); i++) {
        CHECK_EQ(uncompressed[i], bytes[i]) << " test_num "
                                            << test_num
                                            << " byte #" << i;
      }
    }
  }
  double elapsed = timer.Seconds();
  Print("\n"
        "Total uncompressed: {}\n"
        "Total compressed:   {}\n"
        "Average ratio: {:.3f}:1\n"
        "Time: {}\n"
        "kB/sec (round trip plus validation): {:.1f}\n",
        uncompressed_bytes,
        compressed_bytes,
        (double)uncompressed_bytes / compressed_bytes,
        ANSI::Time(elapsed),
        (uncompressed_bytes / 1000.0) / elapsed);
}

int main(int argc, char **argv) {
  ANSI::Init();

  DecoderTests();
  EncoderTests();
  TestRoundTrips();

  TestOffByOne();

  TestMany();

  Print("OK\n");
  return 0;
}
