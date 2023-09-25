
#include "bignum/big.h"
#include "bignum/big-overloads.h"

#include <cstdio>

#include "timer.h"
#include "ansi.h"
#include "base/logging.h"

static void BenchNegate() {
  Timer timer;
  BigInt x = BigInt{1} << 40000000;
  for (int i = 0; i < 1000; i++) {
    x = 2 * -std::move(x) * BigInt{i + 1};
  }
  double took = timer.Seconds();
  printf("BenchNegate: %d digits %s\n",
         (int)x.ToString().size(),
         ANSI::Time(took).c_str());
}

static void TestShift() {
  int xi = -1000;

  BigInt x(-1000);

  for (int s = 0; s < 12; s++) {
    int xis = xi >> s;
    BigInt xs = x >> s;
    CHECK(xs == xis);
  }
}

static void TestAssigningOps() {

  // TODO more! I have screwed these up with copy-paste.

  {
    BigInt x(41);
    x /= 2;
    CHECK(BigInt == 20);
  }

  {
    BigInt x(44);
    x /= BigInt{2};
    CHECK(BigInt == 22);
  }

}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("Start.\n");
  fflush(stdout);

  BenchNegate();
  TestShift();

  TestOps();

  printf("OK\n");
}
