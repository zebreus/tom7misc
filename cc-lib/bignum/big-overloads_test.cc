
#include "bignum/big.h"
#include "bignum/big-overloads.h"

#include <cstdio>
#include <utility>

#include "timer.h"
#include "ansi.h"
#include "base/logging.h"

static void BenchNegate() {
  Timer timer;
  BigInt x = BigInt{1} << 4000000;
  for (int i = 0; i < 10000; i++) {
    x = 2 * -std::move(x) * BigInt{i + 1};
    // if (i % 1000 == 0) printf("%d/%d\n", i, 10000);
  }
  double took = timer.Seconds();
  printf("BenchNegate: done in %s\n",
         ANSI::Time(took).c_str());
  fflush(stdout);
}

static void TestShift() {
  int xi = -1000;

  BigInt x(-1000);

  for (int s = 0; s < 12; s++) {
    int xis = xi >> s;
    BigInt xs = x >> s;
    CHECK(xs == xis) << "at " << s << " want " << xis
                     << " got " << xs.ToString();
  }
}

static void TestAssigningOps() {

  // TODO more! I have screwed these up with copy-paste.

  {
    BigInt x(41);
    x /= 2;
    CHECK(x == 20);
  }

  {
    BigInt x(44);
    x /= BigInt{2};
    CHECK(x == 22);
  }

  {
    BigInt x(42);
    x &= BigInt{3};
    CHECK(x == 2);
  }

  {
    BigInt x(12345);
    const BigInt five(5);
    x %= five;
    CHECK(x == 0);
  }

}

static void TestBinaryOps() {
  {
    BigInt x(13310);
    CHECK((x & 717) == 716);
    CHECK((717 & x) == 716);
  }

  {
    BigInt x(13310);
    CHECK(x % 10 == 0);
    CHECK(x % 2 == 0);
  }

  {
    BigInt x(13317);
    CHECK(x % 10 == 7);
    CHECK(x % 2 == 1);
  }

  {
    // Check that we can add two temporaries (no ambiguity).
    BigInt c = BigInt("123456789012384712304871978") +
      BigInt("1038947598127349857171711710");
    CHECK(c.ToString() == "1162404387139734569476583688");
  }
}

static void TestCompareRat() {
  BigRat a(1, 5);
  BigRat b(10, 3);
  BigRat c(-3, 777);

  BigRat d(11);

  CHECK(a < b);
  CHECK(a <= b);
  CHECK(!(a == b));
  CHECK(!(a > b));
  CHECK(!(a >= b));

  CHECK(a == a);
  CHECK(a <= a);
  CHECK(a >= a);
  CHECK(!(a != a));

  CHECK(c < b);
  CHECK(c <= b);
  CHECK(b > c);
  CHECK(b >= c);
  CHECK(!(b == c));
  CHECK(b != c);

  CHECK(d == 11);
  CHECK(11 == d);

  CHECK(d != 10);
  CHECK(-11 != d);
}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("Start.\n");
  fflush(stdout);

  BenchNegate();
  TestShift();

  TestAssigningOps();
  TestBinaryOps();

  TestCompareRat();

  printf("OK\n");
  return 0;
}
