
#include "freq.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "util.h"

#include "base/logging.h"

using namespace std;

Freq::Freq() {}

Freq *Freq::Load(const std::string &filename,
                 const std::unordered_set<string> &domain) {

  Freq *freq = new Freq;

  for (const string &w : domain) {
    freq->smoothed_counts[w] = 1;
    freq->total_count++;
  }

  vector<string> freqlist =
    Util::ReadFileToLines("freq.txt");

  for (string &line : freqlist) {
    if (line.empty())
      continue;
    string word = Util::NormalizeWhitespace(Util::lcase(Util::chop(line)));
    if (word.empty())
      continue;
    string countstr = Util::chop(line);
    CHECK(!countstr.empty()) << "malformed file";

    if (domain.contains(word)) {
      const int64_t count = atoll(countstr.c_str());
      freq->smoothed_counts[word] += count;
      freq->total_count += count;
    }
  }

  int64_t max_count = 0;
  for (const auto &[w, c] : freq->smoothed_counts) {
    max_count = std::max(c, max_count);
  }

  freq->probability_scale = 1.0 / (double)freq->total_count;
  freq->norm_scale = 1.0 / (double)max_count;

  // Initialize parallel sorted arrays.
  {
    std::vector<std::pair<std::string, int64_t>> sortme;
    sortme.reserve(freq->smoothed_counts.size());
    for (const auto &[w, c] : freq->smoothed_counts)
      sortme.emplace_back(w, c);
    std::sort(sortme.begin(), sortme.end(),
              [](const std::pair<std::string, int64_t> &a,
                 const std::pair<std::string, int64_t> &b) {
                return a.second > b.second;
              });
    freq->sorted.reserve(sortme.size());
    freq->counts.reserve(sortme.size());
    for (auto &[w, c] : sortme) {
      freq->sorted.emplace_back(std::move(w));
      freq->counts.emplace_back(c);
    }
  }

  return freq;
}

std::string Freq::ByWeightedSampleIndex(int64_t sample_idx) const {
  // PERF: There are faster ways to do this.
  // As one simple trick, once the weights become 1, we can just
  // directly index. As a generalization, since weights are nonincreasing,
  // we can compute a stride for larger indices as well (assuming the
  // worst case, which is that the remainder of the array equals the
  // current value).
  size_t idx = 0;
  while (idx < counts.size() && sample_idx >= counts[idx]) {
    sample_idx -= counts[idx];
    idx++;
  }

  if (idx < sorted.size()) return sorted[idx];
  else return "";
}
