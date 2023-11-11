
#include "bignbr.h"
#include "bigconv.h"

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"

#include "modmult.h"
#include "bigconv.h"

static void TestNumLimbs() {
  for (const std::string bs : {"0", "1", "-1", "2", "-2", "3", "4", "5",
      "2147483647", "2147483648", "2147483649",
      "4294967295", "4294967296", "4294967297",
      "18446744073709551615", "18446744073709551616", "18446744073709551617",
      "115792089237316195423570985008687907853269984665640564039457584007913129639935",
      "115792089237316195423570985008687907853269984665640564039457584007913129639936",
      "115792089237316195423570985008687907853269984665640564039457584007913129639937"
      "1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001"}) {
    BigInt B(bs);
    BigInteger b;
    BigIntToBigInteger(B, &b);

    const int num_limbs = BigIntNumLimbs(B);
    CHECK(num_limbs == b.nbrLimbs) << B.ToString();

    std::vector<limb> limbs;
    limbs.resize(num_limbs);
    BigIntToFixedLimbs(B, num_limbs, limbs.data());

    for (int i = 0; i < num_limbs; i++) {
      CHECK(b.Limbs[i].x == limbs[i].x) << B.ToString() << " @ " << i;
    }
  }

  printf("NumLimbs " AGREEN("OK") "\n");
}



static void Montgomery() {
  // Test from original alpertron code.


  const BigInt Modulus("1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001");

  for (const bool with_array : {true, false}) {

    const std::unique_ptr<MontgomeryParams> params = [&]() {
        if (with_array) {
          limb TheModulus[MAX_LEN];
          const int modulus_length = BigIntToLimbs(Modulus, TheModulus);
          TheModulus[modulus_length].x = 0;
          return GetMontgomeryParams(modulus_length, TheModulus);
        } else {
          return GetMontgomeryParams(Modulus);
        }
      }();

    BigInt N = LimbsToBigInt(params->MontgomeryMultN, params->modulus_length);
    BigInt R1 = LimbsToBigInt(params->MontgomeryMultR1, params->modulus_length);
    BigInt R2 = LimbsToBigInt(params->MontgomeryMultR2, params->modulus_length);

    CHECK(R1 == BigInt("24695268717247353376024094994637646342633788102645274852325180976134729557037162826241102651487225375781959289009"));
    CHECK(R2 == BigInt("190098254628648626850155858417461866966631571241684111915135769130076389371840963052220660360120514221998874973069"));
    CHECK(N == BigInt("5146057778955676958024459434755086258061417362313348376976792088453485271799426894988203925673894606468119708364663947265"));

    // R1 is the identity.
    BigInt FirstFactor = R1;
    BigInt SecondFactor(32);

    limb f1[MAX_LEN], f2[MAX_LEN];
    BigIntToFixedLimbs(FirstFactor, params->modulus_length, f1);
    BigIntToFixedLimbs(SecondFactor, params->modulus_length, f2);
    limb product[MAX_LEN];
    ModMult(*params, f1, f2, product);

    BigInt Product = LimbsToBigInt(product, params->modulus_length);

    CHECK(Product == 32);

    printf("Montgomery tests " AGREEN("OK") "\n");
  }
}

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

static std::string BigBytes(const BigInt &X) {
  limb x[MAX_LEN];
  int n = BigIntToLimbs(X, x);
  std::string ret;
  for (int i = 0; i < n; i++) {
    if (i != 0) ret.push_back(':');
    StringAppendF(&ret, "%08x", x[i].x);
  }
  return ret;
}

static void WrapModMult(const BigInt &A,
                        const BigInt &B,
                        const BigInt &Modulus,
                        const BigInt &Expected) {

  auto ProblemIn = [&]() {
      return StringPrintf("%s * %s mod %s\n",
                          A.ToString().c_str(),
                          B.ToString().c_str(),
                          Modulus.ToString().c_str());
    };

  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(Modulus);
  const int modulus_length = params->modulus_length;

  CHECK(BigIntNumLimbs(A) <= modulus_length) << ProblemIn();
  CHECK(BigIntNumLimbs(B) <= modulus_length) << ProblemIn();

  limb out[modulus_length + 1];
  out[modulus_length].x = 0xCAFE;

  limb alimbs[modulus_length], blimbs[modulus_length];
  BigIntToFixedLimbs(A, modulus_length, alimbs);
  BigIntToFixedLimbs(B, modulus_length, blimbs);

  // These could fail if a, b are larger than the modulus to start!
  CHECK(LimbsToBigInt(alimbs, modulus_length) == A);
  CHECK(LimbsToBigInt(blimbs, modulus_length) == B);

  ModMult(*params, alimbs, blimbs, out);

  // XXX check properties! We would need to convert back to
  // standard form though; the arguments and result are in
  // montgomery form.

  BigInt Q = LimbsToBigInt(out, modulus_length);

  CHECK(Q < Modulus);

  auto Problem = [&]() {
      return StringPrintf("%s * %s mod %s = %s\n",
                          A.ToString().c_str(),
                          B.ToString().c_str(),
                          Modulus.ToString().c_str(),
                          Q.ToString().c_str());
    };

  CHECK(Q == Expected) << Problem() <<
    "\nGot:    " << Q.ToString() <<
    "\nwanted: " << Expected.ToString() <<
    "\nQ bytes:  " << BigBytes(Q) <<
    "\nExpected: " << BigBytes(Expected);
}

static void TestModMult() {

  // When modulus_length = 1, we don't use montgomery multiplication.
  WrapModMult(BigInt(0),
              BigInt(0),
              BigInt(7),
              BigInt(0));

  WrapModMult(BigInt(1),
              BigInt(2),
              BigInt(7),
              BigInt(2));

  // Special behavior for powers of two moduli.
  WrapModMult(BigInt("111111111222222222223"),
              BigInt("387492873491872371"),
              // 2^256
              BigInt("115792089237316195423570985008687907853269984665640564039457584007913129639936"),
              BigInt("43054763764373916054953869012715900733"));

  WrapModMult(BigInt("111111111222222222223"),
              BigInt("387492873491872371"),
              // 2^256
              BigInt("115792089237316195423570985008687907853269984665640564039457584007913129639936"),
              BigInt("43054763764373916054953869012715900733"));

  WrapModMult(BigInt("515377520732011331036461129765621272702107522001"),
              BigInt("7888609052210118054117285652827862296732064351090230047702789306640625"),
              // 2^275
              BigInt("60708402882054033466233184588234965832575213720379360039119137804340758912662765568"),
              BigInt("51153539926300668965516258108723933397314872108214048858850678752661169302105362369"));

  WrapModMult(BigInt("15232"),
              BigInt("90210"),
              // 2^77
              BigInt("151115727451828646838272"),
              BigInt("1374078720"));

  WrapModMult(BigInt("15232"),
              BigInt("9021000000000000000000000000"),
              // 2^77
              BigInt("151115727451828646838272"),
              BigInt("127623905056672907788288"));

  WrapModMult(BigInt("152320000000000000000"),
              BigInt("90210"),
              // 2^77
              BigInt("151115727451828646838272"),
              BigInt("140371729335421784555520"));

  // This is the result I get from alpertron (tomtest.c).
  WrapModMult(BigInt("3"),
              BigInt("15"),
              BigInt("9817329874928374987171"),
              BigInt("999295037051484147276"));

  WrapModMult(BigInt("99999999999999999997"),
              BigInt("48"),
              BigInt("99999999999999999999"),
              BigInt("61740692676699804096"));

  WrapModMult(
      BigInt("27"),
      BigInt("777777777777777777777777777771111111111111111111111112"),
      BigInt("911111111111111111111111111111111111111111111111111177"),
      BigInt("609106670332766614883443718117542104223785225502515346"));

  WrapModMult(BigInt("82547317664115340789"),
              BigInt("17619819104174798134"),
              BigInt("88888888833117981921"),
              BigInt("54076733533037296511"));

  printf("ModMult " AGREEN("OK") "\n");
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
    const BigInt Quot = GeneralModularDivision(Num, Den, Modulus);

    // I think that the quotient * the denominator should give us back
    // the numerator (mod the modulus), unless there's some undocumented
    // assumption here? (Or perhaps it's assuming Montgomery form for
    // some of these.)
    //
    // It's possible that GeneralModularDivision is just broken (perhaps
    // by me); it's only called in one place and that code isn't covered.
    // It does do some funny business when the modulus is a multiple
    // of 2, where I might have conflated two different modulus_lengths.

    const BigInt Prod = (Quot * Den) % Modulus;
    CHECK(Prod == NMod) << Problem() << "\n"
      "General Division (definitional)\n"
      "Got Quot: " << Quot.ToString() << "\n"
      "Q * D:    " << (Quot * Den).ToString() << "\n"
      "So  Prod: " << Prod.ToString() << "\n"
      "But want: " << NMod.ToString() << "\n";

    CHECK(Quot == Expected) << Problem() << "\n"
      "General Division (particular choice)\n"
      "Got:  " << Quot.ToString() << "\n"
      "Want: " << Expected.ToString() << "\n"
      "---- also ---\n"
      "Prod: " << Prod.ToString() << "\n"
      "NMod: " << NMod.ToString();
  }


  limb TheModulus[MAX_LEN];
  const int modulus_length = BigIntToLimbs(Modulus, TheModulus);
  TheModulus[modulus_length].x = 0;

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
  printf("---division---\n");

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

static void TestGeneralDivision() {
  auto GeneralDivide = [](const BigInt &Num,
                          const BigInt &Den,
                          const BigInt &Mod,
                          const BigInt &Expected) {
      BigInt NMod = Num % Mod;
      if (NMod < 0) NMod += Mod;
      const BigInt Res = GeneralModularDivision(Num, Den, Mod);

      printf("%s / %s mod %s =\n"
             "%s\n",
             Num.ToString().c_str(),
             Den.ToString().c_str(),
             Mod.ToString().c_str(),
             Res.ToString().c_str());

      BigInt Prod = (Res * Den) % Mod;
      if (Prod < 0) Prod += Mod;
      CHECK(Res == Expected) << Res.ToString() <<
        "\nbut wanted\n" << Expected.ToString() <<
        "\nNum:  " << Num.ToString() <<
        "\nDen:  " << Den.ToString() <<
        "\nMod:  " << Mod.ToString() <<
        "\n--- also ----"
        "\nN%M:  " << NMod.ToString() <<
        "\nProd: " << Prod.ToString();

    };

  // Reference values come from original alpertron via
  // tomtest.c.
  /*
  GeneralDivide(BigInt(1), BigInt(2), BigInt(8),
                BigInt(0));

  GeneralDivide(BigInt("928374917"), BigInt("28341"),
                BigInt("1000000000000000044444"),
                BigInt("526816273243710647073"));

  GeneralDivide(BigInt("928374917"), BigInt("28341"),
                BigInt("10000000000000000444441"),
                BigInt("3189725133199254303619"));


  GeneralDivide(BigInt("9283749173472394717727"),
                BigInt("3371717283747271128341"),
                BigInt("10900090090099900444441"),
                BigInt("1881438153010669145071"));
  */

  GeneralDivide(
      BigInt("11872398472983741987239487198273948719238"),
      BigInt("61875555555555541987239487192222248990000"),
      // 17 * 13 * 2^128
      BigInt("75202403089527400425405788242420774731776"),
      BigInt("22721449913053266398484183918334149103616"));

}

static void WrapPowBaseInt(const BigInt &Modulus,
                           int base,
                           const BigInt &Exp,
                           const BigInt &Expected) {

  limb TheModulus[MAX_LEN];
  const int modulus_length = BigIntToLimbs(Modulus, TheModulus);
  TheModulus[modulus_length].x = 0;

  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus_length, TheModulus);

  // limb modpow[params->modulus_length];
  BigInt Result = ModPowBaseInt(*params, base, Exp);
  // BigInt Result = LimbsToBigInt(modpow, params->modulus_length);

  CHECK(Result == Expected) <<
    "\nWanted  " << Expected.ToString() <<
    "\nBut got " << Result.ToString();
}

static void TestModPowBaseInt() {
  WrapPowBaseInt(BigInt(333), 2, BigInt(1234), BigInt(25));
  WrapPowBaseInt(BigInt("1290387419827141"),
                 8181, BigInt("128374817123451"),
                 BigInt("521768828887416"));
  WrapPowBaseInt(
      // 2^256
      BigInt("115792089237316195423570985008687907853269984665640564039457584007913129639936"),
      1234567,
      BigInt("12837481712345111111111111171881"),
      BigInt("49719668333916770713555620214875638068519952572946181164707416399712219000519"));

}

static void WrapModPow(const BigInt &Modulus,
                       const BigInt &Base,
                       const BigInt &Exp,
                       const BigInt &Expected) {

  limb TheModulus[MAX_LEN];
  const int modulus_length = BigIntToLimbs(Modulus, TheModulus);
  TheModulus[modulus_length].x = 0;

  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus_length, TheModulus);

  limb base[params->modulus_length];
  BigIntToFixedLimbs(Base, params->modulus_length, base);

  limb modpow[params->modulus_length];
  ModPow(*params, base, Exp, modpow);

  BigInt Result = LimbsToBigInt(modpow, params->modulus_length);
  CHECK(Result == Expected) <<
    "\nWanted  " << Expected.ToString() <<
    "\nBut got " << Result.ToString();
}


static void TestModPow() {
  // From BaseInt test cases.

  WrapModPow(BigInt(333), BigInt(2), BigInt(1234), BigInt(25));

  // Weird that it returns a different answer than the base int version.
  // Bug? Or is the interface different because it assumes montgomery
  // form for some args? In any case, this is what alpertron does.
  WrapModPow(BigInt("1290387419827141"),
             BigInt(8181), BigInt("128374817123451"),
             BigInt("786025986329866"));

  // (Power of two exponent, so this matches the BaseInt behavior.)
  WrapModPow(
      // 2^256
      BigInt("115792089237316195423570985008687907853269984665640564039457584007913129639936"),
      BigInt(1234567),
      BigInt("12837481712345111111111111171881"),
      BigInt("49719668333916770713555620214875638068519952572946181164707416399712219000519"));

  WrapModPow(BigInt("917234897192387489127349817"),
             BigInt("120374190872938741"),
             BigInt("128374817123451"),
             BigInt("11477246917995840350430635"));
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestGeneralDivision();

  /*
  TestNumLimbs();

  Montgomery();

  TestSubModN();
  TestModMult();
  TestBIMDivision();

  TestModPowBaseInt();
  TestModPow();
  */

  printf("All explicit tests " AGREEN("OK") "\n");
  return 0;
}
