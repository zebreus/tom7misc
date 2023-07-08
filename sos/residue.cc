
#include <utility>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "base/stringprintf.h"
#include "base/logging.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "arcfour.h"
#include "randutil.h"
#include "factorize.h"

static std::string VecString(const std::vector<int> &v) {
  string out;
  for (int i : v) StringAppendF(&out, "%d, ", i);
  return out;
}

inline constexpr bool OldMaybeSumOfSquares(uint64_t sum) {
  return !(((0x0000000001209048LLU >> (sum % 27)) & 1) |
           ((0x0000040810204080LLU >> (sum % 49)) & 1) |
           ((0xd9c9d8c8d9c8d8c8LLU >> (sum % 64)) & 1));
}

// List from wikipedia. First entry is the modulus m, and second are the
// possible residues for any x^2 mod m.
static std::vector<std::pair<int, std::vector<int>>> QUADRATIC_RESIDUES = {
{1, {0 }},
{2, {0, 1 }},
{3, {0, 1 }},
{4, {0, 1 }},
{5, {0, 1, 4 }},
{6, {0, 1, 3, 4 }},
{7, {0, 1, 2, 4 }},
{8, {0, 1, 4 }},
{9, {0, 1, 4, 7 }},
{10, {0, 1, 4, 5, 6, 9 }},
{11, {0, 1, 3, 4, 5, 9 }},
{12, {0, 1, 4, 9 }},
{13, {0, 1, 3, 4, 9, 10, 12 }},
{14, {0, 1, 2, 4, 7, 8, 9, 11 }},
{15, {0, 1, 4, 6, 9, 10 }},
{16, {0, 1, 4, 9 }},
{17, {0, 1, 2, 4, 8, 9, 13, 15, 16 }},
{18, {0, 1, 4, 7, 9, 10, 13, 16 }},
{19, {0, 1, 4, 5, 6, 7, 9, 11, 16, 17 }},
{20, {0, 1, 4, 5, 9, 16 }},
{21, {0, 1, 4, 7, 9, 15, 16, 18 }},
{22, {0, 1, 3, 4, 5, 9, 11, 12, 14, 15, 16, 20 }},
{23, {0, 1, 2, 3, 4, 6, 8, 9, 12, 13, 16, 18 }},
{24, {0, 1, 4, 9, 12, 16 }},
{25, {0, 1, 4, 6, 9, 11, 14, 16, 19, 21, 24 }},
{26, {0, 1, 3, 4, 9, 10, 12, 13, 14, 16, 17, 22, 23, 25 }},
{27, {0, 1, 4, 7, 9, 10, 13, 16, 19, 22, 25 }},
{28, {0, 1, 4, 8, 9, 16, 21, 25 }},
{29, {0, 1, 4, 5, 6, 7, 9, 13, 16, 20, 22, 23, 24, 25, 28 }},
{30, {0, 1, 4, 6, 9, 10, 15, 16, 19, 21, 24, 25 }},
{31, {0, 1, 2, 4, 5, 7, 8, 9, 10, 14, 16, 18, 19, 20, 25, 28 }},
{32, {0, 1, 4, 9, 16, 17, 25 }},
{33, {0, 1, 3, 4, 9, 12, 15, 16, 22, 25, 27, 31 }},
{34, {0, 1, 2, 4, 8, 9, 13, 15, 16, 17, 18, 19, 21, 25, 26, 30, 32, 33 }},
{35, {0, 1, 4, 9, 11, 14, 15, 16, 21, 25, 29, 30 }},
{36, {0, 1, 4, 9, 13, 16, 25, 28 }},
{37, {0, 1, 3, 4, 7, 9, 10, 11, 12, 16, 21, 25, 26, 27, 28, 30, 33, 34, 36 }},
{38, {0, 1, 4, 5, 6, 7, 9, 11, 16, 17, 19, 20, 23, 24, 25, 26, 28, 30, 35, 36 }},
{39, {0, 1, 3, 4, 9, 10, 12, 13, 16, 22, 25, 27, 30, 36 }},
{40, {0, 1, 4, 9, 16, 20, 24, 25, 36 }},
{41, {0, 1, 2, 4, 5, 8, 9, 10, 16, 18, 20, 21, 23, 25, 31, 32, 33, 36, 37, 39, 40 }},
{42, {0, 1, 4, 7, 9, 15, 16, 18, 21, 22, 25, 28, 30, 36, 37, 39 }},
{43, {0, 1, 4, 6, 9, 10, 11, 13, 14, 15, 16, 17, 21, 23, 24, 25, 31, 35, 36, 38, 40, 41 }},
{44, {0, 1, 4, 5, 9, 12, 16, 20, 25, 33, 36, 37 }},
{45, {0, 1, 4, 9, 10, 16, 19, 25, 31, 34, 36, 40 }},
{46, {0, 1, 2, 3, 4, 6, 8, 9, 12, 13, 16, 18, 23, 24, 25, 26, 27, 29, 31, 32, 35, 36, 39, 41 }},
{47, {0, 1, 2, 3, 4, 6, 7, 8, 9, 12, 14, 16, 17, 18, 21, 24, 25, 27, 28, 32, 34, 36, 37, 42 }},
{48, {0, 1, 4, 9, 16, 25, 33, 36 }},
{49, {0, 1, 2, 4, 8, 9, 11, 15, 16, 18, 22, 23, 25, 29, 30, 32, 36, 37, 39, 43, 44, 46 }},
{50, {0, 1, 4, 6, 9, 11, 14, 16, 19, 21, 24, 25, 26, 29, 31, 34, 36, 39, 41, 44, 46, 49 }},
{51, {0, 1, 4, 9, 13, 15, 16, 18, 19, 21, 25, 30, 33, 34, 36, 42, 43, 49 }},
{52, {0, 1, 4, 9, 12, 13, 16, 17, 25, 29, 36, 40, 48, 49 }},
{53, {0, 1, 4, 6, 7, 9, 10, 11, 13, 15, 16, 17, 24, 25, 28, 29, 36, 37, 38, 40, 42, 43, 44, 46, 47, 49, 52 }},
{54, {0, 1, 4, 7, 9, 10, 13, 16, 19, 22, 25, 27, 28, 31, 34, 36, 37, 40, 43, 46, 49, 52 }},
{55, {0, 1, 4, 5, 9, 11, 14, 15, 16, 20, 25, 26, 31, 34, 36, 44, 45, 49 }},
{56, {0, 1, 4, 8, 9, 16, 25, 28, 32, 36, 44, 49 }},
{57, {0, 1, 4, 6, 7, 9, 16, 19, 24, 25, 28, 30, 36, 39, 42, 43, 45, 49, 54, 55 }},
{58, {0, 1, 4, 5, 6, 7, 9, 13, 16, 20, 22, 23, 24, 25, 28, 29, 30, 33, 34, 35, 36, 38, 42, 45, 49, 51, 52, 53, 54, 57 }},
{59, {0, 1, 3, 4, 5, 7, 9, 12, 15, 16, 17, 19, 20, 21, 22, 25, 26, 27, 28, 29, 35, 36, 41, 45, 46, 48, 49, 51, 53, 57 }},
{60, {0, 1, 4, 9, 16, 21, 24, 25, 36, 40, 45, 49 }},
{61, {0, 1, 3, 4, 5, 9, 12, 13, 14, 15, 16, 19, 20, 22, 25, 27, 34, 36, 39, 41, 42, 45, 46, 47, 48, 49, 52, 56, 57, 58, 60 }},
{62, {0, 1, 2, 4, 5, 7, 8, 9, 10, 14, 16, 18, 19, 20, 25, 28, 31, 32, 33, 35, 36, 38, 39, 40, 41, 45, 47, 49, 50, 51, 56, 59 }},
{63, {0, 1, 4, 7, 9, 16, 18, 22, 25, 28, 36, 37, 43, 46, 49, 58 }},
{64, {0, 1, 4, 9, 16, 17, 25, 33, 36, 41, 49, 57 }},
{65, {0, 1, 4, 9, 10, 14, 16, 25, 26, 29, 30, 35, 36, 39, 40, 49, 51, 55, 56, 61, 64 }},
{66, {0, 1, 3, 4, 9, 12, 15, 16, 22, 25, 27, 31, 33, 34, 36, 37, 42, 45, 48, 49, 55, 58, 60, 64 }},
{67, {0, 1, 4, 6, 9, 10, 14, 15, 16, 17, 19, 21, 22, 23, 24, 25, 26, 29, 33, 35, 36, 37, 39, 40, 47, 49, 54, 55, 56, 59, 60, 62, 64, 65 }},
{68, {0, 1, 4, 8, 9, 13, 16, 17, 21, 25, 32, 33, 36, 49, 52, 53, 60, 64 }},
{69, {0, 1, 3, 4, 6, 9, 12, 13, 16, 18, 24, 25, 27, 31, 36, 39, 46, 48, 49, 52, 54, 55, 58, 64 }},
{70, {0, 1, 4, 9, 11, 14, 15, 16, 21, 25, 29, 30, 35, 36, 39, 44, 46, 49, 50, 51, 56, 60, 64, 65 }},
{71, {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, 18, 19, 20, 24, 25, 27, 29, 30, 32, 36, 37, 38, 40, 43, 45, 48, 49, 50, 54, 57, 58, 60, 64 }},
{72, {0, 1, 4, 9, 16, 25, 28, 36, 40, 49, 52, 64 }},
{73, {0, 1, 2, 3, 4, 6, 8, 9, 12, 16, 18, 19, 23, 24, 25, 27, 32, 35, 36, 37, 38, 41, 46, 48, 49, 50, 54, 55, 57, 61, 64, 65, 67, 69, 70, 71, 72 }},
{74, {0, 1, 3, 4, 7, 9, 10, 11, 12, 16, 21, 25, 26, 27, 28, 30, 33, 34, 36, 37, 38, 40, 41, 44, 46, 47, 48, 49, 53, 58, 62, 63, 64, 65, 67, 70, 71, 73 }},
{75, {0, 1, 4, 6, 9, 16, 19, 21, 24, 25, 31, 34, 36, 39, 46, 49, 51, 54, 61, 64, 66, 69 }},

};


// These residues are impossible to form with a^2 + b^2 mod m.
// The three that are commented out will filter everything (at least up to 100 million) that the entire
// set will filter, so that's what we use.
static std::vector<std::pair<int, std::set<int>>> NON_QUADRATIC_SUM_RESIDUES = {
  // {4, {3, }}, // 25.0%
  // {8, {3, 6, 7, }}, // 37.5%
  // {9, {3, 6, }}, // 22.2%
// {12, {3, 7, 11, }}, // 25.0%
// {16, {3, 6, 7, 11, 12, 14, 15, }}, // 43.8%
// {18, {3, 6, 12, 15, }}, // 22.2%
// {20, {3, 7, 11, 15, 19, }}, // 25.0%
// {24, {3, 6, 7, 11, 14, 15, 19, 22, 23, }}, // 37.5%
{27, {3, 6, 12, 15, 21, 24, }}, // 22.2%
// {28, {3, 7, 11, 15, 19, 23, 27, }}, // 25.0%
// {32, {3, 6, 7, 11, 12, 14, 15, 19, 22, 23, 24, 27, 28, 30, 31, }}, // 46.9%
// {36, {3, 6, 7, 11, 12, 15, 19, 21, 23, 24, 27, 30, 31, 33, 35, }}, // 41.7%
// {40, {3, 6, 7, 11, 14, 15, 19, 22, 23, 27, 30, 31, 35, 38, 39, }}, // 37.5%
// {44, {3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, }}, // 25.0%
// {45, {3, 6, 12, 15, 21, 24, 30, 33, 39, 42, }}, // 22.2%
// {48, {3, 6, 7, 11, 12, 14, 15, 19, 22, 23, 27, 28, 30, 31, 35, 38, 39, 43, 44, 46, 47, }}, // 43.8%
{49, {7, 14, 21, 28, 35, 42, }}, // 12.2%
// {52, {3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, }}, // 25.0%
// {54, {3, 6, 12, 15, 21, 24, 30, 33, 39, 42, 48, 51, }}, // 22.2%
// {56, {3, 6, 7, 11, 14, 15, 19, 22, 23, 27, 30, 31, 35, 38, 39, 43, 46, 47, 51, 54, 55, }}, // 37.5%
// {60, {3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, }}, // 25.0%
// {63, {3, 6, 12, 15, 21, 24, 30, 33, 39, 42, 48, 51, 57, 60, }}, // 22.2%
{64, {3, 6, 7, 11, 12, 14, 15, 19, 22, 23, 24, 27, 28, 30, 31, 35, 38, 39, 43, 44, 46, 47, 48, 51, 54, 55, 56, 59, 60, 62, 63, }}, // 48.4%
// {68, {3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, 63, 67, }}, // 25.0%
// {72, {3, 6, 7, 11, 12, 14, 15, 19, 21, 22, 23, 24, 27, 30, 31, 33, 35, 38, 39, 42, 43, 46, 47, 48, 51, 54, 55, 57, 59, 60, 62, 63, 66, 67, 69, 70, 71, }}, // 51.4%
  };

// Compute stats used to determine which moduli we actually test with.
// First rejection, from low to high.
static std::unordered_map<int, int64_t> first_reject;
// static std::unordered_map<int, int64_t> unique_reject;
bool FindRejection(const std::vector<std::pair<int, std::set<int>>> &nsres,
                   uint64_t sum) {
  for (const auto &[m, nonresidues] : nsres) {
    int r = sum % m;
    if (nonresidues.contains(r)) {
      first_reject[m]++;
      return true;
    }
  }
  return false;
}

// Test that the residues (e.g. pasted from wikipedia) are inclusive
// of real residues.
static void SelfTest(const std::vector<std::pair<int, std::vector<int>>> &qres) {
  for (const auto &[m, residues] : qres) {
    // First test that they are correct.
    std::set<int> rs;
    for (int r : residues) rs.insert(r);
    auto IsResidue = [&rs](int r) {
        return rs.find(r) != rs.end();
      };

    for (uint64_t u = 0; u < 100000; u++) {
      uint64_t uu = u * u;
      int r = uu % m;
      CHECK(IsResidue(r)) << u << "^2 mod " << m << " got " << r;
    }
  }

}

static std::vector<std::pair<int, std::set<int>>>
GetNonSumResidues(
    const std::vector<std::pair<int, std::vector<int>>> &qres) {
  std::vector<std::pair<int, std::set<int>>> ret;
  for (const auto &[m, residues] : qres) {
    std::set<int> sums;
    for (int x : residues)
      for (int y : residues)
        sums.insert((x + y) % m);

    // If all residues are possible, skip.
    if (sums.size() < m) {
      std::set<int> non_res;
      // non_res.reserve(m - sums.size());
      for (int i = 0; i < m; i++) {
        if (sums.find(i) == sums.end()) {
          non_res.insert(i);
        }
      }
      ret.emplace_back(m, std::move(non_res));
    }
  }
  return ret;
}



  // Prints out the contents of NON_QUADRATIC_SUM_RESIDUES above. These are values
// that cannot be the sum (mod m) of two quadratic residues, and so they
// can't be x^2 + y^2 mod m for any x,y.
[[maybe_unused]]
static void PrintNonSumResidues(
    const std::vector<std::pair<int, std::set<int>>> &nsres) {
  printf("\n");
  // Which rows actually reject anything?
  for (const auto &[m, nr] : nsres) {
    printf("{" ACYAN("%d") ", {", m);
    for (int r : nr) printf("%d, ", r);
    printf("}}, // " AGREEN("%.1f%%") "\n", (100.0 * nr.size()) / m);
  }
}

// Compute and print rejection statistics; used to figure out which
// moduli we actually use.
static void RejectionStats(
    const std::vector<std::pair<int, std::set<int>>> &nsres) {
  for (const auto &[m, _] : nsres)
    first_reject[m] = 0;

  Timer run_timer;
  // static constexpr uint64_t MAX_SUM = 100'000'000;
  static constexpr int64_t SAMPLES = 1000000;
  Periodically bar_per(1.0);
  int64_t num_rejected = 0;

  ArcFour rc(StringPrintf("residue.0"));
  for (int64_t i = 0; i < SAMPLES; i++) {
    // numbers in the low trillions, at most
    const uint64_t sum = Rand64(&rc) & 0xFFFFFFFFFF;
    if (sum == 0) continue;
    if (!OldMaybeSumOfSquares(sum)) continue;

    if (FindRejection(nsres, sum)) num_rejected++;

    if ((sum % 65536) == 0 && bar_per.ShouldRun()) {
      printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
             ANSI_BEGINNING_OF_LINE "%s\n",
             ANSI::ProgressBar(i,
                               SAMPLES,
                               StringPrintf("rejected %lld", num_rejected),
                               run_timer.Seconds()).c_str());
    }
  }

  printf("Rejected %lld/%lld (%.2f%%)\n",
         num_rejected, SAMPLES, (100.0 * num_rejected) / SAMPLES);
  printf("First rejected:\n");
  std::vector<std::pair<int, int64_t>> sorted;
  for (const auto &[m, count] : first_reject) {
    if (count != 0) {
      sorted.emplace_back(m, count);
    }
  }

  std::sort(sorted.begin(), sorted.end());
  for (const auto &[m, count] : sorted) {
    printf(ACYAN("%d") ": %lld  " AGREY("(%.2f%%)") "\n", m, count,
           (count * 100.0) / SAMPLES);
  }
}

// Prints out code that tests for NON_QUADRATIC_SUM_RESIDUES using a bitmask.
static void MakeCode() {
  printf("\n"
         "inline constexpr bool MaybeSumOfSquares(uint64_t sum) {\n"
         "  return !(");

  bool first = true;
  for (const auto &[m, non_residues] : NON_QUADRATIC_SUM_RESIDUES) {
    if (!first) printf(" |\n           ");
    uint64_t mask = 0;
    for (int r : non_residues) {
      CHECK(r <= 63) << m << " " << r;
      mask |= (1ULL << r);
    }
    printf("((0x%016llxLLU >> (sum %% %d)) & 1)", mask, m);
    first = false;
  }
  printf(");\n"
         "}\n\n");
}

// Dunno why I didn't just compute these myself; it's easy.
static std::vector<std::pair<int, std::vector<int>>> MakeAllResidues(int max_p) {
  std::vector<std::pair<int, std::vector<int>>> ret;
  printf("Make residues...\n");
  Timer timer;
  for (int p = 1; p < max_p; p++) {
    std::unordered_set<int> residues;
    for (int i = 0; i < p; i++) {
      int r = (i * i) % p;
      residues.insert(r);
    }
    std::vector<int> rs;
    rs.reserve(residues.size());
    for (int r : residues) rs.push_back(r);
    std::sort(rs.begin(), rs.end());
    ret.push_back(make_pair(p, std::move(rs)));
  }
  printf("Made %d residues in %s\n", max_p - 1, ANSI::Time(timer.Seconds()).c_str());

  // Test against wikipedia list
  for (int i = 0; i < std::min(QUADRATIC_RESIDUES.size(), ret.size()); i++) {
    const auto &[qn, qr] = QUADRATIC_RESIDUES[i];
    const auto &[cqn, cqr] = ret[i];
    CHECK(qn == cqn) << qn << " " << cqn;
    CHECK(qr == cqr) << "n: " << qn << "\n qr: " << VecString(qr) <<
      "\ncqr:" << VecString(cqr);
  }
  return ret;
}

int main(int argc, char ** argv) {
  ANSI::Init();
  std::vector<std::pair<int, std::vector<int>>> all_qres =
    MakeAllResidues(2048);

  SelfTest(QUADRATIC_RESIDUES);
  SelfTest(all_qres);

  std::vector<std::pair<int, std::set<int>>> nqsr =
    GetNonSumResidues(all_qres);

  // PrintNonSumResidues(nqsr);

  RejectionStats(nqsr);

  MakeCode();

  return 0;
}
