
#include "bignum/big.h"

#include <unordered_set>
#include <string>
#include <vector>
#include <cstdint>

#include "base/logging.h"
#include "bignum/big-overloads.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "util.h"
#include "bhaskara-util.h"

using namespace std;

static constexpr bool CHECK_INVARIANTS = true;

static BigInt BigFromFile(const string &f) {
  string s = Util::NormalizeWhitespace(Util::ReadFile(f));
  CHECK(!s.empty()) << f;
  BigInt x(s);
  return x;
}

static BigInt SquareError(const BigInt &aa) {
  // PERF GMP offers a "mpz_sqrtrem"
  BigInt a1 = BigInt::Sqrt(aa);
  BigInt a2 = a1 + 1;
  BigInt aa1 = a1 * a1;
  BigInt aa2 = a2 * a2;
  // TODO: We should consider if it's better to only
  // approach from one side?
  return std::min(BigInt::Abs(aa - aa1),
                  BigInt::Abs(aa - aa2));
}

#if 0
// TODO: search more than one bit ahead
static std::pair<BigInt, BigInt> Recur(
    const BigInt &p,
    const BigInt &q,
    const BigInt &r,
    const BigInt &s,
    const BigInt &b,
    const BigInt &c,
    int depth) {
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
    // printf("divisible :)\n");

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
#endif

struct BC {
  BC() : BC(0, 0) {}
  BC(BigInt bb, BigInt cc) : b(std::move(bb)),
                             c(std::move(cc)) {}
  BC(int64_t bb, int64_t cc) : b(bb), c(cc) {}
  inline void Swap(BC *other) {
    b.Swap(&other->b);
    c.Swap(&other->c);
  }

  BC(const BC &other) : b(other.b), c(other.c) {}

  BC &operator =(const BC &other) {
    // Self-assignment does nothing.
    if (this == &other) return *this;
    b = other.b;
    c = other.c;
    return *this;
  }
  BC &operator =(BC &&other) {
    // other must remain valid.
    Swap(&other);
    return *this;
  }

  BigInt b, c;
};

struct HashBC {
  size_t operator()(const BC &bc) const {
    return (size_t)(BigInt::LowWord(bc.c) * 0x314159 +
                    BigInt::LowWord(bc.b));
  }
};

namespace std {
template <> struct hash<BC> {
  size_t operator()(const BC &bc) const {
    return HashBC()(bc);
  }
};
}

static inline bool operator ==(const BC &x, const BC &y) {
  return x.c == y.c && x.b == y.b;
}

using BCSet = std::unordered_set<BC, HashBC>;

#define TERM_B AFGCOLOR(232, 237, 173, "%s")
#define TERM_C AFGCOLOR(160, 237, 237, "%s")

#define TERM_PB AFGCOLOR(173, 237, 173, "%s")
#define TERM_QC AFGCOLOR(250, 200, 173, "%s")
#define TERM_SC AFGCOLOR(170, 130, 250, "%s")
#define TERM_RB AFGCOLOR(120, 250, 200, "%s")

static constexpr bool DEDUP = false;
static constexpr bool VERBOSE = true;

static void Greedy(
    const BigInt &p,
    const BigInt &q,
    const BigInt &r,
    const BigInt &s,
    const BigInt &b_orig,
    const BigInt &c_orig) {

  Timer timer;
  Periodically status_per(5.0);
  BCSet seen;

  BigInt b = b_orig;
  BigInt c = c_orig;

  static constexpr int64_t MAX_ITERS = 3;
  for (int64_t iters = 0; MAX_ITERS < 0 || iters < MAX_ITERS; iters++) {

    if (VERBOSE) {
      printf("Iter %lld.\n"
             "b: " TERM_B "\n"
             "c: " TERM_C "\n",
             iters,
             LongNum(b).c_str(),
             LongNum(c).c_str());
    }

    if (CHECK_INVARIANTS) {
      if (DEDUP) {
        CHECK(seen.find(BC(b, c)) == seen.end());
      }
      BigInt bb = b * b;
      BigInt cc = c * c;

      BigInt res = 360721_b * bb - 222121_b * cc;

      CHECK(res == 138600_b);

      // c^2 = 360721 a^2 + 1
      // b^2 = 222121 a^2 + 1

      CHECK((cc - 1_b) % 360721_b == 0_b);
      CHECK((bb - 1_b) % 222121_b == 0_b);
      // printf("divisible :)\n");

      BigInt a1 = (cc - 1_b) / 360721_b;
      BigInt a2 = (bb - 1_b) / 222121_b;

      CHECK(a1 == a2);
      const BigInt &aa = a1;

      // const BigInt aa = a * a;
      CHECK(360721_b * aa + 1_b == cc);
      CHECK(222121_b * aa + 1_b == bb);
    }

    if (DEDUP) {
      seen.insert(BC(b, c));
    }

    BigInt pb = p * b;
    BigInt qc = q * c;
    BigInt rb = r * b;
    BigInt sc = s * c;

    // Two options:
    BigInt b2 = pb + qc;
    BigInt c2 = sc + rb;

    BigInt b3 = pb - qc;
    BigInt c3 = sc - rb;

    BigInt cc2 = c2 * c2;
    // Compute the resulting a for each one.
    // PERF: DivExact.
    // PERF: We only need one of bb or cc!
    BigInt a2 = (cc2 - 1) / 360721_b;

    BigInt cc3 = c3 * c3;
    BigInt a3 = (cc3 - 1) / 360721_b;

    BigInt err2 = SquareError(a2);
    BigInt err3 = SquareError(a3);

    if (VERBOSE) {
      printf("[" APURPLE("0") "] b: "
             TERM_PB " + " TERM_QC "\n"
             "[" APURPLE("0") "] c: "
             TERM_SC " + " TERM_RB "\n",
             LongNum(pb).c_str(),
             LongNum(qc).c_str(),
             LongNum(sc).c_str(),
             LongNum(rb).c_str());
    }

    BigInt *err;
    char bit;
    if (seen.find(BC(b2, c2)) == seen.end() &&
        err2 < err3) {
      b = std::move(b2);
      c = std::move(c2);
      err = &err2;
      // does this always lead back to the predecessor?
      bit = '0';
    } else if (seen.find(BC(b3, c3)) == seen.end()) {
      b = std::move(b3);
      c = std::move(c3);
      err = &err3;
      bit = '1';
    } else {
      printf(ARED("stuck") "\n");
      return;
    }
    if (!VERBOSE) {
      printf("%c", bit);
    }

    status_per.RunIf([&]() {
        const int digs = (int)err->ToString().size();
        double sec = timer.Seconds();
        double ips = iters / sec;
        printf("\n%d digits. %s (%.2f/sec)\n",
               digs, ANSI::Time(sec).c_str(), ips);
        fflush(stdout);
      });
  }
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

  // Recur(p, q, r, s, x2, y2, 8);

  // Greedy(p, q, r, s, x2, y2);
  Greedy(p, q, r, s, x1, y1);

  return 0;
}
