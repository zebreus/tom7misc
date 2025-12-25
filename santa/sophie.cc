
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "crypt/cryptrand.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "threadutil.h"

DECLARE_COUNTERS(outer_test, inner_test);

BigInt GenerateSophieGermainPrime(int bits, CryptRand *cr) {
  auto Rand = [&]() { return cr->Word64(); };

  Periodically status_per(1.0);
  StatusBar status(1);

  BigInt top = BigInt::LeftShift(BigInt{1}, bits);
  BigInt msb = BigInt::LeftShift(BigInt{1}, bits - 1);

  static constexpr int NUM_THREADS = 8;
  std::vector<ArcFour> rcs;
  for (int i = 0; i < NUM_THREADS; i++) {
    std::string seed = std::format("{}.{}.", time(nullptr), i);
    for (int w = 0; w < (bits / 64) + 1; w++) {
      AppendFormat(&seed, "{:016x}", Rand());
    }
    rcs.emplace_back(seed);
  }

  BigInt sg =
    ParallelAttempt([&](int64_t idx) -> std::optional<BigInt> {
      ArcFour &rc = rcs[idx];
      BigInt x = BigInt::RandTo([&rc]() { return Rand64(&rc); }, top);

      x = BigInt::BitwiseOr(std::move(x), msb);

      CHECK(x > 0);

      // Start with an odd number.
      if (x.IsEven()) {
        x = x + 1;
      }

      for (int att = 0; att < 10; att++) {
        outer_test++;
        if (BigInt::IsProbablyPrime(x, 64)) {
          BigInt y = x * 2 + 1;
          inner_test++;
          if (BigInt::IsProbablyPrime(y, 64)) {
            return std::make_optional(x);
          }
        }
        x += 2;
        status_per.RunIf([&]() {
            int64_t o = outer_test.Read();
            int64_t i = inner_test.Read();
            status.Status("{} outer, {} inner ({}%)",
                          o, i,
                          (i * 100.0) / o);
          });
      }

      return std::nullopt;
    }, NUM_THREADS);

  return sg;
}

bool IsGenerator(const BigInt &sg, const BigInt &p, const BigInt &g) {
  // Only 2 and sg are factors of p.
  if (BigInt::PowMod(g, BigInt(2), p) == 1)
    return false;

  if (BigInt::PowMod(g, sg, p) == 1)
    return false;

  return true;
}

BigInt GetGenerator(const BigInt &sg, CryptRand *cr) {
  auto Rand = [&]() { return cr->Word64(); };

  BigInt p = sg * 2 + 1;
  for (;;) {
    BigInt g = BigInt::RandTo(Rand, p - 2) + 1;
    CHECK(BigInt::Sign(g) == 1);

    if (IsGenerator(sg, p, g)) {
      return g;
    }

    return g;
  }
}

static void Gen(int bits) {
  CryptRand cr;
  BigInt sg = GenerateSophieGermainPrime(bits, &cr);
  BigInt p = sg * 2 + 1;
  Print(ABLUE("Sophie-Germain prime") ": {}\n"
        "\n"
        APURPLE("Safe prime 2p+1") ": {}\n",
        sg.ToString(), p.ToString());

  BigInt g = GetGenerator(sg, &cr);
  Print("\n"
        ACYAN("Generator") ": {}\n",
        g.ToString());

  Print("\n"
        ACYAN("Small generators") ":");
  for (int i = 2; i < 10; i++) {
    if (IsGenerator(sg, p, BigInt(i))) {
      Print(" {}", i);
    }
  }
  Print("\n");
}


int main(int argc, char **argv) {
  ANSI::Init();

  // Gen(2048);
  // Gen(1024);
  Gen(16);

  return 0;
}
