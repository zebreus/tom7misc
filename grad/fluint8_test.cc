
#include "fluint8.h"

#include <cstdint>

#include "expression.h"
#include "half.h"
#include "base/logging.h"
#include "base/stringprintf.h"

using uint8 = uint8_t;

static void CheckCanonical(Fluint8 f, uint8 x, uint8 y,
                           const char *fn, const char *name, int line) {
  uint16_t frep = f.Representation();
  uint8 meaning = f.ToInt();
  Fluint8 canon(meaning);
  uint16_t crep = canon.Representation();
  CHECK(frep == crep) << "Line " << line
                      << " (" << fn << " aka " << name << "): "
                      << "On args "
                      << StringPrintf("(%02x = %d, %02x = %d)", x, x, y, y)
                      << "\nNot canonical: "
                      << StringPrintf("%02x = %d rep by %04x (%.3f) "
                                      "but %04x (%.3f) is canonical.",
                                      meaning, meaning,
                                      frep,
                                      (float)Exp::GetHalf(frep),
                                      crep,
                                      (float)Exp::GetHalf(crep));
}
#define CHECK_CANONICAL(name, f, x, y)                      \
  CheckCanonical(f, x, y, __func__, name, __LINE__)

static void TestToFrom() {
  for (int i = 0; i < 256; i++) {
    uint8 b = i;
    Fluint8 f(b);
    CHECK(f.ToInt() == b);
    CHECK_CANONICAL("tofrom", f, b, b);
  }
}

template<class F>
static void ForAll(F f) {
  for (int x = 0; x < 256; x++) {
    uint8 xx = x;
    Fluint8 xf(xx);
    f(xx, xf);
  }
}


template<class F>
static void ForAllPairs(F f) {
  for (int x = 0; x < 256; x++) {
    for (int y = 0; y < 256; y++) {
      uint8 xx = x;
      uint8 yy = y;

      Fluint8 xf(xx);
      Fluint8 yf(yy);

      f(xx, yy, xf, yf);
    }
  }
}

static void TestPlus() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x + y;
        Fluint8 zf = xf + yf;

        CHECK(z == zf.ToInt()) <<
          StringPrintf("%02x (%d) + %02x (%d) -> %02x (%d) want %02x (%d)",
                       x, x, y, y, zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("plus", zf, x, y);
      });
}

static void TestAddWithCarry() {
  ForAllPairs(
      [](uint16_t x, uint16_t y,
         Fluint8 xf, Fluint8 yf) {
        uint16_t z = x + y;
        uint8_t zsum = z;
        uint8_t zcarry = (z >> 8);
        CHECK(zcarry == 0 || zcarry == 1);
        const auto [carry, sum] = Fluint8::AddWithCarry(xf, yf);

        CHECK(zsum == sum.ToInt() &&
              zcarry == carry.ToInt()) <<
          StringPrintf("%02x (%d) + %02x (%d) -> "
                       "(%02x, %02x) (%d, %d) want (%02x, %02x) (%d, %d)",
                       x, x, y, y,
                       carry.ToInt(), sum.ToInt(),
                       carry.ToInt(), sum.ToInt(),
                       zcarry, zsum, zcarry, zsum);
        CHECK_CANONICAL("addwithcarry-carry", carry, x, y);
        CHECK_CANONICAL("addwithcarry-sum", sum, x, y);
      });
}

static void TestSubtractWithCarry() {
  ForAllPairs(
      [](uint16_t x, uint16_t y,
         Fluint8 xf, Fluint8 yf) {
        uint16_t z = x - y;
        uint8_t zdiff = z;
        uint8_t zcarry = (z >> 8) & 1;
        CHECK(zcarry == 0 || zcarry == 1);
        const auto [carry, diff] = Fluint8::SubtractWithCarry(xf, yf);

        CHECK(zdiff == diff.ToInt() &&
              zcarry == carry.ToInt()) <<
          StringPrintf("%02x (%d) - %02x (%d) -> "
                       "(%02x, %02x) (%d, %d) want (%02x, %02x) (%d, %d)",
                       x, x, y, y,
                       carry.ToInt(), diff.ToInt(),
                       carry.ToInt(), diff.ToInt(),
                       zcarry, zdiff, zcarry, zdiff);
        CHECK_CANONICAL("subwithcarry-carry", carry, x, y);
        CHECK_CANONICAL("subwithcarry-diff", diff, x, y);
      });
}


static void TestMinus() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x - y;
        Fluint8 zf = xf - yf;

        CHECK(z == zf.ToInt()) <<
          StringPrintf("%02x (%d) - %02x (%d) -> %02x (%d) want %02x (%d)",
                       x, x, y, y, zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("minus", zf, x, y);
      });
}

static void TestAnd() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x & y;
        Fluint8 zf = xf & yf;

        CHECK(z == zf.ToInt()) <<
          StringPrintf("%02x (%d) & %02x (%d) -> %02x (%d) want %02x (%d)",
                       x, x, y, y, zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("and", zf, x, y);
      });
}

static void TestAndEq() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x &= y;
        Fluint8 zf = xf &= yf;

        CHECK(x == xf.ToInt());
        CHECK(y == yf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("andeq", xf, x, y);
        CHECK_CANONICAL("andeq", zf, x, y);
      });
}

template<int N>
static void TestAndWith() {
  if constexpr (N < 0) {
      // done.
  } else {
    ForAll(
        [](uint8 x, Fluint8 xf) {
          static constexpr uint8 B = N;
          uint8 z = x & B;
          Fluint8 zf = Fluint8::AndWith<B>(xf);

          CHECK(z == zf.ToInt()) <<
            StringPrintf("%02x (%d) & constant %02x (%d) -> "
                         "%02x (%d) want %02x (%d)",
                         x, x, B, B, zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("and-with", zf, x, B);
      });
    TestAndWith<N - 1>();
  }
}

template<int N>
static void TestOrWith() {
  if constexpr (N < 0) {
      // done.
  } else {
    ForAll(
        [](uint8 x, Fluint8 xf) {
          static constexpr uint8 B = N;
          uint8 z = x | B;
          Fluint8 zf = Fluint8::OrWith<B>(xf);

          CHECK(z == zf.ToInt()) <<
            StringPrintf("%02x (%d) | constant %02x (%d) -> "
                         "%02x (%d) want %02x (%d)",
                         x, x, B, B, zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("or-with", zf, x, B);
      });
    TestOrWith<N - 1>();
  }
}

template<int N>
static void TestXorWith() {
  if constexpr (N < 0) {
      // done.
  } else {
    ForAll(
        [](uint8 x, Fluint8 xf) {
          static constexpr uint8 B = N;
          uint8 z = x ^ B;
          Fluint8 zf = Fluint8::XorWith<B>(xf);

          CHECK(z == zf.ToInt()) <<
            StringPrintf("%02x (%d) ^ constant %02x (%d) -> "
                         "%02x (%d) want %02x (%d)",
                         x, x, B, B, zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("xor-with", zf, x, B);
      });
    TestXorWith<N - 1>();
  }
}


static void TestOr() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x | y;
        Fluint8 zf = xf | yf;

        CHECK(z == zf.ToInt()) <<
          StringPrintf("%02x (%d) | %02x (%d) -> %02x (%d) want %02x (%d)",
                       x, x, y, y, zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("or", zf, x, y);
      });
}

static void TestOrEq() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x |= y;
        Fluint8 zf = xf |= yf;

        CHECK(x == xf.ToInt());
        CHECK(y == yf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("oreq", xf, x, y);
        CHECK_CANONICAL("oreq", zf, x, y);
      });
}

static void TestXor() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x ^ y;
        Fluint8 zf = xf ^ yf;

        CHECK(z == zf.ToInt()) <<
          StringPrintf("%02x (%d) ^ %02x (%d) -> %02x (%d) want %02x (%d)",
                       x, x, y, y, zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("xor", zf, x, y);
      });
}

static void TestXorEq() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x ^= y;
        Fluint8 zf = xf ^= yf;

        CHECK(x == xf.ToInt());
        CHECK(y == yf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("xoreq", xf, x, y);
        CHECK_CANONICAL("xoreq", zf, x, y);
      });
}

static void TestNegate() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = -x;
        Fluint8 zf = -xf;

        CHECK(z == zf.ToInt()) <<
          "-" << x << " -> " << zf.ToInt();
        CHECK_CANONICAL("negate", zf, z, z);
      });
}

static void TestPostIncrement() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = x++;
        Fluint8 zf = xf++;

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("postinc", xf, x, x);
        CHECK_CANONICAL("postinc", zf, x, x);
      });
}

static void TestPreIncrement() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = ++x;
        Fluint8 zf = ++xf;

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("preinc", xf, x, x);
        CHECK_CANONICAL("preinc", zf, x, x);
      });
}

static void TestPostDecrement() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = x--;
        Fluint8 zf = xf--;

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("postdec", xf, x, x);
        CHECK_CANONICAL("postdec", zf, x, x);
      });
}

static void TestPreDecrement() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = --x;
        Fluint8 zf = --xf;

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("predec", xf, x, x);
        CHECK_CANONICAL("predec", zf, x, x);
      });
}

static void TestPlusEq() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x += y;
        Fluint8 zf = xf += yf;

        CHECK(x == xf.ToInt());
        CHECK(y == yf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("pluseq", xf, x, y);
        CHECK_CANONICAL("pluseq", zf, x, y);
      });
}

static void TestMinusEq() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x -= y;
        Fluint8 zf = xf -= yf;

        CHECK(x == xf.ToInt());
        CHECK(y == yf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("minuseq", xf, x, y);
        CHECK_CANONICAL("minuseq", zf, x, y);
      });
}

template<size_t N>
static void TestLeftShift() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = x << N;
        Fluint8 zf = Fluint8::LeftShift<N>(xf);

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("leftshift", zf, x, N);
      });
}

static void TestLeftShifts() {
  TestLeftShift<0>();
  TestLeftShift<1>();
  TestLeftShift<2>();
  TestLeftShift<3>();
  TestLeftShift<4>();
  TestLeftShift<5>();
  TestLeftShift<6>();
  TestLeftShift<7>();
  TestLeftShift<8>();
}

template<size_t N>
static void TestRightShift() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = x >> N;
        Fluint8 zf = Fluint8::RightShift<N>(xf);

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt()) <<
          StringPrintf("%02x (%d) >> %d = %02x (%d) want %02x (%d)",
                       x, x, N,
                       zf.ToInt(), zf.ToInt(), z, z);
        CHECK_CANONICAL("rightshift", zf, x, N);
      });
}

static void TestRightShifts() {
  TestRightShift<0>();
  TestRightShift<1>();
  TestRightShift<2>();
  TestRightShift<3>();
  TestRightShift<4>();
  TestRightShift<5>();
  TestRightShift<6>();
  TestRightShift<7>();
  TestRightShift<8>();
}

[[maybe_unused]]
static void Gen16() {
  for (int i = 0; i < 256; i++) {
    half h = (half)(float)i;
    uint16_t u = Exp::GetU16(h);
    printf("%04x, ", u);
    if (i % 8 == 7) printf("\n");
  }
  printf("\n");
}

static void TestIsZero() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = (x == 0);
        Fluint8 zf = Fluint8::IsZero(xf);
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("is-zero", zf, z, z);
      });
}

static void TestIsntZero() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = (x != 0);
        Fluint8 zf = Fluint8::IsntZero(xf);
        CHECK(z == zf.ToInt());
        CHECK_CANONICAL("isnt-zero", zf, z, z);
      });
}

static void TestIf() {
  /*
  ForAll(
      [](uint8 x, Fluint8 xf) {

        Fluint8 tf = Fluint8::If(Fluint8(0x01), xf);
        Fluint8 ff = Fluint8::If(Fluint8(0x00), xf);
        printf("%02x: t: %02x f: %02x\n", x, tf.ToInt(), ff.ToInt());
      });
  */
  ForAll(
      [](uint8 x, Fluint8 xf) {
        {
          Fluint8 zf = Fluint8::If(Fluint8(0x01), xf);
          CHECK(x == zf.ToInt()) << (int)x << " " << (int)zf.ToInt();
          CHECK_CANONICAL("if-true", zf, x, x);
        }
        {
          Fluint8 zf = Fluint8::If(Fluint8(0x00), xf);
          CHECK(0 == zf.ToInt()) << (int)x << " " << (int)zf.ToInt();
          CHECK_CANONICAL("if-false", zf, x, x);
        }
      });
}

static void TestIfElse() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        {
          Fluint8 zf = Fluint8::IfElse(Fluint8(0x01), xf, yf);
          CHECK(x == zf.ToInt());
          CHECK_CANONICAL("ifelse-true", zf, x, y);
        }
        {
          Fluint8 zf = Fluint8::IfElse(Fluint8(0x00), xf, yf);
          CHECK(y == zf.ToInt());
          CHECK_CANONICAL("ifelse-false", zf, x, y);
        }
      });
}

static void TestEq() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {

        bool eq = x == y;
        Fluint8 feq = Fluint8::Eq(xf, yf);
        if (eq) {
          CHECK(feq.ToInt() == 1);
        } else {
          CHECK(feq.ToInt() == 0);
        }
        CHECK_CANONICAL("eq", feq, x, y);
      });
}

static void TestBooleanAnd() {

  Fluint8 f00 = Fluint8::BooleanAnd(Fluint8(0x00), Fluint8(0x00));
  Fluint8 f01 = Fluint8::BooleanAnd(Fluint8(0x00), Fluint8(0x01));
  Fluint8 f10 = Fluint8::BooleanAnd(Fluint8(0x01), Fluint8(0x00));
  Fluint8 f11 = Fluint8::BooleanAnd(Fluint8(0x01), Fluint8(0x01));

  CHECK_CANONICAL("boolean-and", f00, 0, 0);
  CHECK_CANONICAL("boolean-and", f01, 0, 1);
  CHECK_CANONICAL("boolean-and", f10, 1, 0);
  CHECK_CANONICAL("boolean-and", f11, 1, 1);

  CHECK(f00.ToInt() == 0);
  CHECK(f01.ToInt() == 0);
  CHECK(f10.ToInt() == 0);
  CHECK(f11.ToInt() == 1);
}

static void TestBooleanOr() {

  Fluint8 f00 = Fluint8::BooleanOr(Fluint8(0x00), Fluint8(0x00));
  Fluint8 f01 = Fluint8::BooleanOr(Fluint8(0x00), Fluint8(0x01));
  Fluint8 f10 = Fluint8::BooleanOr(Fluint8(0x01), Fluint8(0x00));
  Fluint8 f11 = Fluint8::BooleanOr(Fluint8(0x01), Fluint8(0x01));

  CHECK_CANONICAL("boolean-or", f00, 0, 0);
  CHECK_CANONICAL("boolean-or", f01, 0, 1);
  CHECK_CANONICAL("boolean-or", f10, 1, 0);
  CHECK_CANONICAL("boolean-or", f11, 1, 1);

  CHECK(f00.ToInt() == 0);
  CHECK(f01.ToInt() == 1);
  CHECK(f10.ToInt() == 1);
  CHECK(f11.ToInt() == 1);
}


int main(int argc, char **argv) {
  // Gen16();

  TestToFrom(); printf("ToFrom OK\n");
  TestPlus(); printf("Plus OK\n");
  TestMinus(); printf("Minus OK\n");
  TestNegate(); printf("Negate OK\n");
  TestPreIncrement(); printf("PreIncrement OK\n");
  TestPostIncrement(); printf("PostIncrement OK\n");
  TestPreDecrement(); printf("PreDecrement OK\n");
  TestPostDecrement(); printf("PostDecrement OK\n");
  TestPlusEq(); printf("PlusEq OK\n");
  TestMinusEq(); printf("MinusEq OK\n");

  TestAddWithCarry(); printf("AddWithCarry OK\n");
  TestSubtractWithCarry(); printf("SubtractWithCarry OK\n");

  TestAnd(); printf("And OK\n");
  TestAndEq(); printf("AndEq OK\n");

  TestOr(); printf("Or OK\n");
  TestOrEq(); printf("OrEq OK\n");

  TestXor(); printf("Xor OK\n");
  TestXorEq(); printf("XorEq OK\n");

  TestAndWith<255>(); printf("AndWith OK\n");
  TestOrWith<255>(); printf("OrWith OK\n");
  TestXorWith<255>(); printf("XorWith OK\n");

  TestLeftShifts(); printf("LeftShifts OK\n");
  TestRightShifts(); printf("RightShifts OK\n");

  TestIf(); printf("If OK\n");
  TestIfElse(); printf("IfElse OK\n");

  TestIsZero(); printf("IsZero OK\n");
  TestIsntZero(); printf("IsntZero OK\n");

  TestEq(); printf("Eq OK\n");

  TestBooleanAnd(); printf("BooleanAnd OK\n");
  TestBooleanOr(); printf("BooleanOr OK\n");

  printf("OK\n");
  return 0;
}
