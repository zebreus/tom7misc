
// Trying to find integers a,b,c that solve
// 222121 a^2 - b^2 + m = 0
// 360721 a^2 - c^2 + n = 0
// for small m,n. Works on (m, n) that have not
// yet been eliminated by mod.exe.

#include <cstdint>
#include <array>

#include "image.h"
#include "threadutil.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "factorization.h"
#include "atomic-util.h"
#include "arcfour.h"
#include "randutil.h"
#include "numbers.h"
#include "vector-util.h"

#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"

#include "work.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"

// for longnum. probably should move that...
#include "bhaskara-util.h"

#include "quad.h"

DECLARE_COUNTERS(tries, eliminated,
                 u3_, u4_, u5_, u6_, u7_, u8_);

#define AORANGE(s) ANSI_FG(247, 155, 57) s ANSI_RESET

bool HasSolutions(const Solutions &sols) {
  return sols.any_integers ||
    !sols.quadratic.empty() ||
    !sols.linear.empty() ||
    !sols.points.empty() ||
    !sols.recursive.empty();
}

// Exclude negative points (these are squared so they're just
// redundant).
static std::vector<std::pair<BigInt, BigInt>> NonNegPoints(
    const std::vector<PointSolution> &points) {
  std::vector<std::pair<BigInt, BigInt>> ret;
  for (const PointSolution &pt : points) {
    if (pt.X >= 0 && pt.Y >= 0) ret.emplace_back(pt.X, pt.Y);
  }
  return ret;
}

static void DoWork() {
  printf("Startup..\n");

  Work work;
  work.Load();

  Periodically save_per(60);

  std::vector<std::pair<int, int>> todo;
  for (int m = -333; m <= 333; m++) {
    for (int n = -333; n <= 333; n++) {
      if (work.GetNoSolAt(m, n) == 0 && Work::Eligible(m, n)) {
        todo.emplace_back(m, n);
      }
    }
  }

  int points = 0, rec = 0;
  Timer sol_timer;
  Periodically status_per(1.0);
  printf("Solve...\n");
  bool dirty = false;
  for (int idx = 0; idx < (int)todo.size(); idx++) {
    const auto &[m, n] = todo[idx];
    status_per.RunIf([&]() {
        printf(ANSI_UP "%s\n",
               ANSI::ProgressBar(idx, todo.size(),
                                 "Solve",
                                 sol_timer.Seconds()).c_str());
      });

    // 222121 a^2 - b^2 + m = 0
    // 360721 a^2 - c^2 + n = 0
    //
    // ax^2 + bxy + cy^2 + dx + ey + f = 0.
    //
    // so we have a1 = 222121, c1 = -1, f1 = m
    // so we have a2 = 360721, c2 = -1, f2 = n
    // and everything else zero.
    Solutions sols1 = QuadBigInt(BigInt(222121), BigInt(0), BigInt(-1),
                                 BigInt(0), BigInt(0), BigInt(m), nullptr);
    Solutions sols2 = QuadBigInt(BigInt(360721), BigInt(0), BigInt(-1),
                                 BigInt(0), BigInt(0), BigInt(n), nullptr);

    if (!HasSolutions(sols1) ||
        !HasSolutions(sols2)) {
      printf("\n\nNo solutions for (%d, %d)!\n\n", m, n);
      work.SetNoSolAt(m, n, Work::NOSOL_ALPERTRON);
      dirty = true;
    }

    for (const Solutions &sols : {sols1, sols2}) {
      // Unexpected solution types.
      CHECK(!sols.any_integers);
      CHECK(sols.quadratic.empty());
      CHECK(sols.linear.empty());
      if (!sols.points.empty()) points++;
      if (!sols.recursive.empty()) rec++;

      if (!sols.recursive.empty()) {
        CHECK(!sols.points.empty()) << "Not expecting recursive solutions "
          "but no point solutions?";
      }

    }

    // If there are a finite number of solutions, we can just check them.
    if (sols1.recursive.empty() || sols2.recursive.empty()) {
      if (sols1.recursive.empty() && sols2.recursive.empty()) {
        std::vector<std::pair<BigInt, BigInt>> s1 = NonNegPoints(sols1.points);
        std::vector<std::pair<BigInt, BigInt>> s2 = NonNegPoints(sols2.points);

        printf("\n\nFor (%d,%d), finite solutions:\n", m, n);

        for (const auto &[x, y] : s1) {
          printf("  (" ABLUE("%s") AWHITE(",") ABLUE("%s") ")\n",
                 LongNum(x).c_str(), LongNum(y).c_str());
        }
        for (const auto &[x, y] : s2) {
          printf("  (" APURPLE("%s") AWHITE(",") APURPLE("%s") ")\n",
                 LongNum(x).c_str(), LongNum(y).c_str());
        }

        // Must have an x shared between them, or else they cannot be
        // simultaneously solved. (Also, the x coordinate cannot be
        // zero, or else the square would be degenerate.)
        auto AnyShared = [&s1, &s2]() {
            for (int i = 0; i < s1.size(); i++) {
              for (int j = 0; j < s2.size(); j++) {
                if (s1[i].first == s2[j].first &&
                    s1[i].first != 0 &&
                    s2[j].first != 0) {
                  return true;
                }
              }
            }
            return false;
          };

        if (!AnyShared()) {
          printf("Can't be solved; they share no (nonzero) x coordinate.\n");
          work.SetNoSolAt(m, n, Work::NOSOL_ALPERTRON_FINITE);
          dirty = true;
        }

        printf("\n");
      }
    } else {
      // One of them is finite.

      auto PrintRecursive = [](
          const std::vector<std::pair<RecursiveSolution,
                                      RecursiveSolution>> &recs) {
          for (const auto &[r1, r2] : recs) {
            printf("  " AYELLOW("@") " "
                   "x_(n+1) = " ACYAN("%s") "x_n + " AGREEN("%s") "y_n + "
                   AWHITE("%s") "\n"
                   "    "
                   "y_(n+1) = " ACYAN("%s") "x_n + " AGREEN("%s") "y_n + "
                   AWHITE("%s") "\n",
                   LongNum(r1.P).c_str(),
                   LongNum(r1.Q).c_str(),
                   LongNum(r1.K).c_str(),
                   LongNum(r1.R).c_str(),
                   LongNum(r1.S).c_str(),
                   LongNum(r1.L).c_str());
            printf("  " AORANGE("@") " "
                   "x_(n+1) = " ACYAN("%s") "x_n + " AGREEN("%s") "y_n + "
                   AWHITE("%s") "\n"
                   "    "
                   "y_(n+1) = " ACYAN("%s") "x_n + " AGREEN("%s") "y_n + "
                   AWHITE("%s") "\n",
                   LongNum(r2.P).c_str(),
                   LongNum(r2.Q).c_str(),
                   LongNum(r2.K).c_str(),
                   LongNum(r2.R).c_str(),
                   LongNum(r2.S).c_str(),
                   LongNum(r2.L).c_str());
          }
        };


      std::vector<std::pair<BigInt, BigInt>> s1 = NonNegPoints(sols1.points);
      std::vector<std::pair<BigInt, BigInt>> s2 = NonNegPoints(sols2.points);
      printf("\n\nFor (%d,%d), finite solutions:\n", m, n);
      for (const auto &[x, y] : s1) {
        printf("  (" ABLUE("%s") AGREY(",") ABLUE("%s") ")\n",
               LongNum(x).c_str(), LongNum(y).c_str());
      }
      PrintRecursive(sols1.recursive);

      for (const auto &[x, y] : s2) {
        printf("  (" APURPLE("%s") AGREY(",") APURPLE("%s") ")\n",
               LongNum(x).c_str(), LongNum(y).c_str());
      }
      PrintRecursive(sols2.recursive);

      printf("\n");
    }
  }

  if (dirty) {
    work.Save();
  }

  printf("Solution types (%d * 2 = %d eqs):\n"
         "Points: %d\n"
         "Rec:    %d\n",
         (int)todo.size(), (int)todo.size() * 2,
         points, rec);

  printf("Took %s\n", ANSI::Time(sol_timer.Seconds()).c_str());

  #if 0
  Timer run_time;
  while (!todo.empty()) {

    /*
    save_per.RunIf([&work]() {
        work.Save();
        printf(AWHITE("Saved") ".\n");
      });
    */

    #if 0
    status_per.RunIf([&work, &run_time, &todo]() {
        std::optional<uint32_t> recent_min;
        for (const auto &[m, n] : todo) {
          const uint32_t p = work.PrimeAt(m, n);
          if (!recent_min.has_value() ||
              p < recent_min.value()) {
            recent_min.emplace(p);
          }
        }

        double sec = run_time.Seconds();
        uint64_t done = tries.Read();
        double dps = done / sec;

        printf(ABLUE("%s") " at " AWHITE("%.2fM") "/s "
               "Elim " AGREEN("%llu") " in %s. "
               "P: " APURPLE("%d") " "
               "Left: " ACYAN("%d") "\n",
               Util::UnsignedWithCommas(done).c_str(),
               dps / 1000000.0,
               eliminated.Read(),
               ANSI::Time(sec).c_str(),
               (int)recent_min.value_or(0),
               work.Remaining());
      });
    #endif


    // PERF: We could synchronize all the primes and then just
    // convert to Montgomery form once (including the coefficients).
    // This would also save us a lot of NextPrime calls.
    //
    // PERF: Suited to GPU!
    std::vector<std::pair<int, int>> res =
      ParallelMap(todo, [&work](const std::pair<int, int> &mn) ->
      std::pair<int, int> {
          const auto &[m, n] = mn;
          // Already ruled this one out.
          if (work.GetNoSolAt(m, n)) return {-1, -1};

          uint32_t last_p = work.PrimeAt(m, n);
          for (int i = 0; i < DEPTH; i++) {
            // Otherwise, get old upper bound.
            const uint32_t p = Factorization::NextPrime(last_p);

            SolutionFinder finder(p);

            // Now solve the simultaneous equations mod p.
            if (!finder.HasSolutionModP(m, n)) {
              eliminated++;
              return {p, p};
            }

            last_p = p;
          }

          tries += DEPTH;
          return {-1, last_p};
        }, 8);

    // Write results without lock.
    for (int i = 0; i < todo.size(); i++) {
      const auto &[m, n] = todo[i];
      const auto &[sol, p] = res[i];
      if (sol != -1) work.SetNoSolAt(m, n, sol);
      if (p != -1) work.PrimeAt(m, n) = p;
    }
  }
#endif
}


int main(int argc, char **argv) {
  ANSI::Init();

  DoWork();

  return 0;
}
