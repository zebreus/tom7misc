
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "inline-vector.h"
#include "randutil.h"
#include "util.h"

// Simple "Secret Santa" with marbles:
// Each player contributes two marbles. They know which marbles
// are theirs. Nobody else knows. Everyone sees the same public, uniformly
// random ordering for the marbles. Each player simultaneously declares
// one of their two marbles to belong to them (using one of the policies
// below). Then everyone computes the assignment (of giver to receiver)
// using the same function of the shared state (and their own knowledge
// of their secret marble). The code below simulates this with various
// policies and assignment functions.

// Nonzero integer. They come in pairs of x and -x.
using Marble = int;

// Apply policy to reveal your marble.
// The positive marble is the one that's revealed,
// so we express our choice by swapping our marbles
// (if the policy returns true). We don't get any
// other information because all of the other marbles
// definitionally have unknown origin. Called with idx1 < idx2.

// This one never swaps, as though we picked the marble
// to reveal ahead of time.
[[maybe_unused]]
bool RevealFixed(const std::vector<Marble> &marbles, int idx1, int idx2) {
  return false;
}

// If the marbles are adjacent (modularly) then reveal the
// first one.
[[maybe_unused]]
bool RevealNotAdjacent(const std::vector<Marble> &marbles, int idx1, int idx2) {
  bool adjacent = (idx1 + 1) % marbles.size() == idx2;
  int m1 = marbles[idx1];
  int m2 = marbles[idx2];
  CHECK((m1 < 0 && m2 > 0) ||
        (m1 > 0 && m2 < 0));
  // swap in just this situation
  if (adjacent && m1 < 0) return true;
  return false;
}

// There are two gaps between marbles (considered modularly): The short
// gap and the long gap. Consider the short gap. This reveals the marble
// at the beginning of the short gap.
[[maybe_unused]]
bool RevealShort(const std::vector<Marble> &marbles, int idx1, int idx2) {
  int n = marbles.size();
  int m1 = marbles[idx1];
  int m2 = marbles[idx2];
  CHECK((m1 < 0 && m2 > 0) ||
        (m1 > 0 && m2 < 0));

  int dist1 = idx2 - idx1;
  int dist2 = n - dist1;

  if (dist1 < dist2) {
    return m1 < 0;
  } else if (dist2 < dist1) {
    return m2 < 0;
  }
  return false;
}

[[maybe_unused]]
bool RevealLong(const std::vector<Marble> &marbles, int idx1, int idx2) {
  return !RevealShort(marbles, idx1, idx2);
}


// Policy for extracting the assignment (as edges from (giver, receiver))
// from the marbles.
[[maybe_unused]]
std::vector<std::pair<int, int>> GetAssignmentOrdered(
    int num_players,
    const std::vector<int> &marbles) {
  CHECK(marbles.size() == num_players * 2);
  std::vector<int> declared, secret;
  declared.reserve(num_players);
  secret.reserve(num_players);
  for (int m : marbles) {
    if (m < 0) secret.push_back(m);
    else declared.push_back(m);
  }

  std::vector<std::pair<int, int>> ret;
  ret.reserve(num_players);

  // Each player computes their position in the secret list,
  // and that tells them the position of their recipient in
  // the declared list.
  for (int pos = 0; pos < secret.size(); pos++) {
    int giver = -secret[pos];
    int receiver = declared[pos];
    ret.push_back({giver, receiver});
  }

  return ret;
}

// Assignment policy: Consider secret marbles to be "open parens"
// and declared marbles to be "close parens". Think of the string
// momentarily as a cylic loop with the same number of open and close
// parentheses. There is always a position in this loop where we
// can begin and read off a balanced parenthesis string (where the
// running total of open - close is nonnegative). Choose the first
// such position. This string gives us the assignment: The owner
// of the secret marble (open paren) gives to the owner of the
// declared marble (matching close paren).
[[maybe_unused]]
std::vector<std::pair<int, int>> GetAssignmentParen(
    int num_players,
    const std::vector<int> &marbles) {
  CHECK(marbles.size() == num_players * 2);
  int n = num_players * 2;

  int current_sum = 0;
  int min_sum = 0;
  int min_idx = -1;
  for (int i = 0; i < n; i++) {
    current_sum += (marbles[i] < 0 ? 1 : -1);
    if (current_sum < min_sum) {
      min_sum = current_sum;
      min_idx = i;
    }
  }

  int start_idx = min_idx + 1;

  std::vector<std::pair<int, int>> ret;
  ret.reserve(num_players);
  std::vector<int> open_stack;
  open_stack.reserve(num_players);

  for (int i = 0; i < n; i++) {
    int idx = (start_idx + i) % n;
    int m = marbles[idx];
    if (m < 0) {
      open_stack.push_back(-m);
    } else {
      CHECK(!open_stack.empty());
      int giver = open_stack.back();
      open_stack.pop_back();
      int receiver = m;
      ret.push_back({giver, receiver});
    }
  }

  return ret;
}

static void Simulate() {
  ArcFour rc("marbles");

  // output in TSV format
  Print("players\tvalid\tself recpt.\tmutual gift\tsingle cycle\n");

  for (int num_players = 3; num_players < 30; num_players++) {

    static constexpr int ROUNDS = 100000;

    std::vector<int> marbles;

    int valid_rounds = 0;
    int self_recipient_rounds = 0;
    int single_cycle_rounds = 0;
    int mutual_gift_rounds = 0;

    for (int p = 1; p <= num_players; p++) {
      marbles.push_back(p);
      marbles.push_back(-p);
    }
    for (int round = 0; round < ROUNDS; round++) {
      Shuffle(&rc, &marbles);

      for (int p = 1; p <= num_players; p++) {
        InlineVector<int> v;
        for (int i = 0; i < num_players * 2; i++) {
          if (marbles[i] == p || marbles[i] == -p) {
            v.push_back(i);
          }
        }

        CHECK(v.size() == 2 && v[0] < v[1]) << "Bug";
        if (RevealShort(marbles, v[0], v[1])) {
          std::swap(marbles[v[0]], marbles[v[1]]);
        }
      }

      std::vector<std::pair<int, int>> assignment =
        GetAssignmentParen(num_players, marbles);

      std::vector<int> next_node(num_players + 1, 0);
      bool has_self_recipient = false;
      std::vector<int> in_degree(num_players + 1, 0);
      std::vector<int> out_degree(num_players + 1, 0);
      for (const std::pair<int, int> &edge : assignment) {
        next_node[edge.first] = edge.second;
        out_degree[edge.first]++;
        in_degree[edge.second]++;
        if (edge.first == edge.second) {
          has_self_recipient = true;
        }
      }

      bool is_valid = true;
      for (int i = 1; i <= num_players; i++) {
        if (in_degree[i] != 1 || out_degree[i] != 1) {
          is_valid = false;
          break;
        }
      }
      if (is_valid) {
        valid_rounds++;
      }

      if (has_self_recipient) {
        self_recipient_rounds++;
      }

      int cycle_length = 0;
      int current = 1;
      while (current != 0 && cycle_length < num_players) {
        current = next_node[current];
        cycle_length++;
        if (current == 1) {
          break;
        }
      }
      if (cycle_length == num_players && current == 1) {
        single_cycle_rounds++;
      }

      bool has_mutual = false;
      for (int i = 1; i <= num_players; i++) {
        int recipient = next_node[i];
        if (recipient != i && next_node[recipient] == i) {
          has_mutual = true;
          break;
        }
      }
      if (has_mutual) {
        mutual_gift_rounds++;
      }
    }

    Print("{}\t{:.4f}\t{:.4f}\t{:.4f}\t{:.4f}\n",
          num_players,
          100.0 * valid_rounds / ROUNDS,
          100.0 * self_recipient_rounds / ROUNDS,
          100.0 * mutual_gift_rounds / ROUNDS,
          100.0 * single_cycle_rounds / ROUNDS);
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  Simulate();

  return 0;
}
