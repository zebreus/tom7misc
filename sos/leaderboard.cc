
#include <vector>
#include <string>
#include <set>
#include <map>

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "threadutil.h"
#include "bhaskara-util.h"

using namespace std;

struct Entry {
  // TODO: Could add metadata
  std::string name;
  std::array<BigInt, 9> square;
};

static std::vector<Entry> Parse(const string &filename) {
  std::vector<string> lines =
    Util::NormalizeLines(Util::ReadFileToLines(filename));

  std::vector<Entry> entries;
  for (string &line : lines) {
    if (line.empty() || Util::StartsWith(line, "#"))
      continue;

    if (Util::TryStripPrefix("SQUARE ", &line)) {
      // 9 bigints, then name.
      Entry entry;
      for (int i = 0; i < 9; i++) {
        string xs = Util::chop(line);
        entry.square[i] = BigInt(xs);
      }
      entry.name = Util::LoseWhiteL(line);
      entries.push_back(std::move(entry));
    } else {
      printf("Unknown line " AGREY("%s") "\n", line.c_str());
    }
  }
  return entries;
}

struct ScoredEntry {
  Entry entry;
  int rows = 0, cols = 0, diag = 0;
  int squares = 0;
  int distinct = 0;
  BigInt sum;
  BigInt total_error;
  BigInt min_error;
  BigInt max_error;
};

ScoredEntry Score(const Entry &entry) {
  ScoredEntry scored;
  scored.entry = entry;

  auto Cell = [&entry](int r, int c) {
      return entry.square[r * 3 + c];
    };

  // Get the most common sum.
  std::map<BigInt, int> sums;
  // rows and cols
  for (int a = 0; a < 3; a++) {
    BigInt rsum, csum;
    for (int b = 0; b < 3; b++) {
      rsum += Cell(a, b);
      csum += Cell(b, a);
    }
    sums[rsum]++;
    sums[csum]++;
  }
  // diags

  {
    BigInt dsum, psum;
    for (int a = 0; a < 3; a++) {
      dsum += Cell(a, a);
      psum += Cell(a, 2 - a);
    }
    sums[dsum]++;
    sums[psum]++;
  }

  CHECK(!sums.empty());
  BigInt sum;
  int best_sum = 0;
  for (const auto &[s, count] : sums) {
    if (count > best_sum) {
      sum = s;
      best_sum = count;
    }
  }
  scored.sum = sum;

  // Now check against sum.
  // rows and cols
  for (int a = 0; a < 3; a++) {
    BigInt rsum, csum;
    for (int b = 0; b < 3; b++) {
      rsum += Cell(a, b);
      csum += Cell(b, a);
    }
    if (rsum == sum) scored.rows++;
    if (csum == sum) scored.cols++;
  }

  // diags
  {
    BigInt dsum, psum;
    for (int a = 0; a < 3; a++) {
      dsum += Cell(a, a);
      psum += Cell(a, 2 - a);
    }
    if (dsum == sum) scored.diag++;
    if (psum == sum) scored.diag++;
  }

  // Number of distinct elements.
  std::set<BigInt> elts;
  for (const BigInt &n : entry.square)
    elts.insert(n);

  scored.distinct = (int)elts.size();

  // Square errors.
  // TODO: output mask
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      const BigInt &nn = Cell(r, c);
      BigInt err = BigInt::Abs(BigSqrtError(nn));
      scored.total_error = scored.total_error + err;
      if (err == 0) scored.squares++;
      // XXX
      if (r == 0 && c == 0) scored.min_error = err;
      if (err > 0) {
        scored.min_error = std::min(scored.min_error, err);
      }
      scored.max_error = std::max(scored.max_error, err);
    }
  }


  return scored;
}

static void PrintScored(const ScoredEntry &scored) {
  printf(AWHITE("%s") ":\n", scored.entry.name.c_str());
  std::array<std::string, 9> nums;
  int max_length = 0;
  for (int i = 0; i < 9; i++) {
    nums[i] = LongNum(scored.entry.square[i]);
    max_length = std::max((int)nums[i].size(), max_length);
  }

  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 3; x++) {
      if (x != 0) printf("  ");
      int idx = y * 3 + x;
      int spc = max_length - nums[idx].size();
      for (int i = 0; i < spc; i++) printf(" ");
      // XXX color from mask
      printf("%s", nums[idx].c_str());
    }
    printf("\n");
  }

  printf("Sum: " ABLUE("%s") "\n",
         LongNum(scored.sum).c_str());

  if (scored.rows == 3 &&
      scored.cols == 3 &&
      scored.diag == 2 &&
      scored.distinct == 9) {
    printf("Is a " AGREEN("magic square") ".\n");
  } else {
    printf("Not a " ARED("magic square") ":\n"
           "Incorrect rows: %s%d" ANSI_RESET "\n"
           "Incorrect cols: %s%d" ANSI_RESET "\n"
           "Incorrect diag: %s%d" ANSI_RESET "\n"
           "Duplicates: %s%d" ANSI_RESET "\n",
           (scored.rows == 3) ? ANSI_GREEN : ANSI_RED,
           3 - scored.rows,
           (scored.cols == 3) ? ANSI_GREEN : ANSI_RED,
           3 - scored.cols,
           (scored.diag == 3) ? ANSI_GREEN : ANSI_RED,
           2 - scored.diag,
           (scored.distinct == 9) ? ANSI_GREEN : ANSI_RED,
           9 - scored.distinct);
  }

  if (scored.squares == 9) {
    printf("Everything is a " AGREEN("square number") "!\n");
  } else {
    printf("Non-square numbers: " ARED("%d") "\n"
           "Total error: " APURPLE("%s") "\n"
           "Min error: " APURPLE("%s") "\n"
           "Max error: " APURPLE("%s") "\n",
           9 - scored.squares,
           LongNum(scored.total_error).c_str(),
           LongNum(scored.min_error).c_str(),
           LongNum(scored.max_error).c_str());
  }
}

static void Leaderboard() {
  std::vector<Entry> entries = Parse("squares.txt");
  std::vector<ScoredEntry> scored_entries =
    ParallelMap(entries, Score, 8);

  // sort, etc.
  for (const ScoredEntry &scored : scored_entries) {
    PrintScored(scored);
    printf("\n");
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Leaderboard();

  return 0;
}
