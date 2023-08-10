

#include <utility>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "base/stringprintf.h"
#include "base/logging.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "arcfour.h"
#include "randutil.h"
#include "factorization.h"
#include "threadutil.h"

static std::string VecString(const std::vector<int> &v) {
  string out;
  for (int i : v) StringAppendF(&out, "%d, ", i);
  return out;
}

// See residue.cc for the source of this.

// a modulus m, and the residues that cannot be formed by x^2 + y^2 mod m.
static std::vector<std::pair<int, std::vector<int>>> todo = {
{121, {11, 22, 33, 44, 55, 66, 77, 88, 99, 110,  }},
{361, {19, 38, 57, 76, 95, 114, 133, 152, 171, 190, 209, 228, 247, 266, 285, 304, 323, 342,  }},
{529, {23, 46, 69, 92, 115, 138, 161, 184, 207, 230, 253, 276, 299, 322, 345, 368, 391, 414, 437, 460, 483, 506,  }},
{961, {31, 62, 93, 124, 155, 186, 217, 248, 279, 310, 341, 372, 403, 434, 465, 496, 527, 558, 589, 620, 651, 682, 713, 744, 775, 806, 837, 868, 899, 930,  }},
{1849, {43, 86, 129, 172, 215, 258, 301, 344, 387, 430, 473, 516, 559, 602, 645, 688, 731, 774, 817, 860, 903, 946, 989, 1032, 1075, 1118, 1161, 1204, 1247, 1290, 1333, 1376, 1419, 1462, 1505, 1548, 1591, 1634, 1677, 1720, 1763, 1806,  }},
{2209, {47, 94, 141, 188, 235, 282, 329, 376, 423, 470, 517, 564, 611, 658, 705, 752, 799, 846, 893, 940, 987, 1034, 1081, 1128, 1175, 1222, 1269, 1316, 1363, 1410, 1457, 1504, 1551, 1598, 1645, 1692, 1739, 1786, 1833, 1880, 1927, 1974, 2021, 2068, 2115, 2162,  }},
{3481, {59, 118, 177, 236, 295, 354, 413, 472, 531, 590, 649, 708, 767, 826, 885, 944, 1003, 1062, 1121, 1180, 1239, 1298, 1357, 1416, 1475, 1534, 1593, 1652, 1711, 1770, 1829, 1888, 1947, 2006, 2065, 2124, 2183, 2242, 2301, 2360, 2419, 2478, 2537, 2596, 2655, 2714, 2773, 2832, 2891, 2950, 3009, 3068, 3127, 3186, 3245, 3304, 3363, 3422,  }},
{4489, {67, 134, 201, 268, 335, 402, 469, 536, 603, 670, 737, 804, 871, 938, 1005, 1072, 1139, 1206, 1273, 1340, 1407, 1474, 1541, 1608, 1675, 1742, 1809, 1876, 1943, 2010, 2077, 2144, 2211, 2278, 2345, 2412, 2479, 2546, 2613, 2680, 2747, 2814, 2881, 2948, 3015, 3082, 3149, 3216, 3283, 3350, 3417, 3484, 3551, 3618, 3685, 3752, 3819, 3886, 3953, 4020, 4087, 4154, 4221, 4288, 4355, 4422,  }},
{5041, {71, 142, 213, 284, 355, 426, 497, 568, 639, 710, 781, 852, 923, 994, 1065, 1136, 1207, 1278, 1349, 1420, 1491, 1562, 1633, 1704, 1775, 1846, 1917, 1988, 2059, 2130, 2201, 2272, 2343, 2414, 2485, 2556, 2627, 2698, 2769, 2840, 2911, 2982, 3053, 3124, 3195, 3266, 3337, 3408, 3479, 3550, 3621, 3692, 3763, 3834, 3905, 3976, 4047, 4118, 4189, 4260, 4331, 4402, 4473, 4544, 4615, 4686, 4757, 4828, 4899, 4970,  }},
{6241, {79, 158, 237, 316, 395, 474, 553, 632, 711, 790, 869, 948, 1027, 1106, 1185, 1264, 1343, 1422, 1501, 1580, 1659, 1738, 1817, 1896, 1975, 2054, 2133, 2212, 2291, 2370, 2449, 2528, 2607, 2686, 2765, 2844, 2923, 3002, 3081, 3160, 3239, 3318, 3397, 3476, 3555, 3634, 3713, 3792, 3871, 3950, 4029, 4108, 4187, 4266, 4345, 4424, 4503, 4582, 4661, 4740, 4819, 4898, 4977, 5056, 5135, 5214, 5293, 5372, 5451, 5530, 5609, 5688, 5767, 5846, 5925, 6004, 6083, 6162,  }},
{6889, {83, 166, 249, 332, 415, 498, 581, 664, 747, 830, 913, 996, 1079, 1162, 1245, 1328, 1411, 1494, 1577, 1660, 1743, 1826, 1909, 1992, 2075, 2158, 2241, 2324, 2407, 2490, 2573, 2656, 2739, 2822, 2905, 2988, 3071, 3154, 3237, 3320, 3403, 3486, 3569, 3652, 3735, 3818, 3901, 3984, 4067, 4150, 4233, 4316, 4399, 4482, 4565, 4648, 4731, 4814, 4897, 4980, 5063, 5146, 5229, 5312, 5395, 5478, 5561, 5644, 5727, 5810, 5893, 5976, 6059, 6142, 6225, 6308, 6391, 6474, 6557, 6640, 6723, 6806,  }},
{10609, {103, 206, 309, 412, 515, 618, 721, 824, 927, 1030, 1133, 1236, 1339, 1442, 1545, 1648, 1751, 1854, 1957, 2060, 2163, 2266, 2369, 2472, 2575, 2678, 2781, 2884, 2987, 3090, 3193, 3296, 3399, 3502, 3605, 3708, 3811, 3914, 4017, 4120, 4223, 4326, 4429, 4532, 4635, 4738, 4841, 4944, 5047, 5150, 5253, 5356, 5459, 5562, 5665, 5768, 5871, 5974, 6077, 6180, 6283, 6386, 6489, 6592, 6695, 6798, 6901, 7004, 7107, 7210, 7313, 7416, 7519, 7622, 7725, 7828, 7931, 8034, 8137, 8240, 8343, 8446, 8549, 8652, 8755, 8858, 8961, 9064, 9167, 9270, 9373, 9476, 9579, 9682, 9785, 9888, 9991, 10094, 10197, 10300, 10403, 10506,  }},
{11449, {107, 214, 321, 428, 535, 642, 749, 856, 963, 1070, 1177, 1284, 1391, 1498, 1605, 1712, 1819, 1926, 2033, 2140, 2247, 2354, 2461, 2568, 2675, 2782, 2889, 2996, 3103, 3210, 3317, 3424, 3531, 3638, 3745, 3852, 3959, 4066, 4173, 4280, 4387, 4494, 4601, 4708, 4815, 4922, 5029, 5136, 5243, 5350, 5457, 5564, 5671, 5778, 5885, 5992, 6099, 6206, 6313, 6420, 6527, 6634, 6741, 6848, 6955, 7062, 7169, 7276, 7383, 7490, 7597, 7704, 7811, 7918, 8025, 8132, 8239, 8346, 8453, 8560, 8667, 8774, 8881, 8988, 9095, 9202, 9309, 9416, 9523, 9630, 9737, 9844, 9951, 10058, 10165, 10272, 10379, 10486, 10593, 10700, 10807, 10914, 11021, 11128, 11235, 11342,  }},
{16129, {127, 254, 381, 508, 635, 762, 889, 1016, 1143, 1270, 1397, 1524, 1651, 1778, 1905, 2032, 2159, 2286, 2413, 2540, 2667, 2794, 2921, 3048, 3175, 3302, 3429, 3556, 3683, 3810, 3937, 4064, 4191, 4318, 4445, 4572, 4699, 4826, 4953, 5080, 5207, 5334, 5461, 5588, 5715, 5842, 5969, 6096, 6223, 6350, 6477, 6604, 6731, 6858, 6985, 7112, 7239, 7366, 7493, 7620, 7747, 7874, 8001, 8128, 8255, 8382, 8509, 8636, 8763, 8890, 9017, 9144, 9271, 9398, 9525, 9652, 9779, 9906, 10033, 10160, 10287, 10414, 10541, 10668, 10795, 10922, 11049, 11176, 11303, 11430, 11557, 11684, 11811, 11938, 12065, 12192, 12319, 12446, 12573, 12700, 12827, 12954, 13081, 13208, 13335, 13462, 13589, 13716, 13843, 13970, 14097, 14224, 14351, 14478, 14605, 14732, 14859, 14986, 15113, 15240, 15367, 15494, 15621, 15748, 15875, 16002,  }},
  };

// Each row is a modulus and a set of residues that we can use to reject the
// input (e.g. if sum mod m is in the set, then we know sum can be skipped).
// Rather than represent the sum literally, we'd like to find a smaller numerical
// representation. So we look for (a, p, lim) where for every n < m, if
//    a * n mod p > lim
// then n is in the set. This is trivially satisifiable for lim = p - 1 (but is
// the empty set). We're trying to find the best one, which is the one that
// captures the most elements of the set.
template<class C>
static void Compress(const std::pair<int, C> &row) {
  std::unordered_set<int64_t> want;
  for (int r : row.second) want.insert(r);
  const int64_t m = row.first;

  std::mutex mu;
  int64_t best = 0;
  int64_t best_p = 2;
  int64_t best_a = 1;
  int64_t best_lim = 1;

  string status = "none yet";

  fprintf(stderr, "\nWorking on " ACYAN("%lld") "...\n\n", m);
  Timer run_timer;

  const int64_t MAX_DIST = m;
  Periodically bar_per(1.0);
  ParallelComp(
      MAX_DIST * 2,
      [m, &want, &run_timer, MAX_DIST, &bar_per, &mu,
       &best, &best_p, &best_a, &best_lim, &status](int64_t off) {
        if ((off >> 1) == 0) return;

        int64_t local_best = 0;
        {
          MutexLock ml(&mu);
          if (best == want.size()) return;
          local_best = best;
        }

        // zig-zag out from m
        const int64_t p = (off & 1) ? m - (off >> 1) : m + (off >> 1);
        if (p < 2) return;
        if (Factorization::IsPrime(p)) {
          for (int64_t a = 1; a < p; a++) {
            // Compute r = a*n % p for all n.

            // the smallest r for n that is in the want set.
            int64_t min_r_in = m + 1;
            // the largest r for n that is not in the want set.
            int64_t max_r_not = -1;

            int64_t r = 0;
            for (int64_t n = 0; n < m; n++) {
              // int r = (a * n) % p;
              /*
              CHECK(r == ((a * n) % p)) <<
                StringPrintf("%d != (%d * %lld) %% %d == %lld",
                             r, a, n, p, ((a * n) % p));
              */
              if (want.contains(n)) {
                min_r_in = std::min(min_r_in, r);
              } else {
                max_r_not = std::max(max_r_not, r);
              }
              r += a;
              if (r >= p) r -= p;
            }

            // No good.
            if (max_r_not >= p - 1) continue;

            // Anything greater than the limit is in the set, then.
            const int64_t lim = max_r_not;
            int64_t got = 0;
            for (int64_t n : want) {
              if ((a * n) % p > lim) {
                got++;
              }
            }

            // Accept!
            if (got > local_best) {
              MutexLock ml(&mu);
              if (got > best) {
                best = got;
                local_best = best;
                best_p = p;
                best_a = a;
                best_lim = lim;
                if (got == want.size()) { status = "done"; return; }
                status = StringPrintf("%lld/%lld %lld*n %% %lld > %lld",
                                      got, (int64_t)want.size(), a, p, lim);
              }
            }
          }
        }

        if (bar_per.ShouldRun()) {
          fprintf(stderr,
                  ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
                  ANSI_BEGINNING_OF_LINE "%s\n",
                  ANSI::ProgressBar(
                      off,
                      MAX_DIST * 2,
                      StringPrintf("%lld | %s", p, status.c_str()),
                      run_timer.Seconds()).c_str());
        }
      },
      12);

  fprintf(stderr,
          "Done for m=%lld. Best gets %lld/%lld: (%lld * n) mod %lld > %lld\n",
          m, best, (int64_t)want.size(), best_a, best_p, best_lim);

  // Self-test
  for (int64_t sum = 0; sum < m; sum++) {
    if ((best_a * sum) % best_p > best_lim) {
      CHECK(want.contains(sum)) << sum;
    }
  }
  fprintf(stderr, AGREEN("OK") "!\n");

  printf("// %lld/%lld\n", best, (int64_t)want.size());
  printf("if ((%lld * (sum %% %lld)) %% %lld > %lld) return false;\n",
         best_a, m, best_p, best_lim);
  fflush(stdout);
}

// XXX to residue-util?

static std::pair<int, std::vector<int>> GetResidues(uint64_t p) {
  std::unordered_set<int> residues;
  for (uint64_t i = 0; i < p; i++) {
    uint64_t r = (i * i) % p;
    residues.insert(r);
  }
  std::vector<int> rs;
  rs.reserve(residues.size());
  for (int r : residues) rs.push_back(r);
  std::sort(rs.begin(), rs.end());
  return make_pair(p, rs);
}

// XXX to residue-util?

static std::pair<int, std::set<int>>
GetNonSumResidues(
    const std::pair<int, std::vector<int>> &qres) {
  const auto &[m, residues] = qres;
  fprintf(stderr, "%d (%d res)... ", m, (int)residues.size());

  std::unordered_set<int> sums;
  for (int i = 0; i < residues.size(); i++) {
    for (int j = i; j < residues.size(); j++) {
      // This is s % m, but since each residue is <m, we don't
      // actually need to do division.
      int s = residues[i] + residues[j];
      if (s > m) s -= m;

      sums.insert(s);
    }
    // Stop early if we have already found every remainder.
    if (sums.size() == m) break;
  }

  // If all residues are possible, skip.
  if (sums.size() < m) {
    // We could store the set or its negation.
    fprintf(stderr, " " AGREEN("%d") " or " AYELLOW("%d") " sum residues\n",
           (int)sums.size(),
           m - (int)sums.size());
    std::set<int> non_res;
    // non_res.reserve(m - sums.size());
    for (int i = 0; i < m; i++) {
      if (sums.find(i) == sums.end()) {
        non_res.insert(i);
      }
    }
    return make_pair(m, std::move(non_res));
  } else {
    fprintf(stderr, ARED("full") ".\n");
    CHECK(false);
  }
}


int main(int argc, char **argv) {

  /*
  for (const auto &row : todo) {
    Compress(row);
  }
  */

  // 5764801 ran for eight hours just in
  // GetNonSumResidues! Would need to make
  // this faster.

  for (int m : std::initializer_list<int>{
      // put the numbers here!
    }) {
    Compress(GetNonSumResidues(GetResidues(m)));
  }

  // Only poor solutions known for these:

  // 2/182
  // if ((2 * (sum % 729)) % 487 > 484) return false;
  // 257/511
  // if ((258 * (sum % 1024)) % 1031 > 770) return false;
  // 6/14706  (17 hours!)
  // if ((6 * (sum % 117649)) % 117643 > 117636) return false;
  // 10/1220
  // if ((12 * (sum % 14641)) % 14653 > 14642) return false;
  // 2/14762
  // if ((2 * (sum % 59049)) % 59029 > 59026) return false;


  return 0;
}
