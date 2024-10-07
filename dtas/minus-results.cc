
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ansi.h"
#include "auto-histo.h"
#include "image.h"
#include "minus.h"

using SolutionRow = MinusDB::SolutionRow;

// One-time update operations.
static void Update() {
  /*
  MinusDB db;
  db.ExecuteAndPrint("alter table solutions add column createdate integer not null "
                     "default 1727301666");
  */

  // Solve method as default.
  /*
  MinusDB db;
  db.ExecuteAndPrint("alter table solutions add column method integer not null "
                     "default 1");

  db.ExecuteAndPrint("select * from solutions");
  */
}

[[maybe_unused]]
static void Dump() {
  MinusDB db;
  db.ExecuteAndPrint("select * from solutions");
}

static void Report() {
  printf("\n");
  MinusDB db;
  std::unordered_set<LevelId> done = db.GetDone();
  std::unordered_set<LevelId> attempted = db.GetAttempted();
  std::unordered_set<LevelId> rejected = db.GetRejected();

  std::vector<SolutionRow> rows = db.GetSolutions();

  std::unordered_map<int, int> method_count;

  // TODO: Check for overlap between done/rejected; this should
  // never happen!

  for (const SolutionRow &row : rows) {
    // Remove levels that are done from the attempted set so that
    // it reflects levels that were tried but not yet solved.
    attempted.erase(row.level);
    method_count[row.method]++;
  }

  for (LevelId level : rejected) {
    attempted.erase(level);
  }

  double done_pct = (done.size() * 100.0) / 65536.0;
  double rejected_pct = (rejected.size() * 100.0) / 65536.0;
  double att_pct = (attempted.size() * 100.0) / 65536.0;
  printf(AGREEN("%d") "/" ABLUE("65536") " solved (%.2f%%)\n"
         ARED("%d") "/" ABLUE("65536") " rejected (%.2f%%)\n"
         AYELLOW("%d") " attempted unsuccessfully (%.2f%%)\n",
         (int)done.size(), done_pct,
         (int)rejected.size(), rejected_pct,
         (int)attempted.size(), att_pct);

  printf(AWHITE(" SOLVE") ": %d\n"
         AWHITE(" CROSS") ": %d\n"
         AWHITE("  MAZE") ": %d\n"
         AWHITE("MANUAL") ": %d\n",
         method_count[MinusDB::METHOD_SOLVE],
         method_count[MinusDB::METHOD_CROSS],
         method_count[MinusDB::METHOD_MAZE],
         method_count[MinusDB::METHOD_MANUAL]);

  ImageRGBA img(256, 256);
  img.Clear32(0x000000FF);
  // Row-major.
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      LevelId level = PackLevel(y, x);
      if (rejected.contains(level)) {
        img.SetPixel32(x, y, 0xFF0000FF);
      } else if (attempted.contains(level)) {
        img.SetPixel32(x, y, 0x333300FF);
      }
    }
  }

  // Now color done cells by method.
  for (const SolutionRow &row : rows) {
    uint32_t c = 0xFF00FFFF;
    switch (row.method) {
    case MinusDB::METHOD_SOLVE: c = 0x00FF00FF; break;
    case MinusDB::METHOD_CROSS: c = 0x77AA00FF; break;
    case MinusDB::METHOD_MAZE: c = 0x0077AAFF; break;
    case MinusDB::METHOD_MANUAL: c = 0xFF33AAFF; break;
    default: break;
    }
    const auto &[major, minor] = UnpackLevel(row.level);
    img.SetPixel32(minor, major, c);
  }

  img.ScaleBy(2).Save("minus.png");

  AutoHisto moves_histo(100000);
  for (const SolutionRow &row : rows) {
      // printf("%lld\n", r.movie.size());
      moves_histo.Observe(row.movie.size());
  }
  printf(AWHITE("Number of moves") " (across %d solutions):\n"
         "%s\n",
         (int)moves_histo.NumSamples(),
         moves_histo.SimpleANSI(24).c_str());
}

int main(int argc, char **argv) {
  ANSI::Init();

  Update();

  Report();

  return 0;
}
