
#include "albrecht.h"

#include <algorithm>
#include <ctime>
#include <optional>
#include <variant>
#include <format>
#include <mutex>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "solve-vertex.h"
#include "status-bar.h"
#include "threadutil.h"

constexpr int START_ID = 0;
constexpr int TARGET_WHY = DB::WHY_VERTEX_IH;
// Number of samples (for a particular constraint) before
// we consider the instance to be hard.
constexpr int HARD_THRESHOLD = 10'000'000;
constexpr int NUM_THREADS = 8;

DECLARE_COUNTERS(ctr_skipped, ctr_fully_solved, ctr_new_hard);

static void ComputeCross() {
  DB db;
  std::vector<DB::Hard> hards = db.AllHard();

  std::mutex m;
  StatusBar status(1);
  Periodically status_per(1.0);

  int max_done = 0;

  ParallelComp(hards.size(), [&](int i) {
    const DB::Hard &hard = hards[i];

    if (hard.id < START_ID ||
        DB::WhyType(hard.why) == TARGET_WHY) {
      ctr_skipped++;
      return;
    }

    std::optional<Polyhedron> opoly =
        PolyhedronFromConvexVertices(hard.poly_points);
    if (!opoly.has_value()) return;

    ArcFour rc(std::format("cross.{}.{}", hard.id, time(nullptr)));
    const Polyhedron &poly = opoly.value();
    Albrecht::AugmentedPoly aug(poly);

    switch (TARGET_WHY) {
    case DB::WHY_VERTEX_IH: {
      int nets = 0;
      const int num_verts = aug.poly.faces->NumVertices();
      for (int vertex_idx = 0; vertex_idx < num_verts; vertex_idx++) {
        bool found_net = false;

        for (int sample = 0; sample < HARD_THRESHOLD; sample++) {
          BitString unfolding =
            SolveVertex::SampleVertex(&rc, aug, vertex_idx);
          if (Albrecht::IsNet(aug, unfolding)) {
            found_net = true;
            nets++;
            break;
          }
        }

        if (!found_net) {
          std::lock_guard<std::mutex> lock(m);
          db.AddHard(poly, DB::VertexIH{.vertex_idx = vertex_idx},
                     hard.method, 0, 0, std::nullopt);
          ctr_new_hard++;
          // Don't insert the same shape multiple times for different
          // constraints.
          break;
        }
      }

      if (nets == num_verts) {
        ctr_fully_solved++;
      }

      break;
    }
    default:
      LOG(FATAL) << "Unsupported TARGET_WHY: " << TARGET_WHY;
    }

    {
      MutexLock ml(&m);
      max_done = std::max(i, max_done);

      status_per.RunIf([&]{
          status.Progress(max_done, hards.size(),
                          "skipped {} solved {} new {}",
                          ctr_skipped.Read(),
                          ctr_fully_solved.Read(),
                          ctr_new_hard.Read());
        });
    }

    },
  NUM_THREADS);
}


int main(int argc, char **argv) {
  ANSI::Init();

  ComputeCross();

  return 0;
}
