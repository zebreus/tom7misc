
#include "sos-util.h"
#include "sos-gpu.h"
#include "clutil.h"

#include "base/logging.h"
#include "ansi.h"
#include "periodically.h"
#include "opt/opt.h"
#include "opt/optimizer.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "atomic-util.h"
#include "factorization.h"

static CL *cl = nullptr;

using namespace std;

#define AORANGE(s) ANSI_FG(247, 155, 57) s ANSI_RESET

// Bitmask of numbers we're around where we're currently searching.
// Used for benchmarking / tuning.
static constexpr uint64_t MASK_CURRENT_RANGE = 0xFFFFFFFFFFFULL;

DECLARE_COUNTERS(tests, u1_, u2_, u3_, u4_, u5_, u6, u7_);

static constexpr int NUM_PASSES = 12;

static double best_sec_per = 999999.0;
static ArcFour *rc = nullptr;

// height, IsPrimeRoutine, Sub128, Geq128, Mul128, FusedTry, NextPrime
// returns failure rate
using FactorizeOpt = Optimizer<7, 0, double>;

static constexpr array PRIMES = {
  2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541,

  547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929, 937, 941, 947, 953, 967, 971, 977, 983, 991, 997, 1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049, 1051, 1061, 1063,

  1069, 1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163, 1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249, 1259, 1277, 1279, 1283, 1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321, 1327, 1361, 1367, 1373, 1381, 1399, 1409, 1423, 1427, 1429, 1433, 1439, 1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487, 1489, 1493, 1499, 1511, 1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571, 1579, 1583, 1597, 1601, 1607, 1609, 1613, 1619, 1621, 1627, 1637, 1657, 1663, 1667, 1669, 1693, 1697, 1699, 1709, 1721, 1723, 1733, 1741, 1747, 1753, 1759, 1777, 1783, 1787, 1789, 1801, 1811, 1823, 1831, 1847, 1861, 1867, 1871, 1873, 1877, 1879, 1889, 1901, 1907, 1913, 1931, 1933, 1949, 1951, 1973, 1979, 1987, 1993, 1997, 1999, 2003, 2011, 2017, 2027, 2029, 2039, 2053, 2063, 2069, 2081, 2083, 2087, 2089, 2099, 2111, 2113, 2129, 2131, 2137, 2141, 2143, 2153, 2161, 2179, 2203, 2207, 2213, 2221, 2237, 2239, 2243, 2251, 2267, 2269, 2273, 2281, 2287, 2293, 2297, 2309, 2311, 2333, 2339, 2341, 2347, 2351, 2357, 2371, 2377, 2381, 2383, 2389, 2393, 2399, 2411,
};

static_assert(PRIMES.size() > 32);
static_assert(PRIMES[32] == 137);

static_assert(PRIMES[178] == 1063);

static double total_compile_time = 0.0;

static std::string ArgString(const FactorizeOpt::arg_type &arg) {
  const auto &[height, is_prime_routine_idx, sub128, geq128, mul128,
               fused_try, next_prime_idx] = arg.first;
  const int next_prime = PRIMES[next_prime_idx];
  const char *routine = [&]() {
      switch (is_prime_routine_idx) {
      default:
      case 0: return "OLD";
      case 1: return "UNR";
      case 2: return "GEN";
      case 3: return "FEW";
      }
    }();

  return StringPrintf(AWHITE("%d") AGREY(".")
                      APURPLE("%s") AGREY(".")
                      "%s" AGREY(".")
                      "%s" AGREY(".")
                      "%s" AGREY(".")
                      "%s" AGREY(".")
                      ABLUE("%d"),
                      height, routine,
                      sub128 ? ACYAN("S") : "_",
                      geq128 ? AYELLOW("G") : "_",
                      mul128 ? AORANGE("M") : "_",
                      fused_try ? AGREEN("F") : "_",
                      next_prime);
}

FactorizeOpt::return_type OptimizeMe(const FactorizeOpt::arg_type &arg) {
  const auto &[height, is_prime_routine_idx, sub128, geq128, mul128,
               fused_try, next_prime_idx] = arg.first;

  CHECK(next_prime_idx > 0 && next_prime_idx < PRIMES.size()) << next_prime_idx;
  const int next_prime = PRIMES[next_prime_idx];

  const FactorizeGPU::IsPrimeRoutine is_prime_routine = [&]() {
      switch (is_prime_routine_idx) {
      default:
      case 0: return FactorizeGPU::IsPrimeRoutine::OLD;
      case 1: return FactorizeGPU::IsPrimeRoutine::UNROLLED;
      case 2: return FactorizeGPU::IsPrimeRoutine::GENERAL;
      case 3: return FactorizeGPU::IsPrimeRoutine::FEW;
      }
    }();
  Timer compile_time;
  FactorizeGPU factorize_gpu(cl, height,
                             is_prime_routine,
                             !!sub128,
                             !!geq128,
                             !!mul128,
                             !!fused_try,
                             next_prime);
  total_compile_time += compile_time.Seconds();

  CHECK(rc != nullptr);

  // Do several passes. Take the average of the best half.
  std::vector<double> results;
  int64_t failures = 0;
  for (int p = 0; p < NUM_PASSES; p++) {

    // Note that we can just get unlucky here if we choose
    // hard-to-factor numbers. But since we are optimizing the
    // height, using a fixed batch seems to have worse failure
    // modes.
    std::vector<uint64_t> nums;
    nums.reserve(height);
    for (int i = 0; i < height; i++) {
      nums.push_back(Rand64(rc) & MASK_CURRENT_RANGE);
    }

    Timer run_timer;
    // XXX discount to success rate
    const auto &[res_factors, res_num_factors] =
      factorize_gpu.Factorize(nums);
    results.push_back(run_timer.Seconds());
    for (uint8_t nf : res_num_factors) {
      if (nf & 0x80) failures++;
    }
  }

  std::sort(results.begin(), results.end());

  double sec_per = 0.0;
  // We skip the slowest half of the passes. It seems to take some
  // time for kernels to warm up.
  int NUM_TO_COUNT = std::max(1, NUM_PASSES / 2);
  CHECK(NUM_TO_COUNT > 0 && NUM_TO_COUNT <= results.size());
  for (int i = 0; i < NUM_TO_COUNT; i++) {
    sec_per += results[i];
  }

  sec_per /= (NUM_TO_COUNT * height);

  printf("%s: %s\n", ArgString(arg).c_str(),
         ANSI::Time(sec_per).c_str());
  if (sec_per < best_sec_per) {
    best_sec_per = sec_per;
    printf(AGREEN("New best") ": %s (%s/ea.)\n",
           ArgString(arg).c_str(),
           ANSI::Time(best_sec_per).c_str());
  }
  double failure_rate = failures / (double)(NUM_PASSES * height);
  return make_pair(sec_per, make_optional(failure_rate));
}

static void Optimize() {
  rc = new ArcFour(StringPrintf("gpu.%lld", time(nullptr)));

  // reset best
  best_sec_per = 999999.0;

  FactorizeOpt opt(OptimizeMe, Rand64(rc));
  opt.SetSaveAll(true);

  // height, IsPrimeRoutine, Sub128, Geq128, FusedTry, NextPrime
  const FactorizeOpt::arg_type recommended =
    std::make_pair(std::array<int32_t, FactorizeOpt::num_ints>{
        2875870, 2, 0, 0, 0, 1, 178
          },
      std::array<double, FactorizeOpt::num_doubles>{});
  opt.Sample(recommended);

  const std::array<std::pair<int32_t, int32_t>, FactorizeOpt::num_ints>
    int_bounds = {
    // don't bother with unreasonably small heights
    make_pair(128, 524288 * 8),
    // [0, 4): 0, 1, 2, 3
    make_pair(0, 4),
    // booleans [0, 2).
    make_pair(0, 2),
    make_pair(0, 2),
    make_pair(0, 2),
    make_pair(0, 2),
    // can't exclude 2
    make_pair(1, PRIMES.size() - 1),
  };

  opt.Run(int_bounds, {}, nullopt, nullopt, {3600});

  string report;
  for (const auto &[arg, sec_per, failure] : opt.GetAll()) {
    StringAppendF(&report,
                  "%s,%.6f,%.6f\n",
                  ANSI::StripCodes(ArgString(arg)).c_str(),
                  sec_per * 1000000.0,
                  failure.value_or(999.0) * 100.0);
  }
  Util::WriteFile("tune-report.txt", report);
  printf("Wrote results to tune-report.txt\n");

  auto besto = opt.GetBest();
  CHECK(besto.has_value());
  const auto &[arg, sec_per, failure] = besto.value();

  printf("Total time compiling: %s\n",
         ANSI::Time(total_compile_time).c_str());

  printf("\nOptimization finished. Best was %s"
         " which took %s/ea (with %.4fs%% error rate).\n",
         ArgString(arg).c_str(),
         ANSI::Time(sec_per).c_str(),
         failure * 100.0);

  printf("\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  cl = new CL;

  Optimize();

  printf("OK\n");
  return 0;
}
