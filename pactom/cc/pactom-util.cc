
#include "pactom-util.h"

#include <memory>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>

#include "util.h"
#include "edit-distance.h"
#include "threadutil.h"
#include "base/logging.h"
#include "timer.h"
#include "randutil.h"
#include "color-util.h"

using namespace std;
using Run = PacTom::Run;

using uint32 = uint32_t;

std::unique_ptr<PacTom> PacTomUtil::Load(bool merge_dates) {
  #define TOMDIR "d:\\oldtom\\runs"
  vector<string> raw_tomdir = Util::ListFiles(TOMDIR);
  vector<string> tomdir;
  for (string &f : raw_tomdir) {
    if (f != "CVS" && f.find("~") == string::npos &&
        !Util::StartsWith(f, "3dwr")) {
      tomdir.push_back(Util::dirplus(TOMDIR, f));
    }
  }
  printf("%d runs in tomdir\n", tomdir.size());

  unique_ptr<PacTom> tompac = PacTom::FromFiles(tomdir, "", true);
  CHECK(tompac.get() != nullptr);

  unique_ptr<PacTom> pactom = PacTom::FromFiles({"../pac.kml",
                                                 "../pac2.kml"},
    "../neighborhoods.kml", false);
  CHECK(pactom.get() != nullptr);

  if (merge_dates) {
    Timer timer;
    PacTomUtil::SetDatesFrom(pactom.get(), *tompac, 18);
    printf("Merged in %.3f sec\n", timer.Seconds());
    SortByDate(pactom.get());
  }

  return pactom;
}

// Lower is better.
static int RunEditDistance(const PacTom::Run &a, const PacTom::Run &b) {

  // ??
  auto DeletionCost = [&a](int x) { return 1; };
  auto InsertionCost = [&b](int x) { return 1; };

  auto SubstCost = [&a, &b](int ai, int bi) -> int {
      const auto [ya, xa] = a.path[ai].first.ToDegs();
      const auto [yb, xb] = b.path[bi].first.ToDegs();

      double dx = xa - xb;
      double dy = ya - yb;
      return sqrt(dx * dx + dy * dy) * 1000;
    };

  const auto [cmds, score] = EditDistance::GetAlignment(a.path.size(),
                                                        b.path.size(),
                                                        DeletionCost,
                                                        InsertionCost,
                                                        SubstCost);
  return score;
}

void PacTomUtil::SetDatesFrom(PacTom *dest, const PacTom &other, int max_threads) {
  ParallelComp(dest->runs.size(),
               [dest, &other](int idx) {
                 PacTom::Run &run = dest->runs[idx];
                 if (run.year > 0)
                   return;

                 std::vector<int> oidxes;
                 oidxes.reserve(other.runs.size());
                 for (int i = 0; i < other.runs.size(); i++) {
                   if (i > 0 && run.name == other.runs[i].name) {
                     // If there's an exact name match, compare it first
                     // to save time.
                     int t = oidxes[0];
                     oidxes[0] = i;
                     oidxes.push_back(t);
                   } else {
                     oidxes.push_back(i);
                   }
                 }
                 CHECK(oidxes.size() == other.runs.size());

                 int best = 10000000;
                 int bestidx = 0;
                 for (int oidx : oidxes) {
                   const PacTom::Run &rother = other.runs[oidx];
                   if (rother.year == 0)
                     continue;

                   // XXX filter by length, etc.?
                   int score = RunEditDistance(run, rother);
                   if (score < best) {
                     bestidx = oidx;
                     best = score;
                     // Once we have an exact match, no reason to
                     // try more options.
                     if (best == 0) break;
                   }
                 }

                 const PacTom::Run &rother = other.runs[bestidx];
                 printf("Matched [%s] to [%s], score %d\n",
                        run.name.c_str(),
                        rother.name.c_str(),
                        best);
                 if (best < 300) {
                   run.year = rother.year;
                   run.month = rother.month;
                   run.day = rother.day;
                 }
               }, max_threads);
}

bool CompareByDate(const Run &a, const Run &b) {
  auto ToNum = [](const Run &r) {
      return (r.year * 10000) + (r.month * 100) + r.day;
    };
  return ToNum(a) < ToNum(b);
}

void PacTomUtil::SortByDate(PacTom *dest) {
  std::sort(dest->runs.begin(),
            dest->runs.end(),
            CompareByDate);
}

uint32 PacTomUtil::RandomBrightColor(ArcFour *rc) {
  const float h = RandFloat(rc);
  const float s = 0.5f + (0.5f * RandFloat(rc));
  const float v = 0.5f + (0.5f * RandFloat(rc));
  float r, g, b;
  ColorUtil::HSVToRGB(h, s, v, &r, &g, &b);
  const uint32 rr = std::clamp((int)roundf(r * 255.0f), 0, 255);
  const uint32 gg = std::clamp((int)roundf(g * 255.0f), 0, 255);
  const uint32 bb = std::clamp((int)roundf(b * 255.0f), 0, 255);

  return (rr << 24) | (gg << 16) | (bb << 8) | 0xFF;
}
