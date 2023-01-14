
#include "freq.h"

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>

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

  return freq;
}
