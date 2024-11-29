
#include "bignum/big.h"

#include <unordered_set>
#include <string>
#include <vector>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "bignum/big-overloads.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "util.h"
#include "bhaskara-util.h"
#include "bounds.h"
#include "image.h"

using namespace std;

static constexpr bool CHECK_INVARIANTS = false;
static constexpr bool DEDUP = false;
static constexpr bool VERBOSE = false;

static std::vector<std::pair<BigInt, BigInt>> ParseXY(
    const string &f) {
  std::vector<std::pair<BigInt, BigInt>> xys;
  vector<string> lines = Util::NormalizeLines(Util::ReadFileToLines(f));
  CHECK(lines.size() % 2 == 0);
  for (int i = 0; i < lines.size(); i += 2) {
    BigInt x(lines[i]);
    BigInt y(lines[i + 1]);
    CHECK(x != 0 && y != 0);
    xys.emplace_back(std::move(x), std::move(y));
  }
  return xys;
}

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

#define TERM_A AFGCOLOR(183, 140, 237, "%s")
#define TERM_B AFGCOLOR(232, 237, 173, "%s")
#define TERM_C AFGCOLOR(160, 237, 237, "%s")

#define TERM_PB AFGCOLOR(173, 237, 173, "%s")
#define TERM_QC AFGCOLOR(250, 200, 173, "%s")
#define TERM_SC AFGCOLOR(170, 130, 250, "%s")
#define TERM_RB AFGCOLOR(120, 250, 200, "%s")

#define TERM_ERR AFGCOLOR(200, 170, 160, "%s")

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

static void Iterate(
    const BigInt &p,
    const BigInt &q,
    const BigInt &r,
    const BigInt &s,
    const BigInt &b_orig,
    const BigInt &c_orig) {

  Timer timer;
  Periodically status_per(5.0);
  Periodically image_per(120.0);
  image_per.SetPeriodOnce(30.0);

  BigInt b = b_orig;
  BigInt c = c_orig;

  std::optional<BigInt> best_err;

  std::vector<BigInt> history;

  static constexpr int64_t MAX_ITERS = -1;
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
      BigInt bb = b * b;
      BigInt cc = c * c;

      BigInt res = 360721 * bb - 222121 * cc;

      CHECK(res == 138600);

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
      CHECK(360721 * aa + 1 == cc);
      CHECK(222121 * aa + 1 == bb);
    }

    // Perf: could base this on the smaller of bb, cc?
    BigInt cc = c * c;
    BigInt a = BigInt::DivExact(cc - 1, 360721);
    BigInt err = SquareError(a);
    history.push_back(err);

    // Not valid when b==c, e.g. for the initial solution b=c=1.
    if (b != c) {
      if (!best_err.has_value() || err < best_err.value()) {
        printf("New best err on iter " AWHITE("%lld") ": "
               TERM_ERR "\n",
               iters, LongNum(err).c_str());
        best_err = {err};
      }
    }

    status_per.RunIf([&]() {
        printf("Iter %lld.\n"
               "a:   " TERM_A "\n"
               "b:   " TERM_B "\n"
               "c:   " TERM_C "\n"
               "err: " TERM_ERR "\n",
               iters,
               LongNum(a).c_str(),
               LongNum(b).c_str(),
               LongNum(c).c_str(),
               LongNum(err).c_str());

        double sec = timer.Seconds();
        double ips = iters / sec;
        printf("%s (%.2f/sec). Best err: " TERM_ERR "\n",
               ANSI::Time(sec).c_str(), ips,
               best_err.has_value() ?
               LongNum(best_err.value()).c_str() : "?");
      });

    image_per.RunIf([&]() {
        constexpr int WIDTH = 2000, HEIGHT = 1000;
        ImageRGBA plot(WIDTH, HEIGHT);
        plot.Clear32(0x000000FF);
        Bounds bounds;
        for (int64_t x = 0; x < history.size(); x++) {
          double y = BigInt::NaturalLog(history[x] + 1);
          bounds.Bound((double)x, y);
        }
        Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();

        double prev_y = BigInt::NaturalLog(history[0] + 1);
        for (int64_t x = 1; x < history.size(); x++) {
          double y = BigInt::NaturalLog(history[x] + 1);
          const auto &[sx0, sy0] = scaler.Scale(x - 1, prev_y);
          const auto &[sx1, sy1] = scaler.Scale(x, y);
          plot.BlendLine32(sx0, sy0, sx1, sy1, 0xFFFF77FF);
          prev_y = y;
        }

        for (int64_t x = 0; x < history.size(); x++) {
          double y = BigInt::NaturalLog(history[x] + 1);
          const auto &[sx, sy] = scaler.Scale(x, y);
          plot.BlendFilledCircleAA32(sx, sy, 4, 0xFF000044);
        }

        plot.BlendText2x32(
            10, 10, 0xFFFFFFAA,
            StringPrintf("Iters: %d. Best: %s",
                         (int)history.size(),
                         best_err.has_value() ?
                         LongNum(best_err.value()).c_str() : "?"));

        plot.Save("recurrence-error.png");
        printf("Wrote " ABLUE("%s") "\n", "recurrence-error.png");
      });

    // Generate next.

    BigInt pb = p * b;
    BigInt qc = q * c;
    BigInt rb = r * b;
    BigInt sc = s * c;

    // Next in sequence:
    b = pb + qc;
    c = sc + rb;
  }
}

static void StartErr(
    const BigInt &p,
    const BigInt &q,
    const BigInt &r,
    const BigInt &s,
    const BigInt &b,
    const BigInt &c) {

  if (CHECK_INVARIANTS) {
    BigInt bb = b * b;
    BigInt cc = c * c;

    BigInt res = 360721 * bb - 222121 * cc;

    CHECK(res == 138600);

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
    CHECK(360721 * aa + 1 == cc);
    CHECK(222121 * aa + 1 == bb);
  }

  // Perf: could base this on the smaller of bb, cc?
  BigInt cc = c * c;
  BigInt a = BigInt::DivExact(cc - 1, 360721);
  BigInt err = SquareError(a);

  printf("---------\n"
         "a:   " TERM_A "\n"
         "b:   " TERM_B "\n"
         "c:   " TERM_C "\n"
         "err: " TERM_ERR "\n",
         LongNum(a).c_str(),
         LongNum(b).c_str(),
         LongNum(c).c_str(),
         LongNum(err).c_str());
}

int main(int argc, char **argv) {
  ANSI::Init();

  // For each (x,y), (-x,-y) is also a solution.
  std::vector<std::pair<BigInt, BigInt>> xys =
    ParseXY("xy.txt");
  printf("There are %d (x,y)s\n", (int)xys.size());

  std::optional<BigInt> best_err;
  BigInt bestx, besty;
  for (const auto &[x, y] : xys) {
    BigInt ax = BigInt::Abs(x);
    BigInt ay = BigInt::Abs(y);
    if (ax != ay) {
      BigInt cc = y * y;
      BigInt aa = BigInt::DivExact(cc - 1, 360721);
      BigInt err = SquareError(aa);

      if (!best_err.has_value() ||
          err < best_err.value()) {
        best_err = err;
        bestx = x;
        besty = y;
      }
    }
  }

  {
    BigInt cc = besty * besty;
    BigInt aa = BigInt::DivExact(cc - 1, 360721);
    BigInt err = SquareError(aa);

    printf("Best:\n"
           "a^2: " TERM_A "\n"
           "b:   " TERM_B "\n"
           "c:   " TERM_C "\n"
           "err: " TERM_ERR "\n",
           LongNum(aa).c_str(),
           LongNum(bestx).c_str(),
           LongNum(besty).c_str(),
           LongNum(err).c_str());

    Util::WriteFile("best-aa.txt", aa.ToString());
  }
  return 0;


  BigInt p = BigFromFile("p.txt");
  BigInt q = BigFromFile("q.txt");
  BigInt r = BigFromFile("r.txt");
  BigInt s = BigFromFile("s.txt");

  // Ah, p = s. So this recurrence is much
  // simpler than it seems. That might actually
  // be good news?
  CHECK(p == s);

  // We also have s^2 - qr = 1
  // or s^2 = qr + 1
  // (Probably more generally this was s * p - q * r == 1)
  CHECK(s * s - q * r == 1);

  // Would be nice for the closed form if this were a square,
  // but it isn't!
  // (Actually it can't be, since it's big and one away from s^2).
  // BigInt rootpr = BigInt::Sqrt(p * r);
  // CHECK(rootpr * rootpr == p * r);

  #if 0
  printf("p^2 - qr = %s\n",
         LongNum(p * p - q * r).c_str());
  printf("pq - qs = %s\n",
         LongNum(p * q - q * s).c_str());
  return 0;
  #endif

  for (const auto &[x, y] : xys) {
    StartErr(p, q, r, s, x, y);
  }

  // Recur(p, q, r, s, x2, y2, 8);

  // Greedy(p, q, r, s, x2, y2);
  CHECK(xys.size() > 1);
  const auto &[x, y] = xys[1];
  Iterate(p, q, r, s, x, y);

  return 0;
}
