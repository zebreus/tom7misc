
#include "fluint8.h"

#include <cstdint>

#include "half.h"
#include "base/logging.h"

using uint8 = uint8_t;

static void CheckCanonical(Fluint8 f) {
  uint16_t frep = f.Representation();
  uint8 meaning = f.ToInt();
  Fluint8 canon(meaning);
  uint16_t crep = canon.Representation();
  CHECK(frep == crep) << "Not canonical: " << meaning
                      << " rep by " << frep << " but "
                      << crep << " is canonical.";
}

static void TestToFrom() {
  for (int i = 0; i < 256; i++) {
    uint8 b = i;
    Fluint8 f(b);
    CHECK(f.ToInt() == b);
    CheckCanonical(f);
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
          x << " * " << y << " -> " << zf.ToInt();
        CheckCanonical(zf);
      });
}

static void TestMinus() {
  ForAllPairs(
      [](uint8 x, uint8 y,
         Fluint8 xf, Fluint8 yf) {
        uint8 z = x - y;
        Fluint8 zf = xf - yf;

        CHECK(z == zf.ToInt()) <<
          x << " * " << y << " -> " << zf.ToInt();
        CheckCanonical(zf);
      });
}

static void TestNegate() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = -x;
        Fluint8 zf = -xf;

        CHECK(z == zf.ToInt()) <<
          "-" << x << " -> " << zf.ToInt();
        CheckCanonical(zf);
      });
}

static void TestPostIncrement() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = x++;
        Fluint8 zf = xf++;

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CheckCanonical(xf);
        CheckCanonical(zf);
      });
}

static void TestPreIncrement() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = ++x;
        Fluint8 zf = ++xf;

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CheckCanonical(xf);
        CheckCanonical(zf);
      });
}

static void TestPostDecrement() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = x--;
        Fluint8 zf = xf--;

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CheckCanonical(xf);
        CheckCanonical(zf);
      });
}

static void TestPreDecrement() {
  ForAll(
      [](uint8 x, Fluint8 xf) {
        uint8 z = --x;
        Fluint8 zf = --xf;

        CHECK(x == xf.ToInt());
        CHECK(z == zf.ToInt());
        CheckCanonical(xf);
        CheckCanonical(zf);
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
        CheckCanonical(xf);
        CheckCanonical(zf);
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
        CheckCanonical(xf);
        CheckCanonical(zf);
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
        CheckCanonical(zf);
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

int main(int argc, char **argv) {
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

  TestLeftShifts();

  printf("OK\n");
  return 0;
}
