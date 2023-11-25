
#include "bignbr.h"
#include "bigconv.h"

#include <cstdint>
#include <initializer_list>
#include <numeric>

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/int128.h"
#include "ansi.h"

#include "modmult.h"
#include "bigconv.h"

static void TestDivFloor() {
  for (int n = -15; n < 16; n++) {
    for (int d = -15; d < 16; d++) {
      if (d != 0) {
        int64_t q = DivFloor64(n, d);
        BigInt Q = BigInt::DivFloor(BigInt(n), BigInt(d));
        CHECK(Q == q) << n << "/" << d << " = " << q
                      << "\nWant: " << Q.ToString();
      }
    }
  }
  printf("DivFloor " AGREEN("OK") "\n");
}

static void TestJacobi() {
  for (int64_t a : std::initializer_list<int64_t>{
      -65537, -190187234, -88, -16, -15, -9, -8, -7, -6, -5, -4, -3, -2, -1,
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31337, 123874198273}) {
    for (int64_t b : std::initializer_list<int64_t>{
        1, 3, 5, 7, 9, 119, 121, 65537, 187238710001}) {

      int bigj = BigInt::Jacobi(BigInt(a), BigInt(b));
      int j = Jacobi64(a, b);
      CHECK(bigj == j) << a << "," << b <<
        "\nGot:  " << j <<
        "\nWant: " << bigj;
    }
  }
  printf("Jacobi " AGREEN("OK") "\n");
}

static void TestGCD() {
  for (int64_t a : std::initializer_list<int64_t>{
      -65537, -190187234, -88, -2, -1,
      0, 1, 2, 3, 16, 31337, 123874198273}) {
    for (int64_t b : std::initializer_list<int64_t>{
        -23897417233, -222222, -32767, -31337, -3, -1,
        0, 1, 2, 5, 6, 120, 65536, 18723871000}) {
      const auto &[gcd, x, y] = ExtendedGCD64(a, b);
      const BigInt gcd2 = BigInt::GCD(BigInt(a), BigInt(b));
      CHECK(gcd == gcd2) << a << "," << b << ": gcd="
                         << gcd << " but BigInt::GCD=" << gcd2.ToString();
      const int64_t gcd3 = std::gcd(a, b);
      CHECK(gcd == gcd3) << a << "," << b << ": gcd="
                         << gcd << " but std::gcd=" << gcd3;

      const auto &[big_gcd, big_x, big_y] =
        BigInt::ExtendedGCD(BigInt(a), BigInt(b));

      /*
        // Note: We don't necessarily return the same solution as
        // BigInt (it seems to return the *minimal* pair). I don't
        // think we need to compute the minimal.
      CHECK(big_gcd == gcd && big_x == x && big_y == y)
        << a << "," << b <<
        "\nEGCD64:  " << gcd << " " << x << " " << y <<
        "\nBigEGCD: " << big_gcd.ToString() << " " << big_x.ToString()
        << " " << big_y.ToString();
      */

      // Even though the gcd must be 64-bit, the intermediate products
      // can overflow.
      CHECK(int128_t(a) * int128_t(x) +
            int128_t(b) * int128_t(y) == int128_t(gcd))
        << "For " << a << "," << b <<
        "\nWe have " << a << " * " << x << " + " << b << " * " << y
        << " = " << (int128_t(a) * int128_t(x) +
                     int128_t(b) * int128_t(y)) <<
        "\nBut expected the gcd: " << gcd;

      if (gcd == 1 && b != 0) {
        int64_t ainv = ModularInverse64(a, b);
        std::optional<BigInt> oainv2 = BigInt::ModInverse(BigInt(a), BigInt(b));
        CHECK(oainv2.has_value()) << a << "," << b;
        if (abs(b) != 1) {
          // We don't return the same value as BigInt for a modulus of 1,
          // but we don't care (anything is an inverse as this ring is
          // degenerate). This case is not useful for alpertron, since
          // we're taking inverses mod a prime power.
          CHECK(ainv == oainv2.value())
            << a << "," << b
            << "\nGot inv: " << ainv
            << "\nBut BigInt::ModInverse: " << oainv2.value().ToString();
        }

        // Following GMP, we use |b|
        int128_t r = (int128_t(a) * int128_t(ainv)) % int128_t(abs(b));
        if (r < 0) r += int128_t(abs(b));
        // mod b again since abs(b) could be 1, and 0 is correct in this
        // case.
        int128_t onemodb = int128_t(1) % int128_t(abs(b));
        CHECK(r == onemodb)
              << "For " << a << "," << b
              << "\nhave (" << a
              << " * " << ainv << ") % " << abs(b) << " = "
              << r
              << "\nbut want " << onemodb;
      }
    }
  }
  printf("GCD " AGREEN("OK") "\n");
}

static void TestNumLimbs() {
  for (const std::string bs : {"0", "1", "2", "3", "4", "5",
      "2147483647", "2147483648", "2147483649",
      "4294967295", "4294967296", "4294967297",
      "18446744073709551615", "18446744073709551616", "18446744073709551617",
      "115792089237316195423570985008687907853269984665640564039457584007913129639935",
      "115792089237316195423570985008687907853269984665640564039457584007913129639936",
      "115792089237316195423570985008687907853269984665640564039457584007913129639937"
      "1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001"}) {
    BigInt B(bs);

    const int num_limbs = BigIntNumLimbs(B);

    std::vector<limb> limbs;
    limbs.resize(num_limbs);
    BigIntToFixedLimbs(B, num_limbs, limbs.data());

    BigInt C = LimbsToBigInt(limbs.data(), num_limbs);

    CHECK(B == C) << B.ToString() << "\n" << C.ToString();
  }

  printf("NumLimbs " AGREEN("OK") "\n");
}



// Only need to support 64-bit integers now.
static void Montgomery() {
  const uint64_t modulus = 100000000000000001ULL;

  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus);

  BigInt N = LimbsToBigInt(params->Ninv.data(), params->modulus_length);
  BigInt R1 = LimbsToBigInt(params->R1.data(), params->modulus_length);
  BigInt R2 = LimbsToBigInt(params->R2.data(), params->modulus_length);

  CHECK(R1 == BigInt("11686018427387858"));
  CHECK(R2 == BigInt("84433638898975679"));
  CHECK(N == BigInt("2324500534556753921"));

  // R1 is the identity.
  BigInt FirstFactor = R1;
  BigInt SecondFactor(32);

  limb f1[params->modulus_length], f2[params->modulus_length];
  BigIntToFixedLimbs(FirstFactor, params->modulus_length, f1);
  BigIntToFixedLimbs(SecondFactor, params->modulus_length, f2);
  limb product[params->modulus_length];
  ModMult(*params, f1, f2, product);

  BigInt Product = LimbsToBigInt(product, params->modulus_length);

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
  const int num_limbs = BigIntNumLimbs(X);
  limb x[num_limbs];
  CHECK(num_limbs == BigIntToLimbs(X, x));
  std::string ret;
  for (int i = 0; i < num_limbs; i++) {
    if (i != 0) ret.push_back(':');
    StringAppendF(&ret, "%08x", x[i].x);
  }
  return ret;
}

static void WrapModMult(const BigInt &A,
                        const BigInt &B,
                        uint64_t modulus,
                        const BigInt &Expected) {

  auto ProblemIn = [&]() {
      return StringPrintf("%s * %s mod %llu\n",
                          A.ToString().c_str(),
                          B.ToString().c_str(),
                          modulus);
    };

  CHECK(A < modulus && B < modulus) << ProblemIn();

  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus);
  const int modulus_length = params->modulus_length;

  CHECK(BigIntNumLimbs(A) <= modulus_length)
    << modulus_length << " " << ProblemIn();
  CHECK(BigIntNumLimbs(B) <= modulus_length)
    << modulus_length << " " << ProblemIn();

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

  CHECK(Q < modulus);

  auto Problem = [&]() {
      return StringPrintf("%s * %s mod %llu = %s\n",
                          A.ToString().c_str(),
                          B.ToString().c_str(),
                          modulus,
                          Q.ToString().c_str());
    };

  CHECK(Q == Expected) << Problem() <<
    "\nGot:    " << Q.ToString() <<
    "\nwanted: " << Expected.ToString() <<
    "\nQ bytes:  " << BigBytes(Q) <<
    "\nExpected: " << BigBytes(Expected);
}

// Most of these test cases are actually just the behavior at r5391,
// before I started switching to 64-bit montgomery params. Could
// cross-check them if something is fishy.
static void TestModMult() {

  // When modulus_length = 1, we don't use montgomery multiplication.
  WrapModMult(BigInt(0),
              BigInt(0),
              7,
              BigInt(0));

  WrapModMult(BigInt(1),
              BigInt(2),
              7,
              BigInt(2));

  // Special behavior for powers of two moduli.
  WrapModMult(BigInt("1111111222222222223"),
              BigInt("387492873491872371"),
              // 2^60
              1152921504606846976ULL,
              BigInt("137882207625613117"));

  WrapModMult(BigInt("1111111222222222223"),
              BigInt("38742371"),
              // 2^60
              1152921504606846976ULL,
              BigInt("1030861126380984141"));

  WrapModMult(BigInt("5153775207320113"),
              BigInt("7888609052210119"),
              // 2^59
              576460752303423488ULL,
              BigInt("138126514092224279"));

  WrapModMult(BigInt("15232"),
              BigInt("90210"),
              // 2^61
              2305843009213693952ULL,
              BigInt("1374078720"));

  WrapModMult(BigInt("15232"),
              BigInt("9021000000000000"),
              // 2^61
              2305843009213693952ULL,
              BigInt("1363134456392056832"));

  WrapModMult(BigInt("1523200000000000"),
              BigInt("90210"),
              // 2^61
              2305843009213693952ULL,
              BigInt("1363134456392056832"));

  #if 0
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
  #endif

  printf("ModMult " AGREEN("OK") "\n");
}

static void WrapPowBaseInt(uint64_t modulus,
                           int base,
                           const BigInt &Exp,
                           const BigInt &Expected) {

  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus);

  // limb modpow[params->modulus_length];
  BigInt Result = ModPowBaseInt(*params, base, Exp);
  // BigInt Result = LimbsToBigInt(modpow, params->modulus_length);

  CHECK(Result == Expected) <<
    "\nFor " << base << "^" << Exp.ToString()
             << " mod " << modulus <<
    "\nWanted  " << Expected.ToString() <<
    "\nBut got " << Result.ToString();
}

static void TestModPowBaseInt() {
  WrapPowBaseInt(333, 2, BigInt(1234), BigInt(25));
  WrapPowBaseInt(1290387419827141ULL,
                 8181, BigInt("128374817123451"),
                 BigInt("521768828887416"));
  WrapPowBaseInt(
      // 2^60
      1152921504606846976ULL,
      1234567,
      BigInt("12837481712345111111111111171881"),
      BigInt("802119408046534343"));

  printf("ModPowBaseInt " AGREEN("OK") "\n");
}

static void WrapModPow(uint64_t modulus,
                       const BigInt &Base,
                       const BigInt &Exp,
                       const BigInt &Expected) {

  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus);

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

  WrapModPow(333, BigInt(2), BigInt(1234), BigInt(25));

  // Weird that it returns a different answer than the base int version.
  // Bug? Or is the interface different because it assumes montgomery
  // form for some args? In any case, this is what alpertron does.
  WrapModPow(1290387419827141ULL,
             BigInt(8181), BigInt("128374817123451"),
             BigInt("786025986329866"));

  // (Power of two exponent, so this matches the BaseInt behavior.)
  WrapModPow(
      // 2^60
      1152921504606846976ULL,
      BigInt(1234567),
      BigInt("12837481712345111111111111171881"),
      BigInt("802119408046534343"));

  WrapModPow(576760752777723488ULL,
             BigInt("120374190872938741"),
             BigInt("128374817123451"),
             BigInt("238423962528506731"));

  printf("ModPow " AGREEN("OK") "\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  TestDivFloor();
  TestGCD();
  TestJacobi();

  TestNumLimbs();

  Montgomery();

  TestSubModN();
  TestModMult();

  TestModPowBaseInt();
  TestModPow();


  printf("All explicit tests " AGREEN("OK") "\n");
  return 0;
}
