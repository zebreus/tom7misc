
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
using RejectedRow = MinusDB::RejectedRow;

// One-time update operations.
static void Update() {
  /*
  MinusDB db;
  db.ExecuteAndPrint("alter table solutions "
                     "add column createdate integer not null "
                     "default 1727301666");
  */

  // Solve method as default.
  /*
  MinusDB db;
  db.ExecuteAndPrint("alter table solutions "
                     "add column method integer not null "
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

  std::vector<SolutionRow> sol_rows = db.GetAllSolutions();
  std::vector<RejectedRow> rej_rows = db.GetAllRejected();

  // Levels with a conclusive answer.
  std::unordered_set<LevelId> done;
  // Known to be unsolvable.
  std::unordered_set<LevelId> rejected;
  // Known to be solvable, with a solution.
  std::unordered_set<LevelId> solved;

  // Levels that have been attempted (with the "solve" strategy)
  // and thus have a partial solution, but no conclusive solution.
  std::unordered_set<LevelId> partial = db.GetHasPartial();

  // Indexed by both solution and rejection methods, which are
  // disjoint.
  std::unordered_map<int, int> method_count;

  for (const SolutionRow &row : sol_rows) {
    done.insert(row.level);
    solved.insert(row.level);
    // Remove levels that are done from the attempted set so that
    // it reflects levels that were tried but not yet solved.
    partial.erase(row.level);
    method_count[row.method]++;
  }

  for (const RejectedRow &row : rej_rows) {
    done.insert(row.level);
    rejected.insert(row.level);
    partial.erase(row.level);
    method_count[row.method]++;
  }

  // TODO: Check for overlap between done/rejected; this should
  // never happen!

  const double solved_pct = (solved.size() * 100.0) / 65536.0;
  const double rejected_pct = (rejected.size() * 100.0) / 65536.0;
  const double done_pct = (done.size() * 100.0) / 65536.0;
  const double att_pct = (partial.size() * 100.0) / 65536.0;
  printf(AGREEN("%d") "/" ABLUE("65536") " solved (%.2f%%)\n"
         ARED("%d") "/" ABLUE("65536") " rejected (%.2f%%)\n"
         APURPLE("%d") "/" ABLUE("65536") " done (%.2f%%)\n"
         AYELLOW("%d") " attempted unsuccessfully (%.2f%%)\n",
         (int)solved.size(), solved_pct,
         (int)rejected.size(), rejected_pct,
         (int)done.size(), done_pct,
         (int)partial.size(), att_pct);

#define AMINT(s) ANSI_FG(220, 250, 195) s ANSI_RESET
#define APINK(s) ANSI_FG(255, 222, 237) s ANSI_RESET

  printf(AMINT("   SOLVE") ": %d\n"
         AMINT("   CROSS") ": %d\n"
         AMINT("    MAZE") ": %d\n"
         AMINT("  MANUAL") ": %d\n",
         method_count[MinusDB::METHOD_SOLVE],
         method_count[MinusDB::METHOD_CROSS],
         method_count[MinusDB::METHOD_MAZE],
         method_count[MinusDB::METHOD_MANUAL]);

  printf("------" "\n"
         APINK("   NEVER") ": %d\n"
         APINK("  ALWAYS") ": %d\n"
         APINK("CUTSCENE") ": %d\n",
         method_count[MinusDB::REJECT_NEVER],
         method_count[MinusDB::REJECT_ALWAYS_DEAD],
         method_count[MinusDB::REJECT_CUTSCENE]);

  printf("\n");

  // First fill in the table with basic color (in case a method
  // is missing).
  ImageRGBA img(256, 256);
  // Row-major.
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      LevelId level = PackLevel(y, x);
      if (solved.contains(level)) {
        img.SetPixel32(x, y, 0x007700FF);
      } else if (rejected.contains(level)) {
        img.SetPixel32(x, y, 0x770000FF);
      } else if (partial.contains(level)) {
        img.SetPixel32(x, y, 0x333300FF);
      } else {
        img.SetPixel32(x, y, 0x000000FF);
      }
    }
  }

  // Now color done cells by method.
  for (const SolutionRow &row : sol_rows) {
    uint32_t c = 0xFF00FFFF;
    switch (row.method) {
    case MinusDB::METHOD_SOLVE: c = 0x00FF00FF; break;
    case MinusDB::METHOD_CROSS: c = 0x77AA00FF; break;
    case MinusDB::METHOD_MAZE: c = 0x0077AAFF; break;
    case MinusDB::METHOD_MANUAL: c = 0xAAFFFFFF; break;
    default: break;
    }
    const auto &[major, minor] = UnpackLevel(row.level);
    img.SetPixel32(minor, major, c);
  }

  // And rejected cells by method.
  for (const RejectedRow &row : rej_rows) {
    uint32_t c = 0x770000FF;
    switch (row.method) {
    case MinusDB::REJECT_NEVER: c = 0x990000FF; break;
    case MinusDB::REJECT_ALWAYS_DEAD: c = 0xAA0000FF; break;
    case MinusDB::REJECT_CUTSCENE: c = 0xFF0000FF; break;
    default: break;
    }
    const auto &[major, minor] = UnpackLevel(row.level);
    img.SetPixel32(minor, major, c);
  }

  img.ScaleBy(2).Save("minus.png");

  AutoHisto moves_histo(100000);
  for (const SolutionRow &row : sol_rows) {
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
