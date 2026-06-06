
#include <ctime>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
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
#include "solve-vertex.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"

DECLARE_COUNTERS(ctr_attempts, ctr_solved, ctr_invalid);

using Hard = DB::Hard;

static constexpr int NUM_THREADS = 8;
static constexpr int MIN_DENOM = 1'000'000;

struct Grinder {

  Grinder() {}

  DB db;

  StatusBar status = StatusBar(1);
  Periodically status_per = Periodically(1);

  Timer run_timer;
  Periodically refresh_per = Periodically(120);

  std::mutex m;
  bool started = false;
  // Could also have a lower-priority set?
  std::deque<std::shared_ptr<Hard>> todo;
  std::unordered_set<int> in_progress;

  void Run() {
    ParallelFan(
        NUM_THREADS,
        [&](int thread_id) {
          RunOne(thread_id);
        });
  }

  void RunOne(int thread_id) {
    ArcFour rc(std::format("grind.{}.{}", time(nullptr), thread_id));
    for (;;) {
      refresh_per.RunIf([&]{
          std::vector<DB::Hard> hards = db.AllHard(false);
          status.Print("Refresh: Got {} hards.\n", hards.size());

          MutexLock ml(&m);
          todo.clear();
          for (Hard &h : hards) {
            if (h.netness_denom < MIN_DENOM ||
                h.netness_numer == 0 ||
                !h.example_net.has_value()) {
              // If some thread is working on it, don't reinsert.
              if (!in_progress.contains(h.id)) {
                todo.emplace_back(std::make_shared<Hard>(std::move(h)));
              }
            }
          }
          hards.clear();
          started = true;
          status.Print("Refresh: To-do size is now {}+{}\n",
                       todo.size(), in_progress.size());
        });

    std::shared_ptr<Hard> next = [&] -> std::shared_ptr<Hard> {
        MutexLock ml(&m);
        if (todo.empty()) {
          return {nullptr};
        }

        std::shared_ptr<Hard> next = std::move(todo.front());
        todo.pop_front();
        in_progress.insert(next->id);
        return next;
      }();

    if (next.get() == nullptr) {
      bool s = ReadWithLock(&m, &started);
      using namespace std::chrono_literals;
      // Wait, unless we just started up and we're waiting for
      // the first batch.
      if (s) {
        status.Print("Nothing to do.\n");
        std::this_thread::sleep_for(180s);
      } else {
        std::this_thread::sleep_for(1s);
      }
      continue;
    }

    auto opoly = PolyhedronFromConvexVertices(next->poly_points);
    if (!opoly) {
      db.MarkValidity(next->id, false);
      ctr_invalid++;
      Print(ARED("{}") " was invalid.\n", next->id);
      continue;
    }

    Albrecht::AugmentedPoly aug(*opoly);

    bool solved = false;
    bool was_unsolved = next->netness_numer == 0 ||
      !next->example_net.has_value();

    if (std::holds_alternative<DB::Any>(next->why)) {
      int num_samples = 1 << 20;
      Netness::NetnessResult res =
        Netness::ComputeWithExample(next->id, aug, num_samples);

      solved = res.numer > 0 && res.example.has_value();

      next->netness_numer += res.numer;
      next->netness_denom += res.denom;

      if (!next->example_net.has_value() && res.example.has_value()) {
        next->example_net = res.example;
      }

      ctr_attempts++;
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

      if (!next->example_net.has_value() && example.has_value()) {
        next->example_net = example;
      }

      ctr_attempts++;
      db.UpdateHard(next->id, next->netness_numer, next->netness_denom,
                    next->example_net);

    } else if (const DB::VertexIH *vertex_ih =
               std::get_if<DB::VertexIH>(&next->why)) {
      const int num_samples = 1 << 20;
      const int max_reps = 8;

      int numer = 0;
      int denom = 0;
      std::optional<BitString> example;

      for (int reps = 0; reps < max_reps && numer == 0; reps++) {
        for (int i = 0; i < num_samples; i++) {
          BitString unfolding = SolveVertex::SampleVertex(
              &rc, aug, vertex_ih->vertex_idx);
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

      if (!next->example_net.has_value() && example.has_value()) {
        next->example_net = example;
      }

      ctr_attempts++;
      db.UpdateHard(next->id, next->netness_numer, next->netness_denom,
                    next->example_net);
    }

    // Only announce if the solution is new, not simply that we are
    // increasing the denominator past the threshold.
    if (was_unsolved && solved) {
      status.Print("Solved #" AGREEN("{}") "! ({}) " AGREY("{}/{}") "\n",
                   next->id, DB::WhyString(next->why),
                   next->netness_numer, next->netness_denom);
      ctr_solved++;
    }

    // Always release, and re-enqueue if we didn't succeed.
    {
      MutexLock ml(&m);
      in_progress.erase(next->id);
      if (!solved) {
        todo.emplace_back(std::move(next));
      }
    }

    status_per.RunIf([&]{
        MutexLock ml(&m);
        status.Status("TO" "DO: " AYELLOW("{}") "+" AORANGE("{}")
                      ". Solved " AGREEN("{}")
                      ". Invalidated " ARED("{}")
                      ". " ACYAN("{}") " attempts in {}\n",
                      todo.size(), in_progress.size(),
                      ctr_solved.Read(),
                      ctr_invalid.Read(),
                      ctr_attempts.Read(),
                      ANSI::Time(run_timer.Seconds()));
      });
    }
  }

};

int main(int argc, char **argv) {
  ANSI::Init();

  {
    Grinder grinder;
    grinder.Run();
  }

  Print("Exited?\n");

  return 0;
}

