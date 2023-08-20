
#include "bignum/big.h"
#include "bignum/big-overloads.h"

#include <cstdio>

#include "timer.h"
#include "ansi.h"

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


int main(int argc, char **argv) {
  ANSI::Init();
  printf("Start.\n");
  fflush(stdout);

  BenchNegate();

  printf("OK\n");
}
