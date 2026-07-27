
#include "ansi.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "base/print.h"
#include "cell-library.h"
#include "util.h"

static constexpr int MAX_WIRE = 256;

// Given a new wire displacement, compute all the wire sums (w1 + w2 and
// w1 - w2).
static int64_t ScoreWithProposedWire(const std::vector<int> &counts,
                                     const std::vector<int> &min_steps,
                                     int wire) {
  int64_t total_solved = 0;

  for (size_t i = 0; i < counts.size(); i++) {
    if (counts[i] == 0 || min_steps[i] == 1) continue;

    int disp = (int)i;
    int steps = 3;

    if (disp == wire) {
      steps = 1;
    } else if (disp == wire + wire) {
      steps = 2;
    } else {
      for (int w : CellLibrary::WIRE_SIZES) {
        if (disp == wire + w || disp == wire - w || disp == w - wire) {
          steps = 2;
          break;
        }
      }
    }

    if (steps < min_steps[i]) {
      total_solved += counts[i];
    }
  }

  return total_solved;
}

// counts is a dense map of wire displacement (index) to
// the number of times it occurred.
static void ProposeWire(const std::vector<int> &counts) {
  std::unordered_set<int> existing_wires;
  for (int w : CellLibrary::WIRE_SIZES) existing_wires.insert(w);

  std::vector<int> min_steps(counts.size(), 3);
  for (size_t i = 0; i < counts.size(); i++) {
    if (counts[i] == 0) continue;
    int disp = (int)i;
    int steps = 3;
    for (int w1 : CellLibrary::WIRE_SIZES) {
      if (disp == w1) {
        steps = 1;
        break;
      }
      for (int w2 : CellLibrary::WIRE_SIZES) {
        if (disp == w1 + w2 || disp == w1 - w2 || disp == w2 - w1) {
          steps = 2;
          break;
        }
      }
    }
    min_steps[i] = steps;
  }

  struct Candidate {
    int wire = 0;
    int64_t score = 0;
  };
  std::vector<Candidate> candidates;

  // What would be the next most valuable displacement to add?
  for (int w = 0; w < MAX_WIRE; w++) {
    if (!existing_wires.contains(w)) {
      int64_t score = ScoreWithProposedWire(counts, min_steps, w);
      candidates.push_back({w, score});
    }
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b) {
              return a.score > b.score;
            });

  Print("Top proposed wires:\n");
  for (size_t i = 0; i < 10 && i < candidates.size(); i++) {
    Print("  Wire size {}: solves {} more desired wires\n",
          candidates[i].wire, candidates[i].score);
  }
}

static std::vector<int> LoadDesiredWires() {
  std::vector<std::string> lines =
    Util::ReadFileToLines("desired-wires.txt");
  std::vector<int> counts;
  for (const std::string &line : lines) {
    std::string_view sv = line;
    std::string_view disp_str = Util::NextToken(&sv, ':');
    if (disp_str.empty() || sv.empty()) continue;

    int displacement = static_cast<int>(Util::ParseInt64(disp_str));
    int count = static_cast<int>(Util::ParseInt64(sv));

    if (displacement >= 0) {
      if (static_cast<size_t>(displacement) >= counts.size()) {
        counts.resize(displacement + 1, 0);
      }
      counts[displacement] += count;
    }
  }
  return counts;
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::vector<int> counts = LoadDesiredWires();
  ProposeWire(counts);

  return 0;
}
