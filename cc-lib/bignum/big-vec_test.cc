
#include "bignum/big-vec.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"

static void TestBigVecQ2() {
  {
    BigVecQ2 a("1", "2");
    BigVecQ2 b("3", "4");
    BigVecQ2 sum = a + b;
    CHECK(sum.x == BigRat("4"));
    CHECK(sum.y == BigRat("6"));
  }

  {
    BigVecQ2 a("5", "7");
    BigVecQ2 b("2", "1");
    BigVecQ2 diff = a - b;
    CHECK(diff.x == BigRat("3"));
    CHECK(diff.y == BigRat("6"));
  }

  {
    BigVecQ2 a("2", "3");
    BigRat s("4");
    BigVecQ2 prod = a * s;
    CHECK(prod.x == BigRat("8"));
    CHECK(prod.y == BigRat("12"));
  }

  {
    BigVecQ2 a("8", "12");
    BigRat s("4");
    BigVecQ2 quot = a / s;
    CHECK(quot.x == BigRat("2"));
    CHECK(quot.y == BigRat("3"));
  }

  {
    BigVecQ2 a("1", "2");
    BigVecQ2 b("3", "4");
    BigRat dot = BigVecQ2::Dot(a, b);
    CHECK(dot == BigRat("11"));
  }
}

static void TestBigVecQ3() {
  {
    BigVecQ3 a("1", "2", "3");
    BigVecQ3 b("4", "5", "6");
    BigVecQ3 sum = a + b;
    CHECK(sum.x == BigRat("5"));
    CHECK(sum.y == BigRat("7"));
    CHECK(sum.z == BigRat("9"));
  }

  {
    BigVecQ3 a("4", "5", "6");
    BigVecQ3 b("1", "2", "3");
    BigVecQ3 diff = a - b;
    CHECK(diff.x == BigRat("3"));
    CHECK(diff.y == BigRat("3"));
    CHECK(diff.z == BigRat("3"));
  }

  {
    BigVecQ3 a("2", "3", "4");
    BigRat s("5");
    BigVecQ3 prod = a * s;
    CHECK(prod.x == BigRat("10"));
    CHECK(prod.y == BigRat("15"));
    CHECK(prod.z == BigRat("20"));
  }

  {
    BigVecQ3 a("10", "15", "20");
    BigRat s("5");
    BigVecQ3 quot = a / s;
    CHECK(quot.x == BigRat("2"));
    CHECK(quot.y == BigRat("3"));
    CHECK(quot.z == BigRat("4"));
  }

  {
    BigVecQ3 a("1", "2", "3");
    BigVecQ3 b("4", "5", "6");
    BigRat dot = BigVecQ3::Dot(a, b);
    CHECK(dot == BigRat("32"));
  }

  {
    BigVecQ3 a("1", "2", "3");
    BigVecQ3 b("4", "5", "6");
    BigVecQ3 cross = BigVecQ3::Cross(a, b);
    CHECK(cross.x == BigRat("-3"));
    CHECK(cross.y == BigRat("6"));
    CHECK(cross.z == BigRat("-3"));
  }

  {
    BigVecQ3 a("2", "0", "0");
    BigVecQ3 b("0", "3", "0");
    BigVecQ3 c("0", "0", "4");
    BigRat det = BigVecQ3::Det(a, b, c);
    CHECK(det == BigRat("24"));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestBigVecQ2();
  TestBigVecQ3();

  Print("OK\n");
  return 0;
}

