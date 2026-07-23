
#include <atomic>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "cell-library.h"
#include "circuit.h"
#include "level.h"
#include "opt/opt-seq.h"
#include "periodically.h"
#include "prop.h"
#include "scene.h"
#include "span-util.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "validation.h"

static constexpr int TARGET_TRIALS = 10000;
static constexpr int NUM_EVAL_THREADS = 16;

// The max distance we move a point during optimization (in blocks).
static constexpr int BLOCK_RADIUS = 8;

static void DoTweak(const ValidationInstance &inst,
                    std::string_view output_filename) {
  AutoHisto histo(10000);

  std::unique_ptr<Level> base_level = Validation::Load(inst);
  std::vector<std::pair<int, int>> target_vertices;

  for (size_t b_idx = 0; b_idx < base_level->bodies.size(); b_idx++) {
    const LevelBody &body = base_level->bodies[b_idx];
    if (body.color == 0x000000FF) {
      for (size_t v_idx = 0; v_idx < body.mesh.vertices.size(); v_idx++) {
        target_vertices.push_back({b_idx, v_idx});
      }
    }
  }

  StatusBar status(2);
  if (target_vertices.empty()) {
    status.Print("No black shapes (0x000000FF) found to optimize.\n");
    return;
  }

  Periodically status_per(1);
  Periodically flush_per(60);
  Timer timer;
  int ctr_total_evals = 0;

  double best_penalty = 1e9;
  Level all_time_best_level = *base_level;
  bool best_dirty = false;
  static constexpr int MAX_ITERS_PER_ROUND = 1000;

  for (;;) {
    std::vector<std::pair<double, double>> bounds;
    double r = BLOCK_RADIUS * Levels::BLOCK_SIZE;
    for (size_t i = 0; i < target_vertices.size(); i++) {
      bounds.push_back({-r, r});
      bounds.push_back({-r, r});
    }

    status.Print("Starting optimization round...\n");
    OptSeq seq(bounds);

    for (int iter = 0; iter < MAX_ITERS_PER_ROUND; iter++) {
      std::vector<double> arg = seq.Next();

      Level level = *base_level;
      for (size_t i = 0; i < target_vertices.size(); i++) {
        int b_idx = target_vertices[i].first;
        int v_idx = target_vertices[i].second;
        level.bodies[b_idx].mesh.vertices[v_idx].x += arg[i * 2];
        level.bodies[b_idx].mesh.vertices[v_idx].y += arg[i * 2 + 1];
      }

      std::atomic<int> min_failure{TARGET_TRIALS};
      std::atomic<int> success_count{0};

      ParallelComp(TARGET_TRIALS, [&](int trial) {
          if (trial >= min_failure.load(std::memory_order_relaxed)) return;

          const auto &[sample, eval_level] =
            Validation::LevelWithInputs(level, inst, trial);

          std::unique_ptr<Scene> scene = Levels::CreateScene(eval_level);
          static constexpr int MAX_SIMULATE_STEPS = 2000;
          for (int step = 0; step < MAX_SIMULATE_STEPS; step++) {
            scene->Update();
            if (scene->AllAsleep()) break;
          }

          std::vector<ChuteValue> actual_outputs =
            Validation::ReadOutputs(inst, eval_level, *scene);

          if (!Validation::IsValidOutput(sample, actual_outputs)) {
            int current_min = min_failure.load(std::memory_order_relaxed);
            while (trial < current_min &&
                   !min_failure.compare_exchange_weak(
                       current_min, trial, std::memory_order_relaxed)) {
            }
          } else {
            success_count.fetch_add(1, std::memory_order_relaxed);
          }
        }, NUM_EVAL_THREADS);

      int fail_idx = min_failure.load();
      int s_count = success_count.load();
      double penalty = -fail_idx - 0.01 * (s_count - fail_idx);

      seq.Result(penalty);
      ctr_total_evals++;

      if (penalty < best_penalty) {
        best_penalty = penalty;
        all_time_best_level = level;
        best_dirty = true;
        status.Print("Iteration {}: New best {} contiguous ({} total); "
                     "penalty: {:.2f}\n",
                     iter, fail_idx, s_count, best_penalty);
      }

      if (best_dirty && (flush_per.ShouldRun() || fail_idx == TARGET_TRIALS)) {
        Levels::SaveSVG(all_time_best_level, output_filename);
        status.Print("Wrote " AGREEN("{}") "\n", output_filename);
        best_dirty = false;
      }

      if (fail_idx == TARGET_TRIALS) {
        status.Print(AGREEN("Success! Reached TARGET_TRIALS.") "\n");
        return;
      }

      status_per.RunIf([&]{
          int64_t total = ctr_total_evals;
          double each = timer.Seconds() / total;
          status.Status("{}\n"
                        "{} iters ({} total), {:.2f} best penalty, {} ea.",
                        histo.OneLineANSI(75),
                        iter, total, best_penalty, ANSI::Time(each));
      });
    }

    *base_level = all_time_best_level;
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  std::unique_ptr<ValidationInstance> vi =
    Validation::ValidateCell(
        library,
        Cell(NOT0, 0, false),
        Span{Prop{Var{.id = 0}}});

  DoTweak(*vi, "not-tweaked.svg");

  // DoTweak(*Validation::Not(), "not-tweaked.svg");

  return 0;
}
