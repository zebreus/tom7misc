
#include "bignbr.h"
#include "bigconv.h"

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"

#include "modmult.h"
#include "bigconv.h"

static void Montgomery() {
  // Test from original alpertron code.

  BigInt Modulus("1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001");

  limb TheModulus[MAX_LEN];
  const int modulus_length = BigIntToLimbs(Modulus, TheModulus);
  TheModulus[modulus_length].x = 0;
  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus_length, TheModulus);

  BigInt N = LimbsToBigInt(params->MontgomeryMultN, modulus_length);
  BigInt R1 = LimbsToBigInt(params->MontgomeryMultR1, modulus_length);
  BigInt R2 = LimbsToBigInt(params->MontgomeryMultR2, modulus_length);

  CHECK(R1 == BigInt("24695268717247353376024094994637646342633788102645274852325180976134729557037162826241102651487225375781959289009"));
  CHECK(R2 == BigInt("190098254628648626850155858417461866966631571241684111915135769130076389371840963052220660360120514221998874973069"));
  CHECK(N == BigInt("5146057778955676958024459434755086258061417362313348376976792088453485271799426894988203925673894606468119708364663947265"));

  // R1 is the identity.
  BigInt FirstFactor = R1;
  BigInt SecondFactor(32);

  limb f1[MAX_LEN], f2[MAX_LEN];
  BigIntToFixedLimbs(FirstFactor, modulus_length, f1);
  BigIntToFixedLimbs(SecondFactor, modulus_length, f2);
  limb product[MAX_LEN];
  ModMult(*params, f1, f2, product);

  BigInt Product = LimbsToBigInt(product, modulus_length);

  CHECK(Product == 32);

  printf("Montgomery tests " AGREEN("OK") "\n");
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

  limb TheModulus[MAX_LEN];
  const int modulus_length = BigIntToLimbs(Modulus, TheModulus);
  TheModulus[modulus_length].x = 0;
  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus_length, TheModulus);

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
  // TODO
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
  BigInteger e;
  BigIntToBigInteger(Exp, &e);
  ModPow(*params, base, e.limbs, e.nbrLimbs, modpow);

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

  Montgomery();

  TestSubModN();
  TestModMult();
  TestBIMDivision();
  TestModPowBaseInt();
  TestModPow();

  printf("Explicit tests " AGREEN("OK") "\n");
  return 0;
}
