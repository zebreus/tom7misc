
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
#include "randutil.h"
#include "scene.h"
#include "status-bar.h"
#include "threadutil.h"

// A validation instance is a spec with an implementation.
// The spec consists of:
//   - A way of sampling inputs
//   - The expected output

// A chute can have nothing, a zero or one bit, or garbage.
enum class ChuteValue {
  NOTHING,
  ZERO,
  ONE,
  GARBAGE,
};

struct ValidationSample {
  // Must match expected_inputs.
  std::vector<ChuteValue> input_values;

  // The expected output. There is typically one, but it is allowed
  // for there to be more than one valid output, e.g. we might
  // allow one chute to contain garbage conditional on another's
  // value.
  std::vector<std::vector<ChuteValue>> valid_outputs;
};

// For separated inputs, we have either the given bit, or nothing.
// SeparatedZero gives the contents of the "zero" chute when the
// bit value is b.
static ChuteValue SeparatedZero(bool b) {
  return b ? ChuteValue::NOTHING : ChuteValue::ZERO;
}
static ChuteValue SeparatedOne(bool b) {
  return b ? ChuteValue::ONE : ChuteValue::NOTHING;
}

struct ValidationInstance {
  virtual std::string_view Filename() const = 0;
  virtual int ExpectedInputs() const = 0;
  virtual int ExpectedOutputs() const = 0;

  virtual ValidationSample OneSample(uint64_t seed) const = 0;
  virtual ~ValidationInstance() {}
};


struct AndValidation : public ValidationInstance {
  std::string_view Filename() const override { return "and3.svg"; }
  int ExpectedInputs() const override { return 4; }
  int ExpectedOutputs() const override { return 1; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);
    bool b = !!(seed & 0b10);

    ValidationSample ret;
    // AND has its inputs already separated: 0110.
    ret.input_values.push_back(SeparatedZero(a));
    ret.input_values.push_back(SeparatedOne(a));
    ret.input_values.push_back(SeparatedOne(b));
    ret.input_values.push_back(SeparatedZero(b));

    // Want a single clean bit out.
    ChuteValue expected = (a && b) ? ChuteValue::ONE : ChuteValue::ZERO;
    ret.valid_outputs = {{expected}};
    return ret;
  }
};

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

// TODO: This should also sample the one bit at different angles,
// but we need to account for the fact that this changes the valid
// x positions.
static LevelBody SampleInput(uint64_t seed, vec2f input_pos, bool bit) {
  PCG32 pcg(seed);
  LevelBody body = bit ? Levels::One() : Levels::Zero();
  body.color = bit ? 0x00FF00FF : 0xFF0000FF;
  body.dynamic = true;
  body.vel = vec2f(pcg.Double() * 2.0f - 1.0f, pcg.Double() * 2.0f - 1.0f);
  body.avel = pcg.Double() * 2.0f - 1.0f;

  float bitw = bit ? Levels::BLOCK_SIZE : 4.0f * Levels::BLOCK_SIZE;
  constexpr float bith = 4.0f * Levels::BLOCK_SIZE;

  constexpr float in_width = Levels::IN_WIDTH * Levels::BLOCK_SIZE;
  constexpr float in_height = Levels::IN_HEIGHT * Levels::BLOCK_SIZE;

  float sample_width = (in_width - bitw) * 0.98f;
  float sample_height = (in_height - bith) * 0.98f;
  vec2f offset = {
    .x = (float)(0.02f + bitw * 0.5f + pcg.Double() * sample_width),
    .y = (float)(0.02f + bith * 0.5f + pcg.Double() * sample_height),
  };

  vec2f in_topleft = {
    .x = input_pos.x - in_width / 2.0f,
    .y = input_pos.y - in_height / 2.0f,
  };

  body.pos = in_topleft + offset;

  return body;
}

static void Validate(const ValidationInstance &inst) {
  static constexpr int NUM_TRIALS = 10000;
  ArcFour rc("validate");
  std::unique_ptr<Level> base_level = Levels::LoadSVG(inst.Filename());
  CHECK(base_level.get() != nullptr) << inst.Filename();

  CHECK(base_level->inputs.size() == (size_t)inst.ExpectedInputs() &&
        base_level->outputs.size() == (size_t)inst.ExpectedOutputs());

  std::mutex m;
  int correct_count = 0, done = 0;
  uint64_t base_seed = rc.Word64();
  static constexpr int NUM_EVAL_THREADS = 8;
  Periodically status_per(1.0);
  StatusBar status(1);

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

          LevelBody body = SampleInput(pcg.Rand64(), level.inputs[i], bit);

          level.bodies.emplace_back(std::move(body));
        }

        std::unique_ptr<Scene> scene = Levels::CreateScene(level);

        static constexpr int MAX_SIMULATE_STEPS = 1000;
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
        {
          MutexLock ml(&m);
          if (correct) correct_count++;
          done++;
        }

        status_per.RunIf([&]{
            MutexLock ml(&m);
            status.Progress(done, NUM_TRIALS, "{}/{} correct",
                            correct_count, done);
          });
      }, NUM_EVAL_THREADS);

  Print("Validation of {}: {} / {} correct\n",
        (int)inst.Filename().size(), inst.Filename().data(),
        correct_count, NUM_TRIALS);
}


int main(int argc, char **argv) {
  ANSI::Init();

  std::unique_ptr<ValidationInstance> instance =
    std::make_unique<AndValidation>();
  Validate(*instance);

  return 0;
}
