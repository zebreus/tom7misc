
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

#include "mod-util.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"

// for longnum. probably should move that...
#include "bhaskara-util.h"

#include "quad.h"

static constexpr bool ALLOW_SAVE = true;

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


static void CheckSolutions(
    const std::unordered_map<BigInt, BigInt> &mpts,
    const std::unordered_map<BigInt, BigInt> &npts) {
  printf(AWHITE("m") " points: %d. " AWHITE("n") " points: %d.\n",
         (int)mpts.size(), (int)npts.size());

  bool any = false;
  // We will find coincident points (e.g. the known solution for 333)
  // that are outside the diamond.
  for (const auto &[x, my] : mpts) {
    // Skip some known solutions.
    if (x == 10 && my == 4713) {
      printf("my: %s\n", LongNum(my).c_str());
      continue;
    }

    auto it = npts.find(x);
    if (it != npts.end()) {
      const BigInt &ny = it->second;
      printf(AORANGE("!!!") " "
             AGREEN("Have both x") " = %s !\n", LongNum(x).c_str());
      printf("  my: " ABLUE("%s") "\n", LongNum(my).c_str());
      printf("  ny: " APURPLE("%s") "\n", LongNum(ny).c_str());
      any = true;
    }
  }

  if (!any) {
    printf(AGREY("No new shared x.") "\n");
  }
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

static int points = 0, rec = 0;
static void ValidateSolutions(const Solutions &sols) {
  // Unexpected solution types.
  CHECK(!sols.any_integers);
  CHECK(sols.quadratic.empty());
  CHECK(sols.linear.empty());

  if (!sols.points.empty()) points++;
  if (!sols.recursive.empty()) rec++;

  if (!sols.recursive.empty()) {
    CHECK(!sols.points.empty()) << "Not expecting recursive without "
      "point solutions?";
  }

  // XXX: Might be a good idea to check a few points?
}

static void ExpandNum(std::unordered_map<BigInt, BigInt> *pts,
                      const RecursiveSolution &rec,
                      int num) {
  std::vector<std::pair<BigInt, BigInt>> todo;
  todo.reserve(pts->size());
  for (const auto &[x, y] : *pts) {
    todo.emplace_back(x, y);
  }

  Timer timer;
  Periodically status_per(1.0);
  printf("Expand\n");
  for (int idx = 0; idx < (int)todo.size(); idx++) {
    const auto &p = todo[idx];
    status_per.RunIf([&]() {
        printf(ANSI_UP "%s\n",
               ANSI::ProgressBar(idx, todo.size(),
                                 StringPrintf("Expand %d", num),
                                 timer.Seconds()).c_str());
      });

    BigInt x = p.first;
    BigInt y = p.second;
    for (int i = 0; i < num; i++) {
      BigInt x1 = rec.P * x + rec.Q * y;
      BigInt y1 = rec.R * x + rec.S * y;

      // I think this is possible. Would need a different
      // data structure or something? But it doesn't really
      // matter unless we find a solution, as different y just
      // give us different error.
      CHECK(!pts->contains(x1)) << LongNum(x1);

      (*pts)[x1] = y1;

      x = std::move(x1);
      y = std::move(y1);
    }
  }
  printf("\n");
}


static std::string AnsiRecursive(const RecursiveSolution &rec) {
  return StringPrintf("  " AYELLOW("@") " "
                      "x_(n+1) = " ACYAN("%s") "x_n + " AGREEN("%s") "y_n + "
                      AWHITE("%s") "\n"
                      "    "
                      "y_(n+1) = " ACYAN("%s") "x_n + " AGREEN("%s") "y_n + "
                      AWHITE("%s") "\n",
                      LongNum(rec.P).c_str(),
                      LongNum(rec.Q).c_str(),
                      LongNum(rec.K).c_str(),
                      LongNum(rec.R).c_str(),
                      LongNum(rec.S).c_str(),
                      LongNum(rec.L).c_str());
}

static bool operator ==(const RecursiveSolution &a,
                        const RecursiveSolution &b) {
  return
    a.P == b.P && a.Q == b.Q && a.K == b.K &&
    a.R == b.R && a.S == b.S && a.L == b.L;
}

// Get the one recursive solution shared by every entry.
// Ignores s[0] (this just reports the point (0,0) because the
// recursive solution is degenerate there).
// Note that there are "two" recursive solutions, which correspond
// to different directions on the curve. The two solutions arrive
// in a different order sometimes (depending on sign of entry).
RecursiveSolution GetSameRec(const std::map<int, Solutions> &s) {
  CHECK(!s.empty());
  const int orig = s.begin()->first;
  CHECK(orig != 0) << "Need to make this smarter if we've "
    "eliminated every negative element.";

  CHECK(s.begin()->second.recursive.size() == 1);
  const auto &[rc1, rc2] = s.begin()->second.recursive[0];
  for (const auto &[o, sols] : s) {
    if (o != 0) {
      CHECK(sols.recursive.size() == 1);
      if (rc1 != sols.recursive[0].first &&
          rc2 != sols.recursive[0].first) {
        printf(ARED("Previously") " (%d):\n"
               "%s"
               ARED("Now") " (%d):\n"
               "%s\n",
               orig, AnsiRecursive(rc1).c_str(),
               o, AnsiRecursive(sols.recursive[0].first).c_str());
        CHECK(false);
      }
    }
  }

  // Zero for these particular equations, so we ignore it.
  CHECK(rc1.K == 0);
  CHECK(rc1.L == 0);

  return rc1;
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

  Timer sol_timer;
  Periodically status_per(1.0);
  printf("Solve...\n");
  bool dirty = false;

  // Alpertron solves the equations independently, so we can just
  // solve each row and column that appears.
  std::map<int, Solutions> msols;
  std::map<int, Solutions> nsols;
  auto GetSolutions = [&msols, &nsols](int m, int n) {
      auto mit = msols.find(m);
      auto nit = nsols.find(n);
      CHECK(mit != msols.end()) << "Missing msol " << m;
      CHECK(nit != nsols.end()) << "Missing nsol " << n;
      return std::make_pair(&mit->second, &nit->second);
    };

  for (int idx = 0; idx < (int)todo.size(); idx++) {
    const auto &[m, n] = todo[idx];
    status_per.RunIf([&]() {
        printf(ANSI_UP "%s\n",
               ANSI::ProgressBar(idx, todo.size(),
                                 StringPrintf("Solved %d m, %d n",
                                              msols.size(),
                                              nsols.size()),
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
    if (msols.find(m) == msols.end()) {
      Solutions sols = QuadBigInt(BigInt(222121), BigInt(0), BigInt(-1),
                                  BigInt(0), BigInt(0), BigInt(m), nullptr);
      ValidateSolutions(sols);
      msols[m] = std::move(sols);
    }

    if (nsols.find(n) == nsols.end()) {
      Solutions sols = QuadBigInt(BigInt(360721), BigInt(0), BigInt(-1),
                                  BigInt(0), BigInt(0), BigInt(n), nullptr);
      ValidateSolutions(sols);
      nsols[n] = std::move(sols);
    }
  }

  // Eliminate points without any solutions.
  // We could eliminate these by row/column, but I already elminated
  // them so I'm trying to keep it simple during a rewrite.
  for (const auto &[m, n] : todo) {
    const auto &[msol, nsol] = GetSolutions(m, n);

    if (!HasSolutions(*msol) ||
        !HasSolutions(*nsol)) {
      printf("\n\nNo solutions for (%d, %d)!\n\n", m, n);
      work.SetNoSolAt(m, n, Work::NOSOL_ALPERTRON);
      dirty = true;
    }
  }

  if (dirty && ALLOW_SAVE) {
    work.Save();
    dirty = false;
  }

  // Fun fact: Aside from 0 (which we can analyze separately), all of the
  // columns have the same recursive solution (with different point solutions)
  // and so to for rows. This does make some sense because the underlying
  // curves are the same, just shifted by a constant m or n.

  const RecursiveSolution mrec = GetSameRec(msols);
  const RecursiveSolution nrec = GetSameRec(nsols);

  auto PrintSame = [](const char *axis, const RecursiveSolution &rec) {
      printf("For all nonzero " AWHITE("%s") ", "
             "the same recursive solution:\n", axis);
      printf("%s", AnsiRecursive(rec).c_str());
    };

  PrintSame("m", mrec);
  PrintSame("n", nrec);

  #if 0
  auto PrintSols = [](const char *axis, const std::map<int, Solutions> &db) {
      printf("\n\nFor " AWHITE("%s") ":\n", axis);
      for (const auto &[o, sols] : db) {
        printf("For " AWHITE("%s") "=" AWHITE("%d") ":\n", axis, o);
        std::vector<std::pair<BigInt, BigInt>> s = NonNegPoints(sols.points);
        for (const auto &[x, y] : s) {
          printf("  (" ABLUE("%s") AWHITE(",") ABLUE("%s") ")\n",
                 LongNum(x).c_str(), LongNum(y).c_str());
        }
      }
    };

  PrintSols("m", msols);
  PrintSols("n", nsols);
  #endif

  // Call m or n the error.
  // So, now we have a bunch of points that are reachable (perhaps resulting
  // in different errors) for each axis.

  std::unordered_map<BigInt, BigInt> mpts;
  std::unordered_map<BigInt, BigInt> npts;

  auto AddPoints = [](const std::map<int, Solutions> &db,
                      std::unordered_map<BigInt, BigInt> *pts) {
      for (const auto &[o, sols] : db) {
        std::vector<std::pair<BigInt, BigInt>> s = NonNegPoints(sols.points);
        for (const auto &[x, y] : s) {
          // We don't allow x == 0 since it would result in a degenerate
          // square.
          if (x != 0) {
            CHECK(!pts->contains(x));
            (*pts)[x] = y;
          }
        }
      }
    };

  AddPoints(msols, &mpts);
  AddPoints(nsols, &npts);

  CheckSolutions(mpts, npts);

  // Now add some points using the recurrence.
  ExpandNum(&mpts, mrec, 500);
  ExpandNum(&npts, nrec, 500);

  printf("Expanded.\n");

  CheckSolutions(mpts, npts);

  for (const auto &[x, my] : mpts) {
    // Skip some known solutions.
    if (x == 10 && my == 4713) {
      printf("my: %s\n", LongNum(my).c_str());
      continue;
    }

    auto it = npts.find(x);
    if (it != npts.end()) {
      const BigInt &ny = it->second;
      printf(AGREEN("Have both x") " = %s !\n", LongNum(x).c_str());
      printf("  my: " ABLUE("%s") "\n", LongNum(my).c_str());
      printf("  ny: " APURPLE("%s") "\n", LongNum(ny).c_str());
    }
  }

  // TODO: For some prime p, compute all the residues reachable for
  // each solution. We can do this by reducing the recurrence and
  // start points to Montgomery64. Then if we find (m,n) where they
  // have no (x) residues in common, we can eliminate that pair.
  //
  // Is this any different from what we're already doing in mod?
  // We check ALL the possible solutions mod the prime there. So this
  // might be more efficient (and maybe worth doing for that reason)
  // but won't find new eliminations.
  //
  // We could this for large moduli that are related to the problem,
  // like the coefficients of the recurrence. Given that these are 300+
  // digits, we would need for the sets to be extremely sparse, but
  // it is plausible if the recurrences become degenerate?

  return;


  // TODO:
  // Eliminate cells with no shared nonzero x coordinate (need for (0,0)).
  // Eliminate rows/columns with just x=0.

  /*
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
  */

  if (dirty && ALLOW_SAVE) {
    work.Save();
    dirty = false;
  }

  printf("Solution types (%d * 2 = %d eqs):\n"
         "Points: %d\n"
         "Rec:    %d\n",
         (int)todo.size(), (int)todo.size() * 2,
         points, rec);

  printf("Took %s\n", ANSI::Time(sol_timer.Seconds()).c_str());
}


int main(int argc, char **argv) {
  ANSI::Init();

  DoWork();

  return 0;
}
