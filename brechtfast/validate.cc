
#include "albrecht.h"

#include <cstdlib>
#include <ctime>
#include <format>
#include <optional>
#include <vector>

#include "ansi.h"
#include "atomic-util.h"
#include "base/logging.h"
#include "base/print.h"
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"

DECLARE_COUNTERS(ctr_done,
                 ctr_no_poly, ctr_ill_conditioned, ctr_not_manifold,
                 ctr_invalid);

using Aug = Albrecht::AugmentedPoly;

static constexpr bool DRY_RUN = false;

void Validate(int id) {
  Timer timer;
  DB db;
  db.Init();

  StatusBar status(1);
  Periodically status_per(0.25);

  status.Print("Fixup DB.\n");
  db.Fixup();
  status.Print("DB fixed.\n");

  std::vector<DB::Hard> hards;
  if (id != 0) {
    hards.push_back(db.GetHard(id));
  } else {
    hards = db.AllHard();
  }

  status.Print("Got {} hard.\n", hards.size());

  static constexpr int max_threads = 8;

  ParallelApp(
      hards,
      [&](const DB::Hard &hard) {
        bool invalid = false;

        std::optional<Polyhedron> poly =
            PolyhedronFromVertices(hard.poly_points);

        if (!poly.has_value()) {
          ctr_no_poly++;
          invalid = true;
        } else {
          if (!IsWellConditioned(poly.value().vertices)) {
            ctr_ill_conditioned++;
            invalid = true;
          } else if (!IsManifold(poly.value())) {
            ctr_not_manifold++;
            invalid = true;
          }
        }

        if (invalid) {
          if (!DRY_RUN) {
            db.MarkValidity(hard.id, false);
          }
          ctr_invalid++;
        }
        ctr_done++;

        status_per.RunIf([&]{
            status.Progress(ctr_done.Read(), hards.size(),
                            "{} invalid ({} nopoly, {} ill, {} noman)",
                            ctr_invalid.Read(),
                            ctr_no_poly.Read(),
                            ctr_ill_conditioned.Read(),
                            ctr_not_manifold.Read());
          });
      },
      max_threads);

  Print("Took {}.\n"
        "Final result:\n"
        "{} invalid ({} nopoly, {} ill, {} noman)",
        ANSI::Time(timer.Seconds()),
        ctr_invalid.Read(),
        ctr_no_poly.Read(),
        ctr_ill_conditioned.Read(),
        ctr_not_manifold.Read());
}


int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 1 || argc == 2) << "./validate.exe [id]";

  int id = 0;
  if (argc == 2) id = atoi(argv[1]);

  Validate(id);

  return 0;
}
