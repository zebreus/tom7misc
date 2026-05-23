
#include <ctime>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "albrecht.h"
#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/logging.h"
#include "base/print.h"
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "netness.h"
#include "periodically.h"
#include "solve-leaf.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"

DECLARE_COUNTERS(ctr_solved, ctr_invalid);

using Hard = DB::Hard;

int main(int argc, char **argv) {
  ANSI::Init();

  DB db;

  StatusBar status(1);
  Periodically status_per(1);

  Timer run_timer;
  Periodically refresh_per(120);

  std::mutex m;
  ArcFour rc(std::format("grind-{}", time(nullptr)));
  // Could also have a lower-priority set?
  std::deque<std::shared_ptr<Hard>> todo;

  for (;;) {

    refresh_per.RunIf([&]{
        std::vector<DB::Hard> hards = db.AllHard(false);
        status.Print("Refresh: Got {} hards.\n", hards.size());

        MutexLock ml(&m);
        todo.clear();
        for (Hard &h : hards) {
          if (h.netness_numer == 0 ||
              !h.example_net.has_value()) {
            todo.emplace_back(std::make_shared<Hard>(std::move(h)));
          }
        }
        hards.clear();

        status.Print("Refresh: To-do size is now {}\n", todo.size());
      });

    std::shared_ptr<Hard> next = [&] -> std::shared_ptr<Hard> {
        MutexLock ml(&m);
        if (todo.empty()) {
          using namespace std::chrono_literals;
          status.Print("Nothing to do.\n");
          std::this_thread::sleep_for(180s);
          return {nullptr};
        }

        std::shared_ptr<Hard> next = std::move(todo.front());
        todo.pop_front();
        return next;
      }();

    if (next.get() == nullptr)
      continue;

    auto opoly = PolyhedronFromConvexVertices(next->poly_points);
    if (!opoly) {
      db.MarkValidity(next->id, false);
      ctr_invalid++;
      Print(ARED("{}") " was invalid.\n", next->id);
      continue;
    }

    Albrecht::AugmentedPoly aug(*opoly);

    bool solved = false;

    if (std::holds_alternative<DB::Any>(next->why)) {
      int num_samples = 1 << 20;
      Netness::NetnessResult res =
        Netness::ComputeWithExample(next->id, aug, num_samples);

      solved = res.numer > 0 && res.example.has_value();

      next->netness_numer += res.numer;
      next->netness_denom += res.denom;

      if (solved) {
        status.Print("Solved #" AGREEN("{}") "! {}/{}\n", next->id,
                     next->netness_numer, next->netness_denom);
        ctr_solved++;
      }

      if (!next->example_net.has_value() && res.example.has_value()) {
        next->example_net = res.example;
      }

      db.UpdateHard(next->id, next->netness_numer, next->netness_denom,
                    next->example_net);
    } else if (const DB::LeafIH *leaf_ih =
               std::get_if<DB::LeafIH>(&next->why)) {
      const int num_samples = 1 << 20;
      const int max_reps = 8;

      int numer = 0;
      int denom = 0;
      std::optional<BitString> example;

      for (int reps = 0; reps < max_reps && numer == 0; reps++) {
        for (int i = 0; i < num_samples; i++) {
          BitString unfolding = SolveLeaf::SampleLeaf(
              &rc, aug, leaf_ih->face_idx, leaf_ih->edge_idx);
          if (Albrecht::IsNet(aug, unfolding)) {
            numer++;
            if (!example.has_value()) {
              example = {std::move(unfolding)};
            }
          }
        }
        denom += num_samples;
      }

      solved = numer > 0 && example.has_value();

      next->netness_numer += numer;
      next->netness_denom += denom;

      if (solved) {
        status.Print("Solved #" AGREEN("{}") "! {}/{}\n", next->id,
                     next->netness_numer, next->netness_denom);
        ctr_solved++;
      }

      if (!next->example_net.has_value() && example.has_value()) {
        next->example_net = example;
      }

      db.UpdateHard(next->id, next->netness_numer, next->netness_denom,
                    next->example_net);
    }

    // Re-enqueue if we didn't succeed.
    if (!solved) {
      todo.emplace_back(std::move(next));
    }

    status_per.RunIf([&]{
        status.Status("TO" "DO: " AYELLOW("{}")
                      ". Solved " AGREEN("{}")
                      ". Invalidated " ARED("{}")
                      ". Running for {}\n",
                      todo.size(),
                      ctr_solved.Read(),
                      ctr_invalid.Read(),
                      ANSI::Time(run_timer.Seconds()));
      });
  }

  status.Print("Exited?\n");

  return 0;
}

