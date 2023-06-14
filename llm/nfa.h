// Simple nondeterministic finite automata.

#ifndef _LLM_NFA_H
#define _LLM_NFA_H

#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>
#include <functional>

#include "base/logging.h"
#include "base/stringprintf.h"

// Tokens are numeric, and must be in [0, radix).
template<int RADIX_>
struct NFA {
  using C = int;
  static_assert(RADIX_ > 0);
  static_assert((RADIX_ - 1) == (C)(RADIX_ - 1));
  static constexpr int RADIX = RADIX_;
  struct Node {
    // If missing, we think of this as transitioning to an (unrepresented)
    // REJECT state, which only loops to itself.
    std::unordered_map<C, std::unordered_set<int>> next_idx;
    bool accepting = false;
  };

  void CheckValidity() const {
    for (int s : start_states) {
      CHECK(s >= 0 && s < (int)nodes.size()) << s << " vs " << nodes.size();
    }

    for (const Node &node : nodes) {
      for (const auto &[c, nexts] : node.next_idx) {
        CHECK(c >= 0 && c < RADIX) << c << " vs " << RADIX;
        for (int s : nexts) {
          CHECK(s >= 0 && s < (int)nodes.size()) <<
            s << " vs " << nodes.size();
        }
      }
    }
  }

  std::string DebugString() const {
    std::string ret = "start states:";
    for (int i : start_states) StringAppendF(&ret, " %d", i);
    StringAppendF(&ret, "\n");
    for (int i = 0; i < (int)nodes.size(); i++) {
      StringAppendF(&ret, "%d.%s\n", i, nodes[i].accepting ? " (ACC)" : "");
      for (const auto &[c, nexts] : nodes[i].next_idx) {
        std::string ch = StringPrintf("(%d)", c);
        if constexpr (RADIX == 256 || RADIX == 257) {
            if (c >= 'a' && c <= 'z') {
              ch = StringPrintf("%c", c);
            } else if (c == 256) {
              ch = "EPS";
            }
          }
        StringAppendF(&ret, "  %s:", ch.c_str());
        for (int n : nexts) StringAppendF(&ret, " %d", n);
        StringAppendF(&ret, "\n");
      }
    }
    return ret;
  }

  std::vector<Node> nodes;
  std::unordered_set<int> start_states;
};

// Epsilon symbols are not supported; convert to a standard NFA first.
template<int RADIX>
struct NFAMatcher {
  using NFA = ::NFA<RADIX>;
  using C = NFA::C;
  // Starts in start state.
  explicit NFAMatcher(const NFA &nfa) :
    nfa(nfa), states(nfa.start_states) {};

  bool IsMatching() const {
    for (int s : states)
      if (nfa.nodes[s].accepting)
        return true;
    return false;
  }

  // Careful: Don't treat chars as signed!
  void Advance(C c) {
    CHECK(c >= 0);
    std::unordered_set<int> next;
    for (int s : states) {
      const typename NFA::Node &node = nfa.nodes[s];
      auto it = node.next_idx.find(c);
      if (it != node.next_idx.end())
        for (int n : it->second)
          next.insert(n);
    }
    states = std::move(next);
  }

  // True if there are no states, which means no string can be
  // accepted. The NFA can represent automata that are equivalent
  // (e.g. cyles of non-accepting states with no escape), so this
  // is incomplete in that case.
  // TODO: Would be straightforward to normalize those away.
  bool Stuck() const {
    return states.empty();
  }

private:
  const NFA &nfa;
  // Active nodes.
  std::unordered_set<int> states;
};

// Takes an epsilon-NFA, whose last symbol (RADIX-1) stands for "epislon,"
// and convert to a NFA with no epsilon symbol.
template<int RADIX>
static inline NFA<RADIX> RemoveEpsilon(const NFA<RADIX + 1> &enfa) {
  using NFA = ::NFA<RADIX>;
  using ENFA = ::NFA<RADIX + 1>;
  static constexpr typename ENFA::C EPSILON = RADIX;

  ENFA in = enfa;

  // Apply f once to every node (index) reachable from the (indexed)
  // node, using only epsilon transitions. Arbitrary order.
  // Includes the node itself.
  auto AppToClosure = [&in](auto f, int idx) {
      std::unordered_set<int> visited;
      std::unordered_set<int> todo = {idx};
      while (!todo.empty()) {
        const int src = *todo.begin();
        visited.insert(src);
        todo.erase(src);

        const typename ENFA::Node &node = in.nodes[src];
        f(src);

        auto it = node.next_idx.find(EPSILON);
        if (it != node.next_idx.end()) {
          for (int dst : it->second) {
            if (!visited.contains(dst)) todo.insert(dst);
          }
        }
      }
    };

  // Compute the transitive closure of accepting states (in place), so
  // we can stop thinking about that. For any node, it's accepting if
  // it has an epsilon transition to an accepting state.
  for (int n = 0; n < (int)in.nodes.size(); n++) {
    bool acc = in.nodes[n].accepting;
    if (!acc) {
      AppToClosure([&acc, &in](int m) {
          if (in.nodes[m].accepting) acc = true;
        }, n);
    }
    in.nodes[n].accepting = acc;
  }

  // The start states become the transitive closure of the start
  // states, since we could start by consuming the epsilon transitions.
  {
    std::unordered_set<int> new_start;
    for (int n : in.start_states) {
      AppToClosure([&new_start](int m) {
          new_start.insert(m);
        }, n);
    }
    in.start_states = std::move(new_start);
  }

  // Now we have an equivalent epislon-NFA, which still has epsilon
  // transitions. Remove them.
  for (int n = 0; n < (int)in.nodes.size(); n++) {
    std::unordered_map<typename ENFA::C, std::unordered_set<int>> new_next =
      in.nodes[n].next_idx;
    // This makes the epsilon transition redundant, which is the point.
    new_next.erase(EPSILON);

    // We may end up doing redundant work here, since if we had this
    // node's transitive closure already, we could just use it
    // instead. Some of this happens because we update the transitions
    // in place. But it would probably be faster to do some path
    // compression during the recursion.
    AppToClosure([&in, &new_next](int m) {
        // This is an epsilon-reachable node. So we can take all its
        // real transitions, too.
        for (const auto &[c, nexts] : in.nodes[m].next_idx) {
          if (c != EPSILON) {
            for (int dst : nexts) {
              new_next[c].insert(dst);
            }
          }
        }
      }, n);
    in.nodes[n].next_idx = std::move(new_next);
  }

  // Now the in NFA has no epsilon transitions, so just convert
  // it to the output type. (PERF: Could probably use std::move here?)
  NFA out;
  out.start_states = in.start_states;
  for (int i = 0; i < (int)in.nodes.size(); i++) {
    const typename ENFA::Node &in_node = in.nodes[i];
    typename NFA::Node out_node;
    out_node.accepting = in_node.accepting;
    for (const auto &[c, nexts] : in_node.next_idx) {
      CHECK(c != EPSILON);
      out_node.next_idx[c] = nexts;
    }
    out.nodes.push_back(out_node);
  }

  return out;
};

template<int RADIX>
struct RegEx {
  using ENFA = ::NFA<RADIX + 1>;
  using C = ENFA::C;
  static constexpr C EPSILON = RADIX;

  static ENFA LiteralString(std::string_view s) {
    std::vector<C> v;
    v.resize(s.size());
    // Careful: Don't treat chars as signed!
    for (int i = 0; i < (int)s.size(); i++) v[i] = (unsigned char)s[i];
    return Literal(v);
  }

  static ENFA Literal(const std::vector<C> &l) {
    ENFA out;
    for (int i = 0; i < (int)l.size(); i++) {
      typename ENFA::Node node;
      node.accepting = false;
      node.next_idx[l[i]].insert(i + 1);
      out.nodes.push_back(node);
    }
    typename ENFA::Node end_node;
    end_node.accepting = true;
    out.nodes.push_back(end_node);
    out.start_states = {0};
    return out;
  }

  static ENFA Concat(const ENFA &a, const ENFA &b) {
    // Every accepting state in a gets an epsilon transition to the
    // start states in b.
    ENFA out = a;

    // Collecting the accepting nodes (and remove the flag; these will
    // need to then match b to be accepted).
    std::unordered_set<int> accepting;
    for (int i = 0; i < (int)out.nodes.size(); i++) {
      if (out.nodes[i].accepting) {
        accepting.insert(i);
        out.nodes[i].accepting = false;
      }
    }

    // Link formerly accepting states in a to the start states of b,
    // using an epsilon transition.
    std::unordered_set<int> bstart = Append(b, &out);
    for (int bridge : accepting) {
      typename ENFA::Node &anode = out.nodes[bridge];
      for (int s : bstart) anode.next_idx[EPSILON].insert(s);
    }

    out.CheckValidity();
    return out;
  }

  static ENFA Or(const ENFA &a, const ENFA &b) {
    // Easy. The accepting states stay accepting. The start states
    // are just the union.
    ENFA out = a;
    std::unordered_set<int> bstart = Append(b, &out);
    for (int s : bstart) out.start_states.insert(s);
    return out;
  }

  static ENFA Plus(const ENFA &a) {
    ENFA out = a;
    // All the accepting states remain accepting. But they
    // also get epsilon transitions back to the start states.
    for (typename ENFA::Node &node : out.nodes) {
      if (node.accepting) {
        for (int s : out.start_states) {
          node.next_idx[EPSILON].insert(s);
        }
      }
    }
    return out;
  }

  static ENFA Question(const ENFA &a) {
    ENFA out = a;
    for (int s : out.start_states) {
      out.nodes[s].accepting = true;
    }
    return out;
  }

  static ENFA Star(const ENFA &a) {
    return Question(Plus(a));
  }

  // Expression that only matches the empty string.
  static ENFA Empty() {
    ENFA out;
    typename ENFA::Node node;
    node.accepting = true;
    out.nodes.push_back(node);
    out.start_states = {0};
    return out;
  }

  // Expression that matches nothing.
  static ENFA Void() {
    return ENFA();
  }

  // Accepts any single symbol.
  // Note this uses O(RADIX) space!
  static ENFA Any() {
    ENFA out;
    typename ENFA::Node node0;
    node0.accepting = false;
    for (int c = 0; c < RADIX; c++) {
      CHECK(c != EPSILON) << "Bug";
      node0.next_idx[c] = {1};
    }
    typename ENFA::Node node1;
    node1.accepting = true;
    out.nodes.push_back(std::move(node0));
    out.nodes.push_back(std::move(node1));
    out.start_states = {0};
    return out;
  }

private:
  // Append the nodes from enfa to out, by putting them at the end of
  // the nodes (and rewriting their indices). Keeps every accepting
  // state. Doesn't modify the start states of out; instead returns
  // the shifted indices.
  static std::unordered_set<int> Append(const ENFA &enfa, ENFA *out) {
    const int amount = out->nodes.size();
    for (int i = 0; i < (int)enfa.nodes.size(); i++) {
      const typename ENFA::Node &old_node = enfa.nodes[i];
      typename ENFA::Node node;
      node.accepting = old_node.accepting;
      for (const auto &[c, nexts] : old_node.next_idx) {
        for (int ox : nexts) {
          node.next_idx[c].insert(ox + amount);
        }
      }
      out->nodes.push_back(node);
    }

    std::unordered_set<int> ret;
    for (int s : enfa.start_states) ret.insert(s + amount);

    out->CheckValidity();
    return ret;
  }

};

// Parse a string to a byte-based Epsilon-NFA.
//
// The following characters are special:
//  [], denoting a character class
//  (), a grouping (no capturing supported)
//  e? e* e+
//  . matches ANY byte (note: many other regex languages
//                      exclude stuff like newline or \0)
//  e1|e2 matches
//  \  escapes the next character.
static NFA<257> Parse(const std::string &s) {
  using RE = ::RegEx<256>;
  using ENFA = typename RE::ENFA;
  static constexpr bool VERBOSE_PARSE = false;

  std::function<ENFA(std::string_view)> ParseRec =
    [&ParseRec](std::string_view s) {

      auto GetToMatchingBracket = [](std::string_view s) {
          CHECK(!s.empty());
          CHECK(s[0] == '[');
          for (int i = 1; i < (int)s.size(); i++) {
            switch (s[i]) {
            case ']':
              return i;
            case '\\':
              // Preserve backslash, but don't interpret the next
              // character.
              CHECK(i + 1 != (int)s.size()) << "Illegal escape";
              i++;
              break;
              // TODO: Something special for ^ and -?
            default:
              break;
            }
          }
          CHECK(false) << "Unclosed [";
          return 0;
        };

      // For a substring that starts with (, return the position
      // of the matching closing parenthesis, or abort.
      std::function<int(std::string_view)> GetToMatchingParen =
        [&GetToMatchingBracket,
         &GetToMatchingParen](std::string_view s) -> int {
          CHECK(!s.empty());
          CHECK(s[0] == '(');

          for (int i = 1; i < (int)s.size(); i++) {
            switch (s[i]) {
            case ')':
              return i;
            case '(':
              // Skip the parenthesized expression, recursively.
              i += GetToMatchingParen(s.substr(i));
              break;
            case '\\':
              CHECK(i + 1 != (int)s.size()) << "Illegal escape";
              i++;
              break;
            case '[':
              // Skip.
              i += GetToMatchingBracket(s.substr(i));
              break;
            default:
              break;
            }
          }
          CHECK(false) << "Unclosed (";
          return 0;
        };

      // Return the sequence of regex strings that are separated by |,
      // respecting parentheses, brackets, and escapes.
      auto SplitOr = [&GetToMatchingBracket,
                      &GetToMatchingParen](std::string_view s) ->
        std::vector<std::string_view> {
        std::vector<std::string_view> ret;

        int cur_start = 0;
        for (int i = 0; i < (int)s.size(); i++) {
          switch (s[i]) {
          case '(':
            i += GetToMatchingParen(s.substr(i));
            break;
          case '[':
            i += GetToMatchingBracket(s.substr(i));
            break;
          case '|':
            // Not including the bar.
            ret.push_back(s.substr(cur_start, i - cur_start));
            cur_start = i + 1;
            break;
          case '\\':
            // Preserve backslash, but don't interpret the next
            // character.
            CHECK(i + 1 != (int)s.size()) << "Illegal escape";
            i++;
            break;
          default:
            break;
          }
        }
        // Always at least one element.
        ret.push_back(s.substr(cur_start));
        return ret;
      };

      // Now, parse. Disjunction is the weakest-binding, so first
      // split by |.
      std::vector<std::string_view> dis = SplitOr(s);
      CHECK(!dis.empty()) << "Bug";

      // Parse a character class string (inside []) to an ENFA
      // that matches exactly one character from the class.
      auto CharacterClass =
        [](std::string_view s) -> ENFA {
          if (s.empty()) return RE::Empty();
          bool negate = false;
          if (s[0] == '^') {
            negate = true;
            s = s.substr(1);
          }

          std::vector<bool> match(256, false);

          // The last character we saw (or -1 if none),
          // which we'll use for ranges.
          int last = -1;
          if (s[0] == '-') {
            match[(uint8_t)'-'] = true;
            s = s.substr(1);
            last = (uint8_t)'-';
          }

          for (int i = 0; i < (int)s.size(); i++) {
            switch (s[i]) {
            case '-': {
              CHECK(i + 1 != (int)s.size()) << "Illegal range in ["
                                            << s << "]";
              CHECK(last >= 0) << "Range has no previous char in ["
                               << s << "]";
              int next = (uint8_t)s[i + 1];
              CHECK(last <= next) << "Range should be from the smaller "
                "byte to the larger byte in [" << s << "]" <<
                StringPrintf(" but got %c-%c", last, next);
              for (int c = last; c <= next; c++) {
                match[c] = true;
              }
              i++;
              last = -1;
              break;
            }
            case '\\':
              // This should actually be impossible since we would
              // not have found the closing bracket, right?
              CHECK(i + 1 != (int)s.size()) << "Illegal escape in []";
              i++;
              [[fallthrough]];
            default:
              match[(uint8_t)s[i]] = true;
              last = (uint8_t)s[i];
              break;
            }
          }

          ENFA out;
          typename ENFA::Node node0;
          node0.accepting = false;
          for (int c = 0; c < (int)match.size(); c++) {
            if (match[c] != negate) {
              node0.next_idx[c] = {1};
            }
          }
          typename ENFA::Node node1;
          node1.accepting = true;
          out.nodes.push_back(std::move(node0));
          out.nodes.push_back(std::move(node1));
          out.start_states = {0};
          return out;
        };

      // Compile one string clause into an ENFA. No disjunctions are
      // allowed at the top-level.
      auto CompileOneClause =
        [&ParseRec,
         &GetToMatchingParen,
         &GetToMatchingBracket,
         &CharacterClass](std::string_view s) -> ENFA {
          std::vector<ENFA> nfas;
          for (int i = 0; i < (int)s.size(); i++) {
            switch (s[i]) {
            case '(': {
              const int len = GetToMatchingParen(s.substr(i));
              CHECK(len >= 2);
              // The expression inside the parentheses.
              std::string_view inner = s.substr(i + 1, len - 1);
              nfas.push_back(ParseRec(inner));
              if (VERBOSE_PARSE)
                printf(" parens got {%s}\n", ((string)inner).c_str());
              // and skip over it
              i += len;
              break;
            }
            case ')':
              CHECK(false) << "Mismatched parens.";
              break;

            case '[': {
              if (VERBOSE_PARSE)
                printf("GetToMatchingBracket \"%s\", at %d.\n",
                       ((string)s).c_str(), i);
              const int len = GetToMatchingBracket(s.substr(i));
              CHECK(len >= 2);
              std::string_view inner = s.substr(i + 1, len - 1);
              if (VERBOSE_PARSE)
                printf(" got {%s}\n", ((string)inner).c_str());

              nfas.push_back(CharacterClass(inner));
              // skip over it
              i += len;
              break;
            }
            case ']':
              CHECK(false) << "Mismatched brackets.";
              break;

            case '*':
              CHECK(!nfas.empty()) << "Illegal leading *";
              nfas.back() = RE::Star(nfas.back());
              break;

            case '+':
              CHECK(!nfas.empty()) << "Illegal leading +";
              nfas.back() = RE::Plus(nfas.back());
              break;

            case '?':
              CHECK(!nfas.empty()) << "Illegal leading ?";
              nfas.back() = RE::Question(nfas.back());
              break;

            case '.':
              nfas.push_back(RE::Any());
              break;

            case '\\':
              CHECK(i + 1 != (int)s.size()) << "Illegal escape";
              i++;
              // Process the next character as a literal.
              [[fallthrough]];
            default: {
              // PERF should probably do a whole literal sequence at once?
              // But we have to be careful about "abc*" which does not mean
              // "(abc)*".
              ENFA cnfa = RE::LiteralString(s.substr(i, 1));
              nfas.push_back(std::move(cnfa));
              break;
            }
            }
          }

          // The result is their concatenation.
          if (nfas.empty()) return RE::Empty();
          ENFA out = std::move(*nfas.begin());
          for (int i = 1; i < (int)nfas.size(); i++)
            out = RE::Concat(out, nfas[i]);
          return out;
        };

      // Now compile each one, joining it with the last.
      // Void is the unit for Or.
      ENFA nfa = RE::Void();
      for (std::string_view ss : dis) {
        if (VERBOSE_PARSE)
          printf("Disjunctive clause {%s}\n", ((std::string)ss).c_str());
        ENFA nfa2 = CompileOneClause(ss);
        nfa2.CheckValidity();
        nfa = RE::Or(nfa, nfa2);
      }

      return nfa;
    };

  return ParseRec(s);
}


#endif
