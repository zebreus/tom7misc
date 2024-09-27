
#include "minus.h"

#include "ansi.h"
#include <cstdio>
#include <unordered_set>

#include "image.h"

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

  // Remove levels that are done from the attempted set so that
  // it reflects levels that were tried but not yet solved.
  for (LevelId level : done) {
    attempted.erase(level);
  }

  double done_pct = (done.size() * 100.0) / 65536.0;
  double att_pct = (attempted.size() * 100.0) / 65536.0;
  printf(AGREEN("%d") "/" ABLUE("65536") " done (%.2f%%)\n"
         AORANGE("%d") " attmpted unsuccessfully (%.2f%%)\n",
         (int)done.size(), done_pct,
         (int)attempted.size(), att_pct);

  ImageRGBA img(256, 256);
  // Row-major.
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      LevelId level = PackLevel(y, x);
      if (done.contains(level)) {
        img.SetPixel32(x, y, 0x00FF00FF);
      } else if (attempted.contains(level)) {
        // Orange
        // img.SetPixel32(x, y, 0xF79B39FF);
        img.SetPixel32(x, y, 0xFF0000FF);
      } else {
        img.SetPixel32(x, y, 0x000000FF);
      }
    }
  }

  img.ScaleBy(2).Save("minus.png");
}

int main(int argc, char **argv) {
  ANSI::Init();

  Update();

  Report();

  return 0;
}
