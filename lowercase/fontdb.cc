#include "fontdb.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/print.h"
#include "util.h"

using namespace std;

static void MakePRCurve() {
  FontDB db;

  std::unordered_map<std::string, FontDB::Info> files = db.Files();

  // P/R curve export
  struct Labeled {
    // "0" = more likely to be same case, 1 = least likely.
    float score = 0.0;
    bool same_case = false;
    Labeled(float score, bool same_case) :
      score(score), same_case(same_case) {}
  };

  vector<Labeled> labs;
  for (const auto &[filename, info] : files) {
    if (info.bitmap_diffs >= 0.0 && info.bitmap_diffs <= 1.0) {
      auto it = info.flags.find(FontDB::Flag::SAME_CASE);
      if (it != info.flags.end()) {
        labs.emplace_back(info.bitmap_diffs, it->second);
      }
    }
  }

  std::sort(labs.begin(), labs.end(),
            [](const Labeled &a, const Labeled &b) {
              return a.score < b.score;
            });

  // "Positive" here means same case (this is a low score).
  //
  // As we go, assuming the threshold is set to the current value,
  // what would our P/R be? This means calling everything we've
  // already seen a positive and everything else a negative. So
  // first, a parallel array giving the number of true positives
  // for the rest of the array (strictly higher scores).
  std::vector<int64_t> remaining_positives(labs.size(), 0);
  int64_t total_positives = 0;
  {
    int64_t pos_above = 0;
    for (int64_t i = labs.size() - 1; i >= 0; i--) {
      remaining_positives[i] = pos_above;
      if (labs[i].same_case) {
        total_positives++;
        pos_above++;
      }
    }
  }

  // Now compute precision at each threshold. Every item up to the
  // threshold being considered is predicted positive.
  int64_t positives_so_far = 0;
  std::vector<std::string> lines;
  lines.reserve(labs.size() + 1);
  lines.push_back("threshold\t"
                  "recall\t"
                  "precision");
  for (int64_t i = 0; i < labs.size(); i++) {
    if (labs[i].same_case) positives_so_far++;
    double precision = (double)positives_so_far / (i + 1);
    lines.push_back(
        std::format("{:.5f}\t{:.5f}\t{:.5f}",
                    labs[i].score,
                    (total_positives - remaining_positives[i]) /
                    (double)total_positives,
                    precision));
  }
  Util::WriteLinesToFile("pr-curve.tsv", lines);
}

int main(int argc, char **argv) {
  MakePRCurve();
  Print("OK\n");
  return 0;
}
