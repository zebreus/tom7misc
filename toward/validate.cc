
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
#include <string_view>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "box2d.h"
#include "level.h"
#include "pcg.h"
#include "periodically.h"
#include "rendering.h"
#include "scene.h"
#include "status-bar.h"
#include "threadutil.h"
#include "validation.h"

static void Validate(const ValidationInstance &inst) {
  static constexpr int NUM_TRIALS = 10000;
  ArcFour rc("validate");
  std::unique_ptr<Level> base_level = Validation::Load(inst);

  std::mutex m;
  int correct_count = 0, done = 0;
  uint64_t base_seed = rc.Word64();
  static constexpr int NUM_EVAL_THREADS = 16;
  Periodically status_per(1.0);
  StatusBar status(1);

  std::unique_ptr<Rendering> debug_rendering =
    CreateImageRendering("validate-debug");
  std::unique_ptr<Rendering> wrong_rendering =
    CreateImageRendering("validate-debug-wrong");


  ParallelComp(
      NUM_TRIALS,
      [&](int trial) {
        const auto &[sample, level] =
          Validation::LevelWithInputs(*base_level, inst, base_seed + trial);

        std::unique_ptr<Scene> scene = Levels::CreateScene(level);

        static constexpr int MAX_SIMULATE_STEPS = 2000;
        for (int step = 0; step < MAX_SIMULATE_STEPS; step++) {
          scene->Update();
          if (scene->AllAsleep()) break;
        }

        std::vector<ChuteValue> actual_outputs =
          Validation::ReadOutputs(inst, level, *scene);

        bool correct = Validation::IsValidOutput(sample, actual_outputs);

        if (!correct) {
          std::vector<Rendering::Triangle> tris = scene->GetTriangles();
          MutexLock ml(&m);
          wrong_rendering->RenderScene(
              vec2f{0.0f, 0.0f}, vec2f{Scene::WIDTH, Scene::HEIGHT}, tris);
        } else if (trial % 500 == 0) {
          std::vector<Rendering::Triangle> tris = scene->GetTriangles();
          MutexLock ml(&m);
          debug_rendering->RenderScene(
              vec2f{0.0f, 0.0f}, vec2f{Scene::WIDTH, Scene::HEIGHT}, tris);
        }

        {
          MutexLock ml(&m);
          if (correct) correct_count++;
          done++;
        }

        status_per.RunIf([&]{
            MutexLock ml(&m);
            status.Progress(done, NUM_TRIALS, "{}/{} correct = {:.2f}%",
                            correct_count, done,
                            (correct_count * 100.0) / done);
          });
      }, NUM_EVAL_THREADS);

  Print("Validation of {}: {} / {} correct = {:.2f}%\n",
        inst.Filename(),
        correct_count, NUM_TRIALS,
        (correct_count * 100.0) / done);
}

[[maybe_unused]]
static void ValidateAll() {
  Validate(*Validation::And());
  Validate(*Validation::Separator());
  Validate(*Validation::Not());
}

int main(int argc, char **argv) {
  ANSI::Init();

  Validate(*Validation::Not());

  return 0;
}
