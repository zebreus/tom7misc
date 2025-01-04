
#include <cstdint>
#include <initializer_list>
#include <numeric>

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/int128.h"
#include "ansi.h"

#include "quad64.h"
#include "quadmodll64.h"

// TODO: Test routines in quadmodll!

int main(int argc, char **argv) {
  ANSI::Init();

  printf(AGREY("Warning: This test doesn't test anything!") "\n");
  return 0;
}
