#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <bit>
#include <tuple>
#include <atomic>

#include <windows.h>

#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"
#include "periodically.h"
#include "timer.h"
#include "ansi.h"
#include "autoparallel.h"
#include "factorize.h"

#include "sos-util.h"
#include "sos-gpu.h"

using namespace std;

static CL *cl = nullptr;

using int64 = int64_t;



// Some useful facts:
//  Must be in OEIS https://oeis.org/A004431, sum of two distinct squares.
//  OEIS https://oeis.org/A000161 gives the count of ways, so if
//  A000161[n] >= 2, it is in this set.
//
//  https://proofwiki.org/wiki/Sum_of_2_Squares_in_2_Distinct_Ways
//    if m and n are in the set, then m*n is in the set (except
//    possibly for some equivalences like a = b).
// https://users.rowan.edu/~hassen/Papers/SUM%20OF%20TWO%20SQUARES%20IN%20MORE%20THAN%20ONE%20WAY.pdf
//    If it's in the set, then it is the product of sums of squares.

static std::atomic<int64_t> rejected_f{0ULL};
static std::atomic<int64_t> rejected_h{0ULL};
static std::atomic<int64_t> rejected_ff{0ULL};
static std::atomic<int64_t> rejected_hh{0ULL};
static std::atomic<int64_t> rejected_aa{0ULL};
#define INCREMENT(rej) rej.fetch_add(1, std::memory_order_relaxed)

// So now take numbers that can be written as sums of squares
// three ways: Z = B^2 + C^2 = D^2 + G^2 = E^2 + I^2
//
//  [a]  B   C
//
//   D   E  [f]
//
//   G  [h]  I
//
// This gives us the SUM = G + E + C, which then uniquely
// determines a, f, h (if they exist). Since the starting
// values were distinct, these residues are also distinct.
//
// The order of (B, C), (D, G), (E, I) matters, although there
// are some symmetries. We can req
template<class F>
inline static void AllWays(
    const std::vector<std::pair<uint64_t, uint64_t>> &ways,
    const F &fn) {
  for (int p = 0; p < ways.size(); p++) {
    const auto &[b, c] = ways[p];
    for (int q = 0; q < ways.size(); q++) {
      if (p != q) {
        const auto &[d, g] = ways[q];
        for (int r = 0; r < ways.size(); r++) {
          if (p != r && q != r) {
            const auto &[e, i] = ways[r];

            // Now eight ways of ordering the pairs.
            fn(/**/  b,    c,
               d,    e,  /**/
               g,  /**/    i);
            fn(/**/  c,    b,
               d,    e,  /**/
               g,  /**/    i);
            fn(/**/  b,    c,
               g,    e,  /**/
               d,  /**/    i);
            fn(/**/  c,    b,
               g,    e,  /**/
               d,  /**/    i);

            fn(/**/  b,    c,
               d,    i,  /**/
               g,  /**/    e);
            fn(/**/  c,    b,
               d,    i,  /**/
               g,  /**/    e);
            fn(/**/  b,    c,
               g,    i,  /**/
               d,  /**/    e);
            fn(/**/  c,    b,
               g,    i,  /**/
               d,  /**/    e);
          }
        }
      }
    }
  }
}

static void Try(int z,
                const std::vector<std::pair<uint64_t, uint64_t>> &ways) {

  AllWays(ways,
          [z](/*     a */ uint64_t b, uint64_t c,
              uint64_t d, uint64_t e, /*     f */
              uint64_t g, /*     h */ uint64_t i) {

            // We could factor these multiplications out, but since
            // AllWays inlines, the compiler can probably do it.
            const uint64_t bb = b * b;
            const uint64_t cc = c * c;
            const uint64_t dd = d * d;
            const uint64_t ee = e * e;
            const uint64_t gg = g * g;
            const uint64_t ii = i * i;

            // f is specified two ways; they must have the
            // same sum then.
            if (cc + ii != dd + ee) {
              INCREMENT(rejected_f);
              return;
            }
            // Same for h.
            if (gg + ii != bb + ee) {
              // XXX This never fails? Is it implied by the above?
              INCREMENT(rejected_h);
              return;
            }

            // Finally, check that a, f, h are integral.
            const uint64_t sum = cc + ee + gg;

            const uint64_t ff = sum - (dd + ee);
            const uint64_t f = Sqrt64(ff);
            if (f * f != ff) {
              INCREMENT(rejected_ff);
              return;
            }

            const uint64_t hh = sum - (bb + ee);
            const uint64_t h = Sqrt64(hh);
            if (h * h != hh) {
              INCREMENT(rejected_hh);
              const uint64_t aa = sum - (bb + cc);
              printf("\n"
                     ARED("sqrt(%llu)^2") " %llu^2 %llu^2\n"
                     "%llu^2 %llu^2 %llu^2\n"
                     "%llu^2 " ARED("sqrt(%llu)^2") " %llu^2\n"
                     ARED("but %llu * %llu != %llu") "\n"
                     "Sum: %llu\n",
                     aa, b, c,
                     d, e, f,
                     g, hh, i,
                     // error
                     h, h, hh,
                     sum);

              return;
            }

            const uint64_t aa = sum - (bb + cc);
            const uint64_t a = Sqrt64(aa);
            if (a * a != aa) {
              INCREMENT(rejected_aa);
              printf("\n"
                     ARED("sqrt(%llu)^2") " %llu^2 %llu^2\n"
                     "%llu^2 %llu^2 %llu^2\n"
                     "%llu^2 %llu^2 %llu^2\n"
                     ARED("but %llu * %llu != %llu") "\n"
                     "Sum: %llu\n",
                     aa, b, c,
                     d, e, f,
                     g, h, i,
                     // error
                     a, a, aa,
                     sum);
              return;
            }

            printf("%llu^2 %llu^2 %llu^2\n"
                   "%llu^2 %llu^2 %llu^2\n"
                   "%llu^2 %llu^2 %llu^2\n"
                   "Sum: %llu\n",
                   a, b, c,
                   d, e, f,
                   g, h, i,
                   sum);

            printf("Note: didn't completely check for uniqueness "
                   "or overflow!\n");
            CHECK(false) << "winner";
          });

}

static void GenCWW() {
  // XXX test that it can compile this opencl code
  CHECK(cl != nullptr);
  // NWaysGPU nways_gpu(cl);

  AutoParallelComp comp(16, 1000, true, "cww.autoparallel");

  std::mutex m;
  int triples = 0;
  Timer timer;

  std::mutex tm;
  double cww_sec = 0.0;
  double nways_sec = 0.0;
  double try_sec = 0.0;

  static constexpr uint64_t START = 72'000'000'000;
  static constexpr uint64_t NUM   = 12'000'000'000; /* ' */
  Periodically status_per(10.0);
  comp.
    ParallelComp(
      NUM,
      [&triples, &status_per, &timer, &m,
       &tm, &cww_sec, &nways_sec, &try_sec](uint64_t idx) {
        uint64_t num = START + idx;

        // Timer cww_timer;
        int nways = ChaiWahWu(num);
        /*
        double cww_sec = cww_timer.Seconds();
        {
          MutexLock tml(&tm);
          cww_sec += cww_sec;
        }
        */

        if (nways > 3) {
          // Timer nways_timer;
          std::vector<std::pair<uint64_t, uint64_t>> ways =
            BruteGetNWays(num, nways);
          // double nways_sec = nways_timer.Seconds();

          // Timer try_timer;
          Try(num, ways);
          /*
          double try_sec = try_timer.Seconds();
          {
            MutexLock tml(&tm);
            nways_sec += nways_sec;
            try_sec += try_sec;
          }
          */

          {
            MutexLock ml(&m);
            triples++;
            if (status_per.ShouldRun()) {
              double pct = (triples * 100.0)/(double)idx;
              double sec = timer.Seconds();
              double nps = idx / sec;
              printf("%d/%llu (%.5f%%) are triples (%s) %.1f/sec\n",
                     triples, idx, pct, AnsiTime(sec).c_str(), nps);
              const int64_t rf = rejected_f.load();
              const int64_t rh = rejected_h.load();
              const int64_t rff = rejected_ff.load();
              const int64_t rhh = rejected_hh.load();
              const int64_t raa = rejected_aa.load();
              printf("%lld " AGREY("rf") " %lld " AGREY("rh")
                     " %lld " AGREY("rff") " %lld " AGREY("rhh")
                     " %lld " AGREY("raa") "\n",
                     rf, rh, rff, rhh, raa);
              // Not counting overhead...
              double total_sec = cww_sec + nways_sec + try_sec;
              printf("Timing: %s cww %s nways %s try (%.2f%% + %.2f%% + %.2f%%)\n",
                     AnsiTime(cww_sec).c_str(),
                     AnsiTime(nways_sec).c_str(),
                     AnsiTime(try_sec).c_str(),
                     100.0 * cww_sec / total_sec,
                     100.0 * nways_sec / total_sec,
                     100.0 * try_sec / total_sec);
              string bar = AnsiProgressBar(idx, NUM, "Running", sec);
              // XXX put in stable spot
              printf("%s\n", bar.c_str());
            }
          }
        }
      });

  double sec = timer.Seconds();
  printf("Total triples: %d/%llu\n", triples, NUM);
  printf(AGREEN ("Done") " in %s. (%s/ea.)\n",
         AnsiTime(sec).c_str(), AnsiTime(sec / NUM).c_str());
  printf("Did %llu-%llu\n", START, START + NUM - 1);
  {
    const int64_t rf = rejected_f.load();
    const int64_t rh = rejected_h.load();
    const int64_t rff = rejected_ff.load();
    const int64_t rhh = rejected_hh.load();
    const int64_t raa = rejected_aa.load();

    FILE *f = fopen("sos.txt", "ab");
    CHECK(f != nullptr);
    fprintf(f, "Done in %s (%s/ea.)\n",
            AnsiStripCodes(AnsiTime(sec)).c_str(),
            AnsiStripCodes(AnsiTime(sec / NUM)).c_str());
    fprintf(f,
            "%lld rf %lld rh"
            " %lld rff %lld rhh"
            " %lld raa\n",
            rf, rh, rff, rhh, raa);
    fprintf(f, "Did %llu-%llu\n", START, START + NUM - 1);
    fclose(f);
  }
}

// So now take numbers that can be written as sums of squares
// three ways: Z = B^2 + C^2 = D^2 + G^2 = E^2 + I^2
//
//  [a]  B   C
//
//   D   E  [f]
//
//   G  [h]  I
//
// This gives us the SUM = G + E + C, which then uniquely
// determines a, f, h (if they exist). Since the starting
// values were distinct, these residues are also distinct.
//
// The order of (B, C), (D, G), (E, I) matters, although there
// are some symmetries. We can req

int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;

  if (!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) {
    LOG(FATAL) << "Unable to go to BELOW_NORMAL priority.\n";
  }

  GenCWW();

  return 0;
}

