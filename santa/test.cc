
#include "ansi.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"

static bool IsGenerator(const BigInt &sg, const BigInt &p, const BigInt &g) {
  // Only 2 and sg are factors of p.
  if (BigInt::PowMod(g, BigInt(2), p) == 1)
    return false;

  if (BigInt::PowMod(g, sg, p) == 1)
    return false;

  return true;
}

static void TestData() {
  BigInt sg("97035875817856538071097984852143659850113040717180487875783371565273451246536742216736174708384786740533253396405747907150289525915530187570793986154826348563714376657128702681501301402031940740650862603724498556707966425139408498592844034405758710413665458781750608095937065069407231405292878119711821831393");

  BigInt p = sg * 2 + 1;

  // CHECK(IsGenerator(sg, p, BigInt{123456789}));
  CHECK(IsGenerator(sg, p, BigInt{2}));

  BigInt base("123456789");

  BigInt exp("12335113756362671231619");

  BigInt r = BigInt::PowMod(base, exp, sg);

  Print("r: {}\n", r.ToString());


  const auto &[g, s, t] = BigInt::ExtendedGCD(
      BigInt("123456789011223550"),
      BigInt("39881178543293726"));
  Print("g: {}\n"
        "s: {}\n"
        "t: {}\n", g.ToString(), s.ToString(), t.ToString());


  auto mo = BigInt::ModInverse(BigInt("12345678912345"), sg);
  CHECK(mo.has_value());
  Print("\nmod inverse: {}\n", mo.value().ToString());
}


int main(int argc, char **argv) {
  ANSI::Init();

  TestData();

  return 0;
}
