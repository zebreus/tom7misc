
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
#include "util.h"
#include "validation.h"
#include "vector-util.h"

// TODO: We should really test the possibility that one input
// arrives way before the other. Initial velocities mostly
// cover the cases of interference coincidences, I think?

static void RemoveValidationImages() {
  // So lifetime is simple for async.
  std::vector<std::string> files = Util::ListFiles(".");
  [[maybe_unused]] int deleted = 0;
  {
    Asynchronously async(8);
    for (const std::string &f : files) {
      if (Util::MatchesWildcard("validate-*.png", f)) {
        deleted++;
        async.Run([&]{ (void)Util::RemoveFile(f); });
      }
    }
  }
  // Print("Deleted {} image files.\n", deleted);
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
                            " {}/{} correct = {}{:.2f}%",
                            inst.Name(),
                            correct_count, done,
                            correct_count == done ?
                            ANSI_GREEN : ANSI_RED,
                            (correct_count * 100.0) / done);
          });
      }, NUM_EVAL_THREADS);

  status.Status("Done.");
  status.Remove();


  std::string_view cc = correct_count == NUM_TRIALS ? ANSI_GREEN : ANSI_RED;
  Print("Validation of " ABLUE("{}") ": {} / {} correct = "
        "{}{:.2f}%" ANSI_RESET "\n",
        inst.Name(),
        correct_count, NUM_TRIALS,
        cc,
        (correct_count * 100.0) / done);

  return correct_count == NUM_TRIALS;
}

static void ValidateWires(const CellLibrary &library) {
  std::vector<int> sizes = CellLibrary::WIRE_SIZES;
  VectorReverse(&sizes);

  for (int v : sizes) {
    std::vector<CellLibrary::Bias> biases = {
      CellLibrary::Bias::RIGHT,
    };

    if (v < CellLibrary::SMALL_WIRE)
      biases.push_back(CellLibrary::Bias::LEFT);

    for (CellLibrary::Bias bias : biases) {
      for (bool f : {false, true}) {
        // The shapes are the same, so we used mixed
        // to just get a combination of both glyphs.
        Cell cell = CellLibrary::Wire(v, bias, CType::MIXED);
        cell.flip = f;

        // All wires take one input.
        std::vector<Prop> args = {Prop{Var{.id = 0}}};

        std::unique_ptr<ValidationInstance> cv =
          Validation::ValidateCell(library, cell, args);
        CHECK(Validate(*cv)) << cv->Name();
      }
    }
  }
}

static void ValidateCells(const CellLibrary &library) {
  // Nothing to validate:
  // SPACER,

  // Not used; problematic:
  // Gate::NOT,

  for (Gate g : {
      Gate::ITE10,
      Gate::NOR1100,
      //      Gate::XOR1100,
      // Gate::XOR1010,
      Gate::NAND0011,
      Gate::XCHG00,
      Gate::XCHG01,
      Gate::XCHG10,
      Gate::XCHG11,
      Gate::SELFXCHG01,
      Gate::SELFXCHG10,
      Gate::NOT01,
      Gate::AND0110,
      Gate::NOT0,
      Gate::NOT1,
      Gate::OR1100,
      Gate::SEPARATOR01,
      Gate::SEPARATOR10,
      Gate::DUP1,
      Gate::DUP0,
      Gate::CONST0,
      Gate::CONST1,
      Gate::DUPSEP0011,
      Gate::SINK,
    }) {
    for (bool f : {false, true}) {
      Cell cell(g, 0, f);
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

      } else if (g == Gate::NAND0011) {
        CHECK(info.inputs.size() == 4);
        args = {
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
        };

      } else if (g == Gate::XOR1010) {
        CHECK(info.inputs.size() == 4);
        args = {
          Prop{Var{.id = 0}},
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
          Prop{Var{.id = 1}},
        };

      } else if (g == Gate::XOR1100) {
        CHECK(info.inputs.size() == 4);
        args = {
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
        };

      } else if (g == Gate::OR1100 || g == Gate::NOR1100) {
        CHECK(info.inputs.size() == 4);
        args = {
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
        };

      } else if (g == Gate::ITE10) {
        CHECK(info.inputs.size() == 4);
        args = {
          Prop{Var{.id = 0}},
          Prop{Var{.id = 1}},
          Prop{Var{.id = 1}},
          Prop{Var{.id = 2}},
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

static void ValidateLibrary() {
  CellLibrary library;

  ValidateCells(library);
  ValidateWires(library);
}

int main(int argc, char **argv) {
  ANSI::Init();

  ValidateLibrary();

  return 0;
}
