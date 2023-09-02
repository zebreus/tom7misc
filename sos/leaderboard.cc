
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

// Find the square s nearest to aa and return
// the signed difference.
// e.g. for an input of 10, return -1, since the
// closest square is 9.
inline BigInt SignedBigSqrtError(const BigInt &aa) {
  // Always rounds down.
  const auto a1 = BigInt::Sqrt(aa);
  BigInt aa1 = a1 * a1;
  BigInt a2 = a1 + 1;
  BigInt aa2 = a2 * a2;
  CHECK(aa1 <= aa && aa < aa2);
  BigInt down = aa - aa1;
  BigInt up = aa2 - aa;
  if (down < up) {
    // closer down
    return -std::move(down);
  } else {
    return up;
  }
}

static std::vector<Entry> Parse(const string &filename) {
  std::vector<string> lines =
    Util::NormalizeLines(Util::ReadFileToLines(filename));

  std::vector<Entry> entries;
  auto GetEntryByName = [&entries](const string &name) {
      for (const Entry &entry : entries) {
        if (entry.name ==  name) return entry;
      }
      CHECK(false) << "Not found: " << name;
      return Entry{};
    };
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
    } else if (Util::TryStripPrefix("SCALE ", &line)) {
      string base = Util::chop(line);
      Entry entry = GetEntryByName(base);
      BigInt scale = BigInt(Util::chop(line));
      for (BigInt &x : entry.square) x = x * scale;
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
  std::array<BigInt, 9> sqerror;
  // the square <= the number, and the next square >.
  std::array<BigInt, 9> prev_sq;
  std::array<BigInt, 9> next_sq;
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

      const auto n1 = BigInt::Sqrt(nn);
      BigInt nn1 = n1 * n1;
      BigInt n2 = n1 + 1;
      BigInt nn2 = n2 * n2;
      CHECK(nn1 <= nn && nn < nn2);
      scored.prev_sq[r * 3 + c] = nn1;
      scored.next_sq[r * 3 + c] = nn2;

      BigInt down = nn - nn1;
      BigInt up = nn2 - nn;
      BigInt err =
        (down < up) ?
        // closer down
        -down :
        up;

      scored.sqerror[r * 3 + c] = err;
      BigInt abs_err = BigInt::Abs(err);
      scored.total_error = scored.total_error + abs_err;
      if (err == 0) scored.squares++;
      // XXX
      if (r == 0 && c == 0) scored.min_error = err;
      if (abs_err > 0) {
        if (abs_err < BigInt::Abs(scored.min_error)) {
          scored.min_error = err;
        }
      }
      if (abs_err > BigInt::Abs(scored.max_error)) {
        scored.max_error = err;
      }
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
      if (scored.sqerror[y * 3 + x] != 0) {
        printf(AFGCOLOR(250, 110, 110, "%s"), nums[idx].c_str());
      } else {
        printf("%s", nums[idx].c_str());
      }
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

    for (int i = 0; i < 9; i++) {
      if (scored.sqerror[i] != 0) {
        BigInt down = scored.entry.square[i] - scored.prev_sq[i];
        BigInt up = scored.next_sq[i] - scored.entry.square[i];

        printf("%s (%s away) [%s] (%s away) %s\n",
               LongNum(scored.prev_sq[i]).c_str(),
               LongNum(down).c_str(),
               LongNum(scored.entry.square[i]).c_str(),
               LongNum(up).c_str(),
               LongNum(scored.next_sq[i]).c_str());
      }
    }
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
