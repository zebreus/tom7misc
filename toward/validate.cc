
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

static bool IsValidOutput(const ValidationSample &sample,
                          std::span<const ChuteValue> actual) {
  for (const auto &valid : sample.valid_outputs) {
    CHECK(valid.size() == actual.size());
    for (size_t out_idx = 0; out_idx < valid.size(); out_idx++) {
      if (valid[out_idx] != ChuteValue::GARBAGE &&
          actual[out_idx] != valid[out_idx]) {
        goto next;
      }
    }

    return true;

  next:;
  }

  return false;
}

static void Validate(const ValidationInstance &inst) {
  static constexpr int NUM_TRIALS = 10000;
  ArcFour rc("validate");
  std::unique_ptr<Level> base_level = Levels::LoadSVG(inst.Filename());
  CHECK(base_level.get() != nullptr) << inst.Filename();

  CHECK(base_level->inputs.size() == (size_t)inst.ExpectedInputs() &&
        base_level->outputs.size() == (size_t)inst.ExpectedOutputs());

  if (inst.AddInputWalls()) {
    // Add vertical walls on the sides of the inputs. Perhaps these
    // should be modeled in the level?
    for (const vec2f inpos : base_level->inputs) {
      int blockheight = Levels::IN_HEIGHT;
      for (float s : { -1.0f, +1.0f }) {
        vec2f pos = {
          .x = inpos.x + s * (Levels::IN_WIDTH * Levels::BLOCK_SIZE * 0.5f +
                              // wall itself
                              Levels::BLOCK_SIZE * 0.5f),
          .y = inpos.y,
        };
        LevelBody wall = Levels::WallRect(pos, 1, blockheight);
        wall.color = 0x888888FF;
        base_level->bodies.push_back(std::move(wall));
      }
    }
  }

  // We need to stop objects from leaving the bottom of the output cup.
  // The levels have rails modeled on the left and right sides, but
  // we add an artificial bottom piece during validation.
  for (const vec2f outpos : base_level->outputs) {
    int blockwidth = Levels::OUT_WIDTH + 2;
    vec2f pos = {
      .x = outpos.x,
      .y = outpos.y + (Levels::OUT_HEIGHT * Levels::BLOCK_SIZE) / 2.0f +
      Levels::BLOCK_SIZE / 2.0f
    };
    LevelBody cup_bottom = Levels::WallRect(pos, blockwidth, 1);
    cup_bottom.color = 0x888888FF;
    base_level->bodies.push_back(std::move(cup_bottom));
  }

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
        PCG32 pcg(base_seed + trial);
        ValidationSample sample = inst.OneSample(pcg.Rand64());
        Level level = *base_level;

        for (int i = 0; i < inst.ExpectedInputs(); i++) {
          ChuteValue v = sample.input_values[i];
          if (v == ChuteValue::NOTHING || v == ChuteValue::GARBAGE) {
            continue;
          }

          bool bit = (v == ChuteValue::ONE);

          LevelBody body = Validation::SampleInput(
              pcg.Rand64(), level.inputs[i], bit);

          level.bodies.emplace_back(std::move(body));
        }

        std::unique_ptr<Scene> scene = Levels::CreateScene(level);

        static constexpr int MAX_SIMULATE_STEPS = 2000;
        for (int step = 0; step < MAX_SIMULATE_STEPS; step++) {
          scene->Update();
          if (scene->AllAsleep()) break;
        }

        std::vector<ChuteValue> actual_outputs(inst.ExpectedOutputs(),
                                               ChuteValue::NOTHING);

        for (const Scene::Obj &obj : scene->objects) {
          if (!obj.user_data.has_value()) continue;
          const LevelBody &lbody = level.bodies[obj.user_data.value()];
          if (!lbody.dynamic) continue;

          ChuteValue type = ChuteValue::GARBAGE;
          if (lbody.item.has_value()) {
            type = (lbody.item.value() == LevelItem::ONE)
                       ? ChuteValue::ONE
                       : ChuteValue::ZERO;
          }

          b2Vec2 pos = b2Body_GetPosition(obj.body_id);

          constexpr float out_width = Levels::OUT_WIDTH * Levels::BLOCK_SIZE;
          constexpr float out_height = Levels::OUT_HEIGHT * Levels::BLOCK_SIZE;
          for (int out_idx = 0; out_idx < inst.ExpectedOutputs(); out_idx++) {
            float out_left = level.outputs[out_idx].x - out_width / 2.0f;
            float out_top = level.outputs[out_idx].y - out_height / 2.0f;

            if (pos.x >= out_left &&
                pos.x <= out_left + out_width &&
                pos.y >= out_top &&
                pos.y <= out_top + out_height) {
              if (actual_outputs[out_idx] == ChuteValue::NOTHING) {
                actual_outputs[out_idx] = type;
              } else {
                actual_outputs[out_idx] = ChuteValue::GARBAGE;
              }
            }
          }
        }

        bool correct = IsValidOutput(sample, actual_outputs);

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
