
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "level.h"
#include "periodically.h"
#include "prop.h"
#include "rendering.h"
#include "scene.h"
#include "status-bar.h"
#include "threadutil.h"
#include "validation.h"
#include "util.h"

// TODO: We should really test the possibility that one input
// arrives way before the other. Initial velocities mostly
// cover the cases of interference coincidences, I think?

static void RemoveValidationImages() {
  // So lifetime is simple for async.
  std::vector<std::string> files = Util::ListFiles(".");
  int deleted = 0;
  {
    Asynchronously async(8);
    for (const std::string &f : files) {
      if (Util::MatchesWildcard("validate-*.png", f)) {
        deleted++;
        async.Run([&]{ (void)Util::RemoveFile(f); });
      }
    }
  }
  Print("Deleted {} image files.\n", deleted);
}

static bool Validate(const ValidationInstance &inst) {
  RemoveValidationImages();

  static constexpr int NUM_TRIALS = 10000;
  ArcFour rc("validate");
  std::unique_ptr<Level> base_level = Validation::Load(inst);

  std::mutex m;
  int correct_count = 0, done = 0;
  int images_left = 100;
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

        bool write_images = false;
        {
          MutexLock ml(&m);
          if (images_left > 0) write_images = true;
        }

        if (write_images) {
          if (!correct) {
            std::vector<Rendering::Triangle> tris = scene->GetTriangles();
            MutexLock ml(&m);
            wrong_rendering->RenderScene(
                vec2f{0.0f, 0.0f}, vec2f{Scene::WIDTH, Scene::HEIGHT}, tris);
            images_left--;
          } else if (trial % 500 == 0) {
            std::vector<Rendering::Triangle> tris = scene->GetTriangles();
            MutexLock ml(&m);
            debug_rendering->RenderScene(
                vec2f{0.0f, 0.0f}, vec2f{Scene::WIDTH, Scene::HEIGHT}, tris);
            images_left--;
          }
        }

        {
          MutexLock ml(&m);
          if (correct) correct_count++;
          done++;
        }

        status_per.RunIf([&]{
            MutexLock ml(&m);
            status.Progress(done, NUM_TRIALS,
                            "[" AYELLOW("{}") "]"
                            " {}/{} correct = {:.2f}%",
                            inst.Name(),
                            correct_count, done,
                            (correct_count * 100.0) / done);
          });
      }, NUM_EVAL_THREADS);

  Print("Validation of {}: {} / {} correct = {:.2f}%\n",
        inst.Name(),
        correct_count, NUM_TRIALS,
        (correct_count * 100.0) / done);

  return correct_count == NUM_TRIALS;
}

[[maybe_unused]]
static void ValidateAll() {
  CHECK(Validate(*Validation::And()));
  CHECK(Validate(*Validation::Separator()));
  CHECK(Validate(*Validation::Not()));
  CHECK(Validate(*Validation::DupSep()));
  CHECK(Validate(*Validation::SepXchg()));
  CHECK(Validate(*Validation::Sep00Xchg()));
  CHECK(Validate(*Validation::Sep01Xchg()));
  CHECK(Validate(*Validation::Sep10Xchg()));
  CHECK(Validate(*Validation::Sep11Xchg()));
}

static void ValidateWires() {
  CHECK(Validate(*Validation::WireA0()));
  CHECK(Validate(*Validation::WireAN1()));
  CHECK(Validate(*Validation::WireAN2()));
  CHECK(Validate(*Validation::WireAN4()));
  CHECK(Validate(*Validation::WireAN8()));
  CHECK(Validate(*Validation::WireAN16()));
  CHECK(Validate(*Validation::WireAN32()));
}

static void ValidateLibrary() {
  CellLibrary library;

  // SPACER,
  // WIREA,
  // WIREB,

  for (Gate g : {
      Gate::CONST0,
      Gate::CONST1,
      Gate::SELFXCHG01,
      Gate::SELFXCHG10,
      Gate::XCHG00,
      Gate::XCHG01,
      Gate::XCHG10,
      Gate::XCHG11,
      Gate::DUPSEP0011,
      Gate::SINK,
      Gate::NOT01,
      Gate::AND0110,
      Gate::NOT,
      Gate::SEPARATOR,
    }) {
    for (bool f : {false, true}) {
      // XXX skip known problematic
      // if (f && g == Gate::AND0110) continue;
      if (f) continue;
      if (g == Gate::NOT) continue;

      Cell cell{.gate = g, .flip = f};
      std::vector<Prop> args;
      CellLibrary::Info info = library.GetInfo(cell);
      if (g == Gate::SELFXCHG01 ||
          g == Gate::SELFXCHG10) {
        CHECK(info.inputs.size() == 2);
        args = {Prop{Var{.id = 0}}, Prop{Var{.id = 0}}};

      } else if (g == Gate::NOT01) {
        CHECK(info.inputs.size() == 2);
        args = {Prop{Var{.id = 0}}, Prop{Var{.id = 0}}};

      } else if (g == Gate::AND0110) {
        CHECK(info.inputs.size() == 4);
        args = {
          Prop{Var{.id = 0}},
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
          Prop{Var{.id = 1}},
        };

      } else {
        // Unconstrained.
        for (int i = 0; i < info.inputs.size(); i++) {
          args.push_back(Prop{Var{.id = i}});
        }
      }

      std::unique_ptr<ValidationInstance> cv =
        Validation::ValidateCell(library, cell, args);
      CHECK(Validate(*cv)) << cv->Name();
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();


  // Validate(*Validation::SepXchg());
  // Validate(*Validation::Sep11Xchg());

  // CHECK(Validate(*Validation::WireAN32()));

  ValidateLibrary();

  return 0;
}
