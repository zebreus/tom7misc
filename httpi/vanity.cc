
#include <format>
#include <string>

#include "ansi.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "periodically.h"
#include "timer.h"

// Cool if this could take a regex or other grammar
// specification.
static void Gen() {

  std::string hex_prefix = "C0FFEE";
  std::string hex_suffix = "D00D7";

  CHECK(BigInt::FromHex(hex_suffix).IsOdd());

  Timer timer;
  Periodically status_per(1.0);
  int64_t tested = 0;
  for (int zeroes = 5500; true; zeroes++) {
    std::string hex = std::format("{}{}{}",
                                  hex_prefix,
                                  std::string(zeroes, '0'),
                                  hex_suffix);
    BigInt b = BigInt::FromHex(hex);
    if (BigInt::IsProbablyPrime(b)) {
      Print("{} zeroes. " AWHITE("Prime") ":\n{}\n",
            zeroes, hex);
      return;
    }

    tested++;
    status_per.RunIf([&]{
        Print("Tested {} in {}\n", tested, ANSI::Time(timer.Seconds()));
      });
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Gen();

  Print("OK\n");
  return 0;
}
