
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

#include "albrecht.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "netness.h"
#include "periodically.h"
#include "status-bar.h"

static constexpr bool DRY_RUN = false;

int main(int argc, char **argv) {
  ANSI::Init();

  StatusBar status(1);
  Periodically status_per(1);

  DB db;
  std::vector<DB::Hard> hards = db.AllHard(false);
  status.Print("Got {} hards.\n", hards.size());

  std::map<int, std::vector<DB::Hard*>> by_faces;

  for (int i = 0; i < (int)hards.size(); i++) {
    DB::Hard &h = hards[i];
    int nfaces = 0;
    if (auto opoly = PolyhedronFromConvexVertices(h.poly_points)) {
      nfaces = opoly->faces->NumFaces();
      CHECK(nfaces >= 4);
      by_faces[nfaces].push_back(&h);
      status_per.RunIf([&] {
          status.Progress(i + 1, hards.size(), "Grouping by faces");
        });
    }
  }

  struct Task {
    DB::Hard *h;
    bool needs_samples;
  };
  std::vector<Task> kept_tasks;
  int64_t deleted_count = 0;

  for (auto &[nfaces, list] : by_faces) {
    std::vector<DB::Hard*> zeroes;
    std::vector<DB::Hard*> nonzeros;

    for (DB::Hard *h : list) {
      if (h->netness_numer == 0) {
        zeroes.push_back(h);
      } else {
        nonzeros.push_back(h);
      }
    }

    // Sort nonzeros ascending by fraction (hardest first).
    std::sort(nonzeros.begin(), nonzeros.end(), [](DB::Hard *a, DB::Hard *b) {
      double frac_a = (double)a->netness_numer / a->netness_denom;
      double frac_b = (double)b->netness_numer / b->netness_denom;
      if (frac_a != frac_b) return frac_a < frac_b;
      return a->id < b->id;
    });

    for (DB::Hard *h : zeroes) {
      kept_tasks.push_back({h, true});
    }

    int nonzeros_count = nonzeros.size();
    int top_10th_limit = std::ceil(nonzeros_count * 0.10);

    for (int i = 0; i < nonzeros_count; i++) {
      DB::Hard *h = nonzeros[i];
      if (i < 100) {
        bool is_top_10th = (i < top_10th_limit);
        bool needs = is_top_10th || !h->example_net.has_value();
        kept_tasks.push_back({h, needs});
      } else {
        if (!DRY_RUN) {
          db.DeleteHard(h->id);
        }
        deleted_count++;
      }
      status_per.RunIf([&] {
          status.Progress(i, nonzeros_count, "Deleted {}", deleted_count);
        });
    }
  }

  int samples_run = 0;
  for (size_t i = 0; i < kept_tasks.size(); i++) {
    Task &task = kept_tasks[i];
    status_per.RunIf([&] {
      status.Progress(i, kept_tasks.size(), "Sampling kept instances");
    });

    if (!task.needs_samples) continue;

    auto opoly = PolyhedronFromConvexVertices(task.h->poly_points);
    if (!opoly) continue;

    Albrecht::AugmentedPoly aug(*opoly);

    int num_samples = 1 << 17;
    Netness::NetnessResult res = Netness::ComputeWithExample(task.h->id, aug, num_samples);

    task.h->netness_numer += res.numer;
    task.h->netness_denom += res.denom;

    if (!task.h->example_net.has_value() && res.example.has_value()) {
      task.h->example_net = res.example;
    }

    if (!DRY_RUN) {
      db.UpdateHard(task.h->id, task.h->netness_numer, task.h->netness_denom,
                    task.h->example_net);
    }
    samples_run++;
  }

  status.Print("Vaccuming...\n");
  db.ExecuteAndPrint("vacuum");

  status.Remove();
  Print("Done! Kept {} instances. Deleted {} instances. Ran samples for {}.\n",
        kept_tasks.size(), deleted_count, samples_run);

  return 0;
}

