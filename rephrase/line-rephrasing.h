
#ifndef _REPHRASE_LINE_REPHRASING_H
#define _REPHRASE_LINE_REPHRASING_H

#include <unordered_set>
#include <string>
#include <functional>
#include <queue>

struct LLM;

struct LineRephrasing {
  // The LLM is expected to already be prompted. It's at the
  // beginning of the line that you want to rephrase. Recreate
  // this for each line, as it stores information about the
  // failures to rephrase the current line.
  LineRephrasing(LLM *llm,
                 // Is this the first line of the paragraph, or else a
                 // continuation line?
                 bool first_line,
                 // At each step, the number of alternative paths to
                 // explore. Setting this higher will cause the
                 // priority queue to grow linearly.
                 int num_alternatives = 3);

  // Sample a rephrased line.
  std::string RephraseOnce(
      // Consider a word to stop once it gets this long. This
      // prevents runaway (e.g. sequences of digits) that would
      // be exceeding the line length anyway.
      int max_word_length,
      // Return true on the line when it is complete. This
      // could be because it has reached or exceeded the line length.
      // Internally we assume that any complete line is a failure,
      // since the caller should abandon the LineRephrasing object
      // once they succeed.
      const std::function<bool(const std::string &)> &complete);

  struct Pending {
    std::string prefix;
    double prob = 0.0;

    // Normal less-than operator. The heap uses this to put the
    // greatest element at the top.
    bool operator <(const Pending &other) const {
      if (prob < other.prob) return true;
      else if (prob > other.prob) return false;
      else return prefix < other.prefix;
    }
  };

  std::priority_queue<Pending> pq;

  // Prefixes (at token boundaries) that have already been seen.
  std::unordered_set<std::string> already;

  LLM *llm = nullptr;
  const bool first_line = false;
  int num_alternatives = 2;
};

#endif
