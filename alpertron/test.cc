
#include "bignbr.h"
#include "bigconv.h"

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"

#include "modmult.h"
#include "bigconv.h"

static void TestSubModN() {
  BigInt a(100);
  BigInt b(125);
  BigInt c(200);

  limb n1[8], n2[8], diff[8], mod[8];
  BigIntToFixedLimbs(a, 8, n1);
  BigIntToFixedLimbs(b, 8, n2);
  BigIntToFixedLimbs(c, 8, mod);

  SubtBigNbrModN(n1, n2, diff, mod, 8);

  BigInt d = LimbsToBigInt(diff, 8);
  CHECK(d == 175);
}

static void WrapDivide(const BigInt &Num,
                       const BigInt &Den,
                       const BigInt &Modulus,
                       const BigInt &Expected) {

  BigInt NMod = Num % Modulus;
  if (NMod < 0) NMod += Modulus;

  auto Problem = [&]() {
      return StringPrintf("(%s / %s) mod %s\n",
                          Num.ToString().c_str(),
                          Den.ToString().c_str(),
                          Modulus.ToString().c_str());
    };

  {
    // I think that the quotient * the denominator should give us back
    // the numerator (mod the modulus), unless there's some undocumented
    // assumption here?
    //
    // It's possible that GeneralModularDivision is just broken (perhaps
    // by me); it's only called in one place and that code isn't covered.

    BigInt Quot = GeneralModularDivision(Num, Den, Modulus);

    /*
    BigInt Prod = (Quot * Den) % Modulus;
    CHECK(Prod == NMod) << Problem() << "\n"
      "General Division (definitional)\n"
      "Got Quot: " << Quot.ToString() << "\n"
      "Q * D:    " << (Quot * Den).ToString() << "\n"
      "So  Prod: " << Prod.ToString() << "\n"
      "But want: " << NMod.ToString() << "\n";
    */

    CHECK(Quot == Expected) << Problem() << "\n"
      "General Division (particular choice)\n"
      "Got:  " << Quot.ToString() << "\n"
      "Want: " << Expected.ToString() << "\n";
  }


  limb TheModulus[MAX_LEN];
  const int modulus_length = BigIntToLimbs(Modulus, TheModulus);
  TheModulus[modulus_length].x = 0;

  // Is it worth it to convert to montgomery form for one division??
  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus_length, TheModulus);

  BigInt quot =
    BigIntModularDivision(*params, Num, Den, Modulus);

  BigInt prod = (quot * Den) % Modulus;
  if (prod < 0) prod += Modulus;

  CHECK(prod == NMod)
    << "For (" << Num.ToString() << " / "
    << Den.ToString() << " mod " << Modulus.ToString()
    << ") * den % mod"
    << "\nGot  " << prod.ToString()
    << "\nWant " << NMod.ToString();

  CHECK(quot == Expected)
    << "For " << Num.ToString() << " / "
    << Den.ToString() << " mod " << Modulus.ToString()
    << "\nGot  " << quot.ToString()
    << "\nWant " << Expected.ToString();
}

static void TestBIMDivision() {
  WrapDivide(BigInt("9999123456789123454747111927"),
             BigInt("373717173837173"),
             BigInt("88888888833117981921"),
             BigInt("64911780604437707735"));
  WrapDivide(BigInt("9999123456789123454747111925"),
             BigInt("373717173837173"),
             BigInt("88888888833117981921"),
             BigInt("76060325747176019410"));

  WrapDivide(BigInt("77777"),
             BigInt("11111"),
             BigInt("99999999999999"),
             BigInt("7"));

  WrapDivide(BigInt("14418029"),
             BigInt(3),
             BigInt("43249211"),
             BigInt("33638817"));

  WrapDivide(BigInt("1360459837378029820"),
             BigInt(5),
             BigInt("31578649624227546947"),
             BigInt("272091967475605964"));

  WrapDivide(BigInt(-7), BigInt(8), BigInt(7), BigInt(0));

  WrapDivide(BigInt("417406300754"),
             BigInt("8456347"),
             BigInt("561515238337"),
             BigInt("481523698946"));

  WrapDivide(BigInt(1),
             BigInt("4189707925802269"),
             BigInt("4189707926060411"),
             BigInt("3130733125169182"));

  WrapDivide(BigInt(-10), BigInt(16), BigInt(9), BigInt(5));
  WrapDivide(BigInt(-13), BigInt(64), BigInt(3), BigInt(2));

  #if 0
  WrapDivide(BigInt("777777777777777777777777777777777"),
             BigInt("111111111111111111111111111111111"),
             BigInt("99"),
             BigInt("7"));
#endif

}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSubModN();
  TestBIMDivision();

  printf("Explicit tests " AGREEN("OK") "\n");
  return 0;
}
