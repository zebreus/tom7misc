// Simple nondeterministic finite automata.

#ifndef _LLM_NFA_H
#define _LLM_NFA_H

#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "base/logging.h"

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
    printf("Valid.\n");
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

  void Advance(C c) {
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

  static ENFA LiteralString(const std::string &s) {
    std::vector<C> v;
    v.resize(s.size());
    for (int i = 0; i < (int)s.size(); i++) v[i] = s[i];
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


#endif
