
#ifndef _LLM_NFA_UTIL_H
#define _LLM_NFA_UTIL_H

#include <string>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "nfa.h"
#include "ansi.h"

using ByteNFA = NFA<256>;
using ByteNFAMatcher = NFAMatcher<256>;
using ByteRegEx = RegEx<256>;

inline void AdvanceString(const std::string &s, ByteNFAMatcher *matcher) {
  for (int i = 0; i < (int)s.size(); i++) {
    unsigned char c = s[i];
    matcher->Advance(c);
  }
}

inline bool Matches(const ByteNFA &nfa, const std::string &s) {
  nfa.AssertValidity();
  ByteNFAMatcher matcher(nfa);
  AdvanceString(s, &matcher);
  return matcher.IsMatching();
}

template<int RADIX>
inline std::pair<int, int> NFADebugSize(const NFA<RADIX> &nfa) {
  size_t transitions = 0;
  for (const auto &node : nfa.nodes)
    for (const auto &[c_, nexts] : node.next_idx)
      transitions += nexts.size();
  return make_pair((int)nfa.nodes.size(), (int)transitions);
}

template<int RADIX>
inline std::string NFADebugString(
    const NFA<RADIX> &nfa,
    // With a matcher.
    const std::unordered_set<int> &in = {}) {
  auto [st, ss] = NFADebugSize(nfa);
  std::string ret = StringPrintf("Size: %d transitions, %d states\n"
                                 "start states:", st, ss);
  for (int i : nfa.start_states) StringAppendF(&ret, " %d", i);
  StringAppendF(&ret, "\n");
  if (!in.empty()) StringAppendF(&ret, "Currently in %d states.\n",
                                 (int)in.size());
  for (int i = 0; i < (int)nfa.nodes.size(); i++) {
    StringAppendF(&ret, AWHITE("%d") ".%s%s%s\n",
                  i,
                  nfa.start_states.contains(i) ? " " ABLUE("(START)") : "",
                  nfa.nodes[i].accepting ? " " AGREEN("(ACC)") : "",
                  in.contains(i) ? " " APURPLE("(IN)") : "");

    // Output them in sorted order.
    std::vector<std::pair<int, std::vector<int>>> next_idx;
    for (const auto &[c, nexts] : nfa.nodes[i].next_idx) {
      std::vector<int> sorted_nexts;
      for (int n : nexts) sorted_nexts.push_back(n);
      std::sort(sorted_nexts.begin(), sorted_nexts.end());
      next_idx.emplace_back(c, std::move(sorted_nexts));
    }
    std::sort(next_idx.begin(), next_idx.end(),
              [](const std::pair<int, std::vector<int>> &a,
                 const std::pair<int, std::vector<int>> &b) {
                return a.first < b.first;
              });

    auto Symbol = [](int c) -> std::string {
        if constexpr (RADIX == 256 || RADIX == 257) {
          if (c >= ' ' && c <= '~') {
            return StringPrintf("%c", c);
          } else if (c == 10) {
            return "\\n";
          } else if (c == 256) {
            return "EPS";
          }
        }
        return StringPrintf("#%d", c);
      };

    for (int n = 0; n < (int)next_idx.size(); n++) {
      const auto &[c, nexts] = next_idx[n];
      int cto = c;
      for (int p = n + 1; p <= (int)next_idx.size(); p++) {
        const auto &[cp, nextsp] = next_idx[p];
        if (nexts != nextsp || p == (int)next_idx.size()) {
          std::string syms;
          if (c == cto) {
            syms = Symbol(c);
          } else {
            syms = StringPrintf(AYELLOW("%s") "-" AYELLOW("%s"),
                                Symbol(c).c_str(),
                                Symbol(cto).c_str());
          }
          StringAppendF(&ret, "  (%s):", syms.c_str());
          for (int n : nexts) StringAppendF(&ret, " %d", n);
          StringAppendF(&ret, "\n");
          n = p - 1;
          // next n
          break;
        } else {
          cto = cp;
        }
      }
    }
  }
  return ret;
}

#endif
