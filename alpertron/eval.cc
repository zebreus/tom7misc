
#include "quad.h"

#include <array>
#include <string>
#include <cstdio>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "threadutil.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "timer.h"
#include "periodically.h"
#include "ansi.h"
#include "atomic-util.h"

using namespace std;

int main(int argc, char **argv) {
  ANSI::Init();

  if (argc != 9) {
    (void)printf("8 args; 6 coefficients:\n"
                 "   Ax^2 + Bxy + Cy^2 + Dx + Ey + F = 0\n"
                 "followed by the two values x, y\n");
    return 1;
  }

  BigInt A(argv[1]);
  BigInt B(argv[2]);
  BigInt C(argv[3]);
  BigInt D(argv[4]);
  BigInt E(argv[5]);
  BigInt F(argv[6]);

  BigInt x(argv[7]);
  BigInt y(argv[8]);

  BigInt r =
    A * (x * x) +
    B * (x * y) +
    C * (y * y) +
    D * x +
    E * y +
    F;

  printf("\n"
         AWHITE("%s") APURPLE("x^2") " + "
         AWHITE("%s") APURPLE("x") ACYAN("y") " + "
         AWHITE("%s") ACYAN("y^2") " + "
         AWHITE("%s") APURPLE("x") " + "
         AWHITE("%s") ACYAN("y") " + "
         AWHITE("%s") " = "
         "%s" "%s" ANSI_RESET "\n"
         "With " APURPLE("x") " = " AWHITE("%s") "\n"
         " and " ACYAN("y") " = " AWHITE("%s") "\n",
         A.ToString().c_str(),
         B.ToString().c_str(),
         C.ToString().c_str(),
         D.ToString().c_str(),
         E.ToString().c_str(),
         F.ToString().c_str(),
         (r == 0) ? ANSI_GREEN : ANSI_RED,
         r.ToString().c_str(),
         x.ToString().c_str(),
         y.ToString().c_str());

  return 0;
}
