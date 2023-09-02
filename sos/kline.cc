
// For this probably ill-fated attempt, enumerate rationals that
// are close to     1/(sqrt(2) sqrt(291421 - sqrt(84418676537)))
// (we can also try 1/(sqrt(2) sqrt(291421 + sqrt(84418676537)))).
// These are the (probably irrational) roots of the "k" function, and so
// if it is to have a rational solution that's close to zero, it would
// be a rational close to one of these.

#include "sos-util.h"

#include <vector>
#include <string>
#include <cstdint>

#include "atomic-util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "bounds.h"
#include "threadutil.h"
#include "periodically.h"
#include "ansi.h"
#include "timer.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "bhaskara-util.h"
#include "util.h"
#include "color-util.h"

using namespace std;

static constexpr bool CHECK_INVARIANTS = false;
static constexpr bool VERBOSE = false;

DECLARE_COUNTERS(points_done, searches_done, bad_starts,
                 u1_, u2_, u3_, u4_, u5_);

static BigInt GetK(const BigInt &a, const BigInt &b) {
  BigInt aa = a * a;
  BigInt bb = b * b;
  BigInt aaaa = aa * aa;
  BigInt bbbb = bb * bb;

  return 1165684 * std::move(aa) * std::move(bb) +
    -2030090816 * std::move(aaaa) -
    std::move(bbbb);
}

static void Iterate() {
  Timer run_timer;
  Periodically status_per(5.0);

  // a/b is the rational. It should be an approximation to
  // 1/(sqrt(2) sqrt(291421 - sqrt(84418676537)))
  // which is 1/41.76307...

  // Best for this epoch.
  BigInt besta{1};
  BigInt bestb{1};
  // absolute value.
  BigInt bestk = GetK(besta, bestb);
  BigInt bestabsk = BigInt::Abs(bestk);
  // Overall best; only updated at epoch.
  BigInt allbestk = bestk;

  FILE *file = fopen("kline.csv", "ab");

  // Rather than explicitly trying to approximate the number,
  // we know that the function k is negative on one side and
  // positive on the other. So for each denominator we consider,
  // we just try to find adjacent numerators that have different
  // signs, and we are done.
  // Do we have to consider every denominator? I guess so?
  for (uint64_t udenom = 15544090624ULL; true; udenom++) {
    // The number will be between 1/41 and 1/42.
    BigInt n1, n2;

    // Use a different approach to setting the initial
    // conditions for different ranges. These are not
    // magic numbers; I just picked something in the
    // ballpark.
    if (udenom < 25165824ULL) {
      uint64_t un1 = udenom / 42;
      uint64_t un2 = udenom / 41;
      // XXX for small denominators, these might start equal.
      if (un1 == un2) ++un2;

      n1 = BigInt(un1);
      n2 = BigInt(un2);
    } if (udenom < 15544090624ULL) {
      // For big denominators, use a more accurate rational.
      //  1.0000
      // 41.7630
      // reduces from ~21.5 splits to 8 splits average;
      // more than twice as fast!
      uint64_t un1 = (udenom * 10000) / 417631;
      uint64_t un2 = (udenom * 10000) / 417630;

      n1 = BigInt(un1);
      n2 = BigInt(un2);
    } else {
      // Getting too big to do the multiplication in 64 bits.
      // PERF: We could use a power of two numerator. (And
      // then in fact we could probably stay in 64 bits?)
      //  1.0000000
      // 41.76307473298086404811185
      // 0.089 splits/ea!
      BigInt uscale = BigInt(udenom) * 100000000;
      n1 = uscale / 4176307474;
      n2 = uscale / 4176307473;
      if (n1 == n1) ++n2;
      if (CHECK_INVARIANTS) {
        CHECK(n1 != n2) << n1.ToString();
      }
    }

    BigInt denom{udenom};
    // Compute the parts that only depend on the denominator.
    // DenomK(a) = GetK(a, denom).
    const BigInt bb = denom * denom;
    const BigInt xbb = 1165684 * bb;
    const BigInt bbbb = bb * bb;
    auto DenomK = [&](const BigInt &a) {
      BigInt aa = a * a;
      BigInt aaaa = aa * aa;
      return xbb * std::move(aa) +
        -2030090816 * std::move(aaaa) -
        bbbb;
      };

    BigInt k1 = DenomK(n1);
    BigInt k2 = DenomK(n2);
    if (VERBOSE) {
      printf("%s/%s: %s\n"
             "%s/%s: %s\n",
             n1.ToString().c_str(), denom.ToString().c_str(),
             k1.ToString().c_str(),
             n2.ToString().c_str(), denom.ToString().c_str(),
             k2.ToString().c_str());
    }

    // ugh
    while (k2 > 0) {
      bad_starts++;
      ++n2;
      k2 = DenomK(n2);
      if (VERBOSE) {
        printf("bad start. n2,k2 %s/d = %s\n",
               n2.ToString().c_str(),
               k2.ToString().c_str());
      }
    }

    CHECK(k1 > 0) << n1.ToString() << "/" << denom.ToString()
                  << ": " << k1.ToString();
    CHECK(k2 < 0) << n2.ToString() << "/" << denom.ToString()
                  << ": " << k2.ToString();

    // Binary search for the "zero".
    for (;;) {
      CHECK(n1 < n2);
      CHECK(k1 > 0);
      CHECK(k2 < 0);

      BigInt diff = n2 - n1;
      if (diff == 1) break;

      BigInt nn = n1 + (diff >> 1);
      // now nn > n1
      if (CHECK_INVARIANTS) {
        CHECK(nn > n1);
      }

      BigInt k = DenomK(nn);
      if (k > 0) {
        if (CHECK_INVARIANTS) {
          CHECK(n1 != nn);
        }
        k1 = std::move(k);
        n1 = std::move(nn);
      } else {
        if (CHECK_INVARIANTS) {
          CHECK(k != 0) << "Supposedly impossible! "
            "nn: " << nn.ToString() << "\n"
            "dd: " << denom.ToString();
          CHECK(n2 != nn);
        }
        k2 = std::move(k);
        n2 = std::move(nn);
      }
      searches_done++;
    }

    points_done++;

    // Now zero is sandwiched between n1/d and n2/d.
    // Either n1 or n2 could be a new best.
    if (BigInt::Abs(k1) < bestabsk) {
      bestabsk = BigInt::Abs(k1);
      besta = n1;
      bestb = denom;
    }

    if (BigInt::Abs(k2) < bestabsk) {
      bestabsk = BigInt::Abs(k2);
      besta = n2;
      bestb = denom;
    }

    if (udenom % (1024 * 1024) == 0) {
      if (bestabsk < BigInt::Abs(allbestk)) {
        allbestk = bestabsk;

        printf("\n\n\nNew best: %s/%s = %s\n\n\n\n",
               LongNum(besta).c_str(), LongNum(bestb).c_str(),
               LongNum(allbestk).c_str());
      }

      fprintf(file,
              "%llu, %s, %s, %s, %s\n",
              udenom,
              allbestk.ToString().c_str(),
              besta.ToString().c_str(),
              bestb.ToString().c_str(),
              bestk.ToString().c_str());

      // reset stats
      bestabsk = BigInt::Abs(k2);
      bestk = k2;
      besta = n2;
      bestb = denom;

      fflush(file);
    }

    status_per.RunIf([&](){
        double sec = run_timer.Seconds();
        int64_t p = points_done.Read();
        int64_t bs = bad_starts.Read();
        int64_t searches = searches_done.Read();
        double spp = searches / (double)p;
        printf(ANSI_UP ANSI_UP ANSI_UP
               "[%s] %.3f splits ea. (%.1f d/s). Best: " ACYAN("%s") "\n"
               "Bad starts %lld. Recent: " ABLUE("%s") "/" APURPLE("%s") " =\n"
               AWHITE("%s") "\n"
               ,
               LongNum(denom).c_str(),
               spp,
               p/sec,
               LongNum(allbestk).c_str(),
               bs,
               LongNum(besta).c_str(),
               LongNum(bestb).c_str(),
               LongNum(bestk).c_str());
      });
  }

  // unreachable, though...
  // fclose(file);
}


int main(int argc, char **argv) {
  ANSI::Init();

  Iterate();

  return 0;
}
