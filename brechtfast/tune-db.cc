
#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <map>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "albrecht.h"
#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "netness.h"
#include "periodically.h"
#include "solve-leaf.h"
#include "status-bar.h"

static constexpr bool DRY_RUN = false;

using NetnessResult = Netness::NetnessResult;

static NetnessResult
Resolve(ArcFour *rc, const DB::Hard &hard, int num_samples) {
  auto opoly = PolyhedronFromConvexVertices(hard.poly_points);
  CHECK(opoly.has_value());

  Albrecht::AugmentedPoly aug(std::move(opoly.value()));

  const int MAX_REPS = 8;

  if (std::holds_alternative<DB::Any>(hard.why)) {
    return Netness::ComputeWithExample(hard.id, aug, num_samples, MAX_REPS);

  } else if (const DB::LeafIH *lih = std::get_if<DB::LeafIH>(&hard.why)) {
    NetnessResult res;

    for (int reps = 0; reps < MAX_REPS && res.numer == 0; reps++) {
      for (int i = 0; i < num_samples; i++) {
        BitString unfolding =
          SolveLeaf::SampleLeaf(rc, aug, lih->face_idx, lih->edge_idx);
        if (Albrecht::IsNet(aug, unfolding)) {
          res.numer++;
          if (!res.example.has_value()) {
            res.example = {std::move(unfolding)};
          }
        }
      }
      res.denom += num_samples;
    }

    return res;
  } else {
    LOG(FATAL) << "Unsupported why-type!";
  }
}

static void Tune() {
  StatusBar status(1);
  Periodically status_per(1);

  DB db;
  std::vector<DB::Hard> hards = db.AllHard(false);
  status.Print("Got {} hards.\n", hards.size());

  std::map<int, std::vector<DB::Hard*>> any_by_faces;

  // These are currently rare, so we just keep all of them.
  std::vector<DB::Hard *> other;

  for (int i = 0; i < (int)hards.size(); i++) {
    DB::Hard &h = hards[i];

    // XXX: This needs to be updated to filter for other
    // types of hardness.
    if (!std::holds_alternative<DB::Any>(h.why)) {
      other.push_back(&h);
      continue;
    }

    int nfaces = 0;
    if (auto opoly = PolyhedronFromConvexVertices(h.poly_points)) {
      nfaces = opoly->faces->NumFaces();
      CHECK(nfaces >= 4);
      any_by_faces[nfaces].push_back(&h);
      status_per.RunIf([&] {
          status.Progress(i + 1, hards.size(), "Grouping by faces");
        });
    }
  }

  struct Task {
    DB::Hard *h = nullptr;
    bool needs_samples = false;
  };
  std::vector<Task> kept_tasks;
  int64_t deleted_count = 0;

  for (DB::Hard *h : other) {
    if (h->netness_numer == 0 ||
        !h->example_net.has_value()) {
      kept_tasks.emplace_back(Task{
          .h = h,
          .needs_samples = true,
        });
    }
  }

  for (auto &[nfaces, list] : any_by_faces) {
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

  ArcFour rc(std::format("tune-db.{}", time(nullptr)));

  int samples_run = 0;
  for (size_t i = 0; i < kept_tasks.size(); i++) {
    Task &task = kept_tasks[i];
    status_per.RunIf([&] {
      status.Progress(i, kept_tasks.size(), "Sampling kept instances");
    });

    if (!task.needs_samples) continue;

    const int num_samples = 1 << 18;
    Netness::NetnessResult res = Resolve(&rc, *task.h, num_samples);

    task.h->netness_numer += res.numer;
    task.h->netness_denom += res.denom;

    if (!task.h->example_net.has_value() && res.example.has_value()) {
      status.Print("Solved #" ACYAN("{}") " ({}) for the first time!\n",
                   task.h->id, DB::WhyString(task.h->why));
      task.h->example_net = res.example;
    } else if (!task.h->example_net.has_value() &&
               task.h->netness_numer == 0) {
      status.Print("#" AORANGE("{}") " ({}) still has no solution\n",
                   task.h->id, DB::WhyString(task.h->why));
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
}


int main(int argc, char **argv) {
  ANSI::Init();

  Tune();

  return 0;
}
