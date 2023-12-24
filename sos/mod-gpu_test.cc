
#include "mod-gpu.h"

#include <optional>
#include <tuple>

#include "clutil.h"

#include "base/logging.h"
#include "ansi.h"
#include "periodically.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "factorization.h"
#include "timer.h"

#include "mod-util.h"

using namespace std;

static CL *cl = nullptr;

using FullRun = ModQuickPassGPU::FullRun;

static std::tuple<int64_t, int64_t, int64_t> AssertSimpleSolve(
    int64_t m, int64_t n, int64_t p) {
  const auto o = SimpleSolve(m, n, p);
  CHECK(o.has_value()) << "No solution for (" << m << "," << n << ") mod "
                       << p;
  return o.value();
}

static void ShowSolutions() {
  printf("Solve...\n");
  const auto [a, b, c] = AssertSimpleSolve(-4, -209, 829811);
  printf("A solution: %lld, %lld, %lld\n", a, b, c);
}

static void TestKnown() {
  std::vector<FullRun> nosol = {
    FullRun(-4, -209, 829811),
    FullRun(-52, 165, 25456631),
  };

  // 222121 a^2 - b^2 + m = 0
  // 360721 a^2 - c^2 + n = 0

  // This does have a solution! (778196, 68053, 189023)
  // All mod 829811:
  // 222121 778196^2 - b^2 -4 = 0
  // 360721 778196^2 - c^2 -209 = 0

  // 35622 - 68053^2 - 4 = 0
  // 522511 - 189023^2 - 209 = 0

  ModQuickPassGPU quick_pass(cl, 1, 1);

  for (const FullRun &f : nosol) {
    CHECK(Factorization::IsPrime(f.prime));
    SolutionFinder<false> finder(f.prime);
    CHECK(!finder.HasSolutionModP(f.m, f.n)) << f.prime;

    std::vector<FullRun> out =
      quick_pass.Run({f.prime}, {{f.m, f.n}});
    CHECK(out.size() == 1);
    CHECK(out[0].prime == f.prime);
    CHECK(out[0].m == f.m);
    CHECK(out[0].n == f.n);
  }

  printf("Known no-solution mod p " AGREEN("OK") "\n");
}

static void TestModQuick() {
  // Find some examples of each type.
  static constexpr int NUM_EACH = 20;
  std::vector<std::tuple<uint64_t, int, int>> has_solution;
  std::vector<std::tuple<uint64_t, int, int>> no_solution;

  // Has to be larger than any m,n and the coefficients.
  static constexpr uint64_t MIN_PRIME  = 400001;
  static constexpr uint64_t PRIME_MASK = 0x00000FFF'FFFFFFFF;

  static constexpr int MAX_ZEROES = 3;

  ArcFour rc("modquick");
  Timer examples_timer;
  int64_t yes = 0, no = 0;
  int with_zero = 0;

  while (has_solution.size() < NUM_EACH ||
         no_solution.size() < NUM_EACH) {

    uint64_t p = (MIN_PRIME + (Rand64(&rc) & PRIME_MASK)) | 1;
    p = Factorization::NextPrime(p);
    SolutionFinder<false> finder(p);

    int m = RandTo(&rc, 667) - 333;
    int n = RandTo(&rc, 667) - 333;

    if (finder.QuickHasSolutionModP<ModQuickPassGPU::QUICK_PASS_SIZE>(
            m, n)) {
      yes++;
      if (has_solution.size() < NUM_EACH) {
        has_solution.emplace_back(p, m, n);
      }
    } else {
      no++;

      if (m == 0 || n == 0) {
        if (with_zero >= MAX_ZEROES) {
          continue;
        }
        with_zero++;
      }

      if (no_solution.size() < NUM_EACH) {
        printf("%d,%d mod %llu\n", m, n, p);
        no_solution.emplace_back(p, m, n);
      }
    }
  }

  printf("Got %d examples of each type in %s. (%lld yes, %lld no)\n",
         NUM_EACH, ANSI::Time(examples_timer.Seconds()).c_str(),
         yes, no);

  printf("Has solution:\n");
  for (const auto &[prime, m, n] : has_solution) {
    printf("(" ABLUE("%d") "," APURPLE("%d") ") mod " AGREEN("%llu") "\n",
           m, n, prime);
  }

  printf("No solution:\n");
  for (const auto &[prime, m, n] : no_solution) {
    printf("(" ABLUE("%d") "," APURPLE("%d") ") mod " AYELLOW("%llu") "\n",
           m, n, prime);
  }

  // Now prepare the batch. We don't try to dedupe primes or m,n and it
  // isn't required anyway.

  std::vector<uint64_t> primes;
  std::vector<std::pair<int, int>> mns;
  for (const auto &[prime, m, n] : has_solution) {
    primes.push_back(prime);
    mns.emplace_back(m, n);
  }

  for (const auto &[prime, m, n] : no_solution) {
    primes.push_back(prime);
    mns.emplace_back(m, n);
  }

  // And just so we have a different width and height, add
  // one more (m, n) pair.
  mns.emplace_back(0, 0);

  ModQuickPassGPU quick_pass(cl, primes.size(), mns.size());

  Timer gpu_timer;
  std::vector<FullRun> need_full = quick_pass.Run(primes, mns);
  printf("Ran GPU in %s.\n", ANSI::Time(gpu_timer.Seconds()).c_str());

  // Check that everything expected is in there.
  auto Has = [&need_full](uint64_t p, int m, int n) {
      for (const FullRun &f : need_full) {
        if (f.prime == p && f.m == m && f.n == n) return true;
      }
      return false;
    };

  for (const auto &[prime, m, n] : no_solution) {
    CHECK(Has(prime, m, n)) << "Missing " << prime;
  }

  // And check that everything in there agrees with the CPU version.
  // We may find more, since we check every (prime, (m,n)) pair.
  for (const FullRun &f : need_full) {
    SolutionFinder<false> finder(f.prime);

    CHECK(!finder.QuickHasSolutionModP<ModQuickPassGPU::QUICK_PASS_SIZE>(
              f.m, f.n)) << "Disagrees: " << f.prime
                         << " " << f.m << "," << f.n;
  }
}

int main(int argc, char **argv) {
  ANSI::Init();
  cl = new CL;

  ShowSolutions();
  TestKnown();

  TestModQuick();

  printf("OK\n");
  return 0;
}
