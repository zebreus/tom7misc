
#include <unordered_set>
#include <vector>
#include <utility>
#include <map>

#include "ansi.h"
#include "arcfour.h"
#include "randutil.h"

// Only given the knowledge that I have.
struct Participant {
  int idx = 0;
  int a = 0, b = 0;
  int c = 0, d = 0;
  int e = 0, f = 0;
};

static void Santa(int num) {
  ArcFour rc("santa");

  std::vector<Participant> ppts;
  for (int i = 0; i < num; i++) {
    ppts.push_back(Participant{.idx = i});
  }

  // Stage 0.
  // Participants pick their a,b and populate this vector in random order.
  // All numbers are distinct.
  std::vector<std::pair<int, int>> ballots;
  std::unordered_set<int> used;
  auto NewRandom = [&rc, &used]() {
      for (;;) {
        int x = RandTo(&rc, 1000);
        if (!used.contains(x)) {
          used.insert(x);
          return x;
        }
      }
    };

  for (int i = 0; i < num; i++) {
    int a = NewRandom();
    int b = NewRandom();
    ballots.emplace_back(a, b);
    ppts[i].a = a;
    ppts[i].b = b;
    Shuffle(&rc, &ballots);
  }

  // Stage 1.

  // This is done by a specific player P0. Note that this player
  // does not know the identity of anyone else!
  Shuffle(&rc, &ballots);
  // Perform shunt.
  std::vector<std::pair<int, int>> shunted;
  for (int i = 0; i < num; i++) {
    int previ = i == 0 ? num - 1 : i - 1;
    shunted.emplace_back(ballots[previ].second, ballots[i].first);
  }

  // Now Pn validates this. But I know I did it right.

  // Stage 2.
  // Everyone swaps out their own a and b for new c and d.
  for (int i = 0; i < num; i++) {
    const int a = ppts[i].a;
    const int b = ppts[i].b;
    const int c = NewRandom();
    const int d = NewRandom();
    ppts[i].c = c;
    ppts[i].d = d;

    for (auto &[aa, bb] : ballots) {
      if (aa == a) aa = c;
      if (bb == b) bb = d;
    }
    Shuffle(&rc, &ballots);
  }

  // Stage 3. Do it again!
  for (int i = 0; i < num; i++) {
    const int c = ppts[i].c;
    const int d = ppts[i].d;
    const int e = NewRandom();
    const int f = NewRandom();
    ppts[i].e = e;
    ppts[i].f = f;

    for (auto &[cc, dd] : ballots) {
      if (cc == c) cc = e;
      if (dd == d) dd = f;
    }
    Shuffle(&rc, &ballots);
  }

  // Now everyone announces their value of E.
  std::map<int, int> idx_from_e;
  for (const Participant &ppt : ppts) {
    idx_from_e[ppt.e] = ppt.idx;
  }

  for (const auto &[e, f] : ballots) {
    printf("%03d (ppt %d) <- bought by f=%03d\n", e, idx_from_e[e], f);
  }

  for (const Participant &ppt : ppts) {
    printf(AWHITE("%d.") " " AYELLOW("%03d %03d %03d %03d %03d %03d") "\n",
           ppt.idx, ppt.a, ppt.b, ppt.c, ppt.d, ppt.e, ppt.f);
  }

}

int main(int argc, char **argv) {
  ANSI::Init();

  Santa(4);

  return 0;
}
