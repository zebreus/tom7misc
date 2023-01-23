
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

static void Gen16() {
  for (int i = 0; i < 256; i++) {
    half h = (half)(float)i;
    uint16_t u = Exp::GetU16(h);
    printf("%04x, ", u);
    if (i % 8 == 7) printf("\n");
  }
  printf("\n");
}

int main(int argc, char **argv) {
  Gen16();

  TestToFrom();
  TestPlus();
  TestMinus();
  TestNegate();
  TestPreIncrement();
  TestPostIncrement();
  TestPreDecrement();
  TestPostDecrement();
  TestPlusEq();
  TestMinusEq();

  TestAnd();
  TestAndEq();

  TestLeftShifts();

  TestRightShifts();

  printf("OK\n");
  return 0;
}
