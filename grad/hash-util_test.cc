
#include "hash-util.h"

#include <array>
#include <string>

#include "ansi.h"
#include "timer.h"
#include "half.h"
#include "choppy.h"
#include "arcfour.h"
#include "randutil.h"

using Choppy = ChoppyGrid<256>;
using Allocator = Exp::Allocator;
using DB = Choppy::DB;

using namespace half_float::literal;

// Standard permutation used in hash.cc
static constexpr std::array<int, 64> PERM = {
  49, 44, 34, 41, 0, 29, 40, 50, 39, 59, 8, 52, 35, 38,
  51, 3, 46, 43, 48, 31, 47, 23, 10, 5, 11, 12, 16, 36,
  60, 42, 19, 57, 22, 30, 4, 33, 15, 6, 45, 53, 61, 58,
  24, 54, 26, 63, 17, 55, 37, 56, 28, 2, 9, 1, 27, 62,
  18, 32, 21, 13, 20, 7, 25, 14,
};

static inline uint8_t GetByte(uint64_t data, int i) {
  return (data >> (8 * (7 - i))) & 0xFF;
}

static inline uint64_t SetByte(uint64_t data, int i, uint8_t byte) {
  // Blank out the byte.
  data &= ~(0xFFULL << (8 * (7 - i)));
  return data | ((uint64_t)byte << (8 * (7 - i)));
}


// This test is for visual inspection/debugging.
// Checking the first byte only.
static void TestPerm1Bit(DB *basis) {
  std::array<const Exp *, 8> fs = HashUtil::PermuteFn(PERM, basis, 0);
  for (int i = 0; i < 8; i++) {
    printf("Expression " ABLUE("%d") " size " APURPLE("%d") "\n",
           i, Exp::ExpSize(fs[i]));
  }

  int errors = 0;

  // Try all single-bit inputs.
  for (int x = 0; x < 64; x++) {
    uint64_t input = 1ULL << (63 - x);
    uint64_t output = HashUtil::Permute64(PERM, input);
    const uint8_t expected_a = GetByte(output, 0);
    if (!expected_a) continue;
    printf(AGREY("----------------------------------") "\n");

    if (false) {
      uint8_t aa = GetByte(input, 0);
      uint8_t bb = GetByte(input, 1);
      uint8_t cc = GetByte(input, 2);
      uint8_t dd = GetByte(input, 3);
      uint8_t ee = GetByte(input, 4);
      uint8_t ff = GetByte(input, 5);
      uint8_t gg = GetByte(input, 6);
      uint8_t hh = GetByte(input, 7);
      for (uint8_t h : {aa, bb, cc, dd, ee, ff, gg, hh}) {
        printf("  %02x", h);
      }
      printf("\n");
    }

    half aa = HashUtil::BitsToHalf(GetByte(input, 0));
    half bb = HashUtil::BitsToHalf(GetByte(input, 1));
    half cc = HashUtil::BitsToHalf(GetByte(input, 2));
    half dd = HashUtil::BitsToHalf(GetByte(input, 3));
    half ee = HashUtil::BitsToHalf(GetByte(input, 4));
    half ff = HashUtil::BitsToHalf(GetByte(input, 5));
    half gg = HashUtil::BitsToHalf(GetByte(input, 6));
    half hh = HashUtil::BitsToHalf(GetByte(input, 7));

    if (false) {
      for (half h : {aa, bb, cc, dd, ee, ff, gg, hh}) {
        printf("  %.8f\n", (double)h);
      }
    }

    // now we compute the new byte 'a'
    half f_a = Exp::GetHalf(Exp::EvaluateOn(fs[0], Exp::GetU16(aa)));
    half f_b = Exp::GetHalf(Exp::EvaluateOn(fs[1], Exp::GetU16(bb)));
    half f_c = Exp::GetHalf(Exp::EvaluateOn(fs[2], Exp::GetU16(cc)));
    half f_d = Exp::GetHalf(Exp::EvaluateOn(fs[3], Exp::GetU16(dd)));
    half f_e = Exp::GetHalf(Exp::EvaluateOn(fs[4], Exp::GetU16(ee)));
    half f_f = Exp::GetHalf(Exp::EvaluateOn(fs[5], Exp::GetU16(ff)));
    half f_g = Exp::GetHalf(Exp::EvaluateOn(fs[6], Exp::GetU16(gg)));
    half f_h = Exp::GetHalf(Exp::EvaluateOn(fs[7], Exp::GetU16(hh)));

    auto Norm = [](half h) -> half {
        return (h * (1.0_h / 128.0_h)) - 1.0_h;
      };


    for (half h : {f_a, f_b, f_c, f_d, f_e, f_f, f_g, f_h}) {
      uint8_t u = HashUtil::HalfToBits(h);
      half n = Norm(h);
      uint8_t un = HashUtil::HalfToBits(n);
      printf("  " ABLUE("%04x") " " ARED("%02x") "  %+.8f -> "
             APURPLE("%02x") "  %+.8f\n",
             Exp::GetU16(h), u, (double)h,
             un, (double)n);
    }

    printf("Expecting: " APURPLE("%02x") "\n", expected_a);

    half a = Norm(f_a + f_b + f_c + f_d + f_e + f_f + f_g + f_h);
    uint8_t a8 = HashUtil::HalfToBits(a);
    string is = (a < -1.0_h || a > 1.0_h) ? (string)ARED("illegal") :
      StringPrintf(APURPLE("%02x"), a8);
    printf("Sum " AYELLOW("%.6f") " which is %s\n", (float)a, is.c_str());
    printf("Bit " AWHITE("%d") " set = " AWHITE("%016x") "\n", x, input);
    for (int i = 0; i < 64; i++) {
      if (i == x) printf(AGREEN("1"));
      else printf("0");
    }
    printf(": " ABLUE("%02x") " ", a8);
    for (int i = 0; i < 8; i++) {
      if (a8 & (1 << (7 - i))) printf(APURPLE("1"));
      else printf("0");
    }
    if (a8 == expected_a) {
      printf(AGREEN(" *"));
    } else {
      errors++;
      printf(ARED(" X") "\n" ARED("^^^ WRONG ^^^"));
    }

    printf("\n");
  }
  CHECK(errors == 0);
}

// Constructs the permutation and tests many iterations of it.
static void TestPermutation(DB *basis,
                            const std::array<int, 64> &perm,
                            const std::string &seed,
                            int iters) {
  Timer create_timer;
  // Need 8x8 expressions.
  std::array<std::array<const Exp *, 8>, 8> fss;
  for (int byte = 0; byte < 8; byte++) {
    fss[byte] = HashUtil::PermuteFn(perm, basis, byte);
  }
  const double create_seconds = create_timer.Seconds();

  auto Norm = [](half h) -> half {
      return (h * (1.0_h / 128.0_h)) - 1.0_h;
    };

  ArcFour rc(StringPrintf("perm.%s", seed));
  Timer t;
  for (int iter = 0; iter < iters; iter++) {
    const uint64_t input = Rand64(&rc);
    const uint64_t expected_output = HashUtil::Permute64(perm, input);

    // Run the whole permutation function.
    half aa = HashUtil::BitsToHalf(GetByte(input, 0));
    half bb = HashUtil::BitsToHalf(GetByte(input, 1));
    half cc = HashUtil::BitsToHalf(GetByte(input, 2));
    half dd = HashUtil::BitsToHalf(GetByte(input, 3));
    half ee = HashUtil::BitsToHalf(GetByte(input, 4));
    half ff = HashUtil::BitsToHalf(GetByte(input, 5));
    half gg = HashUtil::BitsToHalf(GetByte(input, 6));
    half hh = HashUtil::BitsToHalf(GetByte(input, 7));

    uint64_t actual_output = 0ULL;
    for (int byte = 0; byte < 8; byte++) {
      // The functions (of each input byte) to compute this one output
      // byte.
      const std::array<const Exp *, 8> &fs = fss[byte];
      half f_a = Exp::GetHalf(Exp::EvaluateOn(fs[0], Exp::GetU16(aa)));
      half f_b = Exp::GetHalf(Exp::EvaluateOn(fs[1], Exp::GetU16(bb)));
      half f_c = Exp::GetHalf(Exp::EvaluateOn(fs[2], Exp::GetU16(cc)));
      half f_d = Exp::GetHalf(Exp::EvaluateOn(fs[3], Exp::GetU16(dd)));
      half f_e = Exp::GetHalf(Exp::EvaluateOn(fs[4], Exp::GetU16(ee)));
      half f_f = Exp::GetHalf(Exp::EvaluateOn(fs[5], Exp::GetU16(ff)));
      half f_g = Exp::GetHalf(Exp::EvaluateOn(fs[6], Exp::GetU16(gg)));
      half f_h = Exp::GetHalf(Exp::EvaluateOn(fs[7], Exp::GetU16(hh)));

      half out = Norm(f_a + f_b + f_c + f_d + f_e + f_f + f_g + f_h);
      uint8_t out_byte = HashUtil::HalfToBits(out);
      actual_output = SetByte(actual_output, byte, out_byte);
    }

    CHECK(actual_output == expected_output) << seed << ": " << input <<
      "\nGot: " <<
      actual_output << " but wanted " << expected_output;
  }
  double run_seconds = t.Seconds();

  printf(AYELLOW("%s") "  " AGREEN("%d") " iters OK, "
         ABLUE("%.4fs") " setup, "
         APURPLE("%.3f") " iters/sec\n",
         seed.c_str(),
         iters, create_seconds, iters / run_seconds);
}

static void TestPerms(DB *basis) {
  printf("\n\n " AWHITE("==") " " ABLUE("Full tests") " " AWHITE("==") "\n");
  TestPermutation(basis, PERM, "PERM", 1000);

  ArcFour rc("test");
  // deliberately start with the identity permutation
  std::array<int, 64> perm;
  for (int i = 0; i < 64; i++) perm[i] = i;

  for (int i = 0; i < 100; i++) {
    TestPermutation(basis, PERM, StringPrintf("R%03d", i), 10);
    Shuffle(&rc, &perm);
  }
}

static void TestBits() {
  for (int i = 0; i < 256; i++) {
    CHECK(HashUtil::HalfToBits(HashUtil::BitsToHalf(i)) == i) << i;
  }
}

static half ModularPlus(const Exp *modexp, half x, half y) {
  const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
  // x and y are in [-1,1) but we want to treat them as numbers
  // in [0,1).
  half xx = ((x - HALF_GRID) + 1.0_h) * 0.5_h;
  half yy = ((y - HALF_GRID) + 1.0_h) * 0.5_h;

  // now the sum is in [0, 2).
  half sum = xx + yy;
  CHECK(sum >= 0.0_h && sum < 2.0_h) << sum;
  // Now in [-1, 3).
  half asum = (sum * 2.0_h) - 1.0_h;
  CHECK(asum >= -2.0_h && asum < 3.0_h)
    << x << " " << y << " " << asum;
  half msum = Exp::GetHalf(Exp::EvaluateOn(modexp, Exp::GetU16(asum)));

  if (false)
  printf("%.6f + %.6f -> %.6f + %.6f = %.6f -> %.6f (%.6f)\n",
         (float)x, (float)y,
         (float)xx, (float)yy,
         (float)sum, (float)asum,
         (float)msum);

  return msum + HALF_GRID;
}

static half ModularMinus(const Exp *modexp, half x, half y) {
  const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
  half xx = ((x - HALF_GRID) + 1.0_h) * 0.5_h;
  half yy = ((y - HALF_GRID) + 1.0_h) * 0.5_h;

  // Difference is in [-1, 1].
  half diff = xx - yy;
  CHECK(diff >= -1.0_h && diff <= 1.0_h) << diff;
  // And then in [-3, 1].
  half adiff = (diff * 2.0_h) - 1.0_h;
  half mdiff = Exp::GetHalf(Exp::EvaluateOn(modexp, Exp::GetU16(adiff)));
  return mdiff + HALF_GRID;
}

static void TestMod() {
  Allocator alloc;
  const Exp *e = HashUtil::ModExp(&alloc);

  for (uint16_t u = Exp::GetU16((half)-3.0);
       u <= Exp::GetU16((half)3.0);
       u = Exp::NextAfter16(u)) {
    half in = Exp::GetHalf(u);
    half out = Exp::GetHalf(Exp::EvaluateOn(e, u));

    if (in < -1.0_h) {
      CHECK(in + 2.0_h == out) << in << " " << out;
    } else if (in > 1.0_h) {
      CHECK(in - 2.0_h == out) << in << " " << out;
    } else {
      CHECK(in == out);
    }
  }

  // Plus.
  for (int x = 0; x < 256; x++) {
    half xh = HashUtil::BitsToHalf(x);
    for (int y = 0; y < 256; y++) {
      half yh = HashUtil::BitsToHalf(y);
      uint8_t plus = (x + y) & 0xFF;

      half hplus = ModularPlus(e, xh, yh);
      uint8_t hplus8 = HashUtil::HalfToBits(hplus);
      CHECK(plus == hplus8) <<
        StringPrintf("expected %02x + %02x = %02x. Got %02x\n"
                     "(%.6f + %.6f = %.6f)\n",
                     x, y, plus, hplus8,
                     (float)xh, (float)yh, (float)hplus);
    }
  }

  // Minus.
  for (int x = 0; x < 256; x++) {
    half xh = HashUtil::BitsToHalf(x);
    for (int y = 0; y < 256; y++) {
      half yh = HashUtil::BitsToHalf(y);
      uint8_t minus = (x - y) & 0xFF;

      half hminus = ModularMinus(e, xh, yh);
      uint8_t hminus8 = HashUtil::HalfToBits(hminus);
      CHECK(minus == hminus8) <<
        StringPrintf("expected %02x - %02x = %02x. Got %02x\n"
                     "(%.6f + %.6f = %.6f)\n",
                     x, y, minus, hminus8,
                     (float)xh, (float)yh, (float)hminus);
    }
  }
}



int main(int argc, char **argv) {
  AnsiInit();

  TestBits();

  TestMod();

  // would be nice if we didn't keep allocating into
  // this arena,
  DB db;
  db.LoadFile("basis8.txt");

  TestPerm1Bit(&db);
  TestPerms(&db);

  printf("\nOK\n");
  return 0;
}
