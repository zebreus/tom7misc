#include "crypt/cryptrand.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "timer.h"
#include "status-bar.h"

using int64 = int64_t;
using uint16 = uint16_t;
using uint64 = uint64_t;
using uint8 = uint8_t;

#if defined(__MINGW64__)
#include <windows.h>
#include <wincrypt.h>
#include <iostream>
#include <minwindef.h>

static void ListProviders() {
  DWORD dwIndex = 0;
  DWORD dwProvType = 0;
  DWORD dwNameLen = 0;
  char name[1024];

  Print("I want to load PROV_RSA_FULL which is {}\n",
        PROV_RSA_FULL);

  Print("Available Providers:\n");
  Print("--------------------\n");

  while (CryptEnumProviders(dwIndex, NULL, 0, &dwProvType, NULL, &dwNameLen)) {
    // Allocate a buffer for the provider name
    name[0] = '\0';
    if (CryptEnumProviders(dwIndex++, NULL, 0,
                           &dwProvType, name, &dwNameLen)) {
      Print("Provider Type: {}\n", dwProvType);
      Print("Provider Name: {}\n", name);
      Print("\n");
    }
  }
}

#else

static void ListProviders() {
  /* nothing */
}

#endif

static void TestRand() {
  CryptRand cr;
  uint64 w = cr.Word64();
  Print("This should be a different value each time: {:x}\n", w);

  uint64 w2 = cr.Word64();
  CHECK(w != w2) << "Got same 64-bit value twice in a row, "
    "which should basically never happen:\n" << w << "\n" << w2;

  {
    CryptRand cr2;
    uint64 w3 = cr2.Word64();
    uint64 w4 = cr2.Word64();
    std::set<uint64_t> distinct = {w, w2, w3, w4};
    CHECK(distinct.size() == 4) << "A new instance should give two "
      "new numbers!";
  }

  // Really simple distribution tests.
  std::unordered_map<uint8, int64> bits;
  std::unordered_map<uint8, int64> bytes;

  auto IncBits = [&bits](uint8 b) {
      for (int i = 0; i < 8; i++) {
        bits[b & 1]++;
        b >>= 1;
      }
    };

  // Keep reusing the buffer, which will catch if it is not
  // completely overwritten.
  std::vector<uint8_t> buf(640);
  for (int i = 0; i < buf.size(); i++) buf[i] = i & 0xFF;

  static constexpr int TRIALS = 100000;
  Timer run_timer;
  StatusBar status(1);
  for (int i = 0; i < TRIALS; i++) {
    if (i % 10000 == 0) {
      status.Status("{}/{} ({:.2f}%)\n",
                    i, TRIALS, (i * 100.0) / TRIALS);
    }

    cr.Bytes(buf);

    for (uint8_t byte : buf) {
      bytes[byte]++;
      IncBits(byte);
    }
  }
  double seconds = run_timer.Seconds();

  double frac0 = bits[false] / (double)(TRIALS * buf.size() * 8.0);
  double frac1 = bits[true] / (double)(TRIALS * buf.size() * 8.0);

  Print("0 bits: {} ({:.4f}%)\n"
        "1 bits: {} ({:.4f}%)\n",
        bits[false], frac0 * 100.0,
        bits[true], frac1 * 100.0);

  CHECK(frac0 >= 0.495 && frac0 <= 0.505) << "Bits are way too biased!";

  std::vector<std::pair<uint8, int64>> all;
  for (auto [byte, count] : bytes) all.emplace_back(byte, count);
  CHECK(all.size() == 256) << "Some bytes were never output after "
    "a million 64-bit words, which should basically never happen!";
  std::sort(all.begin(),
            all.end(),
            [](const std::pair<uint8, int64> a,
               const std::pair<uint8, int64> b) {
              if (a.second == b.second)
                return a.first < b.first;
              return a.second < b.second;
            });

  auto PrintOne = [&all](int idx) {
    Print("{:02x} x {} ({:.2f}%)\n",
          all[idx].first,
          all[idx].second, (100.0 * all[idx].second) / (TRIALS * 8.0));
    };
  for (int i = 0; i < 8; i++)
    PrintOne(i);
  Print("...\n");
  for (int i = 0; i < 8; i++)
    PrintOne(256 - 8 + i);

  Print("Throughput: " AGREEN("{:.3f}") " bytes/sec\n",
        (TRIALS * buf.size()) / seconds);
}

int main(int argc, char **argv) {
  ANSI::Init();
  ListProviders();

  TestRand();

  Print("OK\n");
  return 0;
}
