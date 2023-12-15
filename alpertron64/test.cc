
#include <cstdint>
#include <initializer_list>
#include <numeric>

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/int128.h"
#include "ansi.h"

#include "quadmodll.h"

// TODO: Test routines in quadmodll!

static void TestSqrtModP() {
  {
    //    int64_t s = SqrtModP(13, 17, 118587876493, 9);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSqrtModP();

  printf(AGREY("Warning: This test doesn't test anything!") "\n");
  return 0;
}
