
#include "arcfour.h"
#include "randutil.h"

#include "bignum/big.h"
#include "bignum/big-overloads.h"

int main(int argc, char **argv) {

  ArcFour rc("gentests2");

  auto RandInt = [&rc]() {
      int64_t r = (int64_t)RandTo(&rc, 65536) - 32768;
      return r;
    };

  static constexpr int NUM_EXAMPLES = 32;
  for (int i = 0; i < NUM_EXAMPLES; i++) {

    // ax^2 + bxy + cy^2 + dx + ey = -f
    BigInt a((rc.Byte() < 200) ? RandInt() : 0);
    BigInt b((rc.Byte() < 128) ? RandInt() : 0);
    BigInt c((rc.Byte() < 200) ? RandInt() : 0);
    BigInt d((rc.Byte() < 128) ? RandInt() : 0);
    BigInt e((rc.Byte() < 128) ? RandInt() : 0);

    // the solution
    BigInt x(RandInt());
    BigInt y(RandInt());

    // now compute f
    BigInt negf = a * x * x + b * x * y + c * y * y + d * x + e * y;

    BigInt f = -negf;

    printf("# x = %s, y = %s\n"
           "./quad.exe %s %s %s %s %s %s 0 >> coverage.out\n",
           x.ToString().c_str(),
           y.ToString().c_str(),

           a.ToString().c_str(),
           b.ToString().c_str(),
           c.ToString().c_str(),
           d.ToString().c_str(),
           e.ToString().c_str(),
           f.ToString().c_str());
  }

  return 0;
}
