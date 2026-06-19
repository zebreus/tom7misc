
#include "validation.h"

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

namespace {
struct AndValidation : public ValidationInstance {
  std::string_view Filename() const override { return "and8.svg"; }
  int ExpectedInputs() const override { return 4; }
  int ExpectedOutputs() const override { return 1; }

  bool AddInputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);
    bool b = !!(seed & 0b10);

    ValidationSample ret;
    // AND has its inputs already separated: 0110.
    ret.input_values.push_back(Validation::SeparatedZero(a));
    ret.input_values.push_back(Validation::SeparatedOne(a));
    ret.input_values.push_back(Validation::SeparatedOne(b));
    ret.input_values.push_back(Validation::SeparatedZero(b));

    // Want a single clean bit out.
    ChuteValue expected = (a && b) ? ChuteValue::ONE : ChuteValue::ZERO;
    ret.valid_outputs = {{expected}};
    return ret;
  }
};

struct SeparatorValidation : public ValidationInstance {
  std::string_view Filename() const override { return "separator.svg"; }
  int ExpectedInputs() const override { return 1; }
  int ExpectedOutputs() const override { return 2; }

  bool AddInputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);

    ValidationSample ret;
    ret.input_values.push_back(a ? ChuteValue::ONE : ChuteValue::ZERO);

    // Separated bits out.
    ret.valid_outputs = {
      {Validation::SeparatedZero(a), Validation::SeparatedOne(a)},
    };
    return ret;
  }
};

struct NotValidation : public ValidationInstance {
  std::string_view Filename() const override { return "not2.svg"; }
  int ExpectedInputs() const override { return 1; }
  int ExpectedOutputs() const override { return 1; }

  bool AddInputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);

    // Mixed bits in/out.
    ValidationSample ret;
    ret.input_values.push_back(a ? ChuteValue::ONE : ChuteValue::ZERO);
    ret.valid_outputs = {{a ? ChuteValue::ZERO : ChuteValue::ONE}};
    return ret;
  }
};
}  // namespace

std::unique_ptr<ValidationInstance> Validation::And() {
  return std::make_unique<AndValidation>();
}

std::unique_ptr<ValidationInstance> Validation::Separator() {
  return std::make_unique<SeparatorValidation>();
}

std::unique_ptr<ValidationInstance> Validation::Not() {
  return std::make_unique<NotValidation>();
}

// TODO: This should also sample the one bit at different angles,
// but we need to account for the fact that this changes the valid
// x positions.
LevelBody Validation::SampleInput(uint64_t seed, vec2f input_pos, bool bit) {
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
