
#include "pactom-util.h"

#include <memory>
#include <vector>
#include <string>
#include <cmath>

#include "util.h"
#include "edit-distance.h"
#include "threadutil.h"
#include "base/logging.h"
#include "timer.h"

using namespace std;

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

                 int best = 10000000;
                 int bestidx = 0;
                 for (int oidx = 0; oidx < other.runs.size(); oidx++) {
                   const PacTom::Run &rother = other.runs[oidx];
                   if (rother.year == 0)
                     continue;

                   // XXX filter by length, etc.?
                   int score = RunEditDistance(run, rother);
                   if (score < best) {
                     bestidx = oidx;
                     best = score;
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
