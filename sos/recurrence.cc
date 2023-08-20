
#include "bignum/big.h"

#include <string>
#include <vector>
#include <cstdint>

#include "base/logging.h"
#include "bignum/big-overloads.h"
#include "ansi.h"

#include "util.h"

using namespace std;

static constexpr bool CHECK_INVARIANTS = true;

static BigInt BigFromFile(const string &f) {
  string s = Util::NormalizeWhitespace(Util::ReadFile(f));
  CHECK(!s.empty()) << f;
  BigInt x(s);
  return x;
}

static void Recur(
    const BigInt &p,
    const BigInt &q,
    const BigInt &r,
    const BigInt &s,
    BigInt b, BigInt c, int depth) {
  if (!depth) return;

  if (CHECK_INVARIANTS) {
    BigInt bb = b * b;
    BigInt cc = c * c;

    BigInt res = 360721_b * bb - 222121_b * cc;

    CHECK(res == 138600_b);

    // c^2 = 360721 a^2 + 1
    // b^2 = 222121 a^2 + 1

    CHECK((cc - 1_b) % 360721_b == 0_b);
    CHECK((bb - 1_b) % 222121_b == 0_b);
    printf("divisible :)\n");

    BigInt a1 = (cc - 1_b) / 360721_b;
    BigInt a2 = (bb - 1_b) / 222121_b;

    CHECK(a1 == a2);
    const BigInt &aa = a1;

    // const BigInt aa = a * a;
    CHECK(360721_b * aa + 1_b == cc);
    CHECK(222121_b * aa + 1_b == bb);
  }

  // x<sub>n+1</sub> = P * x<sub>n</sub> + Q * y<sub>n</sub>
  // y<sub>n+1</sub> = R * x<sub>n</sub> + S * y<sub>n</sub>

  BigInt b2 = p * b + q * c;
  BigInt c2 = r * b + s * c;

  Recur(p, q, r, s, b2, c2, depth - 1);

  BigInt b3 = p * b - q * c;
  BigInt c3 = -(r * b) + s * c;

  Recur(p, q, r, s, b3, c3, depth - 1);
}

int main(int argc, char **argv) {
  ANSI::Init();

  // For each (x,y), (-x,-y) is also a solution.
  BigInt x1 = 1_b;
  BigInt y1 = 1_b;

  BigInt x2 = BigFromFile("x2.txt");
  BigInt y2 = BigFromFile("y2.txt");

  // TODO: lots more, including some that are much smaller.

  BigInt p = BigFromFile("p.txt");
  BigInt q = BigFromFile("q.txt");
  BigInt r = BigFromFile("r.txt");
  BigInt s = BigFromFile("s.txt");

  Recur(p, q, r, s, x2, y2, 8);


  #if 0
  BigInt bb = b * b;
  BigInt cc = c * c;

  BigInt res = 360721_b * bb - 222121_b * cc;

  CHECK(res == 138600_b);

  // c^2 = 360721 a^2 + 1
  // b^2 = 222121 a^2 + 1

  CHECK((cc - 1_b) % 360721_b == 0_b);
  CHECK((bb - 1_b) % 222121_b == 0_b);
  printf("divisible :)\n");

  BigInt a1 = (cc - 1_b) / 360721_b;
  BigInt a2 = (bb - 1_b) / 222121_b;

  CHECK(a1 == a2);
  const BigInt &aa = a1;

  // const BigInt aa = a * a;
  CHECK(360721_b * aa + 1_b == cc);
  CHECK(222121_b * aa + 1_b == bb);

  const BigInt a = BigInt::Sqrt(aa);
  CHECK(a * a == aa) << (a * a - aa).ToString();

  printf("%s\n",
         a.ToString().c_str());
  #endif

  return 0;
}
